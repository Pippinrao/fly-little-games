[CmdletBinding()]
param(
    [string]$AndroidSerial = 'emulator-5554',
    [string]$HarmonyTarget = '127.0.0.1:5557',
    [ValidateRange(0, 60)][int]$CrossDurationMinutes = 10,
    [ValidateRange(1, 10)][int]$CrossRounds = 3,
    [ValidateRange(60, 1000000)][int]$CrossFrames = 600,
    [switch]$CrossOnly,
    [switch]$PhysicalHotspot,
    [switch]$CaptureOnly,
    [switch]$ProductPlay,
    [string]$LocalGameQuery = '',
    [ValidateRange(0, 600)][int]$PlayHoldSeconds = 10,
    [ValidateRange(1, 600)][int]$CaptureSeconds = 120,
    [ValidateSet('none', 'wrong-pin')][string]$DiagnosticFault = 'none'
)

$ErrorActionPreference = 'Stop'
$repo = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
Set-Location $repo

$adb = 'C:/Users/pippin/AppData/Local/Android/Sdk/platform-tools/adb.exe'
$hdc = 'D:/soft/DevEco Studio/sdk/default/openharmony/toolchains/hdc.exe'
$cmake = 'C:/Users/pippin/AppData/Local/Android/Sdk/cmake/3.22.1/bin/cmake.exe'
$ctest = 'C:/Users/pippin/AppData/Local/Android/Sdk/cmake/3.22.1/bin/ctest.exe'
$node = 'D:/soft/DevEco Studio/tools/node/node.exe'
$hvigor = 'D:/soft/DevEco Studio/tools/hvigor/bin/hvigorw.js'
$cargo = (Get-Command cargo -ErrorAction Stop).Source
$zlib = (Resolve-Path '.artifacts/host-deps/zlib-1.3.1-install').Path
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$evidence = Join-Path $repo "out/nearby-mvp/gate-$stamp"
New-Item -ItemType Directory -Path $evidence -Force | Out-Null

function Assert-Tool([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "Required tool is missing: $path" }
}

