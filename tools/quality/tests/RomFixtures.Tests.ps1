[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$schemaPath = Join-Path $repoRoot 'tools\quality\schemas\rom-fixtures.schema.json'
$examplePath = Join-Path $repoRoot 'tools\quality\examples\rom-fixtures.example.json'
$redactedPath = Join-Path $repoRoot 'docs\acceptance\display-quality\rom-fixtures.redacted.json'
$localPath = Join-Path $repoRoot 'local-data\display-quality\rom-fixtures.json'

function New-ManifestWithLocalSource {
    param(
        [Parameter(Mandatory = $true)][ValidateSet('local', 'redacted', 'example')]
        [string] $ManifestKind,
        [Parameter(Mandatory = $true)][string] $PackagePath,
        [Parameter(Mandatory = $true)][string] $EntryLeaf
    )

    $manifest = Get-Content -LiteralPath $examplePath -Raw | ConvertFrom-Json
    $manifest.manifestKind = $ManifestKind
    $manifest.fixtures[0] | Add-Member -NotePropertyName localSources -NotePropertyValue @(
        [pscustomobject]@{
            packagePath = $PackagePath
            entryLeaf = $EntryLeaf
            expectedSha256 = $manifest.fixtures[0].variantSha256
        })
    return ($manifest | ConvertTo-Json -Depth 10)
}

function Test-FixtureSchema {
    param([Parameter(Mandatory = $true)][string] $Json)

    try {
        return ($Json | Test-Json -SchemaFile $schemaPath -ErrorAction Stop)
    }
    catch {
        if ($_.Exception.Message -match 'JSON is not valid with the schema') {
            return $false
        }
        throw
    }
}

Describe 'ROM fixture manifest disclosure contract' {
    $pathLeakCases = foreach ($manifestKind in @('redacted', 'example')) {
        @(
            @{ manifestKind = $manifestKind; label = 'absolute packagePath'
                packagePath = 'X:\private\game.zip'; entryLeaf = 'game.nes' },
            @{ manifestKind = $manifestKind; label = 'relative packagePath'
                packagePath = '..\private\game.zip'; entryLeaf = 'game.nes' },
            @{ manifestKind = $manifestKind; label = 'absolute entryLeaf'
                packagePath = 'fixture.zip'; entryLeaf = 'X:\private\game.nes' },
            @{ manifestKind = $manifestKind; label = 'relative entryLeaf'
                packagePath = 'fixture.zip'; entryLeaf = '..\private\game.nes' }
        )
    }
    It 'rejects <label> from manifestKind=<manifestKind>' -TestCases $pathLeakCases {
        param($manifestKind, $packagePath, $entryLeaf)
        $json = New-ManifestWithLocalSource -ManifestKind $manifestKind `
            -PackagePath $packagePath -EntryLeaf $entryLeaf

        Test-FixtureSchema -Json $json | Should Be $false
    }

    It 'requires every local fixture to declare localSources' {
        $manifest = Get-Content -LiteralPath $examplePath -Raw | ConvertFrom-Json
        $manifest.manifestKind = 'local'

        Test-FixtureSchema -Json ($manifest | ConvertTo-Json -Depth 10) | Should Be $false
    }

    It 'accepts localSources only for a local manifest' {
        $json = New-ManifestWithLocalSource -ManifestKind local `
            -PackagePath 'X:\private\game.zip' -EntryLeaf 'game.nes'

        Test-FixtureSchema -Json $json | Should Be $true
    }

    It 'keeps committed redacted and example manifests schema-valid' {
        foreach ($path in @($redactedPath, $examplePath)) {
            Test-FixtureSchema -Json (Get-Content -LiteralPath $path -Raw) | Should Be $true
        }
    }

    It 'keeps the ignored local manifest schema-valid when it is present' {
        if (Test-Path -LiteralPath $localPath -PathType Leaf) {
            Test-FixtureSchema -Json (Get-Content -LiteralPath $localPath -Raw) |
                Should Be $true
        }
    }

    It 'does not repeat a selected variant in its alternatives' {
        $paths = @($redactedPath, $examplePath)
        if (Test-Path -LiteralPath $localPath -PathType Leaf) { $paths += $localPath }
        foreach ($path in $paths) {
            $manifest = Get-Content -LiteralPath $path -Raw | ConvertFrom-Json
            foreach ($fixture in $manifest.fixtures) {
                @($fixture.alternativeVariantSha256) -contains $fixture.variantSha256 |
                    Should Be $false
            }
        }
    }
}
