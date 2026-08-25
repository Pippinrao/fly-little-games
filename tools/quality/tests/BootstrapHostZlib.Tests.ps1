[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$bootstrapPath = Join-Path $repoRoot 'tools\quality\bootstrap_host_zlib.ps1'
$pwshExe = (Get-Process -Id $PID).Path

function New-FakeToolchain {
    param(
        [Parameter(Mandatory = $true)][string] $Directory,
        [string] $CMakeVersion = '3.22.1-test',
        [string] $CTestVersion = '3.22.1-test',
        [int] $ConfigureExitCode = 0,
        [int] $ConfigureDelayMilliseconds = 0,
        [bool] $RequireTrackFileAccessDisabled = $false,
        [string] $RequiredWorkingDirectory = '',
        [string] $CompilerId = 'MSVC',
        [string] $CompilerVersion = '19.44.35219.0'
    )

    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $cmakePath = Join-Path $Directory 'cmake.ps1'
    $ctestPath = Join-Path $Directory 'ctest.ps1'

    $cmakeTemplate = @'
if ('__REQUIRED_WORKING_DIRECTORY__' -and -not [System.IO.Path]::GetFullPath($PWD.Path).Equals(
        '__REQUIRED_WORKING_DIRECTORY__', [StringComparison]::OrdinalIgnoreCase)) {
    [Console]::Error.WriteLine("expected working directory __REQUIRED_WORKING_DIRECTORY__; found $($PWD.Path)")
    exit 18
}
if ($args -contains '--version') {
    'cmake version __CMAKE_VERSION__'
    exit 0
}
$buildIndex = [Array]::IndexOf($args, '-B')
if ($buildIndex -ge 0 -and $buildIndex + 1 -lt $args.Count) {
    if (__REQUIRE_TRACKING_DISABLED__ -eq 1 -and (
            $args -notcontains '-DCMAKE_VS_GLOBALS=TrackFileAccess=false' -or
            $args -notcontains '-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS')) {
        [Console]::Error.WriteLine('expected propagated TrackFileAccess=false configuration')
        exit 17
    }
    if (__CONFIGURE_DELAY_MS__ -gt 0) { Start-Sleep -Milliseconds __CONFIGURE_DELAY_MS__ }
    $buildDirectory = $args[$buildIndex + 1]
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    if (__CONFIGURE_EXIT__ -eq 0) {
        @(
            'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022'
            'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64'
        ) | Set-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Encoding utf8
        $compilerDirectory = Join-Path $buildDirectory 'CMakeFiles\3.22.1'
        New-Item -ItemType Directory -Path $compilerDirectory -Force | Out-Null
        @(
            'set(CMAKE_C_COMPILER_ID "__COMPILER_ID__")'
            'set(CMAKE_C_COMPILER_VERSION "__COMPILER_VERSION__")'
        ) | Set-Content -LiteralPath (Join-Path $compilerDirectory 'CMakeCCompiler.cmake') -Encoding utf8
    }
    exit __CONFIGURE_EXIT__
}
exit 0
'@
    $cmakeSource = $cmakeTemplate.Replace('__CMAKE_VERSION__', $CMakeVersion)
    $cmakeSource = $cmakeSource.Replace('__CONFIGURE_EXIT__', $ConfigureExitCode.ToString())
    $cmakeSource = $cmakeSource.Replace('__CONFIGURE_DELAY_MS__', $ConfigureDelayMilliseconds.ToString())
    $trackingRequired = if ($RequireTrackFileAccessDisabled) { '1' } else { '0' }
    $cmakeSource = $cmakeSource.Replace('__REQUIRE_TRACKING_DISABLED__', $trackingRequired)
    $escapedWorkingDirectory = $RequiredWorkingDirectory.Replace("'", "''")
    $cmakeSource = $cmakeSource.Replace('__REQUIRED_WORKING_DIRECTORY__', $escapedWorkingDirectory)
    $cmakeSource = $cmakeSource.Replace('__COMPILER_ID__', $CompilerId)
    $cmakeSource = $cmakeSource.Replace('__COMPILER_VERSION__', $CompilerVersion)
    $cmakeSource | Set-Content -LiteralPath $cmakePath -Encoding utf8
    $ctestWorkingDirectory = $escapedWorkingDirectory
    @"
if ('$ctestWorkingDirectory' -and -not [System.IO.Path]::GetFullPath(`$PWD.Path).Equals(
        '$ctestWorkingDirectory', [StringComparison]::OrdinalIgnoreCase)) {
    [Console]::Error.WriteLine("expected working directory $ctestWorkingDirectory; found `$(`$PWD.Path)")
    exit 18
}
'ctest version $CTestVersion'
exit 0
"@ |
        Set-Content -LiteralPath $ctestPath -Encoding utf8

    return [pscustomobject]@{
        CMakeExe = $cmakePath
        CTestExe = $ctestPath
    }
}

function Invoke-BootstrapProcess {
    param(
        [Parameter(Mandatory = $true)][string] $OutputDirectory,
        [string] $CMakeExe,
        [string] $CTestExe,
        [string] $LocalPropertiesPath,
        [string] $ArchivePath,
        [int] $ChildProcessTimeoutSeconds
    )

    $arguments = @('-NoProfile', '-File', $bootstrapPath, '-OutputDirectory', $OutputDirectory)
    if ($PSBoundParameters.ContainsKey('CMakeExe')) { $arguments += @('-CMakeExe', $CMakeExe) }
    if ($PSBoundParameters.ContainsKey('CTestExe')) { $arguments += @('-CTestExe', $CTestExe) }
    if ($PSBoundParameters.ContainsKey('LocalPropertiesPath')) {
        $arguments += @('-LocalPropertiesPath', $LocalPropertiesPath)
    }
    if ($PSBoundParameters.ContainsKey('ArchivePath')) { $arguments += @('-ArchivePath', $ArchivePath) }
    if ($PSBoundParameters.ContainsKey('ChildProcessTimeoutSeconds')) {
        $arguments += @('-ChildProcessTimeoutSeconds', $ChildProcessTimeoutSeconds)
    }

    $output = & $pwshExe @arguments 2>&1 | Out-String
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $output
    }
}

Describe 'bootstrap_host_zlib.ps1 fatal validation' {
    BeforeEach {
        $caseRoot = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $outputDirectory = Join-Path $caseRoot 'host-deps'
    }

    It 'rejects a relative optional CMake path' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe '.\cmake.ps1' -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'CMakeExe must be an absolute path'
    }

    It 'rejects a missing CMake executable' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe (Join-Path $caseRoot 'missing\cmake.exe') -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'CMake executable does not exist'
    }

    It 'rejects a missing CTest executable' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe (Join-Path $caseRoot 'missing\ctest.exe')

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'CTest executable does not exist'
    }

    It 'rejects a missing local.properties when explicit tools are omitted' {
        $missingProperties = Join-Path $caseRoot 'missing.properties'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -LocalPropertiesPath $missingProperties

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'local.properties does not exist'
    }

    It 'rejects a wrong CMake version' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -CMakeVersion '3.27.0-test'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected CMake 3.22.1'
    }

    It 'rejects a wrong CTest version' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -CTestVersion '3.27.0-test'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected CTest 3.22.1'
    }

    It 'rejects different CMake and CTest 3.22.1 distributions' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -CMakeVersion '3.22.1-build-a' -CTestVersion '3.22.1-build-b'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'same 3.22.1 distribution'
    }

    It 'rejects CMake and CTest from different directories' {
        $first = New-FakeToolchain -Directory (Join-Path $caseRoot 'first')
        $second = New-FakeToolchain -Directory (Join-Path $caseRoot 'second')

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $first.CMakeExe -CTestExe $second.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'same directory'
    }

    It 'rejects an unavailable Visual Studio 17 2022 x64 generator' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -ConfigureExitCode 9

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'toolchain probe configure failed'
    }

    It 'fails closed when a child tool exceeds its bounded timeout' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -ConfigureDelayMilliseconds 5000

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ChildProcessTimeoutSeconds 1

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'timed out after 1 second'
    }

    It 'passes propagated TrackFileAccess=false settings to the MSVC configure probe' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -RequireTrackFileAccessDisabled $true -CompilerId 'GNU'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected an x64 MSVC compiler'
        $result.Output | Should Not Match 'expected propagated TrackFileAccess=false configuration'
    }

    It 'anchors every child process in the requested output directory' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -RequiredWorkingDirectory $outputDirectory
        $archive = Join-Path $caseRoot 'zlib-1.3.1.tar.gz'
        'deliberately wrong archive' | Set-Content -LiteralPath $archive -Encoding utf8

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ArchivePath $archive

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'archive SHA-256 mismatch'
        $result.Output | Should Not Match 'expected working directory'
    }

    It 'rejects a configured non-MSVC compiler' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') -CompilerId 'GNU'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected an x64 MSVC compiler'
    }

    It 'rejects a stale zlib build generator' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $buildDirectory = Join-Path $outputDirectory 'zlib-1.3.1-build'
        New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
        @'
CMAKE_GENERATOR:INTERNAL=Ninja
CMAKE_GENERATOR_PLATFORM:INTERNAL=x64
'@ | Set-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Encoding utf8

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'stale build generator'
    }

    It 'rejects a stale zlib build architecture' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $buildDirectory = Join-Path $outputDirectory 'zlib-1.3.1-build'
        New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
        @'
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_PLATFORM:INTERNAL=Win32
'@ | Set-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Encoding utf8

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'stale build architecture'
    }

    It 'rejects an archive whose SHA-256 is not the pinned release hash' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $archive = Join-Path $caseRoot 'zlib-1.3.1.tar.gz'
        'not the pinned archive' | Set-Content -LiteralPath $archive -Encoding utf8

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ArchivePath $archive

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'archive SHA-256 mismatch'
    }

    It 'pins immutable zlib input and canonical configure/install arguments' {
        $source = Get-Content -LiteralPath $bootstrapPath -Raw

        $source | Should Match ([regex]::Escape('https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz'))
        $source | Should Match '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
        $source | Should Match 'Visual Studio 17 2022'
        $source | Should Match 'BUILD_SHARED_LIBS=OFF'
        $source | Should Match 'CMAKE_INSTALL_PREFIX'
        $source | Should Match "'--config', 'Release'"
        $source | Should Match 'CMAKE_VS_GLOBALS=TrackFileAccess=false'
        $source | Should Match 'CMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS'
        $source | Should Match '\$startInfo\.WorkingDirectory\s*=\s*\$WorkingDirectory'
        $source | Should Match '-Description ''zlib archive extraction''[^\r\n]*\r?\n\s*-WorkingDirectory \$extractStagingDirectory'
        $source | Should Match '\$configureSourceRoot\s*=\s*Assert-PathWithinRoot'
        $source | Should Match "'-S', \`$configureSourceRoot"
        @([regex]::Matches($source, 'Get-CanonicalTreeHash -Root \$sourceRoot')).Count | Should BeGreaterThan 1
    }
}