function Invoke-Logged {
    param([string]$Name, [string]$FilePath, [string[]]$Arguments, [string]$WorkingDirectory = $repo)
    $log = Join-Path $evidence "$Name.log"
    Push-Location $WorkingDirectory
    try {
        $lines = & $FilePath @Arguments 2>&1
        $exit = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    $lines | Set-Content -LiteralPath $log -Encoding utf8
    $lines | Write-Host
    if ($exit -ne 0) { throw "$Name failed with exit code $exit (see $log)" }
    return ($lines -join "`n")
}

function Assert-AndroidPass([string]$text, [int]$count, [string]$name) {
    $noun = if ($count -eq 1) { 'test' } else { 'tests' }
    if ($text -notmatch [regex]::Escape("OK ($count $noun)")) {
        throw "$name did not report exactly $count passing Android tests"
    }
    if ($text -match 'FAILURES!!!|INSTRUMENTATION_FAILED|Process crashed') {
        throw "$name reported an Android assertion or process failure"
    }
}

function Assert-HarmonyPass([string]$text, [int]$count, [string]$name) {
    $wanted = "Tests run: $count, Failure: 0, Error: 0, Pass: $count, Ignore: 0"
    if ($text -notmatch [regex]::Escape($wanted)) {
        throw "$name did not report exactly $count passing Harmony tests"
    }
}

function Receive-AdbSandboxFile([string]$remote, [string]$local) {
    $start = [System.Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $adb
    $start.UseShellExecute = $false
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    foreach ($argument in @('-s', $AndroidSerial, 'exec-out', 'run-as', 'com.flynes.emu', 'cat', $remote)) {
        [void]$start.ArgumentList.Add($argument)
    }
    $process = [System.Diagnostics.Process]::Start($start)
    $stream = [System.IO.File]::Create($local)
    try { $process.StandardOutput.BaseStream.CopyTo($stream) } finally { $stream.Dispose() }
    $errorText = $process.StandardError.ReadToEnd()
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) { throw "Failed to receive Android evidence file $remote`: $errorText" }
}

function Invoke-CrossRound([int]$round, [int]$durationMinutes) {
    $roundPassed = $false
    $android = $null
    $networkJob = $null
    $roundDir = Join-Path $evidence "cross-round-$round"
    New-Item -ItemType Directory -Path $roundDir -Force | Out-Null
    $networkJob = Start-Job -ArgumentList $adb, $hdc, $AndroidSerial, $HarmonyTarget -ScriptBlock {
        param($a, $h, $serial, $target)
        while ($true) {
            "time=$([DateTimeOffset]::UtcNow.ToString('o'))"
            & $a -s $serial shell ip -4 addr
            & $h -t $target shell ifconfig wlan0
            Start-Sleep -Seconds 2
        }
    }
    try {
    & $adb -s $AndroidSerial shell am force-stop com.flynes.emu | Out-Null
    & $hdc -t $HarmonyTarget shell aa force-stop com.flynes.emu | Out-Null
    & $adb -s $AndroidSerial shell run-as com.flynes.emu rm -f `
        files/nearby-cross-invite.txt files/nearby-cross-host-session.bin `
        files/nearby-cross-host-frame.rgb565 files/nearby-cross-host-play.txt | Out-Null

    $durationHeadroomMs = if ($durationMinutes -gt 0) { 15000 } else { 0 }
    $endEpoch = [DateTimeOffset]::UtcNow.ToUnixTimeMilliseconds() +
        ($durationMinutes * 60 * 1000) + $durationHeadroomMs
    $androidLog = Join-Path $roundDir 'android-instrumentation.txt'
    $androidError = Join-Path $roundDir 'android-error.txt'
    $args = @('-s', $AndroidSerial, 'shell', 'am', 'instrument', '-w', '-r',
        '-e', 'class', 'com.flynes.emu.ui.NearbyPairingTest#crossAppHostWaitsForHarmonyGuest',
        '-e', 'crossAppFrames', "$CrossFrames", '-e', 'crossAppEndEpochMs', "$endEpoch",
        'com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner')
    if ($ProductPlay) {
        $args = @('-s', $AndroidSerial, 'shell', 'am', 'instrument', '-w', '-r',
            '-e', 'class', 'com.flynes.emu.NearbyMvpProductPlayTest',
            '-e', 'playHoldMs', "$(($PlayHoldSeconds + 12) * 1000)")
        if ($LocalGameQuery) { $args += @('-e', 'localGameQuery', $LocalGameQuery) }
        $args += 'com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner'
    }
    $android = Start-Process -FilePath $adb -ArgumentList $args -WindowStyle Hidden `
        -RedirectStandardOutput $androidLog -RedirectStandardError $androidError -PassThru

    $invite = ''
    $inviteDeadline = [DateTime]::UtcNow.AddSeconds(20)
    while ([DateTime]::UtcNow -lt $inviteDeadline -and $invite -notmatch '^flynes-lan-v1:') {
        Start-Sleep -Milliseconds 100
        $invite = ((& $adb -s $AndroidSerial exec-out run-as com.flynes.emu `
            cat files/nearby-cross-invite.txt 2>$null) -join '').Trim()
    }
    if ($invite -notmatch '^flynes-lan-v1:') {
        if (-not $android.HasExited) { $android.Kill() }
        throw "Cross round $round did not publish an invite"
    }
    $fields = $invite.Split(':')
    if ($fields.Count -ne 5) { throw "Cross round $round published a malformed invite" }
    $guestPort = 0
    if (-not [int]::TryParse($fields[2], [ref]$guestPort) -or $guestPort -lt 1 -or $guestPort -gt 65535) {
        throw "Cross round $round published an invalid UDP port"
    }
    $rewrittenInvite = $invite
    if (-not $PhysicalHotspot) {
    $hostPort = 43190 + $round
    & $adb -s $AndroidSerial emu redir del "udp:$hostPort" 2>$null | Out-Null
    & $adb -s $AndroidSerial emu redir add "udp:$hostPort`:$guestPort" | Out-Null
    if ($LASTEXITCODE -ne 0) { throw "Cross round $round could not create the emulator UDP redirect" }
    $fields[1] = '10.0.2.2'
    $fields[2] = "$hostPort"
    $rewrittenInvite = $fields -join ':'

    } else {
        # Preserve the actual phone endpoint, pin and token. No PC forwarding.
        $ap = (& $adb -s $AndroidSerial shell ip -4 addr) -join "`n"
        if ($ap -notmatch '(?s)(?:ap|swlan)\d*:.*?inet\s+([0-9.]+)/') {
            throw 'Physical run requires an active Android hotspot interface'
        }
        if ($fields[1] -ne $Matches[1]) { throw 'Invite is not bound to the phone hotspot' }
    }

    if ($DiagnosticFault -eq 'wrong-pin') {
        $faultFields = $rewrittenInvite.Split(':')
        $firstNibble = if ($faultFields[3][0] -eq '0') { '1' } else { '0' }
        $faultFields[3] = $firstNibble + $faultFields[3].Substring(1)
        $rewrittenInvite = $faultFields -join ':'
    }
    $harmonyArgs = @(
        '-t', $HarmonyTarget, 'shell', 'aa', 'test', '-b', 'com.flynes.emu', '-m', 'entry_test',
        '-s', 'unittest', 'OpenHarmonyTestRunner', '-s', 'timeout',
        "$([Math]::Max(30000, ($durationMinutes + 2) * 60000))", '-s', 'nearbyCrossOnly', 'true',
        '-s', 'crossAppInvite', $rewrittenInvite, '-s', 'crossAppFrames', "$CrossFrames",
        '-s', 'crossAppEndEpochMs', "$endEpoch")
    if ($ProductPlay) {
        $harmonyArgs = @('-t', $HarmonyTarget, 'shell', 'aa', 'test', '-b', 'com.flynes.emu', '-m', 'entry_test',
            '-s', 'unittest', 'OpenHarmonyTestRunner', '-s', 'timeout', "$(($PlayHoldSeconds + 60) * 1000)",
            '-s', 'nearbyProductPlayOnly', 'true', '-s', 'crossAppInvite', $rewrittenInvite,
            '-s', 'playHoldMs', "$($PlayHoldSeconds * 1000)")
        if ($LocalGameQuery) { $harmonyArgs += @('-s', 'localGameQuery', $LocalGameQuery) }
    }
    $harmonyText = Invoke-Logged -Name "cross-round-$round-harmony" -FilePath $hdc -Arguments $harmonyArgs
    Assert-HarmonyPass $harmonyText 1 "cross round $round Harmony"

    if (-not $android.WaitForExit([Math]::Max(120000, ($durationMinutes + 2) * 60000))) {
        $android.Kill()
        throw "Cross round $round Android test timed out"
    }
    $androidText = Get-Content -LiteralPath $androidLog -Raw
    Assert-AndroidPass $androidText 1 "cross round $round Android"
    if ($ProductPlay) {
        "round=$round mode=product-controls audio=consumed holdSeconds=$PlayHoldSeconds" |
            Set-Content (Join-Path $roundDir 'summary.txt')
        $roundPassed = $true
        return
    }

    Receive-AdbSandboxFile 'files/nearby-cross-host-session.bin' (Join-Path $roundDir 'android-session.bin')
    Receive-AdbSandboxFile 'files/nearby-cross-host-frame.rgb565' (Join-Path $roundDir 'android-frame.rgb565')
    Receive-AdbSandboxFile 'files/nearby-cross-host-play.txt' (Join-Path $roundDir 'android-play.txt')
    foreach ($item in @('session.txt', 'frame.rgb565', 'play.txt')) {
        $remoteName = $item -replace '^session', 'nearby-cross-guest-session' `
                            -replace '^frame', 'nearby-cross-guest-frame' `
                            -replace '^play', 'nearby-cross-guest-play'
        & $hdc -t $HarmonyTarget file recv "/data/app/el2/100/base/com.flynes.emu/files/$remoteName" `
            (Join-Path $roundDir "harmony-$item") | Out-Null
        if ($LASTEXITCODE -ne 0) { throw "Cross round $round could not receive Harmony $item" }
    }

    $androidSession = [System.IO.File]::ReadAllBytes((Join-Path $roundDir 'android-session.bin'))
    $harmonySession = [System.IO.File]::ReadAllBytes((Join-Path $roundDir 'harmony-session.txt'))
    $androidSessionHex = [Convert]::ToHexString($androidSession).ToLowerInvariant()
    $harmonySessionHex = [Text.Encoding]::UTF8.GetString($harmonySession).Trim().ToLowerInvariant()
    if ($androidSession.Length -ne 16 -or $harmonySessionHex.Length -ne 32 -or
        $androidSessionHex -ne $harmonySessionHex) {
        throw "Cross round $round did not use one session id"
    }
    $androidFrame = Get-Item (Join-Path $roundDir 'android-frame.rgb565')
    $harmonyFrame = Get-Item (Join-Path $roundDir 'harmony-frame.rgb565')
    if ($androidFrame.Length -ne 122880 -or $harmonyFrame.Length -ne 122880) {
        throw "Cross round $round did not produce a complete 256x240 RGB565 frame"
    }
    $aHash = (Get-FileHash $androidFrame.FullName -Algorithm SHA256).Hash
    $hHash = (Get-FileHash $harmonyFrame.FullName -Algorithm SHA256).Hash
    if ($aHash -ne $hHash) { throw "Cross round $round produced different completed frames" }
    $aPlay = (Get-Content (Join-Path $roundDir 'android-play.txt') -Raw).Trim().Split(',')
    $hPlay = (Get-Content (Join-Path $roundDir 'harmony-play.txt') -Raw).Trim().Split(',')
    if ($aPlay.Count -ne 3 -or $hPlay.Count -ne 3 -or
        [long]$aPlay[0] -lt $CrossFrames -or [long]$hPlay[0] -lt $CrossFrames -or
        [long]$aPlay[1] -ne [long]$hPlay[1] -or [long]$aPlay[2] -le 0 -or [long]$hPlay[2] -le 0) {
        throw "Cross round $round frame/audio evidence is incomplete or divergent"
    }
    "round=$round durationMinutes=$durationMinutes frames=$($aPlay[0]) frameIndex=$($aPlay[1]) frameSha256=$aHash" |
        Set-Content -LiteralPath (Join-Path $roundDir 'summary.txt') -Encoding utf8
        $roundPassed = $true
    } finally {
        if ($android -and -not $android.HasExited) { $android.Kill() }
        if ($networkJob) {
            Stop-Job $networkJob
            Receive-Job $networkJob | Set-Content (Join-Path $roundDir 'network-timeline.txt')
            Remove-Job $networkJob
        }
        # Capture on success AND failure, without clearing pre-existing device logs.
        if ((& $adb -s $AndroidSerial get-state 2>$null) -match '^device$') {
            & $adb -s $AndroidSerial logcat -d -s FlyNesNearby |
                Set-Content (Join-Path $roundDir 'android-nearby.txt')
        }
        if ((& $hdc list targets) -contains $HarmonyTarget) {
            & $hdc -t $HarmonyTarget shell hilog -x -T FlyNesNearby |
                Set-Content (Join-Path $roundDir 'harmony-nearby.txt')
        }
        if (-not $roundPassed) {
            Add-Content (Join-Path $evidence 'run.txt') 'result=FAIL'
            if ((& $adb -s $AndroidSerial get-state 2>$null) -match '^device$') {
                & $adb -s $AndroidSerial shell am force-stop com.flynes.emu | Out-Null
            }
            if ((& $hdc list targets) -contains $HarmonyTarget) {
                & $hdc -t $HarmonyTarget shell aa force-stop com.flynes.emu | Out-Null
            }
        }
    }
}

foreach ($tool in @($adb, $hdc, $cmake, $ctest, $node, $hvigor)) { Assert-Tool $tool }
if ($PhysicalHotspot) {
    if (-not $CrossOnly -and -not $CaptureOnly) { throw 'Physical diagnostics require -CrossOnly or -CaptureOnly' }
    if ($AndroidSerial -like 'emulator-*' -or $HarmonyTarget -match '^127\.0\.0\.1:') {
        throw 'Physical mode requires two explicitly selected physical devices'
    }
    if ((& $adb -s $AndroidSerial get-state) -notmatch '^device$') { throw 'Android device unavailable' }
    if ((& $hdc list targets) -notcontains $HarmonyTarget) { throw 'Harmony device unavailable' }
} else {
if ($AndroidSerial -notlike 'emulator-*') { throw 'Android target must be an emulator serial' }
if ($HarmonyTarget -notmatch '^127\.0\.0\.1:\d+$') { throw 'Harmony target must be a loopback emulator target' }
if ((& $adb -s $AndroidSerial shell getprop ro.kernel.qemu).Trim() -ne '1') {
    throw 'Android target did not identify itself as an emulator'
}
$harmonyTargets = (& $hdc list targets -v) -join "`n"
if ($harmonyTargets -notmatch [regex]::Escape("$HarmonyTarget`t`tTCP`tConnected`tlocalhost")) {
    throw 'Harmony loopback emulator is not connected'
}
}

