[CmdletBinding()]
param(
    [string] $OutputDirectory = '.artifacts/host-deps',
    [string] $CMakeExe,
    [string] $CTestExe,
    [string] $LocalPropertiesPath = (Join-Path $PSScriptRoot '..\..\local.properties'),
    [string] $ArchivePath,
    [ValidateRange(1, 3600)]
    [int] $ChildProcessTimeoutSeconds = 120
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version 3.0

$zlibVersion = '1.3.1'
$zlibArchiveName = 'zlib-1.3.1.tar.gz'
$zlibArchiveUrl = 'https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz'
$zlibArchiveSha256 = '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
$generator = 'Visual Studio 17 2022'
$architecture = 'x64'
$requiredToolVersion = '3.22.1'
$msbuildTrackingArguments = @(
    '-DCMAKE_VS_GLOBALS=TrackFileAccess=false',
    '-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS'
)

function Get-AbsolutePath {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $BaseDirectory
    )

    if ([System.IO.Path]::IsPathFullyQualified($Path)) {
        return [System.IO.Path]::GetFullPath($Path)
    }
    return [System.IO.Path]::GetFullPath((Join-Path $BaseDirectory $Path))
}

function Assert-PathWithinRoot {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Candidate,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $resolvedRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $resolvedCandidate = [System.IO.Path]::GetFullPath($Candidate).TrimEnd('\')
    $rootPrefix = $resolvedRoot + [System.IO.Path]::DirectorySeparatorChar
    if (-not $resolvedCandidate.Equals($resolvedRoot, [StringComparison]::OrdinalIgnoreCase) -and
            -not $resolvedCandidate.StartsWith($rootPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        throw "$Label escapes the requested output directory: $resolvedCandidate"
    }
    return $resolvedCandidate
}

function Assert-AbsoluteToolPath {
    param(
        [Parameter(Mandatory = $true)][string] $Name,
        [Parameter(Mandatory = $true)][string] $Path
    )

    if (-not [System.IO.Path]::IsPathFullyQualified($Path)) {
        throw "$Name must be an absolute path: $Path"
    }
}

function ConvertFrom-JavaPropertiesPath {
    param([Parameter(Mandatory = $true)][string] $Value)

    $normalized = $Value.Trim()
    $normalized = $normalized.Replace('\:', ':')
    $normalized = $normalized.Replace('\\', '\')
    $normalized = $normalized.Replace('\ ', ' ')
    $normalized = $normalized.Replace('\=', '=')
    return $normalized
}

function Resolve-HostTools {
    param(
        [string] $RequestedCMake,
        [string] $RequestedCTest,
        [Parameter(Mandatory = $true)][string] $PropertiesPath
    )

    if ($RequestedCMake) { Assert-AbsoluteToolPath -Name 'CMakeExe' -Path $RequestedCMake }
    if ($RequestedCTest) { Assert-AbsoluteToolPath -Name 'CTestExe' -Path $RequestedCTest }

    if (-not $RequestedCMake -and -not $RequestedCTest) {
        if (-not (Test-Path -LiteralPath $PropertiesPath -PathType Leaf)) {
            throw "local.properties does not exist: $PropertiesPath"
        }
        $sdkLine = Get-Content -LiteralPath $PropertiesPath |
            Where-Object { $_ -match '^\s*sdk\.dir\s*=' } |
            Select-Object -First 1
        if (-not $sdkLine) {
            throw "local.properties does not define sdk.dir: $PropertiesPath"
        }
        $sdkValue = ($sdkLine -split '=', 2)[1]
        $sdkDirectory = ConvertFrom-JavaPropertiesPath -Value $sdkValue
        if (-not [System.IO.Path]::IsPathFullyQualified($sdkDirectory)) {
            throw "sdk.dir must resolve to an absolute path: $sdkDirectory"
        }
        $toolDirectory = Join-Path $sdkDirectory 'cmake\3.22.1\bin'
        $RequestedCMake = Join-Path $toolDirectory 'cmake.exe'
        $RequestedCTest = Join-Path $toolDirectory 'ctest.exe'
    }
    elseif (-not $RequestedCMake) {
        $RequestedCMake = Join-Path (Split-Path -Parent $RequestedCTest) `
            ('cmake' + [System.IO.Path]::GetExtension($RequestedCTest))
    }
    elseif (-not $RequestedCTest) {
        $RequestedCTest = Join-Path (Split-Path -Parent $RequestedCMake) `
            ('ctest' + [System.IO.Path]::GetExtension($RequestedCMake))
    }

    $resolvedCMake = [System.IO.Path]::GetFullPath($RequestedCMake)
    $resolvedCTest = [System.IO.Path]::GetFullPath($RequestedCTest)
    if (-not (Test-Path -LiteralPath $resolvedCMake -PathType Leaf)) {
        throw "CMake executable does not exist: $resolvedCMake"
    }
    if (-not (Test-Path -LiteralPath $resolvedCTest -PathType Leaf)) {
        throw "CTest executable does not exist: $resolvedCTest"
    }

    $cmakeDirectory = [System.IO.Path]::GetFullPath((Split-Path -Parent $resolvedCMake)).TrimEnd('\')
    $ctestDirectory = [System.IO.Path]::GetFullPath((Split-Path -Parent $resolvedCTest)).TrimEnd('\')
    if (-not $cmakeDirectory.Equals($ctestDirectory, [StringComparison]::OrdinalIgnoreCase)) {
        throw "CMake and CTest must come from the same directory: '$cmakeDirectory' vs '$ctestDirectory'."
    }

    return [pscustomobject]@{
        CMakeExe = $resolvedCMake
        CTestExe = $resolvedCTest
    }
}

function Get-PinnedToolVersion {
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [Parameter(Mandatory = $true)][ValidateSet('CMake', 'CTest')][string] $ToolName,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory
    )

    $process = Invoke-ChildProcess -Executable $Executable -Arguments @('--version') `
        -Description "$ToolName --version" -WorkingDirectory $WorkingDirectory
    if ($process.ExitCode -ne 0) {
        throw "$ToolName --version failed with exit code $($process.ExitCode)."
    }
    $prefix = if ($ToolName -eq 'CMake') { 'cmake' } else { 'ctest' }
    $firstLine = @($process.Output -split "`r?`n" | ForEach-Object { $_.Trim() } |
        Where-Object { $_ -match "^$prefix version " } | Select-Object -First 1)
    if ($firstLine.Count -eq 0 -or $firstLine[0] -notmatch "^$prefix version (?<version>\S+)$") {
        throw "Unable to parse $ToolName version output."
    }
    $distributionVersion = $Matches.version
    if ($distributionVersion -notmatch '^3\.22\.1(?:[-+].*)?$') {
        throw "Expected $ToolName 3.22.1, found $distributionVersion."
    }
    return $distributionVersion
}

function Get-LowerSha256 {
    param([Parameter(Mandatory = $true)][string] $Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-CMakeCacheValue {
    param(
        [Parameter(Mandatory = $true)][string] $CachePath,
        [Parameter(Mandatory = $true)][string] $Key
    )

    if (-not (Test-Path -LiteralPath $CachePath -PathType Leaf)) { return $null }
    $escapedKey = [regex]::Escape($Key)
    $line = Get-Content -LiteralPath $CachePath |
        Where-Object { $_ -match "^$escapedKey(?::[^=]*)?=" } |
        Select-Object -First 1
    if (-not $line) { return $null }
    return ($line -split '=', 2)[1].Trim()
}

function Assert-CompatibleBuildCache {
    param(
        [Parameter(Mandatory = $true)][string] $BuildDirectory,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    if (-not (Test-Path -LiteralPath $cachePath -PathType Leaf)) { return }
    $cachedGenerator = Get-CMakeCacheValue -CachePath $cachePath -Key 'CMAKE_GENERATOR'
    $cachedArchitecture = Get-CMakeCacheValue -CachePath $cachePath -Key 'CMAKE_GENERATOR_PLATFORM'
    if ($cachedGenerator -ne $generator) {
        throw "$Label has a stale build generator '$cachedGenerator'; expected '$generator'."
    }
    if ($cachedArchitecture -ne $architecture) {
        throw "$Label has a stale build architecture '$cachedArchitecture'; expected '$architecture'."
    }
}

function Invoke-ChildProcess {
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string] $Description,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory
    )

    $startInfo = [Diagnostics.ProcessStartInfo]::new()
    if ([System.IO.Path]::GetExtension($Executable) -eq '.ps1') {
        $startInfo.FileName = (Get-Process -Id $PID).Path
        $startInfo.ArgumentList.Add('-NoProfile')
        $startInfo.ArgumentList.Add('-File')
        $startInfo.ArgumentList.Add($Executable)
    }
    else {
        $startInfo.FileName = $Executable
    }
    foreach ($argument in $Arguments) { $startInfo.ArgumentList.Add($argument) }
    if (-not [System.IO.Path]::IsPathFullyQualified($WorkingDirectory) -or
            -not (Test-Path -LiteralPath $WorkingDirectory -PathType Container)) {
        throw "Child process working directory must be an existing absolute directory: $WorkingDirectory"
    }
    $startInfo.WorkingDirectory = $WorkingDirectory
    $startInfo.UseShellExecute = $false
    $startInfo.CreateNoWindow = $true
    $startInfo.RedirectStandardOutput = $true
    $startInfo.RedirectStandardError = $true

    $process = [Diagnostics.Process]::new()
    $process.StartInfo = $startInfo
    try {
        if (-not $process.Start()) { throw "Unable to start $Description." }
        $standardOutput = $process.StandardOutput.ReadToEndAsync()
        $standardError = $process.StandardError.ReadToEndAsync()
        if (-not $process.WaitForExit($ChildProcessTimeoutSeconds * 1000)) {
            try { $process.Kill($true) } catch { }
            $process.WaitForExit()
            throw "$Description timed out after $ChildProcessTimeoutSeconds second(s)."
        }
        $outputParts = @(
            $standardOutput.GetAwaiter().GetResult().TrimEnd()
            $standardError.GetAwaiter().GetResult().TrimEnd()
        ) | Where-Object { $_ }
        return [pscustomobject]@{
            ExitCode = $process.ExitCode
            Output = ($outputParts -join [Environment]::NewLine)
        }
    }
    finally {
        $process.Dispose()
    }
}

function Invoke-Checked {
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string] $Description,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory
    )

    $process = Invoke-ChildProcess -Executable $Executable -Arguments $Arguments `
        -Description $Description -WorkingDirectory $WorkingDirectory
    if ($process.Output) { Write-Host $process.Output }
    if ($process.ExitCode -ne 0) {
        throw "$Description failed with exit code $($process.ExitCode)."
    }
}

function Get-CompilerIdentity {
    param([Parameter(Mandatory = $true)][string] $BuildDirectory)

    $compilerFile = Get-ChildItem -LiteralPath (Join-Path $BuildDirectory 'CMakeFiles') `
        -Recurse -File -Filter 'CMakeCCompiler.cmake' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $compilerFile) {
        throw 'Toolchain probe did not emit CMakeCCompiler.cmake.'
    }
    $source = Get-Content -LiteralPath $compilerFile.FullName -Raw
    $idMatch = [regex]::Match($source, 'set\(CMAKE_C_COMPILER_ID\s+"(?<value>[^"]+)"\)')
    $versionMatch = [regex]::Match($source, 'set\(CMAKE_C_COMPILER_VERSION\s+"(?<value>[^"]+)"\)')
    if (-not $idMatch.Success -or -not $versionMatch.Success) {
        throw 'Toolchain probe compiler identity is incomplete.'
    }
    return [pscustomobject]@{
        Id = $idMatch.Groups['value'].Value
        Version = $versionMatch.Groups['value'].Value
    }
}

function Get-CanonicalTreeHash {
    param([Parameter(Mandatory = $true)][string] $Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Tree root does not exist: $Root"
    }
    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $records = [System.Collections.Generic.List[string]]::new()
    Get-ChildItem -LiteralPath $rootPath -Recurse -File |
        Sort-Object FullName |
        ForEach-Object {
            $relative = [System.IO.Path]::GetRelativePath($rootPath, $_.FullName).Replace('\', '/')
            $records.Add("$(Get-LowerSha256 -Path $_.FullName)  $relative`n")
        }
    $bytes = [Text.Encoding]::UTF8.GetBytes([string]::Concat($records))
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $sha.Dispose()
    }
}

function Assert-PreviousManifestIntegrity {
    param(
        [Parameter(Mandatory = $true)][string] $ManifestPath,
        [Parameter(Mandatory = $true)][string] $OutputRoot,
        [Parameter(Mandatory = $true)][string] $ResolvedCMake,
        [Parameter(Mandatory = $true)][string] $ResolvedCTest
    )

    if (-not (Test-Path -LiteralPath $ManifestPath -PathType Leaf)) { return }
    $manifest = Get-Content -LiteralPath $ManifestPath -Raw | ConvertFrom-Json
    $checks = @(
        @($ResolvedCMake, [string]$manifest.toolchain.cmake.sha256, 'CMake executable'),
        @($ResolvedCTest, [string]$manifest.toolchain.ctest.sha256, 'CTest executable'),
        @((Join-Path $OutputRoot $manifest.archive.file), [string]$manifest.archive.sha256, 'zlib archive'),
        @((Join-Path $OutputRoot $manifest.install.header.path), [string]$manifest.install.header.sha256, 'installed zlib header'),
        @((Join-Path $OutputRoot $manifest.install.staticLibrary.path), [string]$manifest.install.staticLibrary.sha256, 'installed zlib static library'),
        @((Join-Path $OutputRoot $manifest.install.license.path), [string]$manifest.install.license.sha256, 'installed zlib license'),
        @((Join-Path $OutputRoot $manifest.toolchain.manifestFile), [string]$manifest.toolchain.manifestSha256, 'host toolchain manifest')
    )
    foreach ($check in $checks) {
        $path = [string]$check[0]
        $expected = [string]$check[1]
        $label = [string]$check[2]
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Previously recorded $label is missing: $path"
        }
        $actual = Get-LowerSha256 -Path $path
        if ($actual -ne $expected) {
            throw "Previously recorded $label hash changed: expected $expected, found $actual."
        }
    }
    $sourceRoot = Join-Path $OutputRoot $manifest.extractedTree.directory
    $actualTreeHash = Get-CanonicalTreeHash -Root $sourceRoot
    if ($actualTreeHash -ne [string]$manifest.extractedTree.canonicalSha256) {
        throw "Previously recorded zlib extracted tree hash changed: expected $($manifest.extractedTree.canonicalSha256), found $actualTreeHash."
    }
}

function Write-ToolchainManifest {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $ResolvedCMake,
        [Parameter(Mandatory = $true)][string] $ResolvedCTest,
        [Parameter(Mandatory = $true)][string] $CMakeVersion,
        [Parameter(Mandatory = $true)][string] $CTestVersion,
        [Parameter(Mandatory = $true)][pscustomobject] $Compiler
    )

    $escape = { param([string] $Value) $Value.Replace("'", "''") }
    $content = @(
        '@{'
        "    CMakeExe = '$(& $escape $ResolvedCMake)'"
        "    CTestExe = '$(& $escape $ResolvedCTest)'"
        "    CMakeVersion = '$(& $escape $CMakeVersion)'"
        "    CTestVersion = '$(& $escape $CTestVersion)'"
        "    Generator = '$generator'"
        "    Architecture = '$architecture'"
        '    MultiConfig = $true'
        '    MSBuildTrackFileAccess = $false'
        "    CMakeConfigureArguments = @('$($msbuildTrackingArguments -join "', '")')"
        "    CompilerId = '$(& $escape $Compiler.Id)'"
        "    CompilerVersion = '$(& $escape $Compiler.Version)'"
        '}'
    )
    $content | Set-Content -LiteralPath $Path -Encoding utf8
}

