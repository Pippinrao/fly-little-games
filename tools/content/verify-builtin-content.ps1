#!/usr/bin/env pwsh
<#
.SYNOPSIS
    Gate for the bundled homebrew content: one manifest, one ROM set, three consumers.

.DESCRIPTION
    Fails closed when the repository stops being able to prove that every bundled
    game is licensed, hashed and described by exactly one source of truth.

    Checks:
      1. content/assets/builtin-games.json exists, parses and declares schemaVersion 1.
      2. Every game carries the required fields, unique ids, unique sort order and
         non-empty titles.
      3. Every referenced ROM exists, is an iNES payload and matches romSha256.
      4. Every referenced license text exists; copyleft entries declare a sourceOffer.
      5. content/sources.lock.json describes exactly the bundled games with matching
         hashes and a pinned revision, build environment and output path.
      6. No platform keeps its own committed .nes copy - content/assets is the only
         source of truth.
      7. All three platforms read the shared manifest through their BuiltinGames loader.
      8. No shipped source still references from_below.

    Exit code 0 only when every check passes.
#>
[CmdletBinding()]
param(
    [string] $Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'

$script:Failures = [System.Collections.Generic.List[string]]::new()

function Add-Failure {
    # Diagnostics go to the success stream on purpose: the gate is consumed by
    # PowerShell callers and CI wrappers that merge and capture streams, and
    # [Console]::Error.WriteLine would bypass that capture entirely.
    param([Parameter(Mandatory = $true)][string] $Message)
    $script:Failures.Add($Message)
    Write-Output "FAIL $Message"
}

function Get-RelativePath {
    param([Parameter(Mandatory = $true)][string] $FullPath)
    $prefix = $Root.TrimEnd('\', '/') + [System.IO.Path]::DirectorySeparatorChar
    if ($FullPath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)) {
        return $FullPath.Substring($prefix.Length)
    }
    return $FullPath
}

function Get-Sha256 {
    param([Parameter(Mandatory = $true)][string] $Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

# --- 1/2. manifest -----------------------------------------------------------

$manifestPath = Join-Path $Root 'content\assets\builtin-games.json'
if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) {
    Add-Failure "bundled game manifest is missing: content/assets/builtin-games.json"
    Write-Output "FAIL content gate aborted: $($script:Failures.Count) problem(s)"
    exit 1
}

try {
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
}
catch {
    Add-Failure "bundled game manifest is not valid JSON: $($_.Exception.Message)"
    exit 1
}

if ($manifest.schemaVersion -ne 1) {
    Add-Failure "builtin-games.json schemaVersion must be 1"
}
$games = @($manifest.games)
if ($games.Count -eq 0) {
    Add-Failure "builtin-games.json must bundle at least one game"
}

$requiredGameFields = @(
    'canonicalId', 'assetFilename', 'titleEn', 'titleZhHans',
    'mapper', 'romSha256', 'credit', 'sortOrder', 'coverEligible', 'license')
$requiredLicenseFields = @('spdx', 'file', 'sourceUrl', 'sourceRevision')

$seenIds = @{}
$seenFiles = @{}
$seenOrder = @{}
$coverEligibleGames = [System.Collections.Generic.List[string]]::new()
$expectedIds = [System.Collections.Generic.HashSet[string]]::new()

foreach ($game in $games) {
    $id = [string]$game.canonicalId
    $label = "game '$id'"

    foreach ($field in $requiredGameFields) {
        if ($null -eq $game.PSObject.Properties[$field]) {
            Add-Failure "$label is missing field '$field'"
        }
    }
    if ([string]::IsNullOrWhiteSpace($id) -or -not $id.StartsWith('builtin:')) {
        Add-Failure "canonicalId must start with 'builtin:' (found '$id')"
        continue
    }
    if ($seenIds.ContainsKey($id)) { Add-Failure "duplicate canonicalId '$id'" }
    $seenIds[$id] = $true
    [void]$expectedIds.Add($id)

    $fileName = [string]$game.assetFilename
    if ([string]::IsNullOrWhiteSpace($fileName) -or -not $fileName.EndsWith('.nes')) {
        Add-Failure "$label assetFilename must be a .nes filename (found '$fileName')"
    }
    if ($fileName -match '[\\/]') {
        Add-Failure "$label assetFilename must be a bare filename (found '$fileName')"
    }
    if ($seenFiles.ContainsKey($fileName)) { Add-Failure "duplicate assetFilename '$fileName'" }
    $seenFiles[$fileName] = $true

    foreach ($field in @('titleEn', 'titleZhHans', 'credit')) {
        if ([string]::IsNullOrWhiteSpace([string]$game.$field)) {
            Add-Failure "$label $field must be a non-empty string"
        }
    }
    if ($null -ne $game.mapper -and ([int]$game.mapper -lt 0)) {
        Add-Failure "$label mapper must not be negative"
    }
    $order = [string]$game.sortOrder
    if ($seenOrder.ContainsKey($order)) { Add-Failure "$label duplicate sortOrder $order" }
    $seenOrder[$order] = $true

    # The cover quality gate discards flat title screens, so a device test that
    # must observe a captured cover selects its game from this flag. At least one
    # bundled game has to stay capturable or that evidence becomes impossible.
    $coverEligible = $game.coverEligible
    if ($coverEligible -isnot [bool]) {
        Add-Failure "$label coverEligible must be a JSON boolean"
    }
    elseif ($coverEligible) {
        [void]$coverEligibleGames.Add($id)
    }

    $romSha = ([string]$game.romSha256).ToLowerInvariant()
    if ($romSha -notmatch '^[0-9a-f]{64}$') {
        Add-Failure "$label romSha256 must be 64 lowercase hex characters"
    }
    else {
        $romPath = Join-Path $Root "content\assets\roms\$fileName"
        if (-not (Test-Path -LiteralPath $romPath -PathType Leaf)) {
            Add-Failure "$label is missing its ROM asset content/assets/roms/$fileName"
        }
        else {
            $actual = Get-Sha256 -Path $romPath
            if ($actual -ne $romSha) {
                Add-Failure "$label ROM hash does not match the manifest (manifest $romSha, file $actual)"
            }
            $header = [System.IO.File]::ReadAllBytes($romPath)[0..3]
            if (-not ($header[0] -eq 0x4E -and $header[1] -eq 0x45 -and
                      $header[2] -eq 0x53 -and $header[3] -eq 0x1A)) {
                Add-Failure "$label ROM asset $fileName is not an iNES payload"
            }
        }
    }

    $license = $game.license
    if ($null -eq $license) {
        Add-Failure "$label has no license block"
        continue
    }
    foreach ($field in $requiredLicenseFields) {
        if ($null -eq $license.PSObject.Properties[$field]) {
            Add-Failure "$label license is missing field '$field'"
        }
    }
    $spdx = [string]$license.spdx
    if ([string]::IsNullOrWhiteSpace($spdx)) {
        Add-Failure "$label license.spdx must not be empty"
    }
    $licenseFile = [string]$license.file
    if ([string]::IsNullOrWhiteSpace($licenseFile) -or
            -not (Test-Path -LiteralPath (Join-Path $Root "content\assets\licenses\$licenseFile") -PathType Leaf)) {
        Add-Failure "$label license text is missing: content/assets/licenses/$licenseFile"
    }
    if (-not ([string]$license.sourceUrl).StartsWith('https://')) {
        Add-Failure "$label license.sourceUrl must be an https URL"
    }
    if (([string]$license.sourceRevision) -notmatch '^[0-9a-f]{40}$') {
        Add-Failure "$label license.sourceRevision must be a 40-character commit sha"
    }
    if ($spdx -match '^(GPL|AGPL|LGPL|MPL)-') {
        if ([string]::IsNullOrWhiteSpace([string]$license.sourceOffer)) {
            Add-Failure "$label copyleft license '$spdx' requires a sourceOffer statement"
        }
    }
}

if ($coverEligibleGames.Count -eq 0) {
    Add-Failure "no bundled game has coverEligible true; device cover capture can no longer be observed"
}

# --- 5. build lock -----------------------------------------------------------

$lockPath = Join-Path $Root 'content\sources.lock.json'
$lockIds = [System.Collections.Generic.HashSet[string]]::new()
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) {
    Add-Failure "build lock is missing: content/sources.lock.json"
}
else {
    try {
        $lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
        foreach ($source in @($lock.sources)) {
            $id = [string]$source.canonicalId
            if ([string]::IsNullOrWhiteSpace($id)) {
                Add-Failure "sources.lock.json has a record without canonicalId"
                continue
            }
            [void]$lockIds.Add($id)
            $game = $games | Where-Object { [string]$_.canonicalId -eq $id } | Select-Object -First 1
            if ($null -eq $game) {
                Add-Failure "sources.lock.json describes unbundled game '$id'"
                continue
            }
            if (([string]$source.romSha256).ToLowerInvariant() -ne
                    ([string]$game.romSha256).ToLowerInvariant()) {
                Add-Failure "game '$id' lock romSha256 disagrees with the manifest"
            }
            if ([string]$source.sourceRevision -ne [string]$game.license.sourceRevision) {
                Add-Failure "game '$id' lock sourceRevision disagrees with the manifest"
            }
            foreach ($field in @('upstreamRepo', 'buildEnv', 'outputRomPath')) {
                if ([string]::IsNullOrWhiteSpace([string]$source.$field)) {
                    Add-Failure "game '$id' lock record needs a non-empty $field"
                }
            }
        }
    }
    catch {
        Add-Failure "sources.lock.json is not valid JSON: $($_.Exception.Message)"
    }
}
foreach ($id in $expectedIds) {
    if (-not $lockIds.Contains($id)) {
        Add-Failure "game '$id' has no sources.lock.json build record"
    }
}

# --- 6. one source of truth --------------------------------------------------

# Generated copies are fine; a *tracked* copy is not. Platforms must either merge
# content/assets at build time (Android, iOS) or stage it with
# tools/content/sync-builtin-content.ps1 (HarmonyOS), whose output is ignored.
$platformAssetDirs = @(
    'app\src\main\assets\roms',
    'harmony\entry\src\main\resources\rawfile',
    'core\tests\fixtures')
foreach ($relative in $platformAssetDirs) {
    $directory = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $directory -File -Filter '*.nes' -ErrorAction SilentlyContinue) {
        $repoRelative = Get-RelativePath -FullPath $file.FullName
        # A ROM may sit in a platform directory only as a generated file that git
        # ignores. Anything else is a second copy of the truth.
        & git -C $Root check-ignore -q -- $repoRelative 2>$null
        if ($LASTEXITCODE -ne 0) {
            Add-Failure ("a platform keeps its own ROM copy instead of using content/assets " +
                "as the single source of truth (not a generated, ignored file): $repoRelative")
        }
    }
}

