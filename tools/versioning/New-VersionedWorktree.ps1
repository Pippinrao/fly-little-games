param(
    [Parameter(Mandatory)][string]$Path,
    [Parameter(Mandatory)][ValidatePattern('^codex/')][string]$Branch,
    [string]$StartPoint = 'HEAD',
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..')
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Versioning.ps1')
$root = Resolve-FlyNesRepositoryRoot $RepositoryRoot
$baseVersion = Get-FlyNesVersion $root
Assert-FlyNesMajor -RepositoryRoot $root -Version $baseVersion
$commonDir = (& git -C $root rev-parse --path-format=absolute --git-common-dir).Trim()
if ($LASTEXITCODE -ne 0) { throw 'Unable to locate the shared Git directory.' }
$lockPath = Join-Path $commonDir 'flynes-worktree-version.lock'
$statePath = Join-Path $commonDir 'flynes-worktree-version.state'
$lock = $null
$deadline = [DateTime]::UtcNow.AddSeconds(15)
while ($null -eq $lock -and [DateTime]::UtcNow -lt $deadline) {
    try {
        $lock = [IO.File]::Open($lockPath, [IO.FileMode]::OpenOrCreate,
            [IO.FileAccess]::ReadWrite, [IO.FileShare]::None)
    } catch [IO.IOException] {
        Start-Sleep -Milliseconds 100
    }
}
if ($null -eq $lock) { throw 'Timed out waiting for the worktree version allocation lock.' }

try {
    $maxMinor = $baseVersion.Minor
    if (Test-Path -LiteralPath $statePath) {
        $state = (Get-Content -Raw -LiteralPath $statePath).Trim()
        if ($state -match '^(\d+)\.(\d+)$' -and [int]$Matches[1] -eq $baseVersion.Major) {
            $maxMinor = [Math]::Max($maxMinor, [int]$Matches[2])
        }
    }
    $lines = @(& git -C $root worktree list --porcelain)
    foreach ($line in $lines) {
        if ($line -notmatch '^worktree (.+)$') { continue }
        $candidate = Join-Path $Matches[1] 'VERSION'
        if (-not (Test-Path -LiteralPath $candidate)) { continue }
        $value = (Get-Content -Raw -LiteralPath $candidate).Trim()
        if ($value -match '^(\d+)\.(\d+)\.(\d+)$' -and [int]$Matches[1] -eq $baseVersion.Major) {
            $maxMinor = [Math]::Max($maxMinor, [int]$Matches[2])
        }
    }
    if ($maxMinor -ge 999) { throw 'MINOR reached 999; only the user may authorize a new major.' }
    $nextMinor = $maxMinor + 1
    Write-Utf8NoBom -Path $statePath -Text "$($baseVersion.Major).$nextMinor`n"
} finally {
    $lock.Dispose()
}

& git -C $root worktree add -b $Branch $Path $StartPoint
if ($LASTEXITCODE -ne 0) { throw 'git worktree add failed; the allocated minor remains reserved.' }
$newRoot = (Resolve-Path -LiteralPath $Path).Path
Write-Utf8NoBom -Path (Join-Path $newRoot 'VERSION') -Text "$($baseVersion.Major).$nextMinor.0`n"
Write-Utf8NoBom -Path (Join-Path $newRoot 'VERSION_MAJOR') -Text "$($baseVersion.Major)`n"
Sync-FlyNesVersion -RepositoryRoot $newRoot | Out-Null
Write-Output "Created $Branch at $newRoot with version $($baseVersion.Major).$nextMinor.0"