$workingDirectory = (Get-Location).Path
$outputRoot = Get-AbsolutePath -Path $OutputDirectory -BaseDirectory $workingDirectory
$propertiesPath = Get-AbsolutePath -Path $LocalPropertiesPath -BaseDirectory $workingDirectory
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null

$tools = Resolve-HostTools -RequestedCMake $CMakeExe -RequestedCTest $CTestExe `
    -PropertiesPath $propertiesPath
$cmakeVersion = Get-PinnedToolVersion -Executable $tools.CMakeExe -ToolName CMake `
    -WorkingDirectory $outputRoot
$ctestVersion = Get-PinnedToolVersion -Executable $tools.CTestExe -ToolName CTest `
    -WorkingDirectory $outputRoot
if ($cmakeVersion -ne $ctestVersion) {
    throw "CMake and CTest must be the same 3.22.1 distribution: '$cmakeVersion' vs '$ctestVersion'."
}

$cmakeSha256 = Get-LowerSha256 -Path $tools.CMakeExe
$ctestSha256 = Get-LowerSha256 -Path $tools.CTestExe
$toolchainManifestPath = Join-Path $outputRoot 'host-toolchain.psd1'
$preflightManifestPath = Join-Path $outputRoot 'zlib-1.3.1-preflight.json'
Assert-PreviousManifestIntegrity -ManifestPath $preflightManifestPath -OutputRoot $outputRoot `
    -ResolvedCMake $tools.CMakeExe -ResolvedCTest $tools.CTestExe

