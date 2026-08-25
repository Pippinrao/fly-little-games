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
$env:MSBUILDDISABLENODEREUSE = '1'

$zlibVersion = '1.3.1'
$zlibArchiveName = 'zlib-1.3.1.tar.gz'
$zlibArchiveUrl = 'https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz'
$zlibArchiveSha256 = '9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23'
$networkTimeoutSeconds = 60
$generator = 'Visual Studio 17 2022'
$architecture = 'x64'
$requiredToolVersion = '3.22.1'
$msbuildTrackingArguments = @(
    '-DCMAKE_VS_GLOBALS=TrackFileAccess=false',
    '-DCMAKE_TRY_COMPILE_CONFIGURATION=Release',
    '-DCMAKE_TRY_COMPILE_PLATFORM_VARIABLES=CMAKE_VS_GLOBALS;CMAKE_TRY_COMPILE_CONFIGURATION'
)
$reproducibleBuildArguments = @(
    '-DCMAKE_C_FLAGS=/Brepro',
    '-DCMAKE_STATIC_LINKER_FLAGS=/Brepro'
)
$repositoryRoot = [System.IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$repositoryHostDepsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryRoot '.artifacts\host-deps'))
$repositoryTestRunsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $repositoryHostDepsRoot '.test-runs'))

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

function Assert-NoReparsePointAncestors {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $resolvedPath = [System.IO.Path]::GetFullPath($Path)
    $volumeRoot = [System.IO.Path]::GetPathRoot($resolvedPath)
    $current = $volumeRoot
    $relative = $resolvedPath.Substring($volumeRoot.Length)
    foreach ($segment in @($relative -split '[\\/]' | Where-Object { $_ })) {
        $current = Join-Path $current $segment
        $item = Get-Item -LiteralPath $current -Force -ErrorAction SilentlyContinue
        if (-not $item) { break }
        if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
            throw "$Label contains an existing reparse point: $($item.FullName)"
        }
    }
}

function Assert-NoReparsePointsInTree {
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [Parameter(Mandatory = $true)][string] $Label
    )

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) { return }
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue([System.IO.Path]::GetFullPath($Root))
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "$Label contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
        }
    }
}