@(
    "git=$(git rev-parse HEAD)",
    "version=$((Get-Content VERSION -Raw).Trim())",
    "physicalHotspot=$PhysicalHotspot",
    "diagnosticFault=$DiagnosticFault",
    "apkSha256=$((Get-FileHash app/build/outputs/apk/debug/app-debug.apk -Algorithm SHA256).Hash)",
    "hapSha256=$((Get-FileHash harmony/entry/build/default/outputs/default/entry-default-signed.hap -Algorithm SHA256).Hash)",
    "android=$AndroidSerial",
    "harmony=$HarmonyTarget",
    "crossDurationMinutes=$CrossDurationMinutes",
    "crossRounds=$CrossRounds",
    "started=$([DateTimeOffset]::Now.ToString('o'))"
) | Set-Content -LiteralPath (Join-Path $evidence 'run.txt') -Encoding utf8

if ($CaptureOnly) {
    # Observe the actual camera/product UI without starting/stopping apps or injecting invites.
    $until = [DateTime]::UtcNow.AddSeconds($CaptureSeconds)
    try {
        while ([DateTime]::UtcNow -lt $until) {
            $androidOnline = (& $adb -s $AndroidSerial get-state 2>$null) -match '^device$'
            $harmonyOnline = (& $hdc list targets) -contains $HarmonyTarget
            "time=$([DateTimeOffset]::UtcNow.ToString('o')) android=$androidOnline harmony=$harmonyOnline" |
                Add-Content (Join-Path $evidence 'network-timeline.txt')
            if (-not $androidOnline -or -not $harmonyOnline) { break }
            & $adb -s $AndroidSerial shell ip -4 addr | Add-Content (Join-Path $evidence 'network-timeline.txt')
            & $hdc -t $HarmonyTarget shell ifconfig wlan0 | Add-Content (Join-Path $evidence 'network-timeline.txt')
            & $adb -s $AndroidSerial logcat -d -s FlyNesNearby | Set-Content (Join-Path $evidence 'android-nearby.txt')
            & $hdc -t $HarmonyTarget shell hilog -x -T FlyNesNearby | Set-Content (Join-Path $evidence 'harmony-nearby.txt')
            Start-Sleep -Seconds 2
        }
    } finally {
        Add-Content (Join-Path $evidence 'run.txt') 'mode=capture-only; result=EVIDENCE_ONLY'
    }
    Write-Host "Captured diagnostics (no gameplay verdict): $evidence"
    exit 0
}