$probeSourceDirectory = Join-Path $outputRoot 'toolchain-probe-source'
$probeBuildDirectory = Join-Path $outputRoot 'toolchain-probe-build'
$zlibBuildDirectory = Join-Path $outputRoot 'zlib-1.3.1-build'
Assert-CompatibleBuildCache -BuildDirectory $probeBuildDirectory -Label 'Toolchain probe build directory'
Assert-CompatibleBuildCache -BuildDirectory $zlibBuildDirectory -Label 'zlib build directory'

New-Item -ItemType Directory -Path $probeSourceDirectory -Force | Out-Null
@'
cmake_minimum_required(VERSION 3.22.1)
project(flynes_host_toolchain_probe LANGUAGES C)
if(NOT MSVC)
  message(FATAL_ERROR "FlyNES canonical Windows host gate requires MSVC")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "FlyNES canonical Windows host gate requires x64")
endif()
'@ | Set-Content -LiteralPath (Join-Path $probeSourceDirectory 'CMakeLists.txt') -Encoding utf8

$probeArguments = @(
    '-G', $generator,
    '-A', $architecture,
    '-S', $probeSourceDirectory,
    '-B', $probeBuildDirectory
) + $msbuildTrackingArguments
Invoke-Checked -Executable $tools.CMakeExe -Arguments $probeArguments `
    -Description 'Canonical toolchain probe configure' -WorkingDirectory $outputRoot
$probeGenerator = Get-CMakeCacheValue -CachePath (Join-Path $probeBuildDirectory 'CMakeCache.txt') `
    -Key 'CMAKE_GENERATOR'