function Resolve-ManagedOutputRoot {
    param([Parameter(Mandatory = $true)][string] $RequestedPath)

    $resolved = if ([System.IO.Path]::IsPathFullyQualified($RequestedPath)) {
        [System.IO.Path]::GetFullPath($RequestedPath)
    }
    else {
        [System.IO.Path]::GetFullPath((Join-Path $repositoryRoot $RequestedPath))
    }
    $resolved = $resolved.TrimEnd([char[]]@('\', '/'))

    $isCanonical = $resolved.Equals(
        $repositoryHostDepsRoot, [StringComparison]::OrdinalIgnoreCase)
    $testPrefix = $repositoryTestRunsRoot.TrimEnd('\') + `
        [System.IO.Path]::DirectorySeparatorChar
    $isTestRun = $false
    if ($resolved.StartsWith($testPrefix, [StringComparison]::OrdinalIgnoreCase)) {
        $testRelative = [System.IO.Path]::GetRelativePath($repositoryTestRunsRoot, $resolved)
        $isTestRun = $testRelative -match '^[0-9a-fA-F]{32}[\\/]host-deps$'
    }
    if (-not $isCanonical -and -not $isTestRun) {
        throw "OutputDirectory must be the repository .artifacts/host-deps directory " +
            "or a .test-runs/<32-hex-id>/host-deps test child: $resolved"
    }

    Assert-NoReparsePointAncestors -Path $resolved -Label 'OutputDirectory'
    return $resolved
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
    Assert-NoReparsePointAncestors -Path $resolvedRoot -Label 'Output root'
    Assert-NoReparsePointAncestors -Path $resolvedCandidate -Label $Label
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

function Remove-ManagedDirectory {
    param(
        [Parameter(Mandatory = $true)][string] $OutputRoot,
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $resolved = Assert-PathWithinRoot -Root $OutputRoot -Candidate $Path -Label $Label
    if ($resolved.Equals(
            [System.IO.Path]::GetFullPath($OutputRoot).TrimEnd('\'),
            [StringComparison]::OrdinalIgnoreCase)) {
        throw "Refusing to remove the output root itself: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Assert-NoReparsePointAncestors -Path $resolved -Label $Label
        Assert-NoReparsePointsInTree -Root $resolved -Label $Label
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

function Remove-ManagedFile {
    param(
        [Parameter(Mandatory = $true)][string] $OutputRoot,
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $resolved = Assert-PathWithinRoot -Root $OutputRoot -Candidate $Path -Label $Label
    if (Test-Path -LiteralPath $resolved) {
        if (-not (Test-Path -LiteralPath $resolved -PathType Leaf)) {
            throw "$Label must be a regular file: $resolved"
        }
        Assert-NoReparsePointAncestors -Path $resolved -Label $Label
        Remove-Item -LiteralPath $resolved -Force
    }
}

function Assert-ExactStaticInstallTree {
    param([Parameter(Mandatory = $true)][string] $InstallRoot)

    if (-not (Test-Path -LiteralPath $InstallRoot -PathType Container)) {
        throw "Static install root does not exist: $InstallRoot"
    }
    $expected = @(
        'include/zconf.h',
        'include/zlib.h',
        'lib/zlibstatic.lib',
        'share/licenses/zlib-1.3.1/LICENSE'
    )
    $expectedDirectories = @(
        'include',
        'lib',
        'share',
        'share/licenses',
        'share/licenses/zlib-1.3.1'
    )
    $files = [Collections.Generic.List[string]]::new()
    $directories = [Collections.Generic.List[string]]::new()
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue([System.IO.Path]::GetFullPath($InstallRoot))
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Static install tree contains an unexpected reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) {
                $directories.Add([System.IO.Path]::GetRelativePath(
                        $InstallRoot, $item.FullName).Replace('\', '/'))
                $pending.Enqueue($item.FullName)
            }
            else {
                $files.Add([System.IO.Path]::GetRelativePath(
                        $InstallRoot, $item.FullName).Replace('\', '/'))
            }
        }
    }
    $actual = @($files | Sort-Object)
    $actualDirectories = @($directories | Sort-Object)
    $missing = @($expected | Where-Object { $_ -notin $actual })
    $unexpected = @($actual | Where-Object { $_ -notin $expected })
    $missingDirectories = @(
        $expectedDirectories | Where-Object { $_ -notin $actualDirectories })
    $unexpectedDirectories = @(
        $actualDirectories | Where-Object { $_ -notin $expectedDirectories })
    if ($missing.Count -ne 0 -or $unexpected.Count -ne 0 -or
            $missingDirectories.Count -ne 0 -or $unexpectedDirectories.Count -ne 0) {
        throw "Static install tree mismatch; missing=[$($missing -join ', ')]; " +
            "unexpected=[$($unexpected -join ', ')]; missing directories=" +
            "[$($missingDirectories -join ', ')]; unexpected directories=" +
            "[$($unexpectedDirectories -join ', ')]."
    }
}

function Assert-MatchingToolchainIdentity {
    param(
        [Parameter(Mandatory = $true)][pscustomobject] $Reference,
        [Parameter(Mandatory = $true)][pscustomobject] $Candidate,
        [Parameter(Mandatory = $true)][string] $ReferenceLabel,
        [Parameter(Mandatory = $true)][string] $CandidateLabel
    )

    foreach ($property in @(
            'Id',
            'Version',
            'CompilerPath',
            'CompilerSha256',
            'GeneratorInstance',
            'PlatformToolset',
            'WindowsSdk')) {
        $referenceValue = [string]$Reference.$property
        $candidateValue = [string]$Candidate.$property
        if (-not $referenceValue.Equals(
                $candidateValue, [StringComparison]::OrdinalIgnoreCase)) {
            throw "$property drift between $ReferenceLabel '$referenceValue' and " +
                "$CandidateLabel '$candidateValue'."
        }
    }
}

function Initialize-BoundedProcessRunner {
    if ('FlyNes.Quality.BoundedProcessRunner' -as [type]) { return }

    Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Diagnostics;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using Microsoft.Win32.SafeHandles;

namespace FlyNes.Quality {
    public sealed class BoundedProcessResult {
        public int ExitCode { get; set; }
        public string StandardOutput { get; set; }
        public string StandardError { get; set; }
    }

    public static class BoundedProcessRunner {
        private const uint CreateSuspended = 0x00000004;
        private const uint CreateNoWindow = 0x08000000;
        private const uint ExtendedStartupInfoPresent = 0x00080000;
        private const uint StartfUseStdHandles = 0x00000100;
        private const uint HandleFlagInherit = 0x00000001;
        private static readonly IntPtr ProcThreadAttributeHandleList = new IntPtr(0x00020002);
        private const uint JobObjectBasicAccountingInformation = 1;
        private const uint JobObjectExtendedLimitInformation = 9;
        private const uint JobObjectLimitKillOnJobClose = 0x00002000;
        private const uint WaitObject0 = 0;
        private const uint WaitTimeout = 258;

        [StructLayout(LayoutKind.Sequential)]
        private struct SecurityAttributes {
            public uint Length;
            public IntPtr SecurityDescriptor;
            [MarshalAs(UnmanagedType.Bool)] public bool InheritHandle;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        private struct StartupInfo {
            public uint Cb;
            public string Reserved;
            public string Desktop;
            public string Title;
            public uint X;
            public uint Y;
            public uint XSize;
            public uint YSize;
            public uint XCountChars;
            public uint YCountChars;
            public uint FillAttribute;
            public uint Flags;
            public ushort ShowWindow;
            public ushort Reserved2Count;
            public IntPtr Reserved2;
            public IntPtr StandardInput;
            public IntPtr StandardOutput;
            public IntPtr StandardError;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct StartupInfoEx {
            public StartupInfo StartupInfo;
            public IntPtr AttributeList;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct ProcessInformation {
            public IntPtr Process;
            public IntPtr Thread;
            public uint ProcessId;
            public uint ThreadId;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JobObjectBasicAccountingInformationData {
            public long TotalUserTime;
            public long TotalKernelTime;
            public long ThisPeriodTotalUserTime;
            public long ThisPeriodTotalKernelTime;
            public uint TotalPageFaultCount;
            public uint TotalProcesses;
            public uint ActiveProcesses;
            public uint TotalTerminatedProcesses;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JobObjectBasicLimitInformation {
            public long PerProcessUserTimeLimit;
            public long PerJobUserTimeLimit;
            public uint LimitFlags;
            public UIntPtr MinimumWorkingSetSize;
            public UIntPtr MaximumWorkingSetSize;
            public uint ActiveProcessLimit;
            public UIntPtr Affinity;
            public uint PriorityClass;
            public uint SchedulingClass;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct IoCounters {
            public ulong ReadOperationCount;
            public ulong WriteOperationCount;
            public ulong OtherOperationCount;
            public ulong ReadTransferCount;
            public ulong WriteTransferCount;
            public ulong OtherTransferCount;
        }

        [StructLayout(LayoutKind.Sequential)]
        private struct JobObjectExtendedLimitInformationData {
            public JobObjectBasicLimitInformation BasicLimitInformation;
            public IoCounters IoInfo;
            public UIntPtr ProcessMemoryLimit;
            public UIntPtr JobMemoryLimit;
            public UIntPtr PeakProcessMemoryUsed;
            public UIntPtr PeakJobMemoryUsed;
        }

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern IntPtr CreateJobObject(IntPtr securityAttributes, string name);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetInformationJobObject(
            IntPtr job,
            uint informationClass,
            ref JobObjectExtendedLimitInformationData information,
            uint informationLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool QueryInformationJobObject(
            IntPtr job,
            uint informationClass,
            out JobObjectBasicAccountingInformationData information,
            uint informationLength,
            IntPtr returnLength);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool AssignProcessToJobObject(IntPtr job, IntPtr process);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateJobObject(IntPtr job, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool TerminateProcess(IntPtr process, uint exitCode);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CloseHandle(IntPtr handle);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool CreatePipe(
            out IntPtr readPipe,
            out IntPtr writePipe,
            ref SecurityAttributes pipeAttributes,
            uint size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool SetHandleInformation(IntPtr handle, uint mask, uint flags);

        [DllImport("kernel32.dll", CharSet = CharSet.Unicode, SetLastError = true)]
        private static extern bool CreateProcess(
            string applicationName,
            StringBuilder commandLine,
            IntPtr processAttributes,
            IntPtr threadAttributes,
            bool inheritHandles,
            uint creationFlags,
            IntPtr environment,
            string currentDirectory,
            ref StartupInfoEx startupInfo,
            out ProcessInformation processInformation);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool InitializeProcThreadAttributeList(
            IntPtr attributeList,
            int attributeCount,
            int flags,
            ref IntPtr size);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool UpdateProcThreadAttribute(
            IntPtr attributeList,
            uint flags,
            IntPtr attribute,
            IntPtr value,
            IntPtr size,
            IntPtr previousValue,
            IntPtr returnSize);

        [DllImport("kernel32.dll")]
        private static extern void DeleteProcThreadAttributeList(IntPtr attributeList);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint ResumeThread(IntPtr thread);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern uint WaitForSingleObject(IntPtr handle, uint milliseconds);

        [DllImport("kernel32.dll", SetLastError = true)]
        private static extern bool GetExitCodeProcess(IntPtr process, out uint exitCode);

        private static Win32Exception LastError(string operation) {
            return new Win32Exception(Marshal.GetLastWin32Error(), operation + " failed");
        }

        private static long DeadlineFromNow(int milliseconds) {
            return Stopwatch.GetTimestamp() +
                (long)Math.Ceiling(milliseconds * (double)Stopwatch.Frequency / 1000.0);
        }

        private static int RemainingMilliseconds(long deadline) {
            long ticks = deadline - Stopwatch.GetTimestamp();
            if (ticks <= 0) return 0;
            return Math.Max(0, (int)Math.Min(
                Int32.MaxValue,
                Math.Floor(ticks * 1000.0 / Stopwatch.Frequency)));
        }

        private static string QuoteArgument(string value) {
            if (value.Length == 0) return "\"\"";
            if (value.IndexOfAny(new[] { ' ', '\t', '\n', '\v', '\"' }) < 0) return value;
            var quoted = new StringBuilder("\"");
            int backslashes = 0;
            foreach (char character in value) {
                if (character == '\\') {
                    backslashes++;
                } else if (character == '\"') {
                    quoted.Append('\\', backslashes * 2 + 1);
                    quoted.Append('\"');
                    backslashes = 0;
                } else {
                    quoted.Append('\\', backslashes);
                    quoted.Append(character);
                    backslashes = 0;
                }
            }
            quoted.Append('\\', backslashes * 2);
            quoted.Append('\"');
            return quoted.ToString();
        }

        private static string BuildCommandLine(string executable, string[] arguments) {
            var parts = new List<string>();
            parts.Add(QuoteArgument(executable));
            foreach (string argument in arguments) parts.Add(QuoteArgument(argument));
            return String.Join(" ", parts.ToArray());
        }

        private static IntPtr CreateKillOnCloseJob() {
            IntPtr job = CreateJobObject(IntPtr.Zero, null);
            if (job == IntPtr.Zero) throw LastError("CreateJobObject");
            var information = new JobObjectExtendedLimitInformationData();
            information.BasicLimitInformation.LimitFlags = JobObjectLimitKillOnJobClose;
            if (!SetInformationJobObject(
                    job,
                    JobObjectExtendedLimitInformation,
                    ref information,
                    (uint)Marshal.SizeOf(typeof(JobObjectExtendedLimitInformationData)))) {
                var error = LastError("SetInformationJobObject");
                CloseHandle(job);
                throw error;
            }
            return job;
        }

        private static uint GetActiveProcessCount(IntPtr job) {
            JobObjectBasicAccountingInformationData information;
            if (!QueryInformationJobObject(
                    job,
                    JobObjectBasicAccountingInformation,
                    out information,
                    (uint)Marshal.SizeOf(typeof(JobObjectBasicAccountingInformationData)),
                    IntPtr.Zero)) {
                throw LastError("QueryInformationJobObject");
            }
            return information.ActiveProcesses;
        }

        private static string Drain(
                FileStream stream,
                int captureLimitBytes,
                CancellationToken cancellationToken) {
            byte[] buffer = new byte[8192];
            using (var captured = new MemoryStream()) {
                bool truncated = false;
                while (true) {
                    cancellationToken.ThrowIfCancellationRequested();
                    int count = stream.Read(buffer, 0, buffer.Length);
                    if (count == 0) break;
                    int remaining = captureLimitBytes - (int)captured.Length;
                    if (remaining > 0) {
                        int copyCount = Math.Min(remaining, count);
                        captured.Write(buffer, 0, copyCount);
                    }
                    if (count > remaining) truncated = true;
                }
                string text = Encoding.UTF8.GetString(captured.ToArray());
                return truncated ? text + Environment.NewLine + "[output truncated]" : text;
            }
        }

        private static void TerminateOwnedJob(IntPtr job) {
            if (!TerminateJobObject(job, 1)) throw LastError("TerminateJobObject");
        }

        private static bool AwaitEmptyJobAndDrains(
                IntPtr job,
                Task<string> stdoutTask,
                Task<string> stderrTask,
                long deadline) {
            while (RemainingMilliseconds(deadline) > 0) {
                if (GetActiveProcessCount(job) == 0 && stdoutTask.IsCompleted && stderrTask.IsCompleted) {
                    return true;
                }
                Thread.Sleep(Math.Min(10, RemainingMilliseconds(deadline)));
            }
            return GetActiveProcessCount(job) == 0 && stdoutTask.IsCompleted && stderrTask.IsCompleted;
        }

        private static bool AwaitDrains(
                Task<string> stdoutTask,
                Task<string> stderrTask,
                long deadline) {
            while (RemainingMilliseconds(deadline) > 0) {
                if (stdoutTask.IsCompleted && stderrTask.IsCompleted) return true;
                Thread.Sleep(Math.Min(10, RemainingMilliseconds(deadline)));
            }
            return stdoutTask.IsCompleted && stderrTask.IsCompleted;
        }

        public static BoundedProcessResult Run(
                string executable,
                string[] arguments,
                string workingDirectory,
                int timeoutMilliseconds,
                int captureLimitBytes,
                string description) {
            long deadline = DeadlineFromNow(timeoutMilliseconds);
            int cleanupReserve = Math.Min(250, Math.Max(1, timeoutMilliseconds / 4));
            long workDeadline = deadline -
                (long)Math.Ceiling(cleanupReserve * (double)Stopwatch.Frequency / 1000.0);
            IntPtr job = IntPtr.Zero;
            IntPtr stdoutRead = IntPtr.Zero;
            IntPtr stdoutWrite = IntPtr.Zero;
            IntPtr stderrRead = IntPtr.Zero;
            IntPtr stderrWrite = IntPtr.Zero;
            IntPtr stdinRead = IntPtr.Zero;
            IntPtr stdinWrite = IntPtr.Zero;
            IntPtr attributeList = IntPtr.Zero;
            IntPtr inheritedHandles = IntPtr.Zero;
            ProcessInformation process = new ProcessInformation();
            FileStream stdoutStream = null;
            FileStream stderrStream = null;
            CancellationTokenSource cancellation = new CancellationTokenSource();
            Task<string> stdoutTask = null;
            Task<string> stderrTask = null;
            bool processCreated = false;
            bool jobOwnsProcess = false;
            bool terminated = false;
            bool attributeListInitialized = false;
            try {
                job = CreateKillOnCloseJob();
                var pipeAttributes = new SecurityAttributes {
                    Length = (uint)Marshal.SizeOf(typeof(SecurityAttributes)),
                    InheritHandle = true
                };
                if (!CreatePipe(out stdoutRead, out stdoutWrite, ref pipeAttributes, 0)) {
                    throw LastError("CreatePipe(stdout)");
                }
                if (!SetHandleInformation(stdoutRead, HandleFlagInherit, 0)) {
                    throw LastError("SetHandleInformation(stdout)");
                }
                if (!CreatePipe(out stderrRead, out stderrWrite, ref pipeAttributes, 0)) {
                    throw LastError("CreatePipe(stderr)");
                }
                if (!SetHandleInformation(stderrRead, HandleFlagInherit, 0)) {
                    throw LastError("SetHandleInformation(stderr)");
                }
                if (!CreatePipe(out stdinRead, out stdinWrite, ref pipeAttributes, 0)) {
                    throw LastError("CreatePipe(stdin)");
                }
                if (!SetHandleInformation(stdinWrite, HandleFlagInherit, 0)) {
                    throw LastError("SetHandleInformation(stdin)");
                }
                IntPtr attributeListSize = IntPtr.Zero;
                InitializeProcThreadAttributeList(IntPtr.Zero, 1, 0, ref attributeListSize);
                attributeList = Marshal.AllocHGlobal(attributeListSize);
                if (!InitializeProcThreadAttributeList(
                        attributeList, 1, 0, ref attributeListSize)) {
                    throw LastError("InitializeProcThreadAttributeList");
                }
                attributeListInitialized = true;
                inheritedHandles = Marshal.AllocHGlobal(IntPtr.Size * 3);
                Marshal.WriteIntPtr(inheritedHandles, 0, stdinRead);
                Marshal.WriteIntPtr(inheritedHandles, IntPtr.Size, stdoutWrite);
                Marshal.WriteIntPtr(inheritedHandles, IntPtr.Size * 2, stderrWrite);
                if (!UpdateProcThreadAttribute(
                        attributeList,
                        0,
                        ProcThreadAttributeHandleList,
                        inheritedHandles,
                        new IntPtr(IntPtr.Size * 3),
                        IntPtr.Zero,
                        IntPtr.Zero)) {
                    throw LastError("UpdateProcThreadAttribute(handle list)");
                }
                var startup = new StartupInfoEx {
                    StartupInfo = new StartupInfo {
                        Cb = (uint)Marshal.SizeOf(typeof(StartupInfoEx)),
                        Flags = StartfUseStdHandles,
                        StandardInput = stdinRead,
                        StandardOutput = stdoutWrite,
                        StandardError = stderrWrite
                    },
                    AttributeList = attributeList
                };
                var commandLine = new StringBuilder(BuildCommandLine(executable, arguments));
                if (!CreateProcess(
                        executable,
                        commandLine,
                        IntPtr.Zero,
                        IntPtr.Zero,
                        true,
                        CreateSuspended | CreateNoWindow | ExtendedStartupInfoPresent,
                        IntPtr.Zero,
                        workingDirectory,
                        ref startup,
                        out process)) {
                    throw LastError("CreateProcess");
                }
                processCreated = true;
                CloseHandle(stdinRead);
                stdinRead = IntPtr.Zero;
                CloseHandle(stdinWrite);
                stdinWrite = IntPtr.Zero;
                CloseHandle(stdoutWrite);
                stdoutWrite = IntPtr.Zero;
                CloseHandle(stderrWrite);
                stderrWrite = IntPtr.Zero;
                if (!AssignProcessToJobObject(job, process.Process)) {
                    throw LastError("AssignProcessToJobObject");
                }
                jobOwnsProcess = true;
                stdoutStream = new FileStream(
                    new SafeFileHandle(stdoutRead, true), FileAccess.Read, 4096, false);
                stdoutRead = IntPtr.Zero;
                stderrStream = new FileStream(
                    new SafeFileHandle(stderrRead, true), FileAccess.Read, 4096, false);
                stderrRead = IntPtr.Zero;
                stdoutTask = Task.Run(
                    () => Drain(stdoutStream, captureLimitBytes, cancellation.Token));
                stderrTask = Task.Run(
                    () => Drain(stderrStream, captureLimitBytes, cancellation.Token));
                if (ResumeThread(process.Thread) == UInt32.MaxValue) {
                    throw LastError("ResumeThread");
                }

                uint parentWait = WaitForSingleObject(
                    process.Process, (uint)Math.Max(0, RemainingMilliseconds(workDeadline)));
                bool parentExited = parentWait == WaitObject0;
                if (!parentExited && parentWait != WaitTimeout) {
                    throw LastError("WaitForSingleObject");
                }
                uint activeProcessCount = parentExited ? GetActiveProcessCount(job) : 0;
                if (parentExited && activeProcessCount != 0) {
                    long jobSettleDeadline = Math.Min(deadline, DeadlineFromNow(100));
                    while (activeProcessCount != 0 &&
                            RemainingMilliseconds(jobSettleDeadline) > 0) {
                        Thread.Sleep(Math.Min(5, RemainingMilliseconds(jobSettleDeadline)));
                        activeProcessCount = GetActiveProcessCount(job);
                    }
                }
                if (parentExited && activeProcessCount != 0) {
                    bool redirectedStreamHeld =
                        !stdoutTask.IsCompleted || !stderrTask.IsCompleted;
                    TerminateOwnedJob(job);
                    terminated = true;
                    if (!AwaitEmptyJobAndDrains(job, stdoutTask, stderrTask, deadline)) {
                        cancellation.Cancel();
                        throw new TimeoutException(
                            description +
                            " process-tree cleanup was not confirmed before the deadline.");
                    }
                    if (redirectedStreamHeld) {
                        throw new InvalidOperationException(
                            description +
                            " left a job-owned descendant holding a redirected stream after its root exited; " +
                            "process-tree containment failed closed after the 100 ms console-host settle window " +
                            "(active process count before cleanup: " + activeProcessCount + ").");
                    }
                }
                bool drainsCompleted = parentExited &&
                    AwaitDrains(stdoutTask, stderrTask, workDeadline);
                if (!drainsCompleted) {
                    TerminateOwnedJob(job);
                    terminated = true;
                    bool cleanupConfirmed = AwaitEmptyJobAndDrains(
                        job, stdoutTask, stderrTask, deadline);
                    if (!cleanupConfirmed) {
                        cancellation.Cancel();
                        throw new TimeoutException(
                            description + " timed out after " +
                            (timeoutMilliseconds / 1000) +
                            " second(s); process-tree cleanup was not confirmed before the deadline.");
                    }
                    throw new TimeoutException(
                        description + " timed out after " +
                        (timeoutMilliseconds / 1000) + " second(s).");
                }

                uint exitCode;
                if (!GetExitCodeProcess(process.Process, out exitCode)) {
                    throw LastError("GetExitCodeProcess");
                }
                return new BoundedProcessResult {
                    ExitCode = unchecked((int)exitCode),
                    StandardOutput = stdoutTask.GetAwaiter().GetResult().TrimEnd(),
                    StandardError = stderrTask.GetAwaiter().GetResult().TrimEnd()
                };
            } catch {
                bool cleanupConfirmed = true;
                if (jobOwnsProcess && !terminated) {
                    TerminateOwnedJob(job);
                    terminated = true;
                } else if (processCreated && !jobOwnsProcess) {
                    if (!TerminateProcess(process.Process, 1)) throw LastError("TerminateProcess");
                    cleanupConfirmed = WaitForSingleObject(
                        process.Process,
                        (uint)Math.Max(0, RemainingMilliseconds(deadline))) == WaitObject0;
                }
                if (stdoutTask != null && stderrTask != null && jobOwnsProcess) {
                    cleanupConfirmed = AwaitEmptyJobAndDrains(
                        job, stdoutTask, stderrTask, deadline);
                }
                cancellation.Cancel();
                if (!cleanupConfirmed) {
                    throw new TimeoutException(
                        description +
                        " process-tree cleanup was not confirmed before the deadline.");
                }
                throw;
            } finally {
                cancellation.Cancel();
                if (stdoutStream != null) stdoutStream.Dispose();
                if (stderrStream != null) stderrStream.Dispose();
                cancellation.Dispose();
                if (stdoutRead != IntPtr.Zero) CloseHandle(stdoutRead);
                if (stdoutWrite != IntPtr.Zero) CloseHandle(stdoutWrite);
                if (stderrRead != IntPtr.Zero) CloseHandle(stderrRead);
                if (stderrWrite != IntPtr.Zero) CloseHandle(stderrWrite);
                if (stdinRead != IntPtr.Zero) CloseHandle(stdinRead);
                if (stdinWrite != IntPtr.Zero) CloseHandle(stdinWrite);
                if (attributeListInitialized) {
                    DeleteProcThreadAttributeList(attributeList);
                }
                if (attributeList != IntPtr.Zero) {
                    Marshal.FreeHGlobal(attributeList);
                }
                if (inheritedHandles != IntPtr.Zero) Marshal.FreeHGlobal(inheritedHandles);
                if (process.Thread != IntPtr.Zero) CloseHandle(process.Thread);
                if (process.Process != IntPtr.Zero) CloseHandle(process.Process);
                if (job != IntPtr.Zero) CloseHandle(job);
            }
        }
    }
}
'@
}

function Invoke-ChildProcess {
    param(
        [Parameter(Mandatory = $true)][string] $Executable,
        [Parameter(Mandatory = $true)][string[]] $Arguments,
        [Parameter(Mandatory = $true)][string] $Description,
        [Parameter(Mandatory = $true)][string] $WorkingDirectory
    )

    $runnerExecutable = $Executable
    $runnerArguments = [Collections.Generic.List[string]]::new()
    if ([System.IO.Path]::GetExtension($Executable) -eq '.ps1') {
        $runnerExecutable = (Get-Process -Id $PID).Path
        $runnerArguments.Add('-NoProfile')
        $runnerArguments.Add('-File')
        $runnerArguments.Add($Executable)
    }
    foreach ($argument in $Arguments) { $runnerArguments.Add($argument) }
    if (-not [System.IO.Path]::IsPathFullyQualified($WorkingDirectory) -or
            -not (Test-Path -LiteralPath $WorkingDirectory -PathType Container)) {
        throw "Child process working directory must be an existing absolute directory: $WorkingDirectory"
    }

    Initialize-BoundedProcessRunner
    $result = [FlyNes.Quality.BoundedProcessRunner]::Run(
        $runnerExecutable,
        $runnerArguments.ToArray(),
        $WorkingDirectory,
        $ChildProcessTimeoutSeconds * 1000,
        1048576,
        $Description)
    $outputParts = @($result.StandardOutput, $result.StandardError) |
        Where-Object { $_ }
    return [pscustomobject]@{
        ExitCode = $result.ExitCode
        Output = ($outputParts -join [Environment]::NewLine)
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
    param(
        [Parameter(Mandatory = $true)][string] $BuildDirectory,
        [Parameter(Mandatory = $true)][DateTime] $NotBeforeUtc,
        [Parameter(Mandatory = $true)][string] $Label
    )

    $compilerFile = Get-ChildItem -LiteralPath (Join-Path $BuildDirectory 'CMakeFiles') `
        -Recurse -File -Filter 'CMakeCCompiler.cmake' -ErrorAction SilentlyContinue |
        Select-Object -First 1
    if (-not $compilerFile) {
        throw "$Label did not emit CMakeCCompiler.cmake."
    }
    if ($compilerFile.LastWriteTimeUtc -lt $NotBeforeUtc) {
        throw "$Label compiler identity is stale: $($compilerFile.LastWriteTimeUtc.ToString('o')) " +
            "predates $($NotBeforeUtc.ToString('o'))."
    }
    $source = Get-Content -LiteralPath $compilerFile.FullName -Raw
    $idMatch = [regex]::Match($source, 'set\(CMAKE_C_COMPILER_ID\s+"(?<value>[^"]+)"\)')
    $versionMatch = [regex]::Match($source, 'set\(CMAKE_C_COMPILER_VERSION\s+"(?<value>[^"]+)"\)')
    $pathMatch = [regex]::Match($source, 'set\(CMAKE_C_COMPILER\s+"(?<value>[^"]+)"\)')
    if (-not $idMatch.Success -or -not $versionMatch.Success -or -not $pathMatch.Success) {
        throw "$Label compiler identity is incomplete."
    }
    $compilerPath = [System.IO.Path]::GetFullPath($pathMatch.Groups['value'].Value)
    if (-not (Test-Path -LiteralPath $compilerPath -PathType Leaf)) {
        throw "$Label compiler executable does not exist: $compilerPath"
    }

    $cachePath = Join-Path $BuildDirectory 'CMakeCache.txt'
    $generatorInstance = Get-CMakeCacheValue -CachePath $cachePath `
        -Key 'CMAKE_GENERATOR_INSTANCE'
    if (-not $generatorInstance) {
        throw "$Label did not record CMAKE_GENERATOR_INSTANCE."
    }
    $generatorInstance = [System.IO.Path]::GetFullPath($generatorInstance)
    if (-not (Test-Path -LiteralPath $generatorInstance -PathType Container)) {
        throw "$Label Visual Studio generator instance does not exist: $generatorInstance"
    }

    $projectFile = Get-ChildItem -LiteralPath $BuildDirectory -File -Filter '*.vcxproj' |
        Sort-Object Name |
        Select-Object -First 1
    if (-not $projectFile -or $projectFile.LastWriteTimeUtc -lt $NotBeforeUtc) {
        throw "$Label did not emit a fresh Visual Studio project."
    }
    $projectSource = Get-Content -LiteralPath $projectFile.FullName -Raw
    $toolsetMatch = [regex]::Match(
        $projectSource, '<PlatformToolset>(?<value>[^<]+)</PlatformToolset>')
    $sdkMatch = [regex]::Match(
        $projectSource,
        '<WindowsTargetPlatformVersion>(?<value>[^<]+)</WindowsTargetPlatformVersion>')
    if (-not $toolsetMatch.Success -or -not $sdkMatch.Success) {
        throw "$Label Visual Studio toolset identity is incomplete."
    }
    return [pscustomobject]@{
        Id = $idMatch.Groups['value'].Value
        Version = $versionMatch.Groups['value'].Value
        CompilerPath = $compilerPath
        CompilerSha256 = Get-LowerSha256 -Path $compilerPath
        GeneratorInstance = $generatorInstance
        PlatformToolset = $toolsetMatch.Groups['value'].Value
        WindowsSdk = $sdkMatch.Groups['value'].Value
        IdentityFileSha256 = Get-LowerSha256 -Path $compilerFile.FullName
        IdentityFileWrittenUtc = $compilerFile.LastWriteTimeUtc.ToString('o')
        ProjectFileSha256 = Get-LowerSha256 -Path $projectFile.FullName
        ProjectFileWrittenUtc = $projectFile.LastWriteTimeUtc.ToString('o')
    }
}

function Get-CanonicalTreeHash {
    param([Parameter(Mandatory = $true)][string] $Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Tree root does not exist: $Root"
    }
    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $treeFiles = [Collections.Generic.List[System.IO.FileInfo]]::new()
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($rootPath)
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Tree contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            else { $treeFiles.Add($item) }
        }
    }
    $records = [System.Collections.Generic.List[string]]::new()
    $orderedPaths = [string[]]@($treeFiles | ForEach-Object { $_.FullName })
    [Array]::Sort($orderedPaths, [StringComparer]::Ordinal)
    foreach ($path in $orderedPaths) {
        $relative = [System.IO.Path]::GetRelativePath($rootPath, $path).Replace('\', '/')
        $records.Add("$(Get-LowerSha256 -Path $path)  $relative`n")
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

function Get-LegacyCanonicalTreeHash {
    param([Parameter(Mandatory = $true)][string] $Root)

    if (-not (Test-Path -LiteralPath $Root -PathType Container)) {
        throw "Tree root does not exist: $Root"
    }
    $rootPath = [System.IO.Path]::GetFullPath($Root).TrimEnd('\')
    $treeFiles = [Collections.Generic.List[System.IO.FileInfo]]::new()
    $pending = [Collections.Generic.Queue[string]]::new()
    $pending.Enqueue($rootPath)
    while ($pending.Count -ne 0) {
        $directory = $pending.Dequeue()
        foreach ($item in @(Get-ChildItem -LiteralPath $directory -Force)) {
            if (($item.Attributes -band [System.IO.FileAttributes]::ReparsePoint) -ne 0) {
                throw "Tree contains a reparse point: $($item.FullName)"
            }
            if ($item.PSIsContainer) { $pending.Enqueue($item.FullName) }
            else { $treeFiles.Add($item) }
        }
    }
    $records = [System.Collections.Generic.List[string]]::new()
    $treeFiles | Sort-Object FullName | ForEach-Object {
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
    if ($manifest.install.PSObject.Properties.Name -contains 'generatedHeader') {
        $checks += ,@(
            (Join-Path $OutputRoot $manifest.install.generatedHeader.path),
            [string]$manifest.install.generatedHeader.sha256,
            'installed generated zlib header')
    }
    foreach ($check in $checks) {
        $path = [string]$check[0]
        $expected = [string]$check[1]
        $label = [string]$check[2]
        if ($label -notin @('CMake executable', 'CTest executable')) {
            $path = Assert-PathWithinRoot -Root $OutputRoot -Candidate $path -Label $label
        }
        if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
            throw "Previously recorded $label is missing: $path"
        }
        $actual = Get-LowerSha256 -Path $path
        if ($actual -ne $expected) {
            throw "Previously recorded $label hash changed: expected $expected, found $actual."
        }
    }
    $schemaVersion = if ($manifest.PSObject.Properties.Name -contains 'schemaVersion') {
        [int]$manifest.schemaVersion
    }
    else { 1 }
    $sourceRoot = Assert-PathWithinRoot -Root $OutputRoot `
        -Candidate (Join-Path $OutputRoot $manifest.extractedTree.directory) `
        -Label 'Previously recorded zlib extracted tree'
    $actualTreeHash = if ($schemaVersion -le 2) {
        Get-LegacyCanonicalTreeHash -Root $sourceRoot
    }
    else {
        Get-CanonicalTreeHash -Root $sourceRoot
    }
    if ($actualTreeHash -ne [string]$manifest.extractedTree.canonicalSha256) {
        throw "Previously recorded zlib extracted tree hash changed: expected $($manifest.extractedTree.canonicalSha256), found $actualTreeHash."
    }
    if ($schemaVersion -ge 2) {
        $installRoot = Assert-PathWithinRoot -Root $OutputRoot `
            -Candidate (Join-Path $OutputRoot $manifest.install.prefix) `
            -Label 'Previously recorded zlib static install'
        Assert-ExactStaticInstallTree -InstallRoot $installRoot
        $actualInstallHash = if ($schemaVersion -le 2) {
            Get-LegacyCanonicalTreeHash -Root $installRoot
        }
        else {
            Get-CanonicalTreeHash -Root $installRoot
        }
        if ($actualInstallHash -ne [string]$manifest.install.canonicalSha256) {
            throw "Previously recorded zlib install tree hash changed: expected " +
                "$($manifest.install.canonicalSha256), found $actualInstallHash."
        }
    }
}

function Write-ToolchainManifest {
    param(
        [Parameter(Mandatory = $true)][string] $Path,
        [Parameter(Mandatory = $true)][string] $ResolvedCMake,
        [Parameter(Mandatory = $true)][string] $ResolvedCTest,
        [Parameter(Mandatory = $true)][string] $CMakeVersion,
        [Parameter(Mandatory = $true)][string] $CTestVersion,
        [Parameter(Mandatory = $true)][pscustomobject] $Compiler,
        [Parameter(Mandatory = $true)][pscustomobject] $DependencyCompiler
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
        "    ReproducibleBuildArguments = @('$($reproducibleBuildArguments -join "', '")')"
        "    CompilerId = '$(& $escape $Compiler.Id)'"
        "    CompilerVersion = '$(& $escape $Compiler.Version)'"
        "    CompilerPath = '$(& $escape $Compiler.CompilerPath)'"
        "    CompilerSha256 = '$(& $escape $Compiler.CompilerSha256)'"
        "    VisualStudioInstance = '$(& $escape $Compiler.GeneratorInstance)'"
        "    PlatformToolset = '$(& $escape $Compiler.PlatformToolset)'"
        "    WindowsSdkVersion = '$(& $escape $Compiler.WindowsSdk)'"
        "    ProbeIdentityFileSha256 = '$(& $escape $Compiler.IdentityFileSha256)'"
        "    ProbeIdentityWrittenUtc = '$(& $escape $Compiler.IdentityFileWrittenUtc)'"
        "    DependencyIdentityFileSha256 = '$(& $escape $DependencyCompiler.IdentityFileSha256)'"
        "    DependencyIdentityWrittenUtc = '$(& $escape $DependencyCompiler.IdentityFileWrittenUtc)'"
        "    FreshCachePolicy = 'probe and dependency build directories recreated every run'"
        '}'
    )
    $content | Set-Content -LiteralPath $Path -Encoding utf8
}

function Restore-ManifestPairTransaction {
    param(
        [Parameter(Mandatory = $true)][string] $ToolchainPending,
        [Parameter(Mandatory = $true)][string] $PreflightPending,
        [Parameter(Mandatory = $true)][string] $ToolchainFinal,
        [Parameter(Mandatory = $true)][string] $PreflightFinal,
        [Parameter(Mandatory = $true)][string] $ToolchainRollback,
        [Parameter(Mandatory = $true)][string] $PreflightRollback,
        [Parameter(Mandatory = $true)][string] $TransactionMarker
    )

    $markerStagingPath = "$TransactionMarker.staging"
    $temporaryPaths = @(
        $ToolchainPending, $PreflightPending, $ToolchainRollback,
        $PreflightRollback, $markerStagingPath)
    $toolchainFinalExists = Test-Path -LiteralPath $ToolchainFinal -PathType Leaf
    $preflightFinalExists = Test-Path -LiteralPath $PreflightFinal -PathType Leaf
    if (-not (Test-Path -LiteralPath $TransactionMarker -PathType Leaf)) {
        if ($toolchainFinalExists -ne $preflightFinalExists) {
            throw 'The prior manifest pair is inconsistent; refusing recovery without a transaction marker.'
        }
        foreach ($temporaryPath in $temporaryPaths) {
            if (Test-Path -LiteralPath $temporaryPath) {
                if (-not (Test-Path -LiteralPath $temporaryPath -PathType Leaf)) {
                    throw "Manifest transaction temporary path is not a file: $temporaryPath"
                }
                Remove-Item -LiteralPath $temporaryPath -Force
            }
        }
        return
    }

    $state = $null
    try {
        $state = Get-Content -LiteralPath $TransactionMarker -Raw | ConvertFrom-Json
    }
    catch {
        $state = $null
    }
    $toolchainRollbackExists = Test-Path -LiteralPath $ToolchainRollback -PathType Leaf
    $preflightRollbackExists = Test-Path -LiteralPath $PreflightRollback -PathType Leaf
    if ($state -and $state.PSObject.Properties.Name -contains 'oldPairExisted') {
        $oldPairExisted = [bool]$state.oldPairExisted
    }
    elseif ($state -and
            $state.PSObject.Properties.Name -contains 'toolchainHadFinal' -and
            $state.PSObject.Properties.Name -contains 'preflightHadFinal' -and
            [bool]$state.toolchainHadFinal -eq [bool]$state.preflightHadFinal) {
        $oldPairExisted = [bool]$state.toolchainHadFinal
    }
    elseif ($toolchainRollbackExists -and $preflightRollbackExists) {
        # Compatibility for a torn marker or the literal marker used by the
        # subprocess-free recovery regression.
        $oldPairExisted = $true
    }
    else {
        throw 'The manifest publication marker is unreadable and cannot safely recover without a complete rollback pair.'
    }

    if ($oldPairExisted) {
        if (-not $toolchainRollbackExists -or -not $preflightRollbackExists) {
            throw 'Cannot safely recover the prior manifest pair without a complete rollback pair.'
        }
        $toolchainRollbackHash = (Get-FileHash -LiteralPath $ToolchainRollback `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        $preflightRollbackHash = (Get-FileHash -LiteralPath $PreflightRollback `
                -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($state -and $state.PSObject.Properties.Name -contains 'oldToolchainSha256' -and
                $toolchainRollbackHash -ne [string]$state.oldToolchainSha256) {
            throw 'Cannot safely recover: the toolchain rollback hash changed.'
        }
        if ($state -and $state.PSObject.Properties.Name -contains 'oldPreflightSha256' -and
                $preflightRollbackHash -ne [string]$state.oldPreflightSha256) {
            throw 'Cannot safely recover: the preflight rollback hash changed.'
        }
        Copy-Item -LiteralPath $ToolchainRollback -Destination $ToolchainFinal -Force
        Copy-Item -LiteralPath $PreflightRollback -Destination $PreflightFinal -Force
        if ((Get-FileHash -LiteralPath $ToolchainFinal -Algorithm SHA256).Hash.ToLowerInvariant() `
                -ne $toolchainRollbackHash -or
                (Get-FileHash -LiteralPath $PreflightFinal -Algorithm SHA256).Hash.ToLowerInvariant() `
                -ne $preflightRollbackHash) {
            throw 'Cannot safely recover: restored manifest hashes do not match the rollback pair.'
        }
    }
    else {
        if ($toolchainRollbackExists -or $preflightRollbackExists) {
            throw 'Cannot safely recover a first publication with unexpected rollback files.'
        }
        foreach ($finalPath in @($ToolchainFinal, $PreflightFinal)) {
            if (Test-Path -LiteralPath $finalPath) {
                if (-not (Test-Path -LiteralPath $finalPath -PathType Leaf)) {
                    throw "Manifest transaction final path is not a file: $finalPath"
                }
                Remove-Item -LiteralPath $finalPath -Force
            }
        }
        if ((Test-Path -LiteralPath $ToolchainFinal) -or
                (Test-Path -LiteralPath $PreflightFinal)) {
            throw 'Cannot safely recover: a partial first manifest pair remains.'
        }
    }

    Remove-Item -LiteralPath $TransactionMarker -Force
    foreach ($temporaryPath in $temporaryPaths) {
        if (Test-Path -LiteralPath $temporaryPath) {
            if (-not (Test-Path -LiteralPath $temporaryPath -PathType Leaf)) {
                throw "Manifest transaction temporary path is not a file: $temporaryPath"
            }
            Remove-Item -LiteralPath $temporaryPath -Force
        }
    }
}

function Publish-ManifestPair {
    param(
        [Parameter(Mandatory = $true)][string] $ToolchainPending,
        [Parameter(Mandatory = $true)][string] $PreflightPending,
        [Parameter(Mandatory = $true)][string] $ToolchainFinal,
        [Parameter(Mandatory = $true)][string] $PreflightFinal,
        [Parameter(Mandatory = $true)][string] $ToolchainRollback,
        [Parameter(Mandatory = $true)][string] $PreflightRollback,
        [Parameter(Mandatory = $true)][string] $TransactionMarker
    )

    if (-not (Test-Path -LiteralPath $ToolchainPending -PathType Leaf) -or
            -not (Test-Path -LiteralPath $PreflightPending -PathType Leaf)) {
        throw 'The pending manifest pair is incomplete; neither final manifest was changed.'
    }
    $allPaths = @(
        $ToolchainPending, $PreflightPending, $ToolchainFinal, $PreflightFinal,
        $ToolchainRollback, $PreflightRollback, $TransactionMarker)
    $pairDirectory = [System.IO.Path]::GetFullPath(
        (Split-Path -Parent $ToolchainPending)).TrimEnd('\')
    foreach ($path in $allPaths) {
        $pathDirectory = [System.IO.Path]::GetFullPath(
            (Split-Path -Parent $path)).TrimEnd('\')
        if (-not $pathDirectory.Equals(
                $pairDirectory, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'All manifest transaction files must share one directory and volume.'
        }
    }
    if (Test-Path -LiteralPath $TransactionMarker) {
        throw "A prior manifest publication transaction requires recovery: $TransactionMarker"
    }
    $markerStagingPath = "$TransactionMarker.staging"
    if (Test-Path -LiteralPath $markerStagingPath) {
        throw "A stale manifest marker staging file requires recovery: $markerStagingPath"
    }
    foreach ($rollbackPath in @($ToolchainRollback, $PreflightRollback)) {
        if (Test-Path -LiteralPath $rollbackPath) {
            throw "A stale manifest rollback requires recovery: $rollbackPath"
        }
    }

    $toolchainHadFinal = Test-Path -LiteralPath $ToolchainFinal -PathType Leaf
    $preflightHadFinal = Test-Path -LiteralPath $PreflightFinal -PathType Leaf
    if ($toolchainHadFinal -ne $preflightHadFinal) {
        throw 'The prior manifest pair is inconsistent; refusing publication without recovery.'
    }
    $oldPairExisted = $toolchainHadFinal
    $toolchainPendingHash = (Get-FileHash -LiteralPath $ToolchainPending -Algorithm SHA256).Hash.ToLowerInvariant()
    $preflightPendingHash = (Get-FileHash -LiteralPath $PreflightPending -Algorithm SHA256).Hash.ToLowerInvariant()
    $oldToolchainHash = $null
    $oldPreflightHash = $null
    try {
        if ($oldPairExisted) {
            $oldToolchainHash = (Get-FileHash -LiteralPath $ToolchainFinal `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
            $oldPreflightHash = (Get-FileHash -LiteralPath $PreflightFinal `
                    -Algorithm SHA256).Hash.ToLowerInvariant()
            Copy-Item -LiteralPath $ToolchainFinal -Destination $ToolchainRollback
            Copy-Item -LiteralPath $PreflightFinal -Destination $PreflightRollback
            if ((Get-FileHash -LiteralPath $ToolchainRollback -Algorithm SHA256).Hash.ToLowerInvariant() `
                    -ne $oldToolchainHash -or
                    (Get-FileHash -LiteralPath $PreflightRollback -Algorithm SHA256).Hash.ToLowerInvariant() `
                    -ne $oldPreflightHash) {
                throw 'Manifest rollback pair did not preserve the prior final bytes.'
            }
        }
        [ordered]@{
            schemaVersion = 1
            oldPairExisted = $oldPairExisted
            oldToolchainSha256 = $oldToolchainHash
            oldPreflightSha256 = $oldPreflightHash
            newToolchainSha256 = $toolchainPendingHash
            newPreflightSha256 = $preflightPendingHash
        } | ConvertTo-Json | Set-Content -LiteralPath $markerStagingPath -Encoding utf8
        [System.IO.File]::Move($markerStagingPath, $TransactionMarker)

        if ($oldPairExisted) {
            [System.IO.File]::Move($ToolchainPending, $ToolchainFinal, $true)
            [System.IO.File]::Move($PreflightPending, $PreflightFinal, $true)
        }
        else {
            [System.IO.File]::Move($ToolchainPending, $ToolchainFinal)
            [System.IO.File]::Move($PreflightPending, $PreflightFinal)
        }
        $toolchainFinalHash = (Get-FileHash -LiteralPath $ToolchainFinal -Algorithm SHA256).Hash.ToLowerInvariant()
        $preflightFinalHash = (Get-FileHash -LiteralPath $PreflightFinal -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($toolchainFinalHash -ne $toolchainPendingHash -or
                $preflightFinalHash -ne $preflightPendingHash) {
            throw 'Published manifest pair does not match the validated pending pair.'
        }
        Remove-Item -LiteralPath $TransactionMarker -Force
        foreach ($rollbackPath in @($ToolchainRollback, $PreflightRollback)) {
            if (Test-Path -LiteralPath $rollbackPath -PathType Leaf) {
                Remove-Item -LiteralPath $rollbackPath -Force
            }
        }
    }
    catch {
        if (Test-Path -LiteralPath $TransactionMarker -PathType Leaf) {
            if ($oldPairExisted) {
                if (-not (Test-Path -LiteralPath $ToolchainRollback -PathType Leaf) -or
                        -not (Test-Path -LiteralPath $PreflightRollback -PathType Leaf)) {
                    throw 'Manifest publication failed and the complete rollback pair is unavailable.'
                }
                Copy-Item -LiteralPath $ToolchainRollback -Destination $ToolchainFinal -Force
                Copy-Item -LiteralPath $PreflightRollback -Destination $PreflightFinal -Force
                if ((Get-FileHash -LiteralPath $ToolchainFinal -Algorithm SHA256).Hash.ToLowerInvariant() `
                        -ne $oldToolchainHash -or
                        (Get-FileHash -LiteralPath $PreflightFinal -Algorithm SHA256).Hash.ToLowerInvariant() `
                        -ne $oldPreflightHash) {
                    throw 'Manifest publication failed and rollback verification also failed.'
                }
            }
            else {
                foreach ($finalPath in @($ToolchainFinal, $PreflightFinal)) {
                    if (Test-Path -LiteralPath $finalPath -PathType Leaf) {
                        Remove-Item -LiteralPath $finalPath -Force
                    }
                }
            }
            Remove-Item -LiteralPath $TransactionMarker -Force
        }
        elseif (Test-Path -LiteralPath $markerStagingPath -PathType Leaf) {
            Remove-Item -LiteralPath $markerStagingPath -Force
        }
        foreach ($temporaryPath in @(
                $ToolchainPending, $PreflightPending,
                $ToolchainRollback, $PreflightRollback)) {
            if (Test-Path -LiteralPath $temporaryPath -PathType Leaf) {
                Remove-Item -LiteralPath $temporaryPath -Force
            }
        }
        throw
    }
}

$workingDirectory = (Get-Location).Path
$outputRoot = Resolve-ManagedOutputRoot -RequestedPath $OutputDirectory
$propertiesPath = Get-AbsolutePath -Path $LocalPropertiesPath -BaseDirectory $workingDirectory
New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
Assert-NoReparsePointAncestors -Path $outputRoot -Label 'OutputDirectory'

$toolchainManifestPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'host-toolchain.psd1') `
    -Label 'Host toolchain manifest'
$preflightManifestPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-preflight.json') `
    -Label 'zlib preflight manifest'
$toolchainManifestPendingPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'host-toolchain.psd1.pending') `
    -Label 'Pending host toolchain manifest'
$preflightManifestPendingPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-preflight.json.pending') `
    -Label 'Pending zlib preflight manifest'
$toolchainManifestRollbackPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'host-toolchain.psd1.rollback') `
    -Label 'Host toolchain manifest rollback'
$preflightManifestRollbackPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-preflight.json.rollback') `
    -Label 'zlib preflight manifest rollback'
$manifestTransactionMarkerPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'manifest-publication.pending') `
    -Label 'Manifest publication marker'
$manifestTransactionMarkerStagingPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate "$manifestTransactionMarkerPath.staging" `
    -Label 'Manifest publication marker staging file'
Restore-ManifestPairTransaction -ToolchainPending $toolchainManifestPendingPath `
    -PreflightPending $preflightManifestPendingPath -ToolchainFinal $toolchainManifestPath `
    -PreflightFinal $preflightManifestPath `
    -ToolchainRollback $toolchainManifestRollbackPath `
    -PreflightRollback $preflightManifestRollbackPath `
    -TransactionMarker $manifestTransactionMarkerPath

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
Assert-PreviousManifestIntegrity -ManifestPath $preflightManifestPath -OutputRoot $outputRoot `
    -ResolvedCMake $tools.CMakeExe -ResolvedCTest $tools.CTestExe

$probeSourceDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'toolchain-probe-source') `
    -Label 'Toolchain probe source directory'
$probeBuildDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'toolchain-probe-build') `
    -Label 'Toolchain probe build directory'
$zlibBuildDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-build') `
    -Label 'zlib build directory'
Assert-CompatibleBuildCache -BuildDirectory $probeBuildDirectory -Label 'Toolchain probe build directory'
Assert-CompatibleBuildCache -BuildDirectory $zlibBuildDirectory -Label 'zlib build directory'
Remove-ManagedDirectory -OutputRoot $outputRoot -Path $probeSourceDirectory `
    -Label 'Toolchain probe source directory'
Remove-ManagedDirectory -OutputRoot $outputRoot -Path $probeBuildDirectory `
    -Label 'Toolchain probe build directory'
Remove-ManagedDirectory -OutputRoot $outputRoot -Path $zlibBuildDirectory `
    -Label 'zlib build directory'

New-Item -ItemType Directory -Path $probeSourceDirectory -Force | Out-Null
$probeSourceDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate $probeSourceDirectory -Label 'Toolchain probe source directory'
$probeProjectPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $probeSourceDirectory 'CMakeLists.txt') `
    -Label 'Toolchain probe project file'
@'
cmake_minimum_required(VERSION 3.22.1)
project(flynes_host_toolchain_probe LANGUAGES C)
if(NOT MSVC)
  message(FATAL_ERROR "FlyNES canonical Windows host gate requires MSVC")
endif()
if(NOT CMAKE_SIZEOF_VOID_P EQUAL 8)
  message(FATAL_ERROR "FlyNES canonical Windows host gate requires x64")
endif()
'@ | Set-Content -LiteralPath $probeProjectPath -Encoding utf8

$probeArguments = @(
    '-G', $generator,
    '-A', $architecture,
    '-S', $probeSourceDirectory,
    '-B', $probeBuildDirectory
) + $msbuildTrackingArguments
$probeConfigureStartedUtc = [DateTime]::UtcNow
Invoke-Checked -Executable $tools.CMakeExe -Arguments $probeArguments `
    -Description 'Canonical toolchain probe configure' -WorkingDirectory $outputRoot
$probeGenerator = Get-CMakeCacheValue -CachePath (Join-Path $probeBuildDirectory 'CMakeCache.txt') `
    -Key 'CMAKE_GENERATOR'
$probeArchitecture = Get-CMakeCacheValue -CachePath (Join-Path $probeBuildDirectory 'CMakeCache.txt') `
    -Key 'CMAKE_GENERATOR_PLATFORM'
if ($probeGenerator -ne $generator -or $probeArchitecture -ne $architecture) {
    throw "Toolchain probe did not preserve generator '$generator' and architecture '$architecture'."
}
$compiler = Get-CompilerIdentity -BuildDirectory $probeBuildDirectory `
    -NotBeforeUtc $probeConfigureStartedUtc.AddSeconds(-1) -Label 'Toolchain probe'
if ($compiler.Id -ne 'MSVC') {
    throw "Expected an x64 MSVC compiler, found '$($compiler.Id) $($compiler.Version)'."
}

$canonicalArchivePath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot $zlibArchiveName) -Label 'Canonical zlib archive'
$archiveStagingPath = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot "$zlibArchiveName.pending") `
    -Label 'Pending canonical zlib archive'
Remove-ManagedFile -OutputRoot $outputRoot -Path $archiveStagingPath `
    -Label 'Pending canonical zlib archive'
try {
    $publishStagedArchive = $false
    if ($ArchivePath) {
        Assert-AbsoluteToolPath -Name 'ArchivePath' -Path $ArchivePath
        $selectedArchivePath = [System.IO.Path]::GetFullPath($ArchivePath)
        if (-not (Test-Path -LiteralPath $selectedArchivePath -PathType Leaf)) {
            throw "ArchivePath does not exist: $selectedArchivePath"
        }
        $selectedArchiveSha256 = Get-LowerSha256 -Path $selectedArchivePath
        if ($selectedArchiveSha256 -ne $zlibArchiveSha256) {
            throw "zlib archive SHA-256 mismatch: expected $zlibArchiveSha256, " +
                "found $selectedArchiveSha256."
        }
        Copy-Item -LiteralPath $selectedArchivePath -Destination $archiveStagingPath
        $publishStagedArchive = $true
    }
    elseif (-not (Test-Path -LiteralPath $canonicalArchivePath -PathType Leaf)) {
        Write-Host "Downloading immutable zlib $zlibVersion archive."
        Invoke-WebRequest -Uri $zlibArchiveUrl -OutFile $archiveStagingPath -TimeoutSec $networkTimeoutSeconds
        $publishStagedArchive = $true
    }
    if ($publishStagedArchive) {
        $stagedArchiveSha256 = Get-LowerSha256 -Path $archiveStagingPath
        if ($stagedArchiveSha256 -ne $zlibArchiveSha256) {
            throw "zlib archive SHA-256 mismatch after staging: expected " +
                "$zlibArchiveSha256, found $stagedArchiveSha256."
        }
        [System.IO.File]::Move($archiveStagingPath, $canonicalArchivePath, $true)
    }
}
finally {
    Remove-ManagedFile -OutputRoot $outputRoot -Path $archiveStagingPath `
        -Label 'Pending canonical zlib archive'
}
$archiveSha256 = Get-LowerSha256 -Path $canonicalArchivePath
if ($archiveSha256 -ne $zlibArchiveSha256) {
    throw "Canonical zlib archive SHA-256 mismatch: expected $zlibArchiveSha256, " +
        "found $archiveSha256."
}

$extractStagingDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-extract-staging') -Label 'Extraction staging directory'
Remove-ManagedDirectory -OutputRoot $outputRoot -Path $extractStagingDirectory `
    -Label 'Extraction staging directory'
New-Item -ItemType Directory -Path $extractStagingDirectory | Out-Null
Assert-NoReparsePointAncestors -Path $extractStagingDirectory `
    -Label 'Extraction staging directory'
$archiveBeforeExtractionSha256 = Get-LowerSha256 -Path $canonicalArchivePath
if ($archiveBeforeExtractionSha256 -ne $zlibArchiveSha256) {
    throw "Canonical zlib archive changed before extraction: expected " +
        "$zlibArchiveSha256, found $archiveBeforeExtractionSha256."
}
Invoke-Checked -Executable $tools.CMakeExe -Arguments @('-E', 'tar', 'xzf', $canonicalArchivePath) `
    -Description 'zlib archive extraction' `
    -WorkingDirectory $extractStagingDirectory
$stagedSourceRoot = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $extractStagingDirectory 'zlib-1.3.1') `
    -Label 'Staged zlib source directory'
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
    Remove-ManagedDirectory -OutputRoot $outputRoot -Path $extractStagingDirectory `
        -Label 'Extraction staging directory'
}
else {
    $sourceRoot = Assert-PathWithinRoot -Root $outputRoot -Candidate $sourceRoot `
        -Label 'zlib source directory'
    Move-Item -LiteralPath $stagedSourceRoot -Destination $sourceRoot
    Remove-ManagedDirectory -OutputRoot $outputRoot -Path $extractStagingDirectory `
        -Label 'Extraction staging directory'
}
$sourceTreeSha256 = Get-CanonicalTreeHash -Root $sourceRoot

$configureSourceRoot = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-configure-source') `
    -Label 'zlib disposable configure source directory'
Remove-ManagedDirectory -OutputRoot $outputRoot -Path $configureSourceRoot `
    -Label 'zlib disposable configure source directory'
Copy-Item -LiteralPath $sourceRoot -Destination $configureSourceRoot -Recurse
$configureSourceRoot = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate $configureSourceRoot -Label 'zlib disposable configure source directory'
$configureSourceHash = Get-CanonicalTreeHash -Root $configureSourceRoot
if ($configureSourceHash -ne $sourceTreeSha256) {
    throw "Disposable zlib configure source differs from the verified extracted tree: expected $sourceTreeSha256, found $configureSourceHash."
}

$installPrefix = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-install') `
    -Label 'zlib static install directory'
$installStagingDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate (Join-Path $outputRoot 'zlib-1.3.1-install-staging') `
    -Label 'zlib static install staging directory'
$configureArguments = @(
    '-G', $generator,
    '-A', $architecture,
    '-S', $configureSourceRoot,
    '-B', $zlibBuildDirectory,
    '-DBUILD_SHARED_LIBS=OFF',
    "-DCMAKE_INSTALL_PREFIX=$installPrefix"
) + $msbuildTrackingArguments + $reproducibleBuildArguments
$zlibConfigureStartedUtc = [DateTime]::UtcNow
$zlibBuildDirectory = Assert-PathWithinRoot -Root $outputRoot `
    -Candidate $zlibBuildDirectory -Label 'zlib build directory'
Invoke-Checked -Executable $tools.CMakeExe -Arguments $configureArguments `
    -Description 'zlib static Release configure' -WorkingDirectory $outputRoot
$zlibCompiler = Get-CompilerIdentity -BuildDirectory $zlibBuildDirectory `
    -NotBeforeUtc $zlibConfigureStartedUtc.AddSeconds(-1) -Label 'zlib configure'
Assert-MatchingToolchainIdentity -Reference $compiler -Candidate $zlibCompiler `
    -ReferenceLabel 'fresh toolchain probe' -CandidateLabel 'fresh zlib configure'

$buildArguments = @(
    '--build', $zlibBuildDirectory, '--config', 'Release', '--target', 'zlibstatic')
Invoke-Checked -Executable $tools.CMakeExe -Arguments $buildArguments `
    -Description 'zlib static Release build' -WorkingDirectory $outputRoot
$unexpectedBuildArtifact = Get-ChildItem -LiteralPath $zlibBuildDirectory -Recurse -File -Force |
    Where-Object { $_.Extension -ieq '.dll' -or $_.Name -ieq 'zlib.lib' } |
    Select-Object -First 1
if ($unexpectedBuildArtifact) {
    throw "Static-only zlib build emitted a shared/import artifact: " +
        $unexpectedBuildArtifact.FullName
}
$postBuildSourceTreeSha256 = Get-CanonicalTreeHash -Root $sourceRoot
if ($postBuildSourceTreeSha256 -ne $sourceTreeSha256) {
    throw "The immutable zlib extracted tree changed during configure/build: expected $sourceTreeSha256, found $postBuildSourceTreeSha256."
}

$builtHeader = Join-Path $sourceRoot 'zlib.h'
$builtGeneratedHeader = Join-Path $zlibBuildDirectory 'zconf.h'
$builtStaticLibrary = Join-Path $zlibBuildDirectory 'Release\zlibstatic.lib'
$sourceLicense = Join-Path $sourceRoot 'LICENSE'
foreach ($requiredFile in @(
        $builtHeader, $builtGeneratedHeader, $builtStaticLibrary, $sourceLicense)) {
    if (-not (Test-Path -LiteralPath $requiredFile -PathType Leaf)) {
        throw "Expected static zlib build artifact is missing: $requiredFile"
    }
}

Remove-ManagedDirectory -OutputRoot $outputRoot -Path $installStagingDirectory `
    -Label 'zlib static install staging directory'
foreach ($relativeDirectory in @('include', 'lib', 'share\licenses\zlib-1.3.1')) {
    $directory = Assert-PathWithinRoot -Root $outputRoot `
        -Candidate (Join-Path $installStagingDirectory $relativeDirectory) `
        -Label 'zlib static install staging child'
    New-Item -ItemType Directory -Path $directory -Force | Out-Null
    Assert-NoReparsePointAncestors -Path $directory -Label 'zlib static install staging child'
}
$stagedHeader = Join-Path $installStagingDirectory 'include\zlib.h'
$stagedGeneratedHeader = Join-Path $installStagingDirectory 'include\zconf.h'
$stagedStaticLibrary = Join-Path $installStagingDirectory 'lib\zlibstatic.lib'
$stagedLicense = Join-Path $installStagingDirectory 'share\licenses\zlib-1.3.1\LICENSE'
Copy-Item -LiteralPath $builtHeader -Destination $stagedHeader
Copy-Item -LiteralPath $builtGeneratedHeader -Destination $stagedGeneratedHeader
Copy-Item -LiteralPath $builtStaticLibrary -Destination $stagedStaticLibrary
Copy-Item -LiteralPath $sourceLicense -Destination $stagedLicense
Assert-ExactStaticInstallTree -InstallRoot $installStagingDirectory

Remove-ManagedDirectory -OutputRoot $outputRoot -Path $installPrefix `
    -Label 'zlib static install directory'
Move-Item -LiteralPath $installStagingDirectory -Destination $installPrefix
$installPrefix = Assert-PathWithinRoot -Root $outputRoot -Candidate $installPrefix `
    -Label 'zlib static install directory'
Assert-ExactStaticInstallTree -InstallRoot $installPrefix

$installedHeader = Join-Path $installPrefix 'include\zlib.h'
$installedGeneratedHeader = Join-Path $installPrefix 'include\zconf.h'
$installedStaticLibrary = Join-Path $installPrefix 'lib\zlibstatic.lib'
$installedLicense = Join-Path $installPrefix 'share\licenses\zlib-1.3.1\LICENSE'
$installTreeSha256 = Get-CanonicalTreeHash -Root $installPrefix

Write-ToolchainManifest -Path $toolchainManifestPendingPath -ResolvedCMake $tools.CMakeExe `
    -ResolvedCTest $tools.CTestExe -CMakeVersion $cmakeVersion -CTestVersion $ctestVersion `
    -Compiler $compiler -DependencyCompiler $zlibCompiler
$toolchainManifestSha256 = Get-LowerSha256 -Path $toolchainManifestPendingPath

$manifest = [ordered]@{
    schemaVersion = 3
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
        canonicalSha256 = $installTreeSha256
        header = [ordered]@{
            path = 'zlib-1.3.1-install/include/zlib.h'
            sha256 = (Get-LowerSha256 -Path $installedHeader)
        }
        generatedHeader = [ordered]@{
            path = 'zlib-1.3.1-install/include/zconf.h'
            sha256 = (Get-LowerSha256 -Path $installedGeneratedHeader)
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
        reproducibleBuildArguments = $reproducibleBuildArguments
        compilerId = $compiler.Id
        compilerVersion = $compiler.Version
        compilerPath = $compiler.CompilerPath
        compilerSha256 = $compiler.CompilerSha256
        visualStudioInstance = $compiler.GeneratorInstance
        platformToolset = $compiler.PlatformToolset
        windowsSdkVersion = $compiler.WindowsSdk
        freshCachePolicy = 'probe and dependency build directories recreated every run'
        probeIdentity = [ordered]@{
            fileSha256 = $compiler.IdentityFileSha256
            writtenUtc = $compiler.IdentityFileWrittenUtc
            projectFileSha256 = $compiler.ProjectFileSha256
            projectWrittenUtc = $compiler.ProjectFileWrittenUtc
        }
        dependencyIdentity = [ordered]@{
            fileSha256 = $zlibCompiler.IdentityFileSha256
            writtenUtc = $zlibCompiler.IdentityFileWrittenUtc
            projectFileSha256 = $zlibCompiler.ProjectFileSha256
            projectWrittenUtc = $zlibCompiler.ProjectFileWrittenUtc
        }
        identitiesMatch = $true
    }
}
$manifest | ConvertTo-Json -Depth 8 |
    Set-Content -LiteralPath $preflightManifestPendingPath -Encoding utf8
$pendingToolchainTokens = $null
$pendingToolchainParseErrors = $null
[Management.Automation.Language.Parser]::ParseFile(
    $toolchainManifestPendingPath,
    [ref]$pendingToolchainTokens,
    [ref]$pendingToolchainParseErrors) | Out-Null
if ($pendingToolchainParseErrors.Count -ne 0) {
    throw "Pending host toolchain manifest is not valid PowerShell data: " +
        $pendingToolchainParseErrors[0].Message
}
$pendingPreflightManifest = Get-Content -LiteralPath $preflightManifestPendingPath -Raw |
    ConvertFrom-Json
if ([int]$pendingPreflightManifest.schemaVersion -ne 3 -or
        [string]$pendingPreflightManifest.toolchain.manifestSha256 -ne
            (Get-LowerSha256 -Path $toolchainManifestPendingPath)) {
    throw 'Pending preflight and host toolchain manifests failed cross-pair validation.'
}
Publish-ManifestPair -ToolchainPending $toolchainManifestPendingPath `
    -PreflightPending $preflightManifestPendingPath -ToolchainFinal $toolchainManifestPath `
    -PreflightFinal $preflightManifestPath `
    -ToolchainRollback $toolchainManifestRollbackPath `
    -PreflightRollback $preflightManifestRollbackPath `
    -TransactionMarker $manifestTransactionMarkerPath

Write-Host "Pinned zlib $zlibVersion host dependency is ready at $installPrefix"
Write-Host "Preflight manifest: $preflightManifestPath"