if ($CrossOnly) {
    for ($round = 1; $round -le $CrossRounds; $round++) {
        Invoke-CrossRound $round $(if ($round -eq 1) { $CrossDurationMinutes } else { 0 })
    }
    Add-Content -LiteralPath (Join-Path $evidence 'run.txt') -Value @(
        "completed=$([DateTimeOffset]::Now.ToString('o'))", 'result=PASS',
        "crossRounds=$CrossRounds/$CrossRounds", 'mode=cross-only')
    Write-Host "PASS nearby MVP cross gate: $evidence"
    exit 0
}

Invoke-Logged 'content-verify' 'pwsh' @('-NoProfile', '-File', "$repo/tools/content/verify-builtin-content.ps1") | Out-Null
Invoke-Logged 'content-sync' 'pwsh' @('-NoProfile', '-File', "$repo/tools/content/sync-builtin-content.ps1") | Out-Null
Invoke-Logged 'nearby-quic-provider' $cargo @(
    'test', '--manifest-path', 'shared/nearby-quic-provider/Cargo.toml') | Out-Null
Invoke-Logged 'android-build-unit' "$repo/gradlew.bat" @(
    ':app:testDebugUnitTest', ':app:assembleDebug', ':app:assembleDebugAndroidTest', '--no-daemon') | Out-Null

