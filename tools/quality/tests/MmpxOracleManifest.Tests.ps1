[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$generatorPath = Join-Path $repoRoot 'tools\quality\generate_mmpx_oracle_manifest.ps1'
$committedPath = Join-Path $repoRoot 'docs\acceptance\display-quality\mmpx-oracle-manifest.json'
$fixtureRoot = Join-Path $repoRoot 'app\src\test\resources\spatial-golden\oracle'

Describe 'Frozen MMPX oracle manifest' {
    It 'reproduces the committed manifest byte for byte' {
        $temporaryPath = Join-Path $TestDrive 'mmpx-oracle-manifest.json'
        & $generatorPath -RepoRoot $repoRoot -OutputPath $temporaryPath

        (Get-Content -LiteralPath $temporaryPath -Raw) |
            Should Be (Get-Content -LiteralPath $committedPath -Raw)
    }

    It 'accounts for every committed oracle fixture exactly once' {
        $manifest = Get-Content -LiteralPath $committedPath -Raw | ConvertFrom-Json
        $manifestPaths = @($manifest.files | ForEach-Object { $_.path })
        $fixturePaths = @(Get-ChildItem -LiteralPath $fixtureRoot -File | ForEach-Object {
            $_.FullName.Substring($repoRoot.Length + 1).Replace('\', '/')
        })

        @($fixturePaths | Where-Object { $_ -notin $manifestPaths }).Count | Should Be 0
        @($manifestPaths | Where-Object { $_ -like '*/spatial-golden/oracle/*' }).Count |
            Should Be $fixturePaths.Count
    }

    It 'contains only the pinned oracle, notice, and synthetic fixtures' {
        $manifest = Get-Content -LiteralPath $committedPath -Raw | ConvertFrom-Json
        $allowed = @(
            'app/src/test/java/com/flynes/emu/video/reference/MmpxCpuOracle.java',
            'app/src/test/java/com/flynes/emu/video/reference/MMPX_SOURCE_NOTICE.md'
        ) + @(Get-ChildItem -LiteralPath $fixtureRoot -Filter '*.fixture' -File |
            ForEach-Object { $_.FullName.Substring($repoRoot.Length + 1).Replace('\', '/') })

        @($manifest.files.path | Sort-Object) | Should Be @($allowed | Sort-Object)
    }
}
