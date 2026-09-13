<#
.SYNOPSIS
    Stage the shared bundled content into each platform's resource directory.

.DESCRIPTION
    content/assets is the single source of truth. Platforms that cannot merge an
    extra resource directory at build time (HarmonyOS rawfile) get generated
    copies here; the Android assets are merged directly by Gradle and iOS globs
    the content directory from CMake, so neither needs staging.

    Generated files are git-ignored: this script must be run before a HarmonyOS
    build, and verify-builtin-content.ps1 fails if a tracked copy ever appears.
#>
[CmdletBinding()]
param(
    [string] $Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path
)

$ErrorActionPreference = 'Stop'
function Fail { param([string] $Message) [Console]::Error.WriteLine("SYNC_FAIL $Message"); exit 1 }

$content = Join-Path $Root 'content\assets'
$manifest = Join-Path $content 'builtin-games.json'
if (-not (Test-Path -LiteralPath $manifest -PathType Leaf)) { Fail 'content/assets/builtin-games.json is missing' }

$targets = @(
    (Join-Path $Root 'harmony\entry\src\main\resources\rawfile')
)

$staged = 0
foreach ($target in $targets) {
    if (-not (Test-Path -LiteralPath $target -PathType Container)) {
        New-Item -ItemType Directory -Force -Path $target | Out-Null
    }
    Copy-Item -LiteralPath $manifest -Destination $target -Force
    $staged++
    foreach ($folder in @('roms', 'licenses')) {
        $source = Join-Path $content $folder
        if (-not (Test-Path -LiteralPath $source -PathType Container)) { continue }
        foreach ($file in Get-ChildItem -LiteralPath $source -File) {
            Copy-Item -LiteralPath $file.FullName -Destination $target -Force
            $staged++
        }
    }
    Write-Host "staged $staged file(s) into $($target.Substring($Root.Length + 1))"
}

Write-Host ''
Write-Host "PASS staged bundled content ($staged file(s))" -ForegroundColor Green
exit 0
