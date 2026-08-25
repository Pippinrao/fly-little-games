[CmdletBinding()]
param(
    [Parameter(Mandatory = $true, Position = 0)]
    [ValidateNotNullOrEmpty()]
    [string[]] $Path
)

$ErrorActionPreference = 'Stop'
$requiredPesterVersion = [Version]'3.4.0'

function Stop-Gate {
    param(
        [Parameter(Mandatory = $true)][string] $Message,
        [Parameter(Mandatory = $true)][int] $ExitCode
    )

    [Console]::Error.WriteLine($Message)
    exit $ExitCode
}

$loadedPester = @(Get-Module -Name Pester)
$wrongLoadedPester = @($loadedPester | Where-Object { $_.Version -ne $requiredPesterVersion })
if ($wrongLoadedPester.Count -gt 0) {
    $versions = ($wrongLoadedPester.Version | ForEach-Object { $_.ToString() }) -join ', '
    Stop-Gate -Message "Refusing to run with loaded Pester version(s): $versions; required: 3.4.0." -ExitCode 10
}

try {
    Import-Module Pester -RequiredVersion 3.4.0 -ErrorAction Stop
}
catch {
    Stop-Gate -Message "Unable to import required Pester 3.4.0: $($_.Exception.Message)" -ExitCode 11
}

$activePester = @(Get-Module -Name Pester)
if ($activePester.Count -eq 0 -or
        @($activePester | Where-Object { $_.Version -ne $requiredPesterVersion }).Count -gt 0) {
    Stop-Gate -Message 'Pester 3.4.0 was not the only active Pester version after import.' -ExitCode 11
}

$resolvedFiles = [System.Collections.Generic.List[string]]::new()
foreach ($inputPath in $Path) {
    if (-not (Test-Path -LiteralPath $inputPath)) {
        Stop-Gate -Message "Pester gate path does not exist: $inputPath" -ExitCode 12
    }

    $item = Get-Item -LiteralPath $inputPath
    if ($item.PSIsContainer) {
        Get-ChildItem -LiteralPath $item.FullName -Recurse -File -Filter '*.Tests.ps1' |
            ForEach-Object { $resolvedFiles.Add($_.FullName) }
        continue
    }

    if ($item.Name -notlike '*.Tests.ps1') {
        Stop-Gate -Message "Only *.Tests.ps1 files are accepted: $($item.FullName)" -ExitCode 12
    }
    $resolvedFiles.Add($item.FullName)
}

$testFiles = @($resolvedFiles | Sort-Object -Unique)
if ($testFiles.Count -eq 0) {
    Stop-Gate -Message 'No *.Tests.ps1 files were discovered.' -ExitCode 13
}

try {
    $result = Invoke-Pester -Script $testFiles -PassThru
}
catch {
    Stop-Gate -Message "Invoke-Pester terminated unexpectedly: $($_.Exception.Message)" -ExitCode 14
}

if ($null -eq $result -or [int]$result.TotalCount -eq 0) {
    Stop-Gate -Message 'Pester discovery returned zero tests.' -ExitCode 13
}
if ([int]$result.FailedCount -ne 0) {
    exit 1
}

exit 0
