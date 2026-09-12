param(
    [string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'),
    [switch]$Stage,
    [switch]$UseCommitGuard
)
$ErrorActionPreference = 'Stop'
. (Join-Path $PSScriptRoot 'Versioning.ps1')
$root = Resolve-FlyNesRepositoryRoot $RepositoryRoot
$version = Get-FlyNesVersion $root
Assert-FlyNesMajor -RepositoryRoot $root -Version $version

$head = ''
$guardPath = ''
if ($UseCommitGuard) {
    $head = (& git -C $root rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw 'Unable to read Git HEAD for commit guard.' }
    $guardPath = (& git -C $root rev-parse --path-format=absolute --git-path flynes-precommit-version-state).Trim()
    if (Test-Path -LiteralPath $guardPath) {
        $guard = (Get-Content -Raw -LiteralPath $guardPath).Trim()
        if ($guard -eq "$head|$($version.Text)") {
            Sync-FlyNesVersion -RepositoryRoot $root | Out-Null
            if ($Stage) { & git -C $root add -- VERSION VERSION_MAJOR app/build.gradle harmony/AppScope/app.json5 }
            Write-Output "Patch already bumped for HEAD ${head}: $($version.Text)"
            exit 0
        }
    }
}

if ($version.Patch -ge 999) { throw 'PATCH reached 999; create a new versioned worktree/minor line.' }
$next = "$($version.Major).$($version.Minor).$($version.Patch + 1)"
Write-Utf8NoBom -Path (Join-Path $root 'VERSION') -Text "$next`n"
$synced = Sync-FlyNesVersion -RepositoryRoot $root
if ($Stage) {
    & git -C $root add -- VERSION VERSION_MAJOR app/build.gradle harmony/AppScope/app.json5
    if ($LASTEXITCODE -ne 0) { throw 'Unable to stage synchronized version files.' }
}
if ($UseCommitGuard) {
    Write-Utf8NoBom -Path $guardPath -Text "$head|$($synced.Text)`n"
}
Write-Output "FlyNES patch version: $($synced.Text)"