# --- 7. three consumers read the shared manifest -----------------------------

$loaders = [ordered]@{
    Android   = 'app\src\main\java\com\flynes\emu\catalog\BuiltinGames.java'
    HarmonyOS = 'harmony\entry\src\main\ets\service\BuiltinGames.ets'
    iOS       = 'ios\app\platform\BuiltinGames.mm'
}
foreach ($platform in $loaders.Keys) {
    $relative = $loaders[$platform]
    $path = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        Add-Failure "$platform BuiltinGames loader is missing: $relative"
        continue
    }
    if ((Get-Content -Raw -LiteralPath $path) -notmatch 'builtin-games\.json') {
        Add-Failure "$platform BuiltinGames loader must read builtin-games.json"
    }
}

# --- 8. no legacy from_below reference in shipped source ---------------------

$scanRoots = @(
    'app\src', 'harmony\entry\src', 'ios\app', 'ios\tests', 'ios\scripts',
    'shared\src', 'shared\include', 'shared\tests', 'core\src', 'core\include')
$scanExtensions = @('.java', '.kt', '.ets', '.ts', '.js', '.m', '.mm', '.h', '.hpp',
    '.cpp', '.c', '.swift', '.xml', '.json', '.gradle', '.kts', '.txt')
$legacy = [System.Collections.Generic.List[string]]::new()
foreach ($relative in $scanRoots) {
    $directory = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    $files = Get-ChildItem -LiteralPath $directory -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $scanExtensions -contains $_.Extension.ToLowerInvariant() }
    foreach ($file in $files) {
        $match = Select-String -LiteralPath $file.FullName -Pattern 'from[-_ ]below' -List `
            -ErrorAction SilentlyContinue
        if ($match) { [void]$legacy.Add((Get-RelativePath -FullPath $file.FullName)) }
    }
}
foreach ($relative in @('LICENSE', 'README.md')) {
    $path = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { continue }
    if (Select-String -LiteralPath $path -Pattern 'from[-_ ]below' -List -ErrorAction SilentlyContinue) {
        [void]$legacy.Add($relative)
    }
}
if ($legacy.Count -gt 0) {
    Add-Failure ("shipped source still references the retired bundled game ({0}): {1}" -f
        $legacy.Count, (($legacy | Select-Object -First 8) -join ', '))
}

# The retired game was also checked in as a ROM binary on every platform, which a
# text scan cannot see. Any tracked path that still carries its name must go: a
# leftover asset ships inside the package even when no source line mentions it.
$legacyNamed = [System.Collections.Generic.List[string]]::new()
$tracked = & git -C $Root ls-files 2>$null
if ($LASTEXITCODE -eq 0) {
    foreach ($relative in $tracked) {
        if ($relative -match 'from[-_ ]below') { [void]$legacyNamed.Add($relative) }
    }
}
elseif (Test-Path -LiteralPath (Join-Path $Root '.git')) {
    # A repository that answers git but cannot list files is a real failure, not
    # a fixture: the checked-in-asset guarantee would otherwise be unenforced.
    Add-Failure "content gate could not list tracked files, so checked-in assets are unverified"
}
if ($legacyNamed.Count -gt 0) {
    Add-Failure ("retired bundled game is still checked in ({0}): {1}" -f
        $legacyNamed.Count, (($legacyNamed | Select-Object -First 8) -join ', '))
}

# --- 9. decoupling: no platform source may name a bundled game ---------------

# Adding a bundled game must be a manifest edit plus an asset, never a code
# change. If any product source names a game's file, id or title, that promise
# is broken, so this is checked instead of asserted in prose.
$productRoots = @('app\src\main', 'harmony\entry\src\main', 'ios\app')
$productExtensions = @('.java', '.kt', '.ets', '.ts', '.js', '.m', '.mm', '.h', '.hpp',
    '.cpp', '.c', '.swift', '.xml', '.json')
$named = [System.Collections.Generic.List[string]]::new()
foreach ($relative in $productRoots) {
    $directory = Join-Path $Root $relative
    if (-not (Test-Path -LiteralPath $directory -PathType Container)) { continue }
    $files = Get-ChildItem -LiteralPath $directory -Recurse -File -ErrorAction SilentlyContinue |
        Where-Object { $productExtensions -contains $_.Extension.ToLowerInvariant() }
    foreach ($file in $files) {
        $repoRelative = Get-RelativePath -FullPath $file.FullName
        # Generated, git-ignored copies (the staged manifest and assets) are data,
        # not source, so they are allowed to carry the game names.
        & git -C $Root check-ignore -q -- $repoRelative 2>$null
        if ($LASTEXITCODE -eq 0) { continue }
        $text = Get-Content -Raw -LiteralPath $file.FullName -ErrorAction SilentlyContinue
        if ($null -eq $text) { continue }
        foreach ($game in $games) {
            foreach ($token in @([string]$game.assetFilename, [string]$game.canonicalId,
                                 [string]$game.titleEn)) {
                if ($token.Length -gt 0 -and $text.Contains($token)) {
                    [void]$named.Add("${repoRelative}: $token")
                }
            }
        }
    }
}
if ($named.Count -gt 0) {
    Add-Failure ("platform source still names a bundled game, so adding one would need a code " +
        "change ({0}): {1}" -f $named.Count, (($named | Select-Object -First 6) -join '; '))
}

# --- verdict -----------------------------------------------------------------

if ($script:Failures.Count -gt 0) {
    Write-Output "FAIL content gate aborted: $($script:Failures.Count) problem(s)"
    exit 1
}

Write-Output "PASS builtin content gate ($($games.Count) games, single source of truth)"
exit 0
