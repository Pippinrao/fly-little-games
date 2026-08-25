[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$gatePath = Join-Path $repoRoot 'tools\quality\invoke_pester_gate.ps1'
$pwshExe = (Get-Process -Id $PID).Path

function Invoke-GateProcess {
    param([Parameter(Mandatory = $true)][string] $Path)

    $output = & $pwshExe -NoProfile -File $gatePath -Path $Path 2>&1 | Out-String
    return [pscustomobject]@{
        ExitCode = $LASTEXITCODE
        Output = $output
    }
}

Describe 'invoke_pester_gate.ps1' {
    BeforeEach {
        $fixtureRoot = Join-Path $TestDrive ([Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $fixtureRoot | Out-Null
    }

    It 'returns zero for an explicitly discovered passing test file' {
        $fixture = Join-Path $fixtureRoot 'Passing.Tests.ps1'
        @'
Describe 'passing subprocess fixture' {
    It 'passes' { 1 | Should Be 1 }
}
'@ | Set-Content -LiteralPath $fixture -Encoding utf8

        $result = Invoke-GateProcess -Path $fixture

        $result.ExitCode | Should Be 0
        $result.Output | Should Match 'Passed:\s+1'
    }

    It 'returns nonzero for a failing test file instead of trusting Invoke-Pester exit behavior' {
        $fixture = Join-Path $fixtureRoot 'Failing.Tests.ps1'
        @'
Describe 'failing subprocess fixture' {
    It 'fails' { 1 | Should Be 2 }
}
'@ | Set-Content -LiteralPath $fixture -Encoding utf8

        $result = Invoke-GateProcess -Path $fixture

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Failed:\s+1'
    }

    It 'returns nonzero when a directory contains no discovered tests' {
        'Describe ''ignored'' { It ''would fail'' { 1 | Should Be 2 } }' |
            Set-Content -LiteralPath (Join-Path $fixtureRoot 'Ignored.ps1') -Encoding utf8

        $result = Invoke-GateProcess -Path $fixtureRoot

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'No \*\.Tests\.ps1 files were discovered'
    }

    It 'recursively discovers only files ending in .Tests.ps1' {
        $nested = Join-Path $fixtureRoot 'nested\deeper'
        New-Item -ItemType Directory -Path $nested | Out-Null
        @'
Describe 'recursive passing fixture' {
    It 'passes' { 'ok' | Should Be 'ok' }
}
'@ | Set-Content -LiteralPath (Join-Path $nested 'Recursive.Tests.ps1') -Encoding utf8
        'throw ''a non-test file must never be invoked''' |
            Set-Content -LiteralPath (Join-Path $fixtureRoot 'NotATest.ps1') -Encoding utf8

        $result = Invoke-GateProcess -Path $fixtureRoot

        $result.ExitCode | Should Be 0
        $result.Output | Should Match 'Passed:\s+1'
    }

    It 'rejects an explicit file that does not end in .Tests.ps1' {
        $fixture = Join-Path $fixtureRoot 'NotATest.ps1'
        'Describe ''ignored'' { It ''passes'' { 1 | Should Be 1 } }' |
            Set-Content -LiteralPath $fixture -Encoding utf8

        $result = Invoke-GateProcess -Path $fixture

        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'Only \*\.Tests\.ps1 files are accepted'
    }

    It 'rejects a different Pester version already loaded in the subprocess' {
        $passing = Join-Path $fixtureRoot 'Passing.Tests.ps1'
        'Describe ''passing'' { It ''passes'' { 1 | Should Be 1 } }' |
            Set-Content -LiteralPath $passing -Encoding utf8
        $fakeModule = Join-Path $fixtureRoot 'Pester.psm1'
        'function Invoke-Pester { throw ''fake Invoke-Pester must not run'' }' |
            Set-Content -LiteralPath $fakeModule -Encoding utf8

        $escapedModule = $fakeModule.Replace("'", "''")
        $escapedGate = $gatePath.Replace("'", "''")
        $escapedPassing = $passing.Replace("'", "''")
        $command = "Import-Module -Name '$escapedModule' -Force; & '$escapedGate' -Path '$escapedPassing'; exit `$LASTEXITCODE"
        $output = & $pwshExe -NoProfile -Command $command 2>&1 | Out-String
        $exitCode = $LASTEXITCODE

        $exitCode | Should Not Be 0
        $output | Should Match 'Refusing to run with loaded Pester version'
    }

    It 'pins the import to RequiredVersion 3.4.0' {
        $source = Get-Content -LiteralPath $gatePath -Raw

        $source | Should Match 'Import-Module\s+Pester\s+-RequiredVersion\s+3\.4\.0'
    }
}