$probeArchitecture = Get-CMakeCacheValue -CachePath (Join-Path $probeBuildDirectory 'CMakeCache.txt') `
    -Key 'CMAKE_GENERATOR_PLATFORM'
if ($probeGenerator -ne $generator -or $probeArchitecture -ne $architecture) {
    throw "Toolchain probe did not preserve generator '$generator' and architecture '$architecture'."
}
$compiler = Get-CompilerIdentity -BuildDirectory $probeBuildDirectory
if ($compiler.Id -ne 'MSVC') {
    throw "Expected an x64 MSVC compiler, found '$($compiler.Id) $($compiler.Version)'."
}

Write-ToolchainManifest -Path $toolchainManifestPath -ResolvedCMake $tools.CMakeExe `
    -ResolvedCTest $tools.CTestExe -CMakeVersion $cmakeVersion -CTestVersion $ctestVersion `
    -Compiler $compiler
$toolchainManifestSha256 = Get-LowerSha256 -Path $toolchainManifestPath

$canonicalArchivePath = Join-Path $outputRoot $zlibArchiveName
if ($ArchivePath) {
    Assert-AbsoluteToolPath -Name 'ArchivePath' -Path $ArchivePath
    $selectedArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
    if (-not (Test-Path -LiteralPath $selectedArchivePath -PathType Leaf)) {
        throw "ArchivePath does not exist: $selectedArchivePath"
    }
}
else {
    $selectedArchivePath = $canonicalArchivePath
    if (-not (Test-Path -LiteralPath $selectedArchivePath -PathType Leaf)) {
        Write-Host "Downloading immutable zlib $zlibVersion archive."
        Invoke-WebRequest -Uri $zlibArchiveUrl -OutFile $selectedArchivePath
    }
}
$archiveSha256 = Get-LowerSha256 -Path $selectedArchivePath
if ($archiveSha256 -ne $zlibArchiveSha256) {
    throw "zlib archive SHA-256 mismatch: expected $zlibArchiveSha256, found $archiveSha256."
}
if (-not [System.IO.Path]::GetFullPath($selectedArchivePath).Equals(
        [System.IO.Path]::GetFullPath($canonicalArchivePath), [StringComparison]::OrdinalIgnoreCase)) {
    Copy-Item -LiteralPath $selectedArchivePath -Destination $canonicalArchivePath -Force
}

$extractStagingDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-extract-staging') -Label 'Extraction staging directory'
if (Test-Path -LiteralPath $extractStagingDirectory) {
    Remove-Item -LiteralPath $extractStagingDirectory -Recurse -Force
}
New-Item -ItemType Directory -Path $extractStagingDirectory | Out-Null
Invoke-Checked -Executable $tools.CMakeExe -Arguments @('-E', 'tar', 'xzf', $canonicalArchivePath) `
    -Description 'zlib archive extraction' `
    -WorkingDirectory $extractStagingDirectory
$stagedSourceRoot = Join-Path $extractStagingDirectory 'zlib-1.3.1'
if (-not (Test-Path -LiteralPath $stagedSourceRoot -PathType Container)) {
    throw 'Pinned zlib archive did not extract the expected zlib-1.3.1 directory.'
}
$stagedTreeHash = Get-CanonicalTreeHash -Root $stagedSourceRoot
$sourceRoot = Assert-PathWithinRoot -Root $outputRoot -Candidate (Join-Path $outputRoot 'zlib-1.3.1') `
    -Label 'zlib source directory'
