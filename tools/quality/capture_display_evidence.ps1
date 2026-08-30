[CmdletBinding()]
param(
    [string] $Serial,
    [string] $ExpectedModel,
    [string] $ApkPath,
    [string] $OutputRoot,
    [ValidateRange(10, 7200)][int] $DurationSeconds = 1800,
    [ValidateSet('PUBLIC', '60', '120')][string] $Mode = 'PUBLIC',
    [string] $PairId,
    [string] $AdbPath = 'adb',
    [string] $AaptPath,
    [string] $ApkSignerPath
)

$ErrorActionPreference = 'Stop'
$script:FlyNesPackage = 'com.flynes.emu'

function ConvertTo-ProcessArgument {
    param([AllowEmptyString()][Parameter(Mandatory = $true)][string] $Value)
    if ($Value.Length -gt 0 -and $Value -notmatch '[\s"]') { return $Value }
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $slashes = 0
    foreach ($character in $Value.ToCharArray()) {
        if ($character -eq '\') {
            $slashes++
            continue
        }
        if ($character -eq '"') {
            [void]$builder.Append(('\' * ($slashes * 2 + 1)))
            [void]$builder.Append('"')
        }
        else {
            if ($slashes -gt 0) { [void]$builder.Append(('\' * $slashes)) }
            [void]$builder.Append($character)
        }
        $slashes = 0
    }
    if ($slashes -gt 0) { [void]$builder.Append(('\' * ($slashes * 2))) }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CaptureProcess {
    param(
        [Parameter(Mandatory = $true)][string] $FilePath,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [ValidateRange(1, 10000)][int] $TimeoutSeconds = 30
    )
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $FilePath
    # Windows PowerShell 5.1 lacks ProcessStartInfo.ArgumentList. This implements
    # the documented CreateProcess quoting rules without invoking a shell.
    $start.Arguments = (($Arguments | ForEach-Object {
                ConvertTo-ProcessArgument -Value $_
            }) -join ' ')
    $start.UseShellExecute = $false
    $start.CreateNoWindow = $true
    $start.RedirectStandardOutput = $true
    $start.RedirectStandardError = $true
    $process = [Diagnostics.Process]::Start($start)
    $stdoutTask = $process.StandardOutput.ReadToEndAsync()
    $stderrTask = $process.StandardError.ReadToEndAsync()
    if (-not $process.WaitForExit($TimeoutSeconds * 1000)) {
        if ([Environment]::OSVersion.Platform -eq [PlatformID]::Win32NT) {
            $killer = [Diagnostics.Process]::Start((New-Object Diagnostics.ProcessStartInfo -Property @{
                FileName = 'taskkill.exe'
                Arguments = "/PID $($process.Id) /T /F"
                UseShellExecute = $false
                CreateNoWindow = $true
            }))
            if (-not $killer.WaitForExit(5000) -or $killer.ExitCode -ne 0) {
                throw "Unable to terminate timed-out process tree: $FilePath"
            }
        }
        else {
            $process.Kill()
        }
        if (-not $process.WaitForExit(5000)) {
            throw "Timed-out process tree survived termination: $FilePath"
        }
        throw "Process timed out after $TimeoutSeconds seconds: $FilePath"
    }
    return [pscustomobject]@{
        ExitCode = $process.ExitCode
        StdOut = $stdoutTask.GetAwaiter().GetResult()
        StdErr = $stderrTask.GetAwaiter().GetResult()
    }
}

function Invoke-CheckedCaptureProcess {
    param(
        [Parameter(Mandatory = $true)][string] $FilePath,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [ValidateRange(1, 10000)][int] $TimeoutSeconds = 30
    )
    $result = Invoke-CaptureProcess -FilePath $FilePath -Arguments $Arguments `
        -TimeoutSeconds $TimeoutSeconds
    if ($result.ExitCode -ne 0) {
        $displayArguments = $Arguments | ForEach-Object {
            if ($_ -match '[\s"]') { '"' + $_.Replace('"', '\"') + '"' } else { $_ }
        }
        throw "Command failed ($($result.ExitCode)): $FilePath $($displayArguments -join ' ')`n$($result.StdErr)"
    }
    return $result
}

function Get-ConnectedAdbDevices {
    param([Parameter(Mandatory = $true)][string] $Text)
    $devices = @()
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch '^([^\s]+)\s+device(?:\s+(.*))?$') { continue }
        $deviceSerial = $Matches[1]
        $propertyText = $Matches[2] + ''
        $properties = @{}
        foreach ($token in ($propertyText -split '\s+')) {
            if ($token -match '^([^:]+):(.*)$') { $properties[$Matches[1]] = $Matches[2] }
        }
        $devices += [pscustomobject]@{
            Serial = $deviceSerial
            Model = [string]$properties['model']
        }
    }
    return @($devices)
}

function Assert-CaptureDevice {
    param(
        [Parameter(Mandatory = $true)][object[]] $Devices,
        [Parameter(Mandatory = $true)][string] $RequiredSerial,
        [Parameter(Mandatory = $true)][string] $RequiredModel
    )
    if ($Devices.Count -ne 1) {
        throw "Exactly one authorized adb device is required; found $($Devices.Count)."
    }
    if ($Devices[0].Serial -ne $RequiredSerial -or $Devices[0].Model -ne $RequiredModel) {
        throw "Device mismatch: expected $RequiredSerial/$RequiredModel, found $($Devices[0].Serial)/$($Devices[0].Model)."
    }
    return $Devices[0]
}

function Select-GameSurfaceLayer {
    param([Parameter(Mandatory = $true)][string] $Text)
    $matches = @(($Text -split "`r?`n") | Where-Object {
            $_ -match [regex]::Escape($script:FlyNesPackage) -and $_ -match 'SurfaceView'
        } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
    if ($matches.Count -ne 1) {
        throw "Expected exactly one FlyNES SurfaceView layer; found $($matches.Count)."
    }
    return $matches[0]
}

function Get-UiTargetCenter {
    param(
        [Parameter(Mandatory = $true)][string] $XmlPath,
        [Parameter(Mandatory = $true)][string] $ResourceId
    )
    [xml]$document = Get-Content -LiteralPath $XmlPath -Raw
    $nodes = @($document.SelectNodes("//*[@resource-id='$ResourceId']"))
    if ($nodes.Count -ne 1) {
        throw "Expected exactly one UI node for $ResourceId; found $($nodes.Count)."
    }
    $node = $nodes[0]
    if ($node.enabled -ne 'true' -or $node.bounds -notmatch
            '^\[(\d+),(\d+)\]\[(\d+),(\d+)\]$') {
        throw "UI node is disabled or has invalid bounds: $ResourceId"
    }
    $left = [int]$Matches[1]
    $top = [int]$Matches[2]
    $right = [int]$Matches[3]
    $bottom = [int]$Matches[4]
    if ($right -le $left -or $bottom -le $top) {
        throw "UI node has empty bounds: $ResourceId"
    }
    return [pscustomobject]@{
        X = [int](($left + $right) / 2)
        Y = [int](($top + $bottom) / 2)
    }
}

function New-EvidenceDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Leaf
    )
    $rootFull = [IO.Path]::GetFullPath($Root)
    if (-not (Test-Path -LiteralPath $rootFull)) {
        New-Item -ItemType Directory -Path $rootFull | Out-Null
    }
    $target = [IO.Path]::GetFullPath((Join-Path $rootFull $Leaf))
    $prefix = $rootFull.TrimEnd([IO.Path]::DirectorySeparatorChar) + [IO.Path]::DirectorySeparatorChar
    if (-not $target.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Evidence directory escaped its configured root.'
    }
    if (Test-Path -LiteralPath $target) {
        throw "Evidence directory already exists; refusing to overwrite: $target"
    }
    New-Item -ItemType Directory -Path $target | Out-Null
    return $target
}

function Get-ApkIdentity {
    param(
        [Parameter(Mandatory = $true)][string] $Tool,
        [Parameter(Mandatory = $true)][string] $Path
    )
    $result = Invoke-CheckedCaptureProcess -FilePath $Tool `
        -Arguments @('dump', 'badging', $Path)
    $line = @(($result.StdOut -split "`r?`n") | Where-Object { $_ -match '^package:' })[0]
    if (-not $line -or $line -notmatch "name='([^']+)'\s+versionCode='([^']+)'\s+versionName='([^']*)'") {
        throw "Unable to parse APK identity: $Path"
    }
    return [pscustomobject]@{
        Package = $Matches[1]
        VersionCode = $Matches[2]
        VersionName = $Matches[3]
        Debuggable = $result.StdOut -match '(?m)^application-debuggable'
    }
}

function Get-ActiveDisplayMode {
    param([Parameter(Mandatory = $true)][string] $Text)
    if ($Text -match 'DisplayDeviceInfo\{[^\r\n]*?,\s*(\d+) x (\d+), modeId (\d+), renderFrameRate\s+([0-9.]+)') {
        return [pscustomobject]@{
            Width = [int]$Matches[1]
            Height = [int]$Matches[2]
            ModeId = [int]$Matches[3]
            RefreshHz = [double]::Parse($Matches[4],
                    [Globalization.CultureInfo]::InvariantCulture)
        }
    }
    throw 'Unable to parse the active physical display mode from dumpsys display.'
}

function Test-SameActiveDisplayMode {
    param([object] $Expected, [object] $Actual)
    return $Expected -and $Actual -and $Expected.ModeId -eq $Actual.ModeId `
        -and $Expected.Width -eq $Actual.Width -and $Expected.Height -eq $Actual.Height `
        -and [Math]::Abs($Expected.RefreshHz - $Actual.RefreshHz) -le 0.01
}

function Get-CoreConfirmedSourceTiming {
    param([Parameter(Mandatory = $true)][string] $Text)
    $values = @(($Text -split "`r?`n") | ForEach-Object {
        if ($_ -match 'EVIDENCE_SOURCE_TIMING=(NTSC_60_0988|PAL_50)') { $Matches[1] }
    } | Sort-Object -Unique)
    if ($values.Count -ne 1) {
        throw "Expected one core-confirmed source timing; found $($values.Count)."
    }
    return $values[0]
}

function Get-AppRequestedDisplayMode {
    param([Parameter(Mandatory = $true)][string] $Text)
    $modes = @{}
    foreach ($line in ($Text -split "`r?`n")) {
        if ($line -notmatch 'EVIDENCE_DISPLAY_REQUEST\s+generation=\d+\s+policy=\S+\s+modeId=(\d+)\s+width=(\d+)\s+height=(\d+)\s+refreshMilliHz=(\d+)') {
            continue
        }
        $key = "$($Matches[1]):$($Matches[2]):$($Matches[3]):$($Matches[4])"
        $modes[$key] = [pscustomobject]@{
            ModeId = [int]$Matches[1]
            Width = [int]$Matches[2]
            Height = [int]$Matches[3]
            RefreshHz = [int]$Matches[4] / 1000.0
        }
    }
    if ($modes.Count -ne 1) {
        throw "Expected one app-confirmed requested display mode; found $($modes.Count)."
    }
    return @($modes.Values)[0]
}

function Assert-PerfettoRuntime {
    param(
        [Parameter(Mandatory = $true)][DateTime] $StartedAt,
        [Parameter(Mandatory = $true)][DateTime] $LastAliveAt,
        [Parameter(Mandatory = $true)][DateTime] $StoppedAt,
        [Parameter(Mandatory = $true)][int] $ExpectedSeconds
    )
    $actual = ($StoppedAt - $StartedAt).TotalSeconds
    $confirmedAlive = ($LastAliveAt - $StartedAt).TotalSeconds
    if ($confirmedAlive -lt ($ExpectedSeconds - 1.0)) {
        throw "Perfetto was last alive too early: expected ${ExpectedSeconds}s, confirmed $confirmedAlive seconds."
    }
    if ($actual -gt ($ExpectedSeconds + 60.0)) {
        throw "Perfetto exceeded its bounded trace window: observed $actual seconds."
    }
    return $actual
}

function Assert-PerfettoExitStatus {
    param([AllowEmptyString()][Parameter(Mandatory = $true)][string] $Text)
    $trimmed = $Text.Trim()
    if ($trimmed -notmatch '^0$') {
        throw "Perfetto wrapper did not report exact exit status 0: '$trimmed'."
    }
    return 0
}

function Assert-TrialContract {
    param([string] $TrialMode, [string] $TrialPairId, [bool] $Debuggable)
    if ($TrialMode -ne 'PUBLIC' -and (-not $Debuggable -or -not $TrialPairId)) {
        throw '60/120 paired trials require a debuggable APK and a non-empty PairId.'
    }
}

function Test-IsVirtualDevice {
    param([string] $KernelQemu, [string] $BootQemu)
    return $KernelQemu -eq '1' -or $BootQemu -eq '1'
}

function Get-ApkCertificateSha256 {
    param(
        [Parameter(Mandatory = $true)][string] $Tool,
        [Parameter(Mandatory = $true)][string] $Path
    )
    $result = Invoke-CheckedCaptureProcess -FilePath $Tool `
        -Arguments @('verify', '--print-certs', $Path)
    if ($result.StdOut -notmatch '(?im)^Signer #1 certificate SHA-256 digest:\s*([0-9a-f:]+)\s*$') {
        throw "Unable to parse APK signing certificate: $Path"
    }
    return $Matches[1].Replace(':', '').ToLowerInvariant()
}

function Get-BuildToolsSortVersion {
    param([Parameter(Mandatory = $true)][string] $Name)
    if ($Name -match '^(\d+)\.(\d+)\.(\d+)') {
        return [Version]("$($Matches[1]).$($Matches[2]).$($Matches[3])")
    }
    return [Version]'0.0.0'
}

function Resolve-BuildTool {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [string] $ExplicitPath
    )
    if ($ExplicitPath) {
        if (-not (Test-Path -LiteralPath $ExplicitPath -PathType Leaf)) {
            throw "Build tool does not exist: $ExplicitPath"
        }
        return (Resolve-Path -LiteralPath $ExplicitPath).Path
    }
    $sdkRoot = if ($env:ANDROID_SDK_ROOT) { $env:ANDROID_SDK_ROOT } else { $env:ANDROID_HOME }
    if (-not $sdkRoot) { $sdkRoot = Join-Path $env:LOCALAPPDATA 'Android\Sdk' }
    $candidates = @(Get-ChildItem -LiteralPath (Join-Path $sdkRoot 'build-tools') -Directory `
            -ErrorAction SilentlyContinue | Sort-Object `
            @{ Expression = { Get-BuildToolsSortVersion $_.Name }; Descending = $true }, `
            @{ Expression = { $_.Name }; Descending = $true })
    foreach ($candidate in $candidates) {
        $path = Join-Path $candidate.FullName $Name
        if (Test-Path -LiteralPath $path -PathType Leaf) { return $path }
    }
    throw "Unable to locate Android build tool $Name."
}

function Write-Utf8Evidence {
    param([string] $Path, [AllowEmptyString()][string] $Text)
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Remove-RemoteCaptureArtifacts {
    param(
        [Parameter(Mandatory = $true)][string] $Adb,
        [Parameter(Mandatory = $true)][string[]] $AdbBase,
        [Parameter(Mandatory = $true)][string[]] $RemotePaths
    )
    foreach ($remote in $RemotePaths) {
        if (-not $remote) { continue }
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($AdbBase + @('shell', 'rm', '-f', $remote)) `
            -TimeoutSeconds 15 | Out-Null
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($AdbBase + @('shell', 'test', '!', '-e', $remote)) `
            -TimeoutSeconds 15 | Out-Null
    }
}

function Stop-RemotePerfetto {
    param(
        [Parameter(Mandatory = $true)][string] $Adb,
        [Parameter(Mandatory = $true)][string[]] $AdbBase,
        [Parameter(Mandatory = $true)][string] $RemoteTrace,
        [string] $WrapperPid,
        [ValidateRange(1, 100)][int] $PollAttempts = 25,
        [ValidateRange(1, 5000)][int] $PollMilliseconds = 200
    )
    function Get-MatchingPerfettoPids {
        $query = 'pids=$(pidof perfetto 2>/dev/null || true); printf "__FLYNES_OK__%s\n" "$pids"'
        $result = Invoke-CaptureProcess -FilePath $Adb `
            -Arguments ($AdbBase + @('shell', $query)) -TimeoutSeconds 15
        if ($result.ExitCode -ne 0 -or $result.StdOut.Trim() -notmatch '^__FLYNES_OK__(.*)$') {
            throw 'Unable to confirm the remote Perfetto process list.'
        }
        $pidText = $Matches[1].Trim()
        foreach ($candidate in ($pidText -split '\s+')) {
            if ($candidate -notmatch '^\d+$') { continue }
            $commandQuery = "if [ ! -d /proc/$candidate ]; then printf '__FLYNES_GONE__\n'; elif [ -r /proc/$candidate/cmdline ]; then printf '__FLYNES_ALIVE__'; cat /proc/$candidate/cmdline; else printf '__FLYNES_ERROR__\n'; fi"
            $commandResult = Invoke-CaptureProcess -FilePath $Adb `
                -Arguments ($AdbBase + @('shell', $commandQuery)) -TimeoutSeconds 15
            $command = $commandResult.StdOut
            if ($commandResult.ExitCode -ne 0 -or $command -match '^__FLYNES_ERROR__') {
                throw "Unable to verify Perfetto process $candidate."
            }
            if ($command -match '^__FLYNES_GONE__') { continue }
            if ($command -notmatch '^__FLYNES_ALIVE__') {
                throw "Invalid Perfetto process-query sentinel for $candidate."
            }
            if ($command.Substring('__FLYNES_ALIVE__'.Length) `
                    -match [regex]::Escape($RemoteTrace)) { $candidate }
        }
    }
    function Test-MatchingWrapperAlive {
        if ($WrapperPid -notmatch '^\d+$') { return $false }
        $query = "if [ ! -d /proc/$WrapperPid ]; then printf '__FLYNES_GONE__\n'; elif [ -r /proc/$WrapperPid/cmdline ]; then printf '__FLYNES_ALIVE__'; cat /proc/$WrapperPid/cmdline; else printf '__FLYNES_ERROR__\n'; fi"
        $result = Invoke-CaptureProcess -FilePath $Adb `
            -Arguments ($AdbBase + @('shell', $query)) -TimeoutSeconds 5
        if ($result.ExitCode -ne 0 -or $result.StdOut -match '^__FLYNES_ERROR__') {
            throw 'Unable to confirm the Perfetto wrapper state.'
        }
        if ($result.StdOut -match '^__FLYNES_GONE__') { return $false }
        if ($result.StdOut -notmatch '^__FLYNES_ALIVE__') {
            throw 'Invalid Perfetto wrapper-query sentinel.'
        }
        $command = $result.StdOut.Substring('__FLYNES_ALIVE__'.Length)
        if ($command -notmatch [regex]::Escape($RemoteTrace)) {
            throw 'Perfetto wrapper PID was reused before cleanup could verify it.'
        }
        return $true
    }
    function Send-ExactSignal([string] $Signal) {
        foreach ($candidate in @(Get-MatchingPerfettoPids)) {
            Invoke-CaptureProcess -FilePath $Adb `
                -Arguments ($AdbBase + @('shell', 'kill', $Signal, $candidate)) `
                -TimeoutSeconds 5 | Out-Null
        }
        if (Test-MatchingWrapperAlive) {
            Invoke-CaptureProcess -FilePath $Adb `
                -Arguments ($AdbBase + @('shell', 'kill', $Signal, $WrapperPid)) `
                -TimeoutSeconds 5 | Out-Null
        }
    }
    function Wait-ExactExit {
        for ($attempt = 0; $attempt -lt $PollAttempts; $attempt++) {
            if (@(Get-MatchingPerfettoPids).Count -eq 0 `
                    -and -not (Test-MatchingWrapperAlive)) { return $true }
            Start-Sleep -Milliseconds $PollMilliseconds
        }
        return $false
    }

    Send-ExactSignal '-INT'
    if (Wait-ExactExit) { return }
    Send-ExactSignal '-TERM'
    if (Wait-ExactExit) { return }
    Send-ExactSignal '-KILL'
    if (Wait-ExactExit) { return }
    throw 'Perfetto child or wrapper remained alive after INT/TERM/KILL cleanup.'
}

