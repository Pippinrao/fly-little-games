[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Host', 'HarmonyEmulator', 'AndroidEmulator',
        'HarmonyDevice', 'AndroidDevice')]
    [string]$Stage,
    [string]$EvidenceDirectory,
    [string]$CMakeExe = 'cmake',
    [string]$CTestExe = 'ctest',
    [string]$NodeExe,
    [string]$HvigorScript,
    [string]$DevEcoSdkHome,
    [string]$HdcExe = 'hdc',
    [string]$HarmonyTarget,
    [string]$AndroidTarget,
    [ValidateSet('Debug', 'Release')]
    [string]$HostConfiguration = 'Debug'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if (-not $EvidenceDirectory) {
    $stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
    $EvidenceDirectory = Join-Path $repoRoot "out\evidence\completion-$($Stage.ToLowerInvariant())-$stamp"
}
$EvidenceDirectory = [IO.Path]::GetFullPath($EvidenceDirectory)
New-Item -ItemType Directory -Path $EvidenceDirectory -Force | Out-Null

function Invoke-GateStep {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][string]$WorkingDirectory,
        [Parameter(Mandatory = $true)][string]$Executable,
        [Parameter(Mandatory = $true)][string[]]$Arguments
    )
    $logPath = Join-Path $EvidenceDirectory "$Name.log"
    Push-Location $WorkingDirectory
    try {
        & $Executable @Arguments 2>&1 | Tee-Object -FilePath $logPath | Out-Host
        $exitCode = $LASTEXITCODE
    } finally {
        Pop-Location
    }
    if ($exitCode -ne 0) {
        throw "$Name failed with exit code $exitCode; see $logPath"
    }
    return $logPath
}

function Get-HdcArguments {
    param([Parameter(Mandatory = $true)][string[]]$CommandArguments)
    if ($HarmonyTarget) {
        return @('-t', $HarmonyTarget) + $CommandArguments
    }
    return $CommandArguments
}

$gitRevision = (& git -C $repoRoot rev-parse HEAD).Trim()
$gitDirty = [bool](& git -C $repoRoot status --porcelain)
$metadata = [ordered]@{
    stage = $Stage
    startedAt = (Get-Date).ToUniversalTime().ToString('o')
    repository = $repoRoot
    revision = $gitRevision
    dirty = $gitDirty
    harmonyTarget = $HarmonyTarget
    androidTarget = $AndroidTarget
}
$metadata | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $EvidenceDirectory 'metadata.json') -Encoding utf8