if (Test-Path -LiteralPath $sourceRoot -PathType Container) {
    $existingTreeHash = Get-CanonicalTreeHash -Root $sourceRoot
    if ($existingTreeHash -ne $stagedTreeHash) {
        throw "Existing zlib extracted tree differs from the pinned archive: expected $stagedTreeHash, found $existingTreeHash."
    }
    Remove-Item -LiteralPath $extractStagingDirectory -Recurse -Force
}
else {
    Move-Item -LiteralPath $stagedSourceRoot -Destination $sourceRoot
    Remove-Item -LiteralPath $extractStagingDirectory -Recurse -Force
}
$sourceTreeSha256 = Get-CanonicalTreeHash -Root $sourceRoot

$configureSourceRoot = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-configure-source') `
    -Label 'zlib disposable configure source directory'
if (Test-Path -LiteralPath $configureSourceRoot) {
    Remove-Item -LiteralPath $configureSourceRoot -Recurse -Force
}
Copy-Item -LiteralPath $sourceRoot -Destination $configureSourceRoot -Recurse
$configureSourceHash = Get-CanonicalTreeHash -Root $configureSourceRoot
if ($configureSourceHash -ne $sourceTreeSha256) {
    throw "Disposable zlib configure source differs from the verified extracted tree: expected $sourceTreeSha256, found $configureSourceHash."
}

