<#
scripts/ci-check.ps1 — FlyNES local CI gate (Task S1-4)

Checks (all must PASS for exit 0):
  1. platform-header whitelist  : core/src/*.cpp|*.hpp + core/include/nes/nes.h must
                                  not reference any Android/Windows/SDL/allegro/JNI token.
  2. ABI symbol diff             : nes_* exports of the latest NDK build of libnes_abi.a
                                  vs scripts/abi_symbols.golden.txt.
                                  Missing golden symbol  -> FAIL
                                  New (unlisted) symbol  -> WARN (human reviews, updates golden)
  3. host test                   : bootstrap the pinned zlib/toolchain, configure/build
                                  nes_core_test (Release, VS 2022 x64,
                                  NES_BUILD_TESTS=ON) and run it from the repo root;
                                  must print "PASS (0 failures)" and exit 0.
  4. shared session host test    : same canonical toolchain, configure/build the whole
                                   shared suite (FLYNES_BUILD_TESTS=ON) and run its CTest
                                   set. Guards the session/ABI surface, which Check 3
                                   (core only) never builds.
  5. android build               : gradlew assembleDebug must succeed.

Exit: 0 when every check PASSes, 1 otherwise.

Notes:
  - Android builds use the SDK cmake ($ANDROID_HOME\cmake\3.22.1\bin\cmake.exe)
    when present, falling back to cmake on PATH.
  - The Windows host build imports the canonical toolchain manifest emitted by
    tools/quality/bootstrap_host_zlib.ps1; it never uses PATH-dependent tools.
  - Android NDK path: $ANDROID_HOME\ndk\27.0.12077973 (falls back to the newest
    ndk\* directory).
  - Native-command exit codes are checked via $LASTEXITCODE after every call;
    stderr from cmake (e.g. deprecation warnings) is tolerated.
#>
[CmdletBinding()]
param()

$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
Set-Location $root

$results = [System.Collections.Generic.List[object]]::new()

function Write-Check {
    param([string]$name, [string]$status, [string]$detail = '')
    $results.Add([pscustomobject]@{ Name = $name; Status = $status; Detail = $detail })
    $color = switch ($status) { 'PASS' { 'Green' } 'FAIL' { 'Red' } 'WARN' { 'Yellow' } default { 'Gray' } }
    Write-Host ("[{0}] {1}" -f $status, $name) -ForegroundColor $color
    if ($detail) { Write-Host "      $detail" }
}

# --- tool discovery ----------------------------------------------------------

function Get-AndroidSdkRoot {
    foreach ($candidate in @($env:ANDROID_HOME, $env:ANDROID_SDK_ROOT)) {
        if (-not [string]::IsNullOrWhiteSpace($candidate) -and
            (Test-Path -LiteralPath $candidate -PathType Container)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $propertiesPath = Join-Path $root 'local.properties'
    if (Test-Path -LiteralPath $propertiesPath -PathType Leaf) {
        $sdkLine = Get-Content -LiteralPath $propertiesPath |
            Where-Object { $_ -match '^\s*sdk\.dir\s*=' } |
            Select-Object -First 1
        if ($sdkLine) {
            $rawPath = ($sdkLine -split '=', 2)[1].Trim()
            $decodedPath = [regex]::Replace($rawPath, '\\([\\:= ])', '$1')
            if (Test-Path -LiteralPath $decodedPath -PathType Container) {
                return (Resolve-Path -LiteralPath $decodedPath).Path
            }
        }
    }

    throw 'Android SDK not found: set ANDROID_HOME/ANDROID_SDK_ROOT or provide sdk.dir in local.properties'
}

function Get-CMake {
    $sdk = Join-Path $androidSdkRoot 'cmake\3.22.1\bin\cmake.exe'
    if (Test-Path $sdk) { return $sdk }
    $onPath = Get-Command cmake -ErrorAction SilentlyContinue
    if ($onPath) { return $onPath.Source }
    throw "cmake not found: SDK cmake ($sdk) missing and cmake not on PATH"
}

function Get-NdkRoot {
    $known = Join-Path $androidSdkRoot 'ndk\27.0.12077973'
    if (Test-Path $known) { return $known }
    $ndk = Get-ChildItem (Join-Path $androidSdkRoot 'ndk') -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending | Select-Object -First 1
    if ($ndk) { return $ndk.FullName }
    throw "NDK not found under $androidSdkRoot\ndk"
}

function Get-LlvmNm {
    $nm = Join-Path (Get-NdkRoot) 'toolchains\llvm\prebuilt\windows-x86_64\bin\llvm-nm.exe'
    if (-not (Test-Path $nm)) { throw "llvm-nm not found at $nm" }
    return $nm
}

$androidSdkRoot = Get-AndroidSdkRoot
$cmake = Get-CMake

# ============================================================================
# Check 1 — platform-header whitelist
# ============================================================================
Write-Host ''
Write-Host '== Check 1/4: platform-header whitelist ==' -ForegroundColor Cyan
$forbidden = @('jni.h', 'android/', 'ANativeWindow', 'AAudio', 'JNIEnv', 'windows.h', 'allegro', 'SDL')

$scanFiles = @()
Get-ChildItem (Join-Path $root 'core\src') -File |
    Where-Object { $_.Extension -in '.cpp', '.hpp' } | ForEach-Object { $scanFiles += $_.FullName }
$scanFiles += (Join-Path $root 'core\include\nes\nes.h')

$hits = @()
foreach ($file in $scanFiles) {
    $lines = Get-Content $file
    for ($i = 0; $i -lt $lines.Count; $i++) {
        foreach ($token in $forbidden) {
            if ($lines[$i].Contains($token)) {
                $hits += ("{0}:{1}: forbidden token '{2}'" -f $file.Replace("$root\", ''), ($i + 1), $token)
            }
        }
    }
}

if ($hits.Count -gt 0) {
    Write-Check 'platform-header whitelist' 'FAIL' ("{0} hit(s): {1}" -f $hits.Count, ($hits -join '; '))
} else {
    Write-Check 'platform-header whitelist' 'PASS' ("scanned {0} file(s) for {1} forbidden token(s)" -f $scanFiles.Count, $forbidden.Count)
}

# ============================================================================
# Check 2 — ABI symbol diff (vs scripts/abi_symbols.golden.txt)
# ============================================================================
Write-Host ''
Write-Host '== Check 2/4: ABI symbol diff ==' -ForegroundColor Cyan

$androidDir = Join-Path $root 'core\build\android'
$androidCache = Join-Path $androidDir 'CMakeCache.txt'
$lib = Join-Path $androidDir 'libnes_abi.a'
$goldenFile = Join-Path $root 'scripts\abi_symbols.golden.txt'

$androidOk = $true
if (-not (Test-Path $androidCache)) {
    Write-Host '  configuring android build (no cache found)...'
    $ninja = Join-Path $androidSdkRoot 'cmake\3.22.1\bin\ninja.exe'
    $toolchain = Join-Path (Get-NdkRoot) 'build\cmake\android.toolchain.cmake'
    & $cmake -S (Join-Path $root 'core') -B $androidDir -G Ninja `
        "-DCMAKE_TOOLCHAIN_FILE=$toolchain" `
        '-DANDROID_ABI=arm64-v8a' '-DANDROID_PLATFORM=android-24' `
        "-DCMAKE_MAKE_PROGRAM=$ninja" | Out-Host
    if ($LASTEXITCODE -ne 0) {
        $androidOk = $false
        Write-Check 'ABI symbol diff' 'FAIL' 'android configure failed'
    }
} else {
    Write-Host '  android build cache found'
}

# Incremental rebuild: a no-op when fresh, but guarantees we diff the LATEST
# exports (a stale lib would otherwise fail the gate with phantom diffs).
if ($androidOk) {
& $cmake --build $androidDir --target nes_abi -j 4 | Out-Host
if ($LASTEXITCODE -ne 0) {
    Write-Check 'ABI symbol diff' 'FAIL' "android nes_abi build failed (exit $LASTEXITCODE)"
} else {
    $nm = Get-LlvmNm
    $current = @(
        & $nm --defined-only $lib |
            Select-String -Pattern ' [TtWw] ' |
            ForEach-Object { ($_ -split '\s+')[-1] } |
            Where-Object { $_ -like 'nes_*' } |
            Sort-Object -Unique
    )
    $golden = @(
        Get-Content $goldenFile |
            Where-Object { $_ -and -not $_.StartsWith('#') } |
            ForEach-Object { $_.Trim() } |
            Sort-Object -Unique
    )
    $missing = @($golden | Where-Object { $current -notcontains $_ })
    $added   = @($current | Where-Object { $golden -notcontains $_ })

    $detail = "$($golden.Count) golden, $($current.Count) current"
    if ($missing.Count -gt 0) {
        Write-Check 'ABI symbol diff' 'FAIL' ("$detail — MISSING from lib: " + ($missing -join ', '))
    } elseif ($added.Count -gt 0) {
        Write-Check 'ABI symbol diff' 'WARN' ("$detail — NEW symbol(s) not in golden: " + ($added -join ', ') + ' (human review, then update scripts/abi_symbols.golden.txt)')
    } else {
        Write-Check 'ABI symbol diff' 'PASS' "$detail — exports match golden"
    }
}
}

# ============================================================================
# Check 3 — host test (configure/build nes_core_test, run from repo root)
# ============================================================================
Write-Host ''
Write-Host '== Check 3/5: host test ==' -ForegroundColor Cyan

$hostDir = Join-Path $root '.artifacts\build\core-host'
$hostCache = Join-Path $hostDir 'CMakeCache.txt'
$hostDepsDir = Join-Path $root '.artifacts\host-deps'
$hostToolchainManifest = Join-Path $hostDepsDir 'host-toolchain.psd1'
$hostZlibRoot = Join-Path $hostDepsDir 'zlib-1.3.1-install'
$hostBootstrap = Join-Path $root 'tools\quality\bootstrap_host_zlib.ps1'
$pwshExe = Join-Path $PSHOME 'pwsh.exe'
$hostOk = $true
$env:MSBUILDDISABLENODEREUSE = '1'

Write-Host '  bootstrapping canonical host zlib/toolchain...'
& $pwshExe -NoProfile -File $hostBootstrap -OutputDirectory $hostDepsDir | Out-Host
if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $hostToolchainManifest -PathType Leaf)) {
    $hostOk = $false
    Write-Check 'host test' 'FAIL' 'canonical host dependency/toolchain bootstrap failed'
}

$hostTools = $null
if ($hostOk) {
    try {
        $hostTools = Import-PowerShellDataFile -LiteralPath $hostToolchainManifest
    } catch {
        $hostOk = $false
        Write-Check 'host test' 'FAIL' "cannot import canonical host toolchain manifest: $($_.Exception.Message)"
    }
}

if ($hostOk) {
    $hostCMake = [string] $hostTools.CMakeExe
    $hostCTest = [string] $hostTools.CTestExe
    $hostConfigureArgs = @(
        '-S', (Join-Path $root 'core'),
        '-B', $hostDir,
        '-G', ([string] $hostTools.Generator),
        '-A', ([string] $hostTools.Architecture),
        '-DNES_BUILD_TESTS=ON',
        "-DZLIB_ROOT=$hostZlibRoot"
    ) + @($hostTools.CMakeConfigureArguments)

    Write-Host '  configuring host build with the canonical toolchain...'
    & $hostCMake @hostConfigureArgs | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $hostCache -PathType Leaf)) {
        $hostOk = $false
        Write-Check 'host test' 'FAIL' 'host configure failed'
    }
}

if ($hostOk) {
    & $hostCMake --build $hostDir --target nes_core_test temporal_interpolator_test `
        --config Release -j 4 | Out-Host
    if ($LASTEXITCODE -ne 0) {
        Write-Check 'host test' 'FAIL' "host test build failed (exit $LASTEXITCODE)"
    } else {
        & $hostCTest --test-dir $hostDir -C Release --output-on-failure | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Check 'host test' 'FAIL' "CTest failed (exit $LASTEXITCODE)"
        } else {
            $exe = Get-ChildItem $hostDir -Recurse -Filter 'nes_core_test.exe' |
                Sort-Object LastWriteTime -Descending | Select-Object -First 1
            if (-not $exe) {
                Write-Check 'host test' 'FAIL' 'nes_core_test.exe not found under .artifacts/build/core-host'
            } else {
                # Run from the repo root: the test's fixture paths are repo-root-relative.
                $testOut = & $exe.FullName 2>&1 | Out-String
                $testExit = $LASTEXITCODE
                if ($testExit -eq 0 -and $testOut -match 'PASS \(0 failures\)') {
                    Write-Check 'host test' 'PASS' `
                        "CTest plus nes_core_test exit=$testExit, RESULT PASS (0 failures)"
                } else {
                    Write-Check 'host test' 'FAIL' `
                        "nes_core_test exit=$testExit, expected exit 0 and 'PASS (0 failures)'"
                    Write-Host ($testOut | Select-Object -Last 15 | Out-String)
                }
            }
        }
    }
}

# ============================================================================
# Check 4 — shared session host test (reuses the Check 3 canonical toolchain)
# ============================================================================
Write-Host ''
Write-Host '== Check 4/5: shared session host test ==' -ForegroundColor Cyan

$sharedDir = Join-Path $root '.artifacts\build\shared-host'
$sharedCache = Join-Path $sharedDir 'CMakeCache.txt'

if (-not $hostTools) {
    # Check 3 already reported why the canonical toolchain is unavailable.
    Write-Check 'shared session test' 'FAIL' 'canonical host toolchain unavailable (see host test)'
} else {
    $sharedOk = $true
    $sharedConfigureArgs = @(
        '-S', (Join-Path $root 'shared'),
        '-B', $sharedDir,
        '-G', ([string] $hostTools.Generator),
        '-A', ([string] $hostTools.Architecture),
        '-DFLYNES_BUILD_TESTS=ON',
        "-DZLIB_ROOT=$hostZlibRoot"
    ) + @($hostTools.CMakeConfigureArguments)

    Write-Host '  configuring shared build with the canonical toolchain...'
    & $hostCMake @sharedConfigureArgs | Out-Host
    if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $sharedCache -PathType Leaf)) {
        $sharedOk = $false
        Write-Check 'shared session test' 'FAIL' 'shared configure failed'
    }

    if ($sharedOk) {
        & $hostCMake --build $sharedDir --config Release -j 4 | Out-Host
        if ($LASTEXITCODE -ne 0) {
            Write-Check 'shared session test' 'FAIL' "shared build failed (exit $LASTEXITCODE)"
        } else {
            $sharedTestOut = & $hostCTest --test-dir $sharedDir -C Release --output-on-failure 2>&1 | Out-String
            $sharedTestExit = $LASTEXITCODE
            $sharedSummary = ($sharedTestOut -split "`r?`n" |
                Where-Object { $_ -match 'tests passed|tests failed out of' } |
                Select-Object -Last 1)
            if ($sharedTestExit -eq 0 -and $sharedTestOut -match '100% tests passed') {
                Write-Check 'shared session test' 'PASS' ("shared CTest green: " + $sharedSummary.Trim())
            } else {
                Write-Check 'shared session test' 'FAIL' `
                    "shared CTest failed (exit $sharedTestExit)"
                Write-Host ($sharedTestOut -split "`r?`n" | Select-Object -Last 25 | Out-String)
            }
        }
    }
}

# ============================================================================
# Check 5 — android build (gradlew assembleDebug)
# ============================================================================
Write-Host ''
Write-Host '== Check 5/5: android build (assembleDebug) ==' -ForegroundColor Cyan

$gradleOut = & (Join-Path $root 'gradlew.bat') assembleDebug 2>&1 | Out-String
$gradleExit = $LASTEXITCODE
if ($gradleExit -eq 0 -and $gradleOut -match 'BUILD SUCCESSFUL') {
    Write-Check 'android build' 'PASS' 'assembleDebug BUILD SUCCESSFUL'
} else {
    Write-Check 'android build' 'FAIL' "assembleDebug failed (exit $gradleExit)"
    Write-Host ($gradleOut -split "`r?`n" | Select-Object -Last 15 | Out-String)
}

# ============================================================================
# Summary
# ============================================================================
Write-Host ''
Write-Host '================ CI GATE SUMMARY ================' -ForegroundColor Cyan
foreach ($r in $results) {
    Write-Host ("  [{0}] {1}" -f $r.Status, $r.Name)
}
$failed = @($results | Where-Object { $_.Status -eq 'FAIL' })
if ($failed.Count -gt 0) {
    Write-Host ("CI GATE FAILED: {0} check(s) failed" -f $failed.Count) -ForegroundColor Red
    exit 1
}
Write-Host 'CI GATE PASSED: all checks green' -ForegroundColor Green
exit 0
