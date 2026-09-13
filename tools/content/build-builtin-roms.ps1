<#
.SYNOPSIS
    Build every bundled homebrew ROM from its pinned upstream revision.

.DESCRIPTION
    Reads content/sources.lock.json, fetches and SHA-256-verifies any missing
    source tarball on the Windows side (WSL's GitHub access is unreliable on
    this machine), then hands the extraction and compilation to
    tools/content/build-in-wsl.sh running inside WSL.

    A ROM whose bytes no longer match the lock is a FAILURE. Accepting a new
    hash requires -AcceptNewHash together with -Reason; the lock is then
    rewritten for exactly the games that changed and the reason is recorded.

    Verified ROMs land in content/assets/roms/ under their asset filenames.

.PARAMETER GameId
    Build only one game by canonical id (useful while iterating).

.PARAMETER SkipFetch
    Do not touch the network; fail if a pinned tarball is not already cached.
#>
[CmdletBinding()]
param(
    [string] $Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $WslDistro = 'Ubuntu-24.04',
    [string] $GameId = '',
    [switch] $AcceptNewHash,
    [string] $Reason = '',
    [switch] $SkipFetch
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string] $Message) Write-Host "==> $Message" -ForegroundColor Cyan }
function Fail { param([string] $Message) [Console]::Error.WriteLine("BUILD_FAIL $Message"); exit 1 }
$forward = {
    # WSL sees this repository under /mnt/<drive>/..., never as a Windows path.
    param([string] $p)
    $full = [System.IO.Path]::GetFullPath($p)
    $drive = $full.Substring(0, 1).ToLowerInvariant()
    return '/mnt/' + $drive + ($full.Substring(2) -replace '\\', '/')
}

if ($AcceptNewHash -and [string]::IsNullOrWhiteSpace($Reason)) {
    Fail '-AcceptNewHash requires -Reason explaining why the pinned ROM hash changed'
}

$lockPath = Join-Path $Root 'content\sources.lock.json'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { Fail 'missing content/sources.lock.json' }
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json

$artifacts = Join-Path $Root '.artifacts\content'
$cache = Join-Path $artifacts 'src-cache'
$toolchain = Join-Path $artifacts 'toolchain'
$romDir = Join-Path $Root 'content\assets\roms'
New-Item -ItemType Directory -Force -Path $cache, $romDir | Out-Null

if (-not (Test-Path -LiteralPath (Join-Path $toolchain 'millfork\millfork.jar'))) {
    Fail 'the pinned toolchain is missing; run tools/content/bootstrap-content-toolchain.ps1 first'
}

$sources = @($lock.sources)
if ($GameId) {
    $sources = @($sources | Where-Object { $_.canonicalId -eq $GameId })
    if ($sources.Count -eq 0) { Fail "no lock record for '$GameId'" }
}

foreach ($source in $sources) {
    $id = [string]$source.canonicalId
    $revision = [string]$source.sourceRevision
    $short = $revision.Substring(0, 7)
    # Cache names must be filesystem-safe: the canonical id carries a namespace
    # colon, which Windows rejects in a filename.
    $slug = $id -replace '^builtin:', ''
    $name = "$slug-$short.tar.gz"
    $target = Join-Path $cache $name
    $expected = ([string]$source.sourceTarballSha256).ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') { Fail "the lock has no sourceTarballSha256 pin for $id" }

    if (Test-Path -LiteralPath $target) {
        $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected) {
            if ($SkipFetch) { Fail "$name is cached but has drifted from its pin" }
            Write-Host "    $name cache is stale; refetching" -ForegroundColor Yellow
            Remove-Item -LiteralPath $target -Force
        }
    }
    if (-not (Test-Path -LiteralPath $target)) {
        if ($SkipFetch) { Fail "$name is not cached and -SkipFetch was requested" }
        $repo = ([string]$source.upstreamRepo) -replace '^https://github\.com/', ''
        $url = "https://codeload.github.com/$repo/tar.gz/$revision"
        Write-Step "fetching $name"
        try { Invoke-WebRequest -Uri $url -OutFile $target -TimeoutSec 900 }
        catch { Fail "download failed for $name ($url): $($_.Exception.Message)" }
        $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -ne $expected) { Fail "SHA-256 mismatch for ${name}: pinned $expected but downloaded $actual" }
    }
}

Write-Step 'building in WSL'
$worker = (& $forward (Join-Path $Root 'tools\content\build-in-wsl.sh'))
$lockArg = (& $forward $lockPath)
$cacheArg = (& $forward $cache)
$toolchainArg = (& $forward $toolchain)
# Builds land in a staging directory; only verified ROMs reach content/assets.
$staging = Join-Path $artifacts 'built-roms'
Remove-Item -LiteralPath $staging -Recurse -Force -ErrorAction SilentlyContinue
New-Item -ItemType Directory -Force -Path $staging | Out-Null
$stagingArg = (& $forward $staging)

$script = "FLYNES_ACCEPT_NEW_HASH=$(if ($AcceptNewHash) { 1 } else { 0 }) bash '$worker' " +
          "'$lockArg' '$cacheArg' '$toolchainArg' '$stagingArg' '$GameId'"