function Invoke-DisplayEvidenceCapture {
    param(
        [Parameter(Mandatory = $true)][string] $DeviceSerial,
        [Parameter(Mandatory = $true)][string] $DeviceModel,
        [Parameter(Mandatory = $true)][string] $InputApk,
        [Parameter(Mandatory = $true)][string] $EvidenceRoot,
        [Parameter(Mandatory = $true)][int] $TraceSeconds,
        [Parameter(Mandatory = $true)][string] $Adb,
        [Parameter(Mandatory = $true)][string] $Aapt,
        [Parameter(Mandatory = $true)][string] $ApkSigner,
        [Parameter(Mandatory = $true)][ValidateSet('PUBLIC', '60', '120')]
        [string] $TrialMode,
        [string] $TrialPairId
    )
    $resolvedApk = (Resolve-Path -LiteralPath $InputApk -ErrorAction Stop).Path
    $devicesText = (Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments @('devices', '-l')).StdOut
    $transportDevices = @(Get-ConnectedAdbDevices $devicesText)
    if ($transportDevices.Count -ne 1) {
        throw "Exactly one authorized adb device is required; found $($transportDevices.Count)."
    }
    $reportedSerial = (Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments @('-s', $transportDevices[0].Serial, 'shell', 'getprop', 'ro.serialno')).StdOut.Trim()
    $reportedModel = (Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments @('-s', $transportDevices[0].Serial, 'shell', 'getprop',
                    'ro.product.model')).StdOut.Trim()
    $isEmulator = (Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments @('-s', $transportDevices[0].Serial, 'shell', 'getprop',
                    'ro.kernel.qemu')).StdOut.Trim()
    $isBootEmulator = (Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments @('-s', $transportDevices[0].Serial, 'shell', 'getprop',
                    'ro.boot.qemu')).StdOut.Trim()
    if (Test-IsVirtualDevice -KernelQemu $isEmulator -BootQemu $isBootEmulator) {
        throw 'Physical display evidence cannot be captured from an emulator.'
    }
    $verifiedDevice = @([pscustomobject]@{ Serial = $reportedSerial; Model = $reportedModel })
    $device = Assert-CaptureDevice -Devices $verifiedDevice `
        -RequiredSerial $DeviceSerial -RequiredModel $DeviceModel
    $identity = Get-ApkIdentity -Tool $Aapt -Path $resolvedApk
    if ($identity.Package -ne $script:FlyNesPackage) {
        throw "Unexpected APK package: $($identity.Package)"
    }
    Assert-TrialContract -TrialMode $TrialMode -TrialPairId $TrialPairId `
        -Debuggable $identity.Debuggable
    $certificate = Get-ApkCertificateSha256 -Tool $ApkSigner -Path $resolvedApk
    $apkHash = (Get-FileHash -LiteralPath $resolvedApk -Algorithm SHA256).Hash.ToLowerInvariant()
    $timestamp = [DateTime]::UtcNow.ToString('yyyyMMddTHHmmssfffZ')
    $output = New-EvidenceDirectory -Root $EvidenceRoot `
        -Leaf "$timestamp-$($DeviceSerial)-$([Guid]::NewGuid().ToString('N').Substring(0, 8))"
    $token = [Guid]::NewGuid().ToString('N')
    $remoteApk = ''
    $remoteTrace = "/data/misc/perfetto-traces/flynes-$token.pftrace"
    $remotePerfettoStatus = "/data/local/tmp/flynes-$token.perfetto-exit"
    $remoteScreenshot = "/data/local/tmp/flynes-$token.png"
    $remoteHierarchy = "/data/local/tmp/flynes-$token.xml"
    $adbBase = @('-s', $DeviceSerial)
    $startedAt = [DateTime]::UtcNow
    $perfettoPid = ''
    $cleanupComplete = $false
    try {
        $packageDump = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'dumpsys', 'package', $script:FlyNesPackage))).StdOut
        Write-Utf8Evidence (Join-Path $output 'package.txt') $packageDump
        $packagePathText = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'pm', 'path', $script:FlyNesPackage))).StdOut.Trim()
        $packagePaths = @(($packagePathText -split "`r?`n") | Where-Object { $_ -match '^package:' })
        if ($packagePaths.Count -ne 1) { throw 'Expected one installed base APK path.' }
        $remoteApk = $packagePaths[0].Substring('package:'.Length).Trim()
        $installedApk = Join-Path $output 'installed-base.apk'
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('pull', $remoteApk, $installedApk)) -TimeoutSeconds 120 | Out-Null
        $installedIdentity = Get-ApkIdentity -Tool $Aapt -Path $installedApk
        $installedCertificate = Get-ApkCertificateSha256 -Tool $ApkSigner -Path $installedApk
        $installedHash = (Get-FileHash -LiteralPath $installedApk -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($installedIdentity.Package -ne $identity.Package -or
                $installedIdentity.VersionCode -ne $identity.VersionCode -or
                $installedCertificate -ne $certificate -or $installedHash -ne $apkHash) {
            throw 'Installed APK identity, certificate, or bytes do not match the supplied APK.'
        }

        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('shell', 'am', 'force-stop', $script:FlyNesPackage)) | Out-Null
        if ($TrialMode -eq 'PUBLIC') {
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'monkey', '-p', $script:FlyNesPackage,
                        '-c', 'android.intent.category.LAUNCHER', '1')) | Out-Null
            Start-Sleep -Seconds 2
            $homeHierarchy = Join-Path $output 'home-ui.xml'
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'uiautomator', 'dump', $remoteHierarchy)) `
                -TimeoutSeconds 60 | Out-Null
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('pull', $remoteHierarchy, $homeHierarchy)) | Out-Null
            $builtin = Get-UiTargetCenter -XmlPath $homeHierarchy `
                -ResourceId "$($script:FlyNesPackage):id/category_builtin"
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'input', 'tap', [string]$builtin.X,
                        [string]$builtin.Y)) | Out-Null
            Start-Sleep -Seconds 1
            $builtinHierarchy = Join-Path $output 'builtin-ui.xml'
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'uiautomator', 'dump', $remoteHierarchy)) `
                -TimeoutSeconds 60 | Out-Null
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('pull', $remoteHierarchy, $builtinHierarchy)) | Out-Null
            $launch = Get-UiTargetCenter -XmlPath $builtinHierarchy `
                -ResourceId "$($script:FlyNesPackage):id/launch_selected"
            Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'input', 'tap', [string]$launch.X,
                        [string]$launch.Y)) | Out-Null
        }
        else {
            Invoke-CheckedCaptureProcess -FilePath $Adb -Arguments ($adbBase + @(
                    'shell', 'am', 'start', '-W', '-n',
                    "$($script:FlyNesPackage)/.MainActivity", '--es',
                    'com.flynes.emu.extra.CERTIFY_NATIVE_TIME_MODE', $TrialMode)) | Out-Null
        }
        Start-Sleep -Seconds 2
        $appPid = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'pidof', '-s', $script:FlyNesPackage))).StdOut.Trim()
        if ($appPid -notmatch '^\d+$') { throw 'FlyNES did not remain alive after launch.' }
        $layers = ''
        $layer = $null
        for ($attempt = 0; $attempt -lt 30 -and -not $layer; $attempt++) {
            $layers = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                    -Arguments ($adbBase + @('shell', 'dumpsys', 'SurfaceFlinger', '--list'))).StdOut
            try { $layer = Select-GameSurfaceLayer $layers } catch { Start-Sleep -Seconds 1 }
        }
        if (-not $layer) { throw 'FlyNES game SurfaceView did not appear within 30 seconds.' }
        Write-Utf8Evidence (Join-Path $output 'surface-layers.txt') $layers

        $settleDeadline = [DateTime]::UtcNow.AddSeconds(10)
        do {
            $settleDump = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                    -Arguments ($adbBase + @('shell', 'dumpsys', 'display'))).StdOut
            $targetDisplayMode = Get-ActiveDisplayMode $settleDump
            $settled = $TrialMode -eq 'PUBLIC' -or
                    [Math]::Abs($targetDisplayMode.RefreshHz - [double]$TrialMode) -le 1.0
            if (-not $settled) { Start-Sleep -Milliseconds 250 }
        } while (-not $settled -and [DateTime]::UtcNow -lt $settleDeadline)
        if (-not $settled) {
            throw "Display did not settle at $TrialMode Hz before trace; actual=$($targetDisplayMode.RefreshHz)"
        }
        $requestedDisplayMode = $null
        if ($TrialMode -ne 'PUBLIC') {
            $requestLog = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                    -Arguments ($adbBase + @('logcat', '-d', "--pid=$appPid", '-v', 'brief'))).StdOut
            $requestedDisplayMode = Get-AppRequestedDisplayMode $requestLog
            $sameRequestedMode = Test-SameActiveDisplayMode $requestedDisplayMode $targetDisplayMode
            $sameRequestedRate = [Math]::Abs(
                $requestedDisplayMode.RefreshHz - [double]$TrialMode) -le 1.0
            if (-not $sameRequestedRate -or -not $sameRequestedMode) {
                throw "System mode does not match the app-confirmed request identity."
            }
        }
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('shell', 'dumpsys', 'SurfaceFlinger', '--latency-clear', $layer)) | Out-Null

        $perfettoCommand = "sh -c 'perfetto -o $remoteTrace -t ${TraceSeconds}s -b 64mb sched freq idle gfx view; code=`$?; echo `$code > $remotePerfettoStatus' >/dev/null 2>&1 & echo `$!"
        $perfettoStart = Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('shell', $perfettoCommand)) -TimeoutSeconds 45
        if ($perfettoStart.StdOut -notmatch '(?m)^\s*(\d+)\s*$') {
            throw 'Perfetto background launch did not return an exact process id.'
        }
        $perfettoPid = $Matches[1]
        $modeSamples = @()
        $sampleStarted = [DateTime]::UtcNow
        $previousSampleAt = $null
        $lastPerfettoAliveAt = $null
        $perfettoStoppedAt = $null
        while (-not $perfettoStoppedAt) {
            $sampleAt = [DateTime]::UtcNow
            if ($previousSampleAt -and ($sampleAt - $previousSampleAt).TotalSeconds -gt 5.0) {
                throw 'Display-mode sampling gap exceeded five seconds.'
            }
            $aliveResult = Invoke-CaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'kill', '-0', $perfettoPid)) `
                -TimeoutSeconds 5
            $alive = $aliveResult.ExitCode -eq 0
            if ($alive) { $lastPerfettoAliveAt = $sampleAt }
            $elapsed = ($sampleAt - $sampleStarted).TotalSeconds
            if (-not $alive -and $elapsed -lt ($TraceSeconds - 1.0)) {
                throw "Perfetto exited before its requested ${TraceSeconds}s trace completed."
            }
            if ($alive -and $elapsed -gt ($TraceSeconds + 60.0)) {
                throw 'Perfetto did not finish after its bounded trace window.'
            }
            $sampleDump = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                    -Arguments ($adbBase + @('shell', 'dumpsys', 'display')) `
                    -TimeoutSeconds 5).StdOut
            $sampleMode = Get-ActiveDisplayMode $sampleDump
            if (-not (Test-SameActiveDisplayMode $targetDisplayMode $sampleMode)) {
                throw "Active display mode changed during trace: expected $($targetDisplayMode | ConvertTo-Json -Compress), actual $($sampleMode | ConvertTo-Json -Compress)"
            }
            $modeSamples += [ordered]@{
                observedAtUtc = $sampleAt.ToString('o')
                modeId = $sampleMode.ModeId
                width = $sampleMode.Width
                height = $sampleMode.Height
                refreshHz = $sampleMode.RefreshHz
            }
            $previousSampleAt = $sampleAt
            if ($alive) { Start-Sleep -Seconds 1 } else { $perfettoStoppedAt = [DateTime]::UtcNow }
        }
        if (-not $lastPerfettoAliveAt) { throw 'Perfetto was never observed alive.' }
        $observedTraceSeconds = Assert-PerfettoRuntime -StartedAt $sampleStarted `
            -LastAliveAt $lastPerfettoAliveAt `
            -StoppedAt $perfettoStoppedAt -ExpectedSeconds $TraceSeconds
        $perfettoExitText = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'cat', $remotePerfettoStatus))).StdOut
        Assert-PerfettoExitStatus $perfettoExitText | Out-Null
        Write-Utf8Evidence (Join-Path $output 'display-mode-samples.json') `
            ($modeSamples | ConvertTo-Json -Depth 3)
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('pull', $remoteTrace, (Join-Path $output 'display.pftrace'))) `
            -TimeoutSeconds 180 | Out-Null
        if ((Get-Item -LiteralPath (Join-Path $output 'display.pftrace')).Length -le 0) {
            throw 'Perfetto trace is empty.'
        }
        $afterPid = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'pidof', '-s', $script:FlyNesPackage))).StdOut.Trim()
        if ($afterPid -ne $appPid) { throw 'FlyNES exited or restarted during the capture.' }

        $displayDump = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'dumpsys', 'display'))).StdOut
        $finalDisplayMode = Get-ActiveDisplayMode $displayDump
        if (-not (Test-SameActiveDisplayMode $targetDisplayMode $finalDisplayMode)) {
            throw 'Active display mode changed after the final trace sample.'
        }
        $actualRefreshHz = $finalDisplayMode.RefreshHz
        Write-Utf8Evidence (Join-Path $output 'display.txt') $displayDump
        $latency = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'dumpsys', 'SurfaceFlinger', '--latency', $layer))).StdOut
        Write-Utf8Evidence (Join-Path $output 'surface-latency.txt') $latency
        $gfx = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'dumpsys', 'gfxinfo', $script:FlyNesPackage,
                        'framestats'))).StdOut
        Write-Utf8Evidence (Join-Path $output 'gfxinfo-framestats.txt') $gfx
        $log = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('logcat', '-d', "--pid=$appPid", '-v', 'threadtime'))).StdOut
        Write-Utf8Evidence (Join-Path $output 'logcat.txt') $log
        $sourceTiming = Get-CoreConfirmedSourceTiming $log
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('shell', 'screencap', '-p', $remoteScreenshot)) | Out-Null
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('pull', $remoteScreenshot, (Join-Path $output 'screen.png'))) | Out-Null
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('shell', 'uiautomator', 'dump', $remoteHierarchy)) `
            -TimeoutSeconds 60 | Out-Null
        Invoke-CheckedCaptureProcess -FilePath $Adb `
            -Arguments ($adbBase + @('pull', $remoteHierarchy, (Join-Path $output 'ui.xml'))) | Out-Null
        $deviceDump = (Invoke-CheckedCaptureProcess -FilePath $Adb `
                -Arguments ($adbBase + @('shell', 'getprop'))).StdOut
        Write-Utf8Evidence (Join-Path $output 'device-properties.txt') $deviceDump

        Stop-RemotePerfetto -Adb $Adb -AdbBase $adbBase -RemoteTrace $remoteTrace `
            -WrapperPid $perfettoPid
        Remove-RemoteCaptureArtifacts -Adb $Adb -AdbBase $adbBase `
            -RemotePaths @($remoteTrace, $remotePerfettoStatus, $remoteScreenshot,
                    $remoteHierarchy)
        $cleanupComplete = $true

        $manifest = [ordered]@{
            schema = 1
            complete = $true
            package = $identity.Package
            versionCode = $identity.VersionCode
            versionName = $identity.VersionName
            apkSha256 = $apkHash
            signerCertificateSha256 = $certificate
            serial = $device.Serial
            model = $device.Model
            surfaceLayer = $layer
            durationSeconds = $TraceSeconds
            trialMode = $TrialMode
            pairId = if ($TrialMode -eq 'PUBLIC') { $null } else { $TrialPairId }
            requestedRefreshHz = if ($TrialMode -eq 'PUBLIC') { $null } else { [int]$TrialMode }
            requestedModeId = if ($requestedDisplayMode) { $requestedDisplayMode.ModeId } else { $null }
            requestedWidth = if ($requestedDisplayMode) { $requestedDisplayMode.Width } else { $null }
            requestedHeight = if ($requestedDisplayMode) { $requestedDisplayMode.Height } else { $null }
            actualRefreshHz = $actualRefreshHz
            sourceTiming = $sourceTiming
            activeModeId = $finalDisplayMode.ModeId
            activeWidth = $finalDisplayMode.Width
            activeHeight = $finalDisplayMode.Height
            observedTraceSeconds = $observedTraceSeconds
            displayModeSampleCount = $modeSamples.Count
            startedAtUtc = $startedAt.ToString('o')
            completedAtUtc = [DateTime]::UtcNow.ToString('o')
        }
        Write-Utf8Evidence (Join-Path $output 'manifest.json') `
            ($manifest | ConvertTo-Json -Depth 4)
        return $output
    }
    finally {
        if (-not $cleanupComplete) {
            Stop-RemotePerfetto -Adb $Adb -AdbBase $adbBase `
                -RemoteTrace $remoteTrace -WrapperPid $perfettoPid
            Remove-RemoteCaptureArtifacts -Adb $Adb -AdbBase $adbBase `
                -RemotePaths @($remoteTrace, $remotePerfettoStatus,
                        $remoteScreenshot, $remoteHierarchy)
        }
    }
}

if ($MyInvocation.InvocationName -ne '.') {
    if (-not $Serial -or -not $ExpectedModel -or -not $ApkPath) {
        throw '-Serial, -ExpectedModel, and -ApkPath are mandatory for a capture transaction.'
    }
    if (-not $OutputRoot) {
        $repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
        $OutputRoot = Join-Path $repoRoot '.artifacts\evidence\display'
    }
    $resolvedAapt = Resolve-BuildTool -Name 'aapt.exe' -ExplicitPath $AaptPath
    $resolvedSigner = Resolve-BuildTool -Name 'apksigner.bat' -ExplicitPath $ApkSignerPath
    $result = Invoke-DisplayEvidenceCapture -DeviceSerial $Serial `
        -DeviceModel $ExpectedModel -InputApk $ApkPath -EvidenceRoot $OutputRoot `
        -TraceSeconds $DurationSeconds -Adb $AdbPath -Aapt $resolvedAapt `
        -ApkSigner $resolvedSigner -TrialMode $Mode -TrialPairId $PairId
    Write-Output $result
}
