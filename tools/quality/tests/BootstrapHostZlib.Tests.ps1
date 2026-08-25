[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$bootstrapPath = Join-Path $repoRoot 'tools\quality\bootstrap_host_zlib.ps1'
$pwshExe = (Get-Process -Id $PID).Path
$repositoryHostDeps = Join-Path $repoRoot '.artifacts\host-deps'
$repositoryTestRuns = Join-Path $repositoryHostDeps '.test-runs'

function New-FakeToolchain {
    param(
        [Parameter(Mandatory = $true)][string] $Directory,
        [string] $CMakeVersion = '3.22.1-test',
        [string] $CTestVersion = '3.22.1-test',
        [int] $ConfigureExitCode = 0,
        [int] $ConfigureDelayMilliseconds = 0,
        [bool] $SpawnInheritedPipeDescendant = $false,
        [string] $DescendantPidFile = '',
        [int] $DescendantDelaySeconds = 8,
        [bool] $RequireTrackFileAccessDisabled = $false,
        [bool] $RequireMsBuildNodeReuseDisabled = $false,
        [bool] $RequireReleaseTryCompile = $false,
        [string] $RequiredWorkingDirectory = '',
        [string] $CompilerId = 'MSVC',
        [string] $CompilerVersion = '19.44.35219.0'
    )

    New-Item -ItemType Directory -Path $Directory -Force | Out-Null
    $cmakePath = Join-Path $Directory 'cmake.ps1'
    $ctestPath = Join-Path $Directory 'ctest.ps1'
    $compilerPath = Join-Path $Directory 'cl.exe'
    'fake compiler identity' | Set-Content -LiteralPath $compilerPath -Encoding utf8

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
    if (__REQUIRE_MSBUILD_NODE_REUSE_DISABLED__ -eq 1 -and
            $env:MSBUILDDISABLENODEREUSE -ne '1') {
        [Console]::Error.WriteLine('expected MSBUILDDISABLENODEREUSE=1')
        exit 19
    }
    if (__REQUIRE_TRACKING_DISABLED__ -eq 1 -and (
            $args -notcontains '-DCMAKE_VS_GLOBALS=TrackFileAccess=false' -or
            $args -notcontains '-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS;CMAKE_TRY_COMPILE_CONFIGURATION')) {
        [Console]::Error.WriteLine('expected propagated TrackFileAccess=false configuration')
        exit 17
    }
    if (__REQUIRE_RELEASE_TRY_COMPILE__ -eq 1 -and (
            $args -notcontains '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release' -or
            $args -notcontains '-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS;CMAKE_TRY_COMPILE_CONFIGURATION')) {
        [Console]::Error.WriteLine('expected Release try-compile configuration')
        exit 20
    }
    if (__SPAWN_INHERITED_PIPE_DESCENDANT__ -eq 1) {
        $childInfo = [Diagnostics.ProcessStartInfo]::new()
        $childInfo.FileName = (Get-Process -Id $PID).Path
        $childInfo.ArgumentList.Add('-NoProfile')
        $childInfo.ArgumentList.Add('-Command')
        $childInfo.ArgumentList.Add("[IO.File]::WriteAllText('__DESCENDANT_PID_FILE__',[string]`$PID); Start-Sleep -Seconds __DESCENDANT_DELAY_SECONDS__")
        $childInfo.UseShellExecute = $false
        $childInfo.CreateNoWindow = $true
        $child = [Diagnostics.Process]::Start($childInfo)
        $pidDeadline = [DateTime]::UtcNow.AddSeconds(2)
        while (-not (Test-Path -LiteralPath '__DESCENDANT_PID_FILE__') -and
                [DateTime]::UtcNow -lt $pidDeadline) {
            Start-Sleep -Milliseconds 20
        }
        exit 0
    }
    if (__CONFIGURE_DELAY_MS__ -gt 0) { Start-Sleep -Milliseconds __CONFIGURE_DELAY_MS__ }
    $buildDirectory = $args[$buildIndex + 1]
    New-Item -ItemType Directory -Path $buildDirectory -Force | Out-Null
    if (__CONFIGURE_EXIT__ -eq 0) {
        @(
            'CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022'
            'CMAKE_GENERATOR_PLATFORM:INTERNAL=x64'
            'CMAKE_GENERATOR_INSTANCE:INTERNAL=__GENERATOR_INSTANCE__'
        ) | Set-Content -LiteralPath (Join-Path $buildDirectory 'CMakeCache.txt') -Encoding utf8
        $compilerDirectory = Join-Path $buildDirectory 'CMakeFiles\3.22.1'
        New-Item -ItemType Directory -Path $compilerDirectory -Force | Out-Null
        @(
            'set(CMAKE_C_COMPILER "__COMPILER_PATH__")'
            'set(CMAKE_C_COMPILER_ID "__COMPILER_ID__")'
            'set(CMAKE_C_COMPILER_VERSION "__COMPILER_VERSION__")'
        ) | Set-Content -LiteralPath (Join-Path $compilerDirectory 'CMakeCCompiler.cmake') -Encoding utf8
        @(
            '<Project>'
            '  <PropertyGroup>'
            '    <WindowsTargetPlatformVersion>10.0.26100.0</WindowsTargetPlatformVersion>'
            '    <PlatformToolset>v143</PlatformToolset>'
            '  </PropertyGroup>'
            '</Project>'
        ) | Set-Content -LiteralPath (Join-Path $buildDirectory 'ZERO_CHECK.vcxproj') -Encoding utf8
    }
    exit __CONFIGURE_EXIT__
}
exit 0
'@
    $cmakeSource = $cmakeTemplate.Replace('__CMAKE_VERSION__', $CMakeVersion)
    $cmakeSource = $cmakeSource.Replace('__CONFIGURE_EXIT__', $ConfigureExitCode.ToString())
    $cmakeSource = $cmakeSource.Replace('__CONFIGURE_DELAY_MS__', $ConfigureDelayMilliseconds.ToString())
    $spawnDescendant = if ($SpawnInheritedPipeDescendant) { '1' } else { '0' }
    $cmakeSource = $cmakeSource.Replace('__SPAWN_INHERITED_PIPE_DESCENDANT__', $spawnDescendant)
    $escapedPidFile = $DescendantPidFile.Replace("'", "''")
    $cmakeSource = $cmakeSource.Replace('__DESCENDANT_PID_FILE__', $escapedPidFile)
    $cmakeSource = $cmakeSource.Replace(
        '__DESCENDANT_DELAY_SECONDS__', $DescendantDelaySeconds.ToString())
    $trackingRequired = if ($RequireTrackFileAccessDisabled) { '1' } else { '0' }
    $cmakeSource = $cmakeSource.Replace('__REQUIRE_TRACKING_DISABLED__', $trackingRequired)
    $nodeReuseDisabledRequired = if ($RequireMsBuildNodeReuseDisabled) { '1' } else { '0' }
    $cmakeSource = $cmakeSource.Replace(
        '__REQUIRE_MSBUILD_NODE_REUSE_DISABLED__', $nodeReuseDisabledRequired)
    $releaseTryCompileRequired = if ($RequireReleaseTryCompile) { '1' } else { '0' }
    $cmakeSource = $cmakeSource.Replace(
        '__REQUIRE_RELEASE_TRY_COMPILE__', $releaseTryCompileRequired)
    $escapedWorkingDirectory = $RequiredWorkingDirectory.Replace("'", "''")
    $cmakeSource = $cmakeSource.Replace('__REQUIRED_WORKING_DIRECTORY__', $escapedWorkingDirectory)
    $cmakeSource = $cmakeSource.Replace('__COMPILER_ID__', $CompilerId)
    $cmakeSource = $cmakeSource.Replace('__COMPILER_VERSION__', $CompilerVersion)
    $cmakeSource = $cmakeSource.Replace(
        '__COMPILER_PATH__', $compilerPath.Replace('\', '/').Replace('"', '\"'))
    $cmakeSource = $cmakeSource.Replace(
        '__GENERATOR_INSTANCE__', $Directory.Replace('\', '/'))
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

function Get-ScriptFunctionBody {
    param([Parameter(Mandatory = $true)][string] $Name)

    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        $bootstrapPath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        throw "Bootstrap script has parse errors: $($parseErrors[0].Message)"
    }
    $definition = $ast.Find({
            param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq $Name
        }, $true)
    if (-not $definition) { return $null }
    return $definition.Body.GetScriptBlock()
}

function Get-ScriptFunctionText {
    param([Parameter(Mandatory = $true)][string] $Name)

    $tokens = $null
    $parseErrors = $null
    $ast = [Management.Automation.Language.Parser]::ParseFile(
        $bootstrapPath, [ref]$tokens, [ref]$parseErrors)
    if ($parseErrors.Count -ne 0) {
        throw "Bootstrap script has parse errors: $($parseErrors[0].Message)"
    }
    $definition = $ast.Find({
            param($node)
            $node -is [Management.Automation.Language.FunctionDefinitionAst] -and
                $node.Name -eq $Name
        }, $true)
    if (-not $definition) { return $null }
    return $definition.Extent.Text
}

function Get-TransactionFunctionBundle {
    $names = @(
        'Get-LowerSha256',
        'Assert-NoReparsePointAncestors',
        'Assert-NoReparsePointsInTree',
        'Assert-PathWithinRoot',
        'Remove-ManagedDirectory',
        'Remove-ManagedFile',
        'Get-CanonicalTreeHash',
        'Get-BootstrapTransactionArtifacts',
        'Assert-BootstrapTransactionPaths',
        'Test-BootstrapTransactionArtifact',
        'Get-BootstrapTransactionArtifactHash',
        'Remove-BootstrapTransactionArtifact',
        'Copy-BootstrapTransactionArtifact',
        'Move-BootstrapTransactionArtifact',
        'Restore-BootstrapTransaction',
        'Publish-BootstrapTransaction'
    )
    $definitions = [Collections.Generic.List[string]]::new()
    foreach ($name in $names) {
        $definition = Get-ScriptFunctionText -Name $name
        if (-not $definition) { return $null }
        $definitions.Add($definition)
    }
    return ($definitions -join [Environment]::NewLine)
}

function ConvertTo-PowerShellLiteral {
    param([Parameter(Mandatory = $true)][string] $Value)

    return "'$($Value.Replace("'", "''"))'"
}

function New-TransactionFixture {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][bool] $ExistingInstall
    )

    New-Item -ItemType Directory -Path $Root -Force | Out-Null
    $paths = [ordered]@{
        OutputRoot = $Root
        InstallPending = Join-Path $Root 'zlib-1.3.1-install-staging'
        InstallFinal = Join-Path $Root 'zlib-1.3.1-install'
        InstallRollback = Join-Path $Root 'zlib-1.3.1-install.rollback'
        ToolchainPending = Join-Path $Root 'host-toolchain.psd1.pending'
        PreflightPending = Join-Path $Root 'zlib-1.3.1-preflight.json.pending'
        ToolchainFinal = Join-Path $Root 'host-toolchain.psd1'
        PreflightFinal = Join-Path $Root 'zlib-1.3.1-preflight.json'
        ToolchainRollback = Join-Path $Root 'host-toolchain.psd1.rollback'
        PreflightRollback = Join-Path $Root 'zlib-1.3.1-preflight.json.rollback'
        TransactionMarker = Join-Path $Root 'manifest-publication.pending'
    }
    New-Item -ItemType Directory -Path $paths.InstallPending | Out-Null
    'new-install' | Set-Content -LiteralPath `
        (Join-Path $paths.InstallPending 'identity.txt') -Encoding utf8
    'new-toolchain' | Set-Content -LiteralPath $paths.ToolchainPending -Encoding utf8
    'new-preflight' | Set-Content -LiteralPath $paths.PreflightPending -Encoding utf8
    if ($ExistingInstall) {
        New-Item -ItemType Directory -Path $paths.InstallFinal | Out-Null
        'old-install' | Set-Content -LiteralPath `
            (Join-Path $paths.InstallFinal 'identity.txt') -Encoding utf8
        'old-toolchain' | Set-Content -LiteralPath $paths.ToolchainFinal -Encoding utf8
        'old-preflight' | Set-Content -LiteralPath $paths.PreflightFinal -Encoding utf8
    }
    return [pscustomobject]$paths
}

function Invoke-TransactionWorker {
    param(
        [Parameter(Mandatory = $true)][pscustomobject] $Paths,
        [Parameter(Mandatory = $true)][ValidateSet('Publish', 'Restore')]
        [string] $Action,
        [string] $CrashPoint = ''
    )

    $bundle = Get-TransactionFunctionBundle
    if (-not $bundle) {
        return [pscustomobject]@{
            ExitCode = -900
            Output = 'Required bootstrap transaction functions are missing.'
        }
    }
    $arguments = [Collections.Generic.List[string]]::new()
    foreach ($name in @(
            'OutputRoot', 'InstallPending', 'InstallFinal', 'InstallRollback',
            'ToolchainPending', 'PreflightPending', 'ToolchainFinal', 'PreflightFinal',
            'ToolchainRollback', 'PreflightRollback', 'TransactionMarker')) {
        $arguments.Add("-$name $(ConvertTo-PowerShellLiteral -Value $Paths.$name)")
    }
    $faultSource = if ($CrashPoint) {
        $quotedCrashPoint = ConvertTo-PowerShellLiteral -Value $CrashPoint
        @"
`$faultInjector = {
    param([string] `$point)
    if (`$point -eq $quotedCrashPoint) {
        [Diagnostics.Process]::GetCurrentProcess().Kill()
        Start-Sleep -Seconds 30
    }
}
"@
    }
    else { '$faultInjector = $null' }
    $invocation = if ($Action -eq 'Publish') {
        "Publish-BootstrapTransaction $($arguments -join ' ') -FaultInjector `$faultInjector"
    }
    else {
        "Restore-BootstrapTransaction $($arguments -join ' ')"
    }
    $source = $bundle + [Environment]::NewLine + $faultSource + `
        [Environment]::NewLine + $invocation
    $workerPath = Join-Path $Paths.OutputRoot `
        "transaction-$($Action.ToLowerInvariant())-$([Guid]::NewGuid().ToString('N')).ps1"
    $source | Set-Content -LiteralPath $workerPath -Encoding utf8
    $output = & $pwshExe -NoProfile -File $workerPath 2>&1 | Out-String
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $output
    }
}