$installPrefix = Join-Path $outputRoot 'zlib-1.3.1-install'
$configureArguments = @(
    '-G', $generator,
    '-A', $architecture,
    '-S', $configureSourceRoot,
    '-B', $zlibBuildDirectory,
    '-DBUILD_SHARED_LIBS=OFF',
    "-DCMAKE_INSTALL_PREFIX=$installPrefix"
) + $msbuildTrackingArguments
Invoke-Checked -Executable $tools.CMakeExe -Arguments $configureArguments `
    -Description 'zlib static Release configure' -WorkingDirectory $outputRoot
$buildArguments = @('--build', $zlibBuildDirectory, '--config', 'Release')
Invoke-Checked -Executable $tools.CMakeExe -Arguments $buildArguments `
    -Description 'zlib static Release build' -WorkingDirectory $outputRoot
$installArguments = @('--install', $zlibBuildDirectory, '--config', 'Release')
Invoke-Checked -Executable $tools.CMakeExe -Arguments $installArguments `
    -Description 'zlib static Release install' -WorkingDirectory $outputRoot
$postBuildSourceTreeSha256 = Get-CanonicalTreeHash -Root $sourceRoot
if ($postBuildSourceTreeSha256 -ne $sourceTreeSha256) {
    throw "The immutable zlib extracted tree changed during configure/build: expected $sourceTreeSha256, found $postBuildSourceTreeSha256."
}

$installedHeader = Join-Path $installPrefix 'include\zlib.h'
$installedStaticLibrary = Join-Path $installPrefix 'lib\zlibstatic.lib'
$sourceLicense = Join-Path $sourceRoot 'LICENSE'
$installedLicense = Join-Path $installPrefix 'share\licenses\zlib-1.3.1\LICENSE'
foreach ($requiredFile in @($installedHeader, $installedStaticLibrary, $sourceLicense)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Expected zlib artifact was not installed: $requiredFile"
    }
}
New-Item -ItemType Directory -Path (Split-Path -Parent $installedLicense) -Force | Out-Null
Copy-Item -LiteralPath $sourceLicense -Destination $installedLicense -Force

$manifest = [ordered]@{
    schemaVersion = 1
    dependency = [ordered]@{
        name = 'zlib'
        version = $zlibVersion
    }
    archive = [ordered]@{
        file = $zlibArchiveName
        url = $zlibArchiveUrl
        sha256 = (Get-LowerSha256 -Path $canonicalArchivePath)
    }
    extractedTree = [ordered]@{
        directory = 'zlib-1.3.1'
        hashAlgorithm = 'SHA-256 of UTF-8 records: lowercase file SHA-256, two spaces, slash-normalized relative path, LF; ordinal full-path order'
        canonicalSha256 = $sourceTreeSha256
        disposableConfigureCopy = 'zlib-1.3.1-configure-source'
    }
    install = [ordered]@{
        prefix = 'zlib-1.3.1-install'
        header = [ordered]@{
            path = 'zlib-1.3.1-install/include/zlib.h'
            sha256 = (Get-LowerSha256 -Path $installedHeader)
        }
        staticLibrary = [ordered]@{
            path = 'zlib-1.3.1-install/lib/zlibstatic.lib'
            sha256 = (Get-LowerSha256 -Path $installedStaticLibrary)
        }
        license = [ordered]@{
            path = 'zlib-1.3.1-install/share/licenses/zlib-1.3.1/LICENSE'
            sha256 = (Get-LowerSha256 -Path $installedLicense)
        }
    }
    toolchain = [ordered]@{
        manifestFile = 'host-toolchain.psd1'
        manifestSha256 = $toolchainManifestSha256
        cmake = [ordered]@{
            version = $cmakeVersion
            sha256 = $cmakeSha256
        }
        ctest = [ordered]@{
            version = $ctestVersion
            sha256 = $ctestSha256
        }
        generator = $generator
        architecture = $architecture
        multiConfig = $true
        msbuildTrackFileAccess = $false
        cmakeConfigureArguments = $msbuildTrackingArguments
        compilerId = $compiler.Id
        compilerVersion = $compiler.Version
    }
}
$manifest | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $preflightManifestPath -Encoding utf8

Write-Host "Pinned zlib $zlibVersion host dependency is ready at $installPrefix"
Write-Host "Preflight manifest: $preflightManifestPath"
