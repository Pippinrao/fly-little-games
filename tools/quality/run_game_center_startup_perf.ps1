[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string]$Serial,
    [ValidateRange(1, 100)]
    [int]$Runs = 12,
    [ValidateRange(0, 20)]
    [int]$Warmups = 2
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..')).Path
$sdkRoot = if ($env:ANDROID_HOME) { $env:ANDROID_HOME } else {
    Join-Path $env:LOCALAPPDATA 'Android\Sdk'
}
$adb = Join-Path $sdkRoot 'platform-tools\adb.exe'
if (-not (Test-Path -LiteralPath $adb)) { throw "adb not found at $adb" }

function Invoke-Adb {
    $arguments = @($args)
    $output = & $adb -s $Serial @arguments 2>&1
    if ($LASTEXITCODE -ne 0) {
        throw "adb $($Arguments -join ' ') failed:`n$($output -join "`n")"
    }
    return @($output)
}

$deviceRows = & $adb devices
if (-not ($deviceRows -match "^$([regex]::Escape($Serial))\s+device$")) {
    throw "Serial $Serial is not an online adb device"
}
$qemu = ((Invoke-Adb shell getprop ro.kernel.qemu) -join '').Trim()
$bootQemu = ((Invoke-Adb shell getprop ro.boot.qemu) -join '').Trim()
$hardware = ((Invoke-Adb shell getprop ro.hardware) -join '').Trim().ToLowerInvariant()
if ($qemu -ne '1' -and $bootQemu -ne '1' -and
        -not $hardware.Contains('ranchu') -and -not $hardware.Contains('goldfish')) {
    throw "Performance fixture may run only on a disposable emulator; $Serial reports hardware=$hardware"
}

$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$outputRoot = Join-Path $repoRoot "out\game-center-fast-start\gate-$timestamp"
New-Item -ItemType Directory -Force -Path $outputRoot | Out-Null

Push-Location $repoRoot
try {
    & .\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest
    if ($LASTEXITCODE -ne 0) { throw 'Android performance fixture build failed' }

    # The emulator check above is deliberately before data removal. These exact package names are
    # the only mutable state owned by this disposable performance fixture.
    & $adb -s $Serial uninstall com.flynes.emu.test *> $null
    & $adb -s $Serial uninstall com.flynes.emu *> $null
    Invoke-Adb install -r 'app\build\outputs\apk\debug\app-debug.apk' | Out-Null
    Invoke-Adb install -r 'app\build\outputs\apk\androidTest\debug\app-debug-androidTest.apk' | Out-Null

    $seedClass = 'com.flynes.emu.catalog.android.AndroidLargeCatalogPerformanceTest#seedDisposable2224GameSnapshot'
    $seed = Invoke-Adb shell am instrument -w -r `
            -e flynes.disposablePerformanceFixture true `
            -e class $seedClass `
            'com.flynes.emu.test/.SingleDeviceCertificationRunner'
    $seedText = $seed -join "`n"
    $seedText | Set-Content -LiteralPath (Join-Path $outputRoot 'seed.log') -Encoding utf8
    if ($seedText -notmatch 'OK \(1 test\)') { throw "Performance fixture seeding failed:`n$seedText" }
    # Match a release-installed app's ahead-of-time compiled state while keeping every measured
    # launch process-cold. This does not start or warm the application process.
    Invoke-Adb shell cmd package compile -f -m speed com.flynes.emu | Out-Null

    $samples = [System.Collections.Generic.List[object]]::new()
    $total = $Warmups + $Runs
    for ($index = 0; $index -lt $total; $index++) {
        $kind = if ($index -lt $Warmups) { 'warmup' } else { 'measured' }
        $number = if ($kind -eq 'warmup') { $index + 1 } else { $index - $Warmups + 1 }
        $visible = $null
        $counters = $null
        $logFile = $null
        for ($attempt = 1; $attempt -le 3; $attempt++) {
            Invoke-Adb logcat -c | Out-Null
            Invoke-Adb shell am force-stop com.flynes.emu | Out-Null
            $startOutput = Invoke-Adb shell am start -W -n 'com.flynes.emu/.HomeActivity'

            $deadline = [DateTime]::UtcNow.AddSeconds(10)
            $logText = ''
            do {
                Start-Sleep -Milliseconds 50
                $logText = (Invoke-Adb logcat -d -s 'FlyNesStartup:I' '*:S') -join "`n"
                $hasVisible = $logText -match 'GAME_CENTER_VISIBLE\s+elapsedMs='
                $hasCounters = $logText -match 'STARTUP_COUNTERS\s+BUILTIN_SCAN_COUNT='
            } while ((-not $hasVisible -or -not $hasCounters) -and [DateTime]::UtcNow -lt $deadline)

            $suffix = if ($attempt -eq 1) { '' } else { "-attempt-$attempt" }
            $logFile = Join-Path $outputRoot ("{0}-{1:D2}{2}.log" -f $kind, $number, $suffix)
            (($startOutput -join "`n") + "`n" + $logText) |
                    Set-Content -LiteralPath $logFile -Encoding utf8

            $visible = [regex]::Matches(
                    $logText,
                    'GAME_CENTER_VISIBLE\s+elapsedMs=(\d+)\s+count=(\d+)\s+cache=([A-Z_]+)')
            $counters = [regex]::Matches(
                    $logText,
                    'STARTUP_COUNTERS\s+BUILTIN_SCAN_COUNT=(\d+)\s+EXTERNAL_SCAN_COUNT=(\d+)\s+BULK_SNAPSHOT_COUNT=(\d+)\s+USER_STATE_JNI_COUNT=(\d+)')
            if ($visible.Count -gt 0 -and $counters.Count -gt 0) { break }
            Write-Warning "Launch did not produce startup markers ($kind $number, attempt $attempt)"
        }
        if ($visible.Count -eq 0 -or $counters.Count -eq 0) {
            throw "Missing startup marker after 3 attempts; last log: $logFile"
        }
        $v = $visible[$visible.Count - 1]
        $c = $counters[$counters.Count - 1]
        $sample = [pscustomobject]@{
            kind = $kind
            run = $number
            elapsedMs = [int]$v.Groups[1].Value
            count = [int]$v.Groups[2].Value
            cache = $v.Groups[3].Value
            builtinScanCount = [int]$c.Groups[1].Value
            externalScanCount = [int]$c.Groups[2].Value
            bulkSnapshotCount = [int]$c.Groups[3].Value
            userStateJniCount = [int]$c.Groups[4].Value
        }
        $samples.Add($sample)
        Write-Host ("{0} {1}: {2} ms, rows={3}, cache={4}, scans={5}/{6}, bulk={7}, userJNI={8}" -f
                $kind, $number, $sample.elapsedMs, $sample.count, $sample.cache,
                $sample.builtinScanCount, $sample.externalScanCount,
                $sample.bulkSnapshotCount, $sample.userStateJniCount)
    }

    $measured = @($samples | Where-Object kind -eq 'measured')
    $sorted = @($measured.elapsedMs | Sort-Object)
    $p95Index = [Math]::Ceiling(0.95 * $sorted.Count) - 1
    $p95 = $sorted[$p95Index]
    $model = ((Invoke-Adb shell getprop ro.product.model) -join '').Trim()
    $api = ((Invoke-Adb shell getprop ro.build.version.sdk) -join '').Trim()
    $abi = ((Invoke-Adb shell getprop ro.product.cpu.abi) -join '').Trim()
    $failures = [System.Collections.Generic.List[string]]::new()
    if ($measured.Count -ne $Runs) { $failures.Add("expected $Runs measured samples") }
    foreach ($sample in $measured) {
        if ($sample.count -lt 2000) { $failures.Add("run $($sample.run) rendered only $($sample.count) rows") }
        if ($sample.cache -ne 'HIT') { $failures.Add("run $($sample.run) cache=$($sample.cache)") }
        if ($sample.builtinScanCount -ne 0 -or $sample.externalScanCount -ne 0) {
            $failures.Add("run $($sample.run) performed a startup scan")
        }
        if ($sample.bulkSnapshotCount -ne 0) { $failures.Add("run $($sample.run) rebuilt the projection") }
        if ($sample.userStateJniCount -ne 0) { $failures.Add("run $($sample.run) used per-game JNI") }
    }
    if ($p95 -gt 200) { $failures.Add("nearest-rank P95 is $p95 ms, above 200 ms") }

    $report = [ordered]@{
        generatedAt = (Get-Date).ToString('o')
        commit = (git rev-parse HEAD).Trim()
        serial = $Serial
        model = $model
        api = $api
        abi = $abi
        buildType = 'debug'
        warmups = $Warmups
        runs = $Runs
        p95Ms = $p95
        passed = ($failures.Count -eq 0)
        failures = @($failures)
        samples = @($samples)
    }
    $report | ConvertTo-Json -Depth 6 |
            Set-Content -LiteralPath (Join-Path $outputRoot 'run.json') -Encoding utf8
    Write-Host "Evidence: $outputRoot"
    Write-Host "Nearest-rank P95: $p95 ms"
    if ($failures.Count -ne 0) { throw ($failures -join '; ') }
} finally {
    Pop-Location
}