$output = & wsl.exe -d $WslDistro -u root -- bash -lc $script 2>&1
$lines = $output -split "`r?`n"
$lines | Where-Object { $_ -and $_ -notmatch '^RESULT\|' } | ForEach-Object { Write-Host $_ }

$results = @{}
foreach ($line in $lines) {
    if ($line -match '^RESULT\|([^|]+)\|([^|]+)\|(.*)$') {
        $results[$Matches[1]] = [pscustomobject]@{
            Status = $Matches[2]
            Detail = $Matches[3]
        }
    }
}
if ($results.Count -eq 0) { Fail "the WSL worker produced no results; raw output:`n$($lines -join "`n")" }

$failed = @($results.GetEnumerator() | Where-Object { $_.Value.Status -ne 'OK' })
if ($failed.Count -gt 0) {
    foreach ($entry in $failed) {
        [Console]::Error.WriteLine("FAIL $($entry.Key): $($entry.Value.Detail)")
    }
    Fail "$($failed.Count) bundled game(s) did not build"
}

Write-Step 'verifying hashes and mapper against the lock'
$changed = @()
foreach ($source in $sources) {
    $id = [string]$source.canonicalId
    $result = $results[$id]
    if ($null -eq $result) { Fail "$id has no build result" }
    $parts = $result.Detail -split '\|'
    $sha = $parts[0]; $size = $parts[1]; $asset = $parts[2]
    $policy = if ($parts.Count -gt 3) { $parts[3] } else { 'pinned' }
    $staged = Join-Path $staging $asset
    if (-not (Test-Path -LiteralPath $staged -PathType Leaf)) { Fail "$id produced no staged ROM at $asset" }
    $onDisk = (Get-FileHash -LiteralPath $staged -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($onDisk -ne $sha) { Fail "$id staged hash does not match the worker report" }

    $pinned = ([string]$source.romSha256).ToLowerInvariant()
    $assetPath = Join-Path $romDir $asset

    if ([string]::IsNullOrWhiteSpace($pinned)) {
        Copy-Item -LiteralPath $staged -Destination $assetPath -Force
        $source.romSha256 = $onDisk
        $changed += $id
        Write-Host ("    {0,-24} {1,8} bytes  sha256 {2}  (pinned now)" -f $asset, $size, $onDisk.Substring(0, 16)) -ForegroundColor Yellow
        continue
    }
    if ($onDisk -eq $pinned) {
        Copy-Item -LiteralPath $staged -Destination $assetPath -Force
        Write-Host ("    {0,-24} {1,8} bytes  sha256 {2}  verified" -f $asset, $size, $onDisk.Substring(0, 16))
        continue
    }
    if ($AcceptNewHash) {
        Copy-Item -LiteralPath $staged -Destination $assetPath -Force
        $source.romSha256 = $onDisk
        $changed += $id
        Write-Host ("    {0,-24} {1,8} bytes  sha256 {2}  (hash accepted)" -f $asset, $size, $onDisk.Substring(0, 16)) -ForegroundColor Yellow
        continue
    }
    if ($policy -eq 'artifact') {
        Write-Host ("    {0,-24} rebuild differs (upstream is not byte-reproducible): built {1}, shipped {2}" -f `
            $asset, $onDisk.Substring(0, 16), $pinned.Substring(0, 16)) -ForegroundColor Yellow
        Write-Host ("    {0,-24} the shipped asset stays pinned; no file was replaced" -f '')
        continue
    }
    Fail "$id ROM hash drift: lock $pinned, built $onDisk"
}

if ($changed.Count -gt 0) {
    if ([string]::IsNullOrWhiteSpace($Reason)) { $Reason = 'initial pin of a newly bundled game' }
    $stamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    $entry = [ordered]@{ acceptedAt = $stamp; reason = $Reason; games = $changed }
    if ($null -eq $lock.PSObject.Properties['acceptedHashChanges']) {
        $lock | Add-Member -NotePropertyName acceptedHashChanges -NotePropertyValue @()
    }
    $lock.acceptedHashChanges = @($lock.acceptedHashChanges) + $entry
    $lock | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $lockPath -Encoding utf8

    # The runtime manifest is the shipped pin; keep it in step with the lock so
    # the two can never disagree about what is bundled.
    $manifestPath = Join-Path $Root 'content\assets\builtin-games.json'
    $manifest = Get-Content -Raw -LiteralPath $manifestPath | ConvertFrom-Json
    foreach ($game in $manifest.games) {
        $record = $lock.sources | Where-Object { $_.canonicalId -eq $game.canonicalId } | Select-Object -First 1
        if ($null -ne $record) { $game.romSha256 = [string]$record.romSha256 }
    }
    $manifest | ConvertTo-Json -Depth 10 | Set-Content -LiteralPath $manifestPath -Encoding utf8

    Write-Host "    updated the lock and the manifest for: $($changed -join ', ')" -ForegroundColor Yellow
    Write-Host "    reason recorded: $Reason"
}

Write-Host ''
Write-Host "PASS built $($sources.Count) bundled ROM(s) from pinned sources" -ForegroundColor Green
exit 0