if ($Stage -eq 'Host') {
    $buildDirectory = Join-Path $repoRoot 'out\harmony-host'
    if (-not (Test-Path -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt'))) {
        $configure = @('-S', (Join-Path $repoRoot 'harmony\tests'), '-B', $buildDirectory,
            '-G', 'Visual Studio 17 2022', '-A', 'x64')
        if ($env:FLYNES_ZLIB_ROOT) {
            $configure += "-DZLIB_ROOT=$($env:FLYNES_ZLIB_ROOT)"
        }
        Invoke-GateStep -Name 'host-configure' -WorkingDirectory $repoRoot `
            -Executable $CMakeExe -Arguments $configure | Out-Null
    }
    Invoke-GateStep -Name 'host-build' -WorkingDirectory $repoRoot `
        -Executable $CMakeExe -Arguments @('--build', $buildDirectory, '--config',
            $HostConfiguration) | Out-Null
    Invoke-GateStep -Name 'host-ctest' -WorkingDirectory $repoRoot `
        -Executable $CTestExe -Arguments @('--test-dir', $buildDirectory, '-C',
            $HostConfiguration, '--output-on-failure') | Out-Null
} elseif ($Stage -eq 'HarmonyEmulator' -or $Stage -eq 'HarmonyDevice') {
    if (-not $NodeExe -or -not $HvigorScript -or -not $DevEcoSdkHome) {
        throw 'Harmony stages require -NodeExe, -HvigorScript, and -DevEcoSdkHome.'
    }
    $DevEcoSdkHome = [IO.Path]::GetFullPath($DevEcoSdkHome)
    if (-not (Test-Path -LiteralPath (Join-Path $DevEcoSdkHome 'default'))) {
        throw "DevEco SDK home must contain the default SDK: $DevEcoSdkHome"
    }
    if ($Stage -eq 'HarmonyDevice' -and -not $HarmonyTarget) {
        throw 'HarmonyDevice requires an explicit -HarmonyTarget.'
    }
    $previousDevEcoSdkHome = $env:DEVECO_SDK_HOME
    try {
        $env:DEVECO_SDK_HOME = $DevEcoSdkHome
        $harmonyRoot = Join-Path $repoRoot 'harmony'
        Invoke-GateStep -Name 'harmony-app-build' -WorkingDirectory $harmonyRoot `
            -Executable $NodeExe -Arguments @($HvigorScript, 'assembleHap', '-p',
                'product=default', '-p', 'module=entry@default', '-p', 'buildMode=debug') | Out-Null
        Invoke-GateStep -Name 'harmony-test-build' -WorkingDirectory $harmonyRoot `
            -Executable $NodeExe -Arguments @($HvigorScript, 'assembleHap', '-p',
                'product=default', '-p', 'module=entry@ohosTest', '-p', 'buildMode=debug') | Out-Null
        $mainHap = Join-Path $harmonyRoot `
            'entry\build\default\outputs\default\entry-default-unsigned.hap'
        $testHap = Join-Path $harmonyRoot `
            'entry\build\default\outputs\ohosTest\entry-ohosTest-unsigned.hap'
        Invoke-GateStep -Name 'harmony-app-install' -WorkingDirectory $repoRoot `
            -Executable $HdcExe -Arguments (Get-HdcArguments -CommandArguments `
                @('install', '-r', $mainHap)) | Out-Null
        Invoke-GateStep -Name 'harmony-test-install' -WorkingDirectory $repoRoot `
            -Executable $HdcExe -Arguments (Get-HdcArguments -CommandArguments `
                @('install', '-r', $testHap)) | Out-Null
        $hypiumLog = Invoke-GateStep -Name 'harmony-hypium' -WorkingDirectory $repoRoot `
            -Executable $HdcExe -Arguments (Get-HdcArguments -CommandArguments `
                @('shell', 'aa', 'test', '-b', 'com.flynes.emu', '-m', 'entry_test',
                    '-s', 'unittest', 'OpenHarmonyTestRunner'))
    } finally {
        $env:DEVECO_SDK_HOME = $previousDevEcoSdkHome
    }
    if (-not (Select-String -LiteralPath $hypiumLog `
            -Pattern 'Failure: 0, Error: 0' -Quiet)) {
        throw "Hypium did not report a zero-failure result; see $hypiumLog"
    }
} else {
    if ($Stage -eq 'AndroidDevice' -and -not $AndroidTarget) {
        throw 'AndroidDevice requires an explicit -AndroidTarget.'
    }
    $previousSerial = $env:ANDROID_SERIAL
    try {
        if ($AndroidTarget) {
            $env:ANDROID_SERIAL = $AndroidTarget
        }
        Invoke-GateStep -Name 'android-unit-and-package' -WorkingDirectory $repoRoot `
            -Executable (Join-Path $repoRoot 'gradlew.bat') `
            -Arguments @(':app:testDebugUnitTest', ':app:assembleDebug', '--console=plain') | Out-Null
        Invoke-GateStep -Name 'android-instrumentation' -WorkingDirectory $repoRoot `
            -Executable (Join-Path $repoRoot 'gradlew.bat') `
            -Arguments @(':app:connectedDebugAndroidTest', '--console=plain') | Out-Null
    } finally {
        $env:ANDROID_SERIAL = $previousSerial
    }
}

$metadata.completedAt = (Get-Date).ToUniversalTime().ToString('o')
$metadata.result = 'PASS'
$metadata | ConvertTo-Json | Set-Content -LiteralPath (
    Join-Path $EvidenceDirectory 'metadata.json') -Encoding utf8
Write-Output "PASS $Stage evidence=$EvidenceDirectory"
