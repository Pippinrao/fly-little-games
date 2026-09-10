$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '..\..\..')).Path
$syncScript = Join-Path $repoRoot 'tools\versioning\Sync-Version.ps1'
$bumpScript = Join-Path $repoRoot 'tools\versioning\Bump-Patch.ps1'
$worktreeScript = Join-Path $repoRoot 'tools\versioning\New-VersionedWorktree.ps1'

function New-VersionFixture {
    $root = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'app') | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $root 'harmony\AppScope') | Out-Null
    "1.2.3`n" | Set-Content -NoNewline -Encoding utf8 (Join-Path $root 'VERSION')
    "1`n" | Set-Content -NoNewline -Encoding utf8 (Join-Path $root 'VERSION_MAJOR')
    @'
android {
    defaultConfig {
        versionCode 1
        versionName "0.1.0"
    }
}
'@ | Set-Content -Encoding utf8 (Join-Path $root 'app\build.gradle')
    @'
{
  "app": {
    "versionCode": 1,
    "versionName": "0.1.0"
  }
}
'@ | Set-Content -Encoding utf8 (Join-Path $root 'harmony\AppScope\app.json5')
    return $root
}

Describe 'FlyNES repository versioning' {
    It 'synchronizes one semantic version and collision-free numeric code to both apps' {
        $fixture = New-VersionFixture
        & $syncScript -RepositoryRoot $fixture
        $android = Get-Content -Raw (Join-Path $fixture 'app\build.gradle')
        $harmony = Get-Content -Raw (Join-Path $fixture 'harmony\AppScope\app.json5')
        $android | Should Match 'versionCode 1002003'
        $android | Should Match 'versionName "1\.2\.3"'
        $harmony | Should Match '"versionCode": 1002003'
        $harmony | Should Match '"versionName": "1\.2\.3"'
    }

    It 'increments only the patch component' {
        $fixture = New-VersionFixture
        & $bumpScript -RepositoryRoot $fixture
        (Get-Content -Raw (Join-Path $fixture 'VERSION')).Trim() | Should Be '1.2.4'
    }

    It 'rejects an unauthorized major change' {
        $fixture = New-VersionFixture
        "2.2.3`n" | Set-Content -NoNewline -Encoding utf8 (Join-Path $fixture 'VERSION')
        $shell = (Get-Process -Id $PID).Path
        & $shell -NoProfile -ExecutionPolicy Bypass -File $syncScript -RepositoryRoot $fixture 2>&1 | Out-Null
        $LASTEXITCODE | Should Not Be 0
    }

    It 'provides a serialized worktree allocator and commit hook entrypoint' {
        (Test-Path $worktreeScript) | Should Be $true
        $worktreeSource = Get-Content -Raw $worktreeScript
        $worktreeSource | Should Match 'FileShare\]::None'
        $worktreeSource | Should Match 'git worktree add'
        (Test-Path (Join-Path $repoRoot '.githooks\pre-commit')) | Should Be $true
    }
}