function Get-ThrownMessage {
    param([Parameter(Mandatory = $true)][scriptblock] $Action)

    try {
        & $Action
        return $null
    }
    catch {
        return $_.Exception.Message
    }
}

function Get-LowerSha256 {
    param([Parameter(Mandatory = $true)][string] $Path)

    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Get-TestTreeRecordHash {
    param([Parameter(Mandatory = $true)][string[]] $OrderedPaths)

    $records = [Collections.Generic.List[string]]::new()
    $commonRoot = Split-Path -Parent $OrderedPaths[0]
    while (@($OrderedPaths | Where-Object {
                -not $_.StartsWith(
                    $commonRoot + [IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)
            }).Count -ne 0) {
        $commonRoot = Split-Path -Parent $commonRoot
    }
    foreach ($path in $OrderedPaths) {
        $relative = [IO.Path]::GetRelativePath($commonRoot, $path).Replace('\', '/')
        $records.Add("$(Get-LowerSha256 -Path $path)  $relative`n")
    }
    $bytes = [Text.Encoding]::UTF8.GetBytes([string]::Concat($records))
    $sha = [Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($sha.ComputeHash($bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally { $sha.Dispose() }
}

Describe 'bootstrap_host_zlib.ps1 fatal validation' {
    BeforeEach {
        $caseRoot = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $caseRoot | Out-Null
        $testRunId = [Guid]::NewGuid().ToString('N')
        $testRunRoot = Join-Path $repositoryTestRuns $testRunId
        $outputDirectory = Join-Path $testRunRoot 'host-deps'
    }

    AfterEach {
        if (Test-Path -LiteralPath $testRunRoot) {
            $resolvedTestRuns = [System.IO.Path]::GetFullPath($repositoryTestRuns).TrimEnd('\')
            $resolvedRun = [System.IO.Path]::GetFullPath($testRunRoot)
            if (-not $resolvedRun.StartsWith(
                    $resolvedTestRuns + [System.IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clean unexpected test run path: $resolvedRun"
            }
            Remove-Item -LiteralPath $resolvedRun -Recurse -Force
        }
    }

    It 'rejects a relative optional CMake path' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe '.\cmake.ps1' -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'CMakeExe must be an absolute path'
    }

    It 'rejects an absolute output outside repository host-deps before creating it' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $outsideOutput = Join-Path $caseRoot 'outside-host-deps'

        $result = Invoke-BootstrapProcess -OutputDirectory $outsideOutput `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'repository \.artifacts[/\\]host-deps'
        Test-Path -LiteralPath $outsideOutput | Should Be $false
    }

    It 'rejects a relative output outside repository host-deps before creating it' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $relativeOutput = 'build/bootstrap-output-guard-' + [Guid]::NewGuid().ToString('N')
        $resolvedOutput = Join-Path $repoRoot $relativeOutput

        $result = Invoke-BootstrapProcess -OutputDirectory $relativeOutput `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe
        $created = Test-Path -LiteralPath $resolvedOutput
        if ($created) {
            $resolvedBuildRoot = (Resolve-Path -LiteralPath (Join-Path $repoRoot 'build')).Path
            if (-not [System.IO.Path]::GetFullPath($resolvedOutput).StartsWith(
                    $resolvedBuildRoot + [System.IO.Path]::DirectorySeparatorChar,
                    [StringComparison]::OrdinalIgnoreCase)) {
                throw "Refusing to clean unexpected output path: $resolvedOutput"
            }
            Remove-Item -LiteralPath $resolvedOutput -Recurse -Force
        }

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'repository \.artifacts[/\\]host-deps'
        $created | Should Be $false
    }

    It 'normalizes a trailing separator on a valid managed output path' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -ConfigureExitCode 9
        $outputWithSeparator = $outputDirectory.TrimEnd('\') + '\'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputWithSeparator `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'toolchain probe configure failed'
        $result.Output | Should Not Match 'OutputDirectory must be'
    }

    It 'rejects a reparse-point output ancestor before writing through it' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $testContainer = Join-Path $repositoryHostDeps '.test-runs'
        New-Item -ItemType Directory -Path $testContainer -Force | Out-Null
        $outsideTarget = Join-Path $caseRoot 'junction-target'
        New-Item -ItemType Directory -Path $outsideTarget | Out-Null
        $junction = Join-Path $testContainer ([Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Junction -Path $junction -Target $outsideTarget | Out-Null
        $escapedOutput = Join-Path $junction 'host-deps'

        try {
            $result = Invoke-BootstrapProcess -OutputDirectory $escapedOutput `
                -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe
            $escapedChildCreated = Test-Path -LiteralPath (Join-Path $outsideTarget 'host-deps')
        }
        finally {
            if (Test-Path -LiteralPath $junction) {
                Remove-Item -LiteralPath $junction -Force
            }
        }

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'reparse point'
        $escapedChildCreated | Should Be $false
    }

    It 'rejects a reparse-point managed child before writing through it' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        New-Item -ItemType Directory -Path $outputDirectory -Force | Out-Null
        $outsideTarget = Join-Path $caseRoot 'managed-child-junction-target'
        New-Item -ItemType Directory -Path $outsideTarget | Out-Null
        $junction = Join-Path $outputDirectory 'toolchain-probe-source'
        New-Item -ItemType Junction -Path $junction -Target $outsideTarget | Out-Null

        try {
            $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
                -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe
            $outsideWriteOccurred = Test-Path -LiteralPath (Join-Path $outsideTarget 'CMakeLists.txt')
        }
        finally {
            if (Test-Path -LiteralPath $junction) {
                Remove-Item -LiteralPath $junction -Force
            }
        }

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'reparse point'
        $outsideWriteOccurred | Should Be $false
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

    It 'bounds inherited-pipe descendant cleanup and leaves no surviving descendant' {
        $descendantPidFile = Join-Path $caseRoot 'descendant.pid'
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -SpawnInheritedPipeDescendant $true -DescendantPidFile $descendantPidFile
        $watch = [Diagnostics.Stopwatch]::StartNew()

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ChildProcessTimeoutSeconds 1
        $watch.Stop()

        Test-Path -LiteralPath $descendantPidFile | Should Be $true
        $descendantPid = [int](Get-Content -LiteralPath $descendantPidFile -Raw)
        $descendantAlive = $null -ne (Get-Process -Id $descendantPid -ErrorAction SilentlyContinue)
        if ($descendantAlive) {
            Stop-Process -Id $descendantPid -Force -ErrorAction SilentlyContinue
        }
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'job-owned descendant[\s\S]*redirected stream'
        $watch.Elapsed.TotalSeconds | Should BeLessThan 3
        $descendantAlive | Should Be $false
    }

    It 'fails promptly when an inherited-pipe descendant would exit inside a longer timeout' {
        $descendantPidFile = Join-Path $caseRoot 'short-descendant.pid'
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -SpawnInheritedPipeDescendant $true -DescendantPidFile $descendantPidFile `
            -DescendantDelaySeconds 4
        $watch = [Diagnostics.Stopwatch]::StartNew()

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ChildProcessTimeoutSeconds 10
        $watch.Stop()

        Test-Path -LiteralPath $descendantPidFile | Should Be $true
        $descendantPid = [int](Get-Content -LiteralPath $descendantPidFile -Raw)
        $descendantAlive = $null -ne (Get-Process -Id $descendantPid -ErrorAction SilentlyContinue)
        if ($descendantAlive) {
            Stop-Process -Id $descendantPid -Force -ErrorAction SilentlyContinue
        }
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'job-owned descendant[\s\S]*redirected stream'
        $watch.Elapsed.TotalSeconds | Should BeLessThan 3
        $descendantAlive | Should Be $false
    }

    It 'accepts the native pinned CTest version process after job accounting settles' {
        $initializer = Get-ScriptFunctionBody -Name 'Initialize-BoundedProcessRunner'
        $initializer | Should Not BeNullOrEmpty
        if (-not $initializer) { return }
        & $initializer
        $sdkLine = Get-Content -LiteralPath (Join-Path $repoRoot 'local.properties') |
            Where-Object { $_ -match '^\s*sdk\.dir\s*=' } |
            Select-Object -First 1
        $sdkDirectory = (($sdkLine -split '=', 2)[1].Trim()).Replace('\:', ':').Replace('\\', '\')
        $ctestExe = Join-Path $sdkDirectory 'cmake\3.22.1\bin\ctest.exe'

        $result = [FlyNes.Quality.BoundedProcessRunner]::Run(
            $ctestExe, [string[]]@('--version'), $caseRoot, 2000, 1MB, 'Native CTest --version')

        $result.ExitCode | Should Be 0
        $result.StandardOutput | Should Match '^ctest version 3\.22\.1'
    }

    It 'transfers pipe handles exactly once and proves cleanup with partial drain setup' {
        $source = Get-Content -LiteralPath $bootstrapPath -Raw

        $source | Should Not Match `
            'new FileStream\(\s*new SafeFileHandle\((?:stdout|stderr)Read, true\)'
        $source | Should Match `
            'stdoutSafeHandle\s*=\s*new SafeFileHandle\(stdoutRead, true\);\s*stdoutRead\s*=\s*IntPtr\.Zero;\s*stdoutStream\s*=\s*new FileStream\(\s*stdoutSafeHandle,[^;]+;\s*stdoutSafeHandle\s*=\s*null;'
        $source | Should Match `
            'stderrSafeHandle\s*=\s*new SafeFileHandle\(stderrRead, true\);\s*stderrRead\s*=\s*IntPtr\.Zero;\s*stderrStream\s*=\s*new FileStream\(\s*stderrSafeHandle,[^;]+;\s*stderrSafeHandle\s*=\s*null;'
        $source | Should Match `
            '\(stdoutTask\s*==\s*null\s*\|\|\s*stdoutTask\.IsCompleted\)'
        $source | Should Match `
            '\(stderrTask\s*==\s*null\s*\|\|\s*stderrTask\.IsCompleted\)'
        $source | Should Match `
            'if \(jobOwnsProcess\)\s*\{\s*cleanupConfirmed\s*=\s*AwaitEmptyJobAndDrains'
        $source | Should Match `
            'if \(stdoutStream != null\) stdoutStream\.Dispose\(\);\s*if \(stdoutSafeHandle != null\) stdoutSafeHandle\.Dispose\(\);'
        $source | Should Match `
            'if \(stderrStream != null\) stderrStream\.Dispose\(\);\s*if \(stderrSafeHandle != null\) stderrSafeHandle\.Dispose\(\);'
    }

    It 'cleans a non-pipe descendant without rejecting its successful root process' {
        $initializer = Get-ScriptFunctionBody -Name 'Initialize-BoundedProcessRunner'
        $initializer | Should Not BeNullOrEmpty
        if (-not $initializer) { return }
        & $initializer
        $descendantPidFile = Join-Path $caseRoot 'isolated-descendant.pid'
        $escapedPidFile = $descendantPidFile.Replace("'", "''")
        $childCommand = 'Start-Sleep -Seconds 8'
        $encodedChildCommand = [Convert]::ToBase64String(
            [Text.Encoding]::Unicode.GetBytes($childCommand))
        $rootCommand = @"
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Text;
public static class NonInheritingFixtureProcess {
    [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
    private struct STARTUPINFO {
        public uint cb; public string reserved; public string desktop; public string title;
        public uint x; public uint y; public uint xSize; public uint ySize;
        public uint xCountChars; public uint yCountChars; public uint fillAttribute;
        public uint flags; public ushort showWindow; public ushort reserved2Count;
        public IntPtr reserved2; public IntPtr stdin; public IntPtr stdout; public IntPtr stderr;
    }
    [StructLayout(LayoutKind.Sequential)]
    private struct PROCESS_INFORMATION {
        public IntPtr process; public IntPtr thread; public uint processId; public uint threadId;
    }
    [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
    private static extern bool CreateProcess(
        string applicationName, StringBuilder commandLine, IntPtr processAttributes,
        IntPtr threadAttributes, bool inheritHandles, uint creationFlags,
        IntPtr environment, string currentDirectory, ref STARTUPINFO startup,
        out PROCESS_INFORMATION process);
    [DllImport("kernel32.dll")]
    private static extern bool CloseHandle(IntPtr handle);
    public static uint Start(string executable, string arguments, string workingDirectory) {
        var startup = new STARTUPINFO { cb = (uint)Marshal.SizeOf(typeof(STARTUPINFO)) };
        PROCESS_INFORMATION process;
        var command = new StringBuilder("\"" + executable + "\" " + arguments);
        if (!CreateProcess(executable, command, IntPtr.Zero, IntPtr.Zero, false,
                0x08000000, IntPtr.Zero, workingDirectory, ref startup, out process)) {
            throw new Win32Exception(Marshal.GetLastWin32Error());
        }
        CloseHandle(process.thread);
        CloseHandle(process.process);
        return process.processId;
    }
}
'@
`$childPid = [NonInheritingFixtureProcess]::Start(
    '$($pwshExe.Replace("'", "''"))', '-NoProfile -EncodedCommand $encodedChildCommand',
    '$($caseRoot.Replace("'", "''"))')
[IO.File]::WriteAllText('$escapedPidFile', [string]`$childPid)
exit 0
"@

        $result = [FlyNes.Quality.BoundedProcessRunner]::Run(
            $pwshExe, [string[]]@('-NoProfile', '-Command', $rootCommand),
            $caseRoot, 3000, 1MB, 'Isolated descendant fixture')

        Test-Path -LiteralPath $descendantPidFile | Should Be $true
        $descendantPid = [int](Get-Content -LiteralPath $descendantPidFile -Raw)
        $descendantAlive = $null -ne (Get-Process -Id $descendantPid -ErrorAction SilentlyContinue)
        if ($descendantAlive) {
            Stop-Process -Id $descendantPid -Force -ErrorAction SilentlyContinue
        }
        $result.ExitCode | Should Be 0
        $descendantAlive | Should Be $false
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

    It 'disables MSBuild node reuse in every child toolchain process' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -RequireMsBuildNodeReuseDisabled $true -CompilerId 'GNU'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected an x64 MSVC compiler'
        $result.Output | Should Not Match 'expected MSBUILDDISABLENODEREUSE=1'
    }

    It 'uses Release for every MSVC try-compile configuration' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools') `
            -RequireReleaseTryCompile $true -CompilerId 'GNU'

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Expected an x64 MSVC compiler'
        $result.Output | Should Not Match 'expected Release try-compile configuration'
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

    It 'recreates a compatible probe cache before deriving compiler identity' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $probeBuild = Join-Path $outputDirectory 'toolchain-probe-build'
        New-Item -ItemType Directory -Path $probeBuild -Force | Out-Null
        @'
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_PLATFORM:INTERNAL=x64
'@ | Set-Content -LiteralPath (Join-Path $probeBuild 'CMakeCache.txt') -Encoding utf8
        $staleMarker = Join-Path $probeBuild 'stale.marker'
        'must be removed' | Set-Content -LiteralPath $staleMarker -Encoding utf8
        $archive = Join-Path $caseRoot 'zlib-1.3.1.tar.gz'
        'not the pinned archive' | Set-Content -LiteralPath $archive -Encoding utf8

        $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
            -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ArchivePath $archive

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'archive SHA-256 mismatch'
        Test-Path -LiteralPath $staleMarker | Should Be $false
    }

    It 'rejects a nested reparse point before recursively removing a managed cache' {
        $tools = New-FakeToolchain -Directory (Join-Path $caseRoot 'tools')
        $probeBuild = Join-Path $outputDirectory 'toolchain-probe-build'
        New-Item -ItemType Directory -Path $probeBuild -Force | Out-Null
        @'
CMAKE_GENERATOR:INTERNAL=Visual Studio 17 2022
CMAKE_GENERATOR_PLATFORM:INTERNAL=x64
'@ | Set-Content -LiteralPath (Join-Path $probeBuild 'CMakeCache.txt') -Encoding utf8
        $outsideTarget = Join-Path $caseRoot 'nested-junction-target'
        New-Item -ItemType Directory -Path $outsideTarget | Out-Null
        $outsideMarker = Join-Path $outsideTarget 'must-survive.txt'
        'preserve' | Set-Content -LiteralPath $outsideMarker -Encoding utf8
        $junction = Join-Path $probeBuild 'escape'
        New-Item -ItemType Junction -Path $junction -Target $outsideTarget | Out-Null
        $archive = Join-Path $caseRoot 'zlib-1.3.1.tar.gz'
        'not the pinned archive' | Set-Content -LiteralPath $archive -Encoding utf8

        try {
            $result = Invoke-BootstrapProcess -OutputDirectory $outputDirectory `
                -CMakeExe $tools.CMakeExe -CTestExe $tools.CTestExe -ArchivePath $archive
        }
        finally {
            if (Test-Path -LiteralPath $junction) {
                Remove-Item -LiteralPath $junction -Force
            }
        }

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'reparse point'
        Test-Path -LiteralPath $outsideMarker -PathType Leaf | Should Be $true
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

    It 'accepts only the exact static install tree and rejects shared or import artifacts' {
        $validator = Get-ScriptFunctionBody -Name 'Assert-ExactStaticInstallTree'
        $validator | Should Not BeNullOrEmpty
        if (-not $validator) { return }

        $installRoot = Join-Path $caseRoot 'static-install'
        foreach ($relativeDirectory in @(
                'include', 'lib', 'share\licenses\zlib-1.3.1')) {
            New-Item -ItemType Directory -Path (Join-Path $installRoot $relativeDirectory) `
                -Force | Out-Null
        }
        foreach ($relativeFile in @(
                'include\zlib.h',
                'include\zconf.h',
                'lib\zlibstatic.lib',
                'share\licenses\zlib-1.3.1\LICENSE')) {
            'fixture' | Set-Content -LiteralPath (Join-Path $installRoot $relativeFile) -Encoding utf8
        }

        { & $validator -InstallRoot $installRoot } | Should Not Throw

        New-Item -ItemType Directory -Path (Join-Path $installRoot 'bin') | Out-Null
        'shared' | Set-Content -LiteralPath (Join-Path $installRoot 'bin\zlib.dll') -Encoding utf8
        $sharedError = Get-ThrownMessage {
            & $validator -InstallRoot $installRoot
        }
        $sharedError | Should Match 'unexpected.*bin/zlib\.dll'
        Remove-Item -LiteralPath (Join-Path $installRoot 'bin') -Recurse -Force

        'import' | Set-Content -LiteralPath (Join-Path $installRoot 'lib\zlib.lib') -Encoding utf8
        $importError = Get-ThrownMessage {
            & $validator -InstallRoot $installRoot
        }
        $importError | Should Match 'unexpected.*lib/zlib\.lib'
        Remove-Item -LiteralPath (Join-Path $installRoot 'lib\zlib.lib') -Force

        New-Item -ItemType Directory -Path (Join-Path $installRoot 'unexpected-empty') | Out-Null
        $directoryError = Get-ThrownMessage {
            & $validator -InstallRoot $installRoot
        }
        $directoryError | Should Match 'unexpected director.*unexpected-empty'
    }

    It 'hashes canonical tree records in explicit ordinal full-path order' {
        $hasher = Get-ScriptFunctionBody -Name 'Get-CanonicalTreeHash'
        $hasher | Should Not BeNullOrEmpty
        if (-not $hasher) { return }
        $treeRoot = Join-Path $caseRoot 'mixed-case-tree'
        New-Item -ItemType Directory -Path $treeRoot | Out-Null
        'lower' | Set-Content -LiteralPath (Join-Path $treeRoot 'a.txt') -Encoding utf8
        'upper' | Set-Content -LiteralPath (Join-Path $treeRoot 'B.txt') -Encoding utf8
        $ordinalPaths = [string[]]@(
            (Join-Path $treeRoot 'a.txt'), (Join-Path $treeRoot 'B.txt'))
        [Array]::Sort($ordinalPaths, [StringComparer]::Ordinal)
        $expected = Get-TestTreeRecordHash -OrderedPaths $ordinalPaths

        $actual = & $hasher -Root $treeRoot

        $actual | Should Be $expected
    }

    It 'rejects compiler version drift between the fresh probe and zlib configure' {
        $validator = Get-ScriptFunctionBody -Name 'Assert-MatchingToolchainIdentity'
        $validator | Should Not BeNullOrEmpty
        if (-not $validator) { return }
        $probe = [pscustomobject]@{
            Id = 'MSVC'; Version = '19.44.35227.0'; CompilerPath = 'C:\tool\cl.exe'
            CompilerSha256 = 'compiler-hash'; GeneratorInstance = 'C:\VS'
            PlatformToolset = 'v143'; WindowsSdk = '10.0.26100.0'
        }
        $zlib = $probe.PSObject.Copy()
        $zlib.Version = '19.43.34809.0'

        $message = Get-ThrownMessage {
            & $validator -Reference $probe -Candidate $zlib `
                -ReferenceLabel 'probe' -CandidateLabel 'zlib'
        }
        $message | Should Match 'Version drift'
    }

    It 'rejects Visual Studio instance drift between the fresh probe and zlib configure' {
        $validator = Get-ScriptFunctionBody -Name 'Assert-MatchingToolchainIdentity'
        $validator | Should Not BeNullOrEmpty
        if (-not $validator) { return }
        $probe = [pscustomobject]@{
            Id = 'MSVC'; Version = '19.44.35227.0'; CompilerPath = 'C:\tool\cl.exe'
            CompilerSha256 = 'compiler-hash'; GeneratorInstance = 'C:\VS\BuildTools'
            PlatformToolset = 'v143'; WindowsSdk = '10.0.26100.0'
        }
        $zlib = $probe.PSObject.Copy()
        $zlib.GeneratorInstance = 'C:\VS\Community'

        $message = Get-ThrownMessage {
            & $validator -Reference $probe -Candidate $zlib `
                -ReferenceLabel 'probe' -CandidateLabel 'zlib'
        }
        $message | Should Match 'GeneratorInstance drift'
    }

    It 'rejects MSVC platform toolset drift between the fresh probe and zlib configure' {
        $validator = Get-ScriptFunctionBody -Name 'Assert-MatchingToolchainIdentity'
        $validator | Should Not BeNullOrEmpty
        if (-not $validator) { return }
        $probe = [pscustomobject]@{
            Id = 'MSVC'; Version = '19.44.35227.0'; CompilerPath = 'C:\tool\cl.exe'
            CompilerSha256 = 'compiler-hash'; GeneratorInstance = 'C:\VS'
            PlatformToolset = 'v143'; WindowsSdk = '10.0.26100.0'
        }
        $zlib = $probe.PSObject.Copy()
        $zlib.PlatformToolset = 'v142'

        $message = Get-ThrownMessage {
            & $validator -Reference $probe -Candidate $zlib `
                -ReferenceLabel 'probe' -CandidateLabel 'zlib'
        }
        $message | Should Match 'PlatformToolset drift'
    }


    foreach ($crashPoint in @(
            'AfterOldInstallRemoved',
            'AfterInstallPublished',
            'AfterToolchainManifestPublished')) {
        It "recovers idempotently from a real $crashPoint process crash" {
            $bundle = Get-TransactionFunctionBundle
            $bundle | Should Not BeNullOrEmpty
            if (-not $bundle) { return }

            foreach ($existingInstall in @($false, $true)) {
                $mode = if ($existingInstall) { 'upgrade' } else { 'first-run' }
                $transactionRoot = Join-Path $caseRoot "$mode-$crashPoint"
                $paths = New-TransactionFixture -Root $transactionRoot `
                    -ExistingInstall $existingInstall
                $unmanaged = Join-Path $caseRoot "$mode-$crashPoint-unmanaged"
                New-Item -ItemType Directory -Path $unmanaged | Out-Null
                'keep' | Set-Content -LiteralPath (Join-Path $unmanaged 'sentinel.txt') -Encoding utf8

                $crash = Invoke-TransactionWorker -Paths $paths -Action Publish `
                    -CrashPoint $crashPoint

                $crash.ExitCode | Should Not Be 0
                Test-Path -LiteralPath $paths.TransactionMarker -PathType Leaf |
                    Should Be $true

                $firstRecovery = Invoke-TransactionWorker -Paths $paths -Action Restore
                $secondRecovery = Invoke-TransactionWorker -Paths $paths -Action Restore

                $firstRecovery.ExitCode | Should Be 0
                $secondRecovery.ExitCode | Should Be 0
                if ($existingInstall) {
                    (Get-Content -LiteralPath `
                            (Join-Path $paths.InstallFinal 'identity.txt') -Raw).Trim() |
                        Should Be 'old-install'
                    (Get-Content -LiteralPath $paths.ToolchainFinal -Raw).Trim() |
                        Should Be 'old-toolchain'
                    (Get-Content -LiteralPath $paths.PreflightFinal -Raw).Trim() |
                        Should Be 'old-preflight'
                }
                else {
                    Test-Path -LiteralPath $paths.InstallFinal | Should Be $false
                    Test-Path -LiteralPath $paths.ToolchainFinal | Should Be $false
                    Test-Path -LiteralPath $paths.PreflightFinal | Should Be $false
                }
                foreach ($temporaryPath in @(
                        $paths.InstallPending, $paths.InstallRollback,
                        $paths.ToolchainPending, $paths.PreflightPending,
                        $paths.ToolchainRollback, $paths.PreflightRollback,
                        $paths.TransactionMarker, "$($paths.TransactionMarker).staging")) {
                    Test-Path -LiteralPath $temporaryPath | Should Be $false
                }
                (Get-Content -LiteralPath (Join-Path $unmanaged 'sentinel.txt') -Raw).Trim() |
                    Should Be 'keep'
            }
        }
    }

    It 'rejects a differently named in-root transaction artifact without deleting it' {
        $transactionRoot = Join-Path $caseRoot 'in-root-unmanaged-transaction'
        $paths = New-TransactionFixture -Root $transactionRoot -ExistingInstall $true
        $unmanagedRollback = Join-Path $transactionRoot 'unmanaged-install-data'
        New-Item -ItemType Directory -Path $unmanagedRollback | Out-Null
        'keep' | Set-Content -LiteralPath `
            (Join-Path $unmanagedRollback 'sentinel.txt') -Encoding utf8
        $paths.InstallRollback = $unmanagedRollback

        $publish = Invoke-TransactionWorker -Paths $paths -Action Publish
        $restore = Invoke-TransactionWorker -Paths $paths -Action Restore

        $publish.ExitCode | Should Not Be 0
        $restore.ExitCode | Should Not Be 0
        (Get-Content -LiteralPath (Join-Path $unmanagedRollback 'sentinel.txt') -Raw).Trim() |
            Should Be 'keep'
        (Get-Content -LiteralPath $paths.ToolchainFinal -Raw).Trim() |
            Should Be 'old-toolchain'
        Test-Path -LiteralPath $paths.TransactionMarker | Should Be $false
    }

    It 'fails closed and preserves evidence when an install rollback hash is corrupt' {
        $transactionRoot = Join-Path $caseRoot 'corrupt-install-rollback'
        $paths = New-TransactionFixture -Root $transactionRoot -ExistingInstall $true
        $crash = Invoke-TransactionWorker -Paths $paths -Action Publish `
            -CrashPoint 'AfterOldInstallRemoved'
        $crash.ExitCode | Should Not Be 0
        'corrupt' | Add-Content -LiteralPath `
            (Join-Path $paths.InstallRollback 'identity.txt') -Encoding utf8

        $recovery = Invoke-TransactionWorker -Paths $paths -Action Restore

        $recovery.ExitCode | Should Not Be 0
        $recovery.Output | Should Match 'rollback install hash changed'
        Test-Path -LiteralPath $paths.TransactionMarker -PathType Leaf | Should Be $true
        Test-Path -LiteralPath $paths.InstallRollback -PathType Container | Should Be $true
        Test-Path -LiteralPath $paths.InstallFinal | Should Be $false
        (Get-Content -LiteralPath $paths.ToolchainFinal -Raw).Trim() |
            Should Be 'old-toolchain'
        (Get-Content -LiteralPath $paths.PreflightFinal -Raw).Trim() |
            Should Be 'old-preflight'
    }

    It 'preserves ambiguous rollback evidence when no journal or final triad exists' {
        $transactionRoot = Join-Path $caseRoot 'orphaned-first-run-rollback'
        $paths = New-TransactionFixture -Root $transactionRoot -ExistingInstall $false
        [IO.Directory]::Move($paths.InstallPending, $paths.InstallRollback)

        $recovery = Invoke-TransactionWorker -Paths $paths -Action Restore

        $recovery.ExitCode | Should Not Be 0
        $recovery.Output | Should Match 'rollback.*without a journal'
        Test-Path -LiteralPath $paths.InstallRollback -PathType Container | Should Be $true
        Test-Path -LiteralPath $paths.ToolchainPending -PathType Leaf | Should Be $true
        Test-Path -LiteralPath $paths.PreflightPending -PathType Leaf | Should Be $true
    }

    It 'rejects a nested install reparse point before publication or cleanup' {
        $transactionRoot = Join-Path $caseRoot 'reparse-install-transaction'
        $paths = New-TransactionFixture -Root $transactionRoot -ExistingInstall $true
        $outside = Join-Path $caseRoot 'reparse-outside'
        New-Item -ItemType Directory -Path $outside | Out-Null
        'keep' | Set-Content -LiteralPath (Join-Path $outside 'sentinel.txt') -Encoding utf8
        New-Item -ItemType Junction -Path (Join-Path $paths.InstallPending 'outside-link') `
            -Target $outside | Out-Null

        $publish = Invoke-TransactionWorker -Paths $paths -Action Publish
        $restore = Invoke-TransactionWorker -Paths $paths -Action Restore

        $publish.ExitCode | Should Not Be 0
        $restore.ExitCode | Should Not Be 0
        $publish.Output | Should Match 'reparse point'
        $restore.Output | Should Match 'reparse point'
        (Get-Content -LiteralPath (Join-Path $outside 'sentinel.txt') -Raw).Trim() |
            Should Be 'keep'
        Test-Path -LiteralPath $paths.InstallPending -PathType Container | Should Be $true
        (Get-Content -LiteralPath $paths.ToolchainFinal -Raw).Trim() |
            Should Be 'old-toolchain'
    }

    It 'rejects transaction paths outside its output root without side effects' {
        $bundle = Get-TransactionFunctionBundle
        $bundle | Should Not BeNullOrEmpty
        if (-not $bundle) { return }
        $transactionRoot = Join-Path $caseRoot 'contained-transaction'
        $paths = New-TransactionFixture -Root $transactionRoot -ExistingInstall $true
        $outsideRollback = Join-Path $caseRoot 'outside-install-rollback'
        New-Item -ItemType Directory -Path $outsideRollback | Out-Null
        'keep' | Set-Content -LiteralPath `
            (Join-Path $outsideRollback 'sentinel.txt') -Encoding utf8
        $paths.InstallRollback = $outsideRollback

        $publish = Invoke-TransactionWorker -Paths $paths -Action Publish
        $restore = Invoke-TransactionWorker -Paths $paths -Action Restore

        $publish.ExitCode | Should Not Be 0
        $restore.ExitCode | Should Not Be 0
        (Get-Content -LiteralPath (Join-Path $outsideRollback 'sentinel.txt') -Raw).Trim() |
            Should Be 'keep'
        (Get-Content -LiteralPath $paths.ToolchainFinal -Raw).Trim() |
            Should Be 'old-toolchain'
        Test-Path -LiteralPath $paths.TransactionMarker | Should Be $false
    }

    It 'pins immutable zlib input and canonical configure/install arguments' {
        $source = Get-Content -LiteralPath $bootstrapPath -Raw

        $source | Should Match ([regex]::Escape('https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz'))
        $source | Should Match '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
        $source | Should Match 'Visual Studio 17 2022'
        $source | Should Match 'BUILD_SHARED_LIBS=OFF'
        $source | Should Match 'CMAKE_INSTALL_PREFIX'
        $source | Should Match 'CMAKE_C_FLAGS=/Brepro'
        $source | Should Match 'CMAKE_STATIC_LINKER_FLAGS=/Brepro'
        $source | Should Match 'Invoke-WebRequest[^\r\n]+-TimeoutSec \$networkTimeoutSeconds'
        $source | Should Match '\$archiveStagingPath\s*=\s*Assert-PathWithinRoot'
        @([regex]::Matches(
                $source, 'Get-LowerSha256 -Path \$canonicalArchivePath')).Count |
            Should BeGreaterThan 1
        $source | Should Match "'--target', 'zlibstatic'"
        $source | Should Not Match "@\('--install'"
        $source | Should Match "'--config', 'Release'"
        $source | Should Match 'zlib-1\.3\.1-install/include/zconf\.h'
        $staticTreeValidation = $source.IndexOf(
            'Assert-ExactStaticInstallTree -InstallRoot $installStagingDirectory')
        $pendingToolchainWrite = $source.IndexOf(
            'Write-ToolchainManifest -Path $toolchainManifestPendingPath')
        $pairPublication = $source.LastIndexOf('Publish-BootstrapTransaction')
        $staticTreeValidation | Should BeGreaterThan -1
        $pendingToolchainWrite | Should BeGreaterThan $staticTreeValidation
        $pairPublication | Should BeGreaterThan $pendingToolchainWrite
        $source | Should Match 'CMAKE_VS_GLOBALS=TrackFileAccess=false'
        $source | Should Match 'CMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS'
        $source | Should Match '\[FlyNes\.Quality\.BoundedProcessRunner\]::Run\('
        $source | Should Match '\$runnerArguments\.ToArray\(\),\s*\r?\n\s*\$WorkingDirectory,'
        $source | Should Match '-Description ''zlib archive extraction''[^\r\n]*\r?\n\s*-WorkingDirectory \$extractStagingDirectory'
        $source | Should Match '\$configureSourceRoot\s*=\s*Assert-PathWithinRoot'
        $source | Should Match "'-S', \`$configureSourceRoot"
        @([regex]::Matches($source, 'Get-CanonicalTreeHash -Root \$sourceRoot')).Count | Should BeGreaterThan 1
    }
}
