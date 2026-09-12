param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
    [switch]$AllowMajorChange
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Versioning.ps1')
$root = Resolve-FlyNesRepositoryRoot $RepositoryRoot
$version = Sync-FlyNesVersion -RepositoryRoot $root -AllowMajorChange:$AllowMajorChange
Write-Output "FlyNES version synchronized: $($version.Text)"