Invoke-Logged 'shared-configure' $cmake @('-S', 'shared', '-B', 'out/nearby-mvp/host',
    '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DFLYNES_BUILD_TESTS=ON',
    '-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON', "-DZLIB_ROOT=$zlib") | Out-Null
Invoke-Logged 'shared-build' $cmake @('--build', 'out/nearby-mvp/host', '--config', 'Debug',
    '--parallel', '2', '--target', 'flynes_lan_mvp_protocol_test', 'flynes_lan_mvp_session_test',
    'flynes_lan_mvp_invite_test') | Out-Null
$sharedText = Invoke-Logged 'shared-ctest' $ctest @('--test-dir', 'out/nearby-mvp/host', '-C', 'Debug',
    '--output-on-failure', '-R', '^flynes_lan_mvp_')
if ($sharedText -notmatch '100% tests passed') { throw 'Shared nearby MVP CTest did not pass completely' }

Invoke-Logged 'harmony-host-configure' $cmake @('-S', 'harmony/tests', '-B', 'out/nearby-mvp/harmony-host',
    '-G', 'Visual Studio 17 2022', '-A', 'x64', '-DFLYNES_BUILD_TESTS=ON', "-DZLIB_ROOT=$zlib") | Out-Null
Invoke-Logged 'harmony-host-build' $cmake @('--build', 'out/nearby-mvp/harmony-host', '--config', 'Debug', '--parallel', '2') | Out-Null
$harmonyHost = Invoke-Logged 'harmony-host-ctest' $ctest @('--test-dir', 'out/nearby-mvp/harmony-host',
    '-C', 'Debug', '--output-on-failure', '--parallel', '2')
