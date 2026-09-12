param([string]$RepositoryRoot = (Join-Path $PSScriptRoot '..\..'))
$ErrorActionPreference = 'Stop'
$root = (Resolve-Path -LiteralPath $RepositoryRoot).Path
& git -C $root config core.hooksPath .githooks
if ($LASTEXITCODE -ne 0) { throw 'Unable to configure core.hooksPath.' }
Write-Output 'Git hooks enabled: .githooks'