if ($harmonyHost -notmatch '100% tests passed, 0 tests failed out of 14') { throw 'Harmony host suite was incomplete' }

$env:DEVECO_SDK_HOME = 'D:/soft/DevEco Studio/sdk'
Invoke-Logged 'harmony-main-build' $node @($hvigor, '--mode', 'module', '-p', 'product=default',
    'assembleHap', '--no-daemon') (Join-Path $repo 'harmony') | Out-Null
Invoke-Logged 'harmony-test-build' $node @($hvigor, '--mode', 'module', '-p', 'product=default',
    '-p', 'module=entry@ohosTest', '-p', 'buildMode=debug', 'assembleHap', '--no-daemon') `
    (Join-Path $repo 'harmony') | Out-Null

Invoke-Logged 'android-install-app' $adb @('-s', $AndroidSerial, 'install', '-r',
    'app/build/outputs/apk/debug/app-debug.apk') | Out-Null
Invoke-Logged 'android-install-test' $adb @('-s', $AndroidSerial, 'install', '-r',
    'app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk') | Out-Null
Invoke-Logged 'harmony-install-app' $hdc @('-t', $HarmonyTarget, 'install', '-r',
    'harmony/entry/build/default/outputs/default/entry-default-signed.hap') | Out-Null
Invoke-Logged 'harmony-install-test' $hdc @('-t', $HarmonyTarget, 'install', '-r',
    'harmony/entry/build/default/outputs/ohosTest/entry-ohosTest-signed.hap') | Out-Null

$androidNearbyClasses = @(
    'com.flynes.emu.ui.NearbyFriendsManageTest','com.flynes.emu.ui.NearbyFriendsTest',
    'com.flynes.emu.ui.NearbyInviteCodeTest','com.flynes.emu.ui.NearbyLobbyConfirmationTest',
    'com.flynes.emu.ui.NearbyLobbyTest','com.flynes.emu.ui.NearbyUiParityTest',
    'com.flynes.emu.ui.NearbyUxRestorationTest','com.flynes.emu.ui.NearbyLandscapeTest',
    'com.flynes.emu.ui.NearbyPairingTest#createPageMatchesApprovedInviteMockup',
    'com.flynes.emu.ui.NearbyPairingTest#createPagePublishesMachineReadableQr',
    'com.flynes.emu.ui.NearbyPairingTest#invitationStillWorksAfterThirtySecondsAndCanBeRegenerated',
    'com.flynes.emu.ui.NearbyPairingTest#productQrConnectsARealNativeGuestIntoTheSameSession',
    'com.flynes.emu.ui.NearbyPairingTest#joinPageMatchesApprovedCodeMockup',
    'com.flynes.emu.ui.NearbyPairingTest#anonymousJoinControlSetIsNotBuilt') -join ','
$androidUi = Invoke-Logged 'android-nearby-ui' $adb @('-s', $AndroidSerial, 'shell', 'am', 'instrument',
    '-w', '-r', '-e', 'class', $androidNearbyClasses,
    'com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner')
Assert-AndroidPass $androidUi 40 'Android nearby UI'
$androidSingle = Invoke-Logged 'android-single-regression' $adb @('-s', $AndroidSerial, 'shell', 'am',
    'instrument', '-w', '-r', '-e', 'class',
    'com.flynes.emu.GamepadCancelTest,com.flynes.emu.GamepadTouchDispatchTest,com.flynes.emu.BuiltinPlaySmokeTest',
    'com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner')
Assert-AndroidPass $androidSingle 16 'Android single-player regression'

foreach ($suite in @(@('service','nearbyServiceOnly','10'), @('scan','nearbyMvpOnly','5'))) {
    & $hdc -t $HarmonyTarget shell aa force-stop com.flynes.emu | Out-Null
    $text = Invoke-Logged "harmony-$($suite[0])" $hdc @('-t', $HarmonyTarget, 'shell', 'aa', 'test',
        '-b', 'com.flynes.emu', '-m', 'entry_test', '-s', 'unittest', 'OpenHarmonyTestRunner',
        '-s', $suite[1], 'true')
    Assert-HarmonyPass $text ([int]$suite[2]) "Harmony $($suite[0])"
}
foreach ($case in @(
    'NearbyUxRestoration#single_screen_landscape_1',
    'NearbyUxRestoration#single_screen_landscape_1.3',
    'NearbyUxRestoration#single_screen_landscape_2',
    'NearbyUxRestoration#lobby_has_fixed_summary_and_paged_details_1',
    'NearbyUxRestoration#lobby_has_fixed_summary_and_paged_details_1.3',
    'NearbyUxRestoration#lobby_has_fixed_summary_and_paged_details_2',
    'NearbyWantParser#debug_want_accepts_supported_scales_only')) {
    & $hdc -t $HarmonyTarget shell aa force-stop com.flynes.emu | Out-Null
    $safeName = $case -replace '[^A-Za-z0-9]+','-'
    $text = Invoke-Logged "harmony-ux-$safeName" $hdc @('-t', $HarmonyTarget, 'shell', 'aa', 'test',
        '-b', 'com.flynes.emu', '-m', 'entry_test', '-s', 'unittest', 'OpenHarmonyTestRunner',
        '-s', 'timeout', '15000', '-s', 'nearbyUxOnly', 'true', '-s', 'class', $case)
    Assert-HarmonyPass $text 1 "Harmony UX $case"
}
foreach ($single in @(@('BuiltinPlaySmoke_1','3'), @('BuiltinGameplay_1','15'))) {
    & $hdc -t $HarmonyTarget shell aa force-stop com.flynes.emu | Out-Null
    $text = Invoke-Logged "harmony-$($single[0])" $hdc @('-t', $HarmonyTarget, 'shell', 'aa', 'test',
        '-b', 'com.flynes.emu', '-m', 'entry_test', '-s', 'unittest', 'OpenHarmonyTestRunner',
        '-s', 'timeout', '30000', '-s', 'class', $single[0])
    Assert-HarmonyPass $text ([int]$single[1]) "Harmony $($single[0])"
}

for ($round = 1; $round -le $CrossRounds; $round++) {
    Invoke-CrossRound $round $(if ($round -eq 1) { $CrossDurationMinutes } else { 0 })
}

Add-Content -LiteralPath (Join-Path $evidence 'run.txt') -Value @(
    "completed=$([DateTimeOffset]::Now.ToString('o'))",
    'result=PASS',
    'androidNearby=40/40',
    'androidSingle=16/16',
    'harmonyService=10/10',
    'harmonyScan=5/5',
    'harmonyUx=7/7',
    'harmonySingle=18/18',
    "crossRounds=$CrossRounds/$CrossRounds")
Write-Host "PASS nearby MVP simulator gate: $evidence"
