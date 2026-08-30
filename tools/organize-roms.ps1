[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)] [string] $SourceRoot,
    [Parameter(Mandatory = $true)] [string] $AuditPayloadsCsv,
    [Parameter(Mandatory = $true)] [string] $OutputRoot,
    [switch] $Replace
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Sha256Bytes {
    param([byte[]] $Bytes)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $algorithm.Dispose() }
}

function Read-AllBytes {
    param([System.IO.Stream] $Stream)
    $memory = [System.IO.MemoryStream]::new()
    try { $Stream.CopyTo($memory); return $memory.ToArray() }
    finally { $memory.Dispose() }
}

function Find-ZipEntryOrNull {
    param([System.IO.Compression.ZipArchive] $Archive, [string] $Name)
    foreach ($entry in $Archive.Entries) {
        if ([string]::Equals($entry.FullName, $Name, [StringComparison]::Ordinal)) { return $entry }
    }
    return $null
}

function Find-ZipEntry {
    param([System.IO.Compression.ZipArchive] $Archive, [string] $Name)
    $entry = Find-ZipEntryOrNull $Archive $Name
    if ($null -ne $entry) { return $entry }
    throw "audit locator entry not found: $Name"
}

function Read-EntryBytes {
    param([System.IO.Compression.ZipArchiveEntry] $Entry)
    $stream = $Entry.Open()
    try { return Read-AllBytes $stream }
    finally { $stream.Dispose() }
}

function Read-AuditedPayload {
    param([string] $SourcePath, [string] $Locator)
    if ($Locator -eq '@raw') { return [System.IO.File]::ReadAllBytes($SourcePath) }

    $archive = [System.IO.Compression.ZipFile]::OpenRead($SourcePath)
    try {
        $direct = Find-ZipEntryOrNull $archive $Locator
        if ($null -ne $direct) { return Read-EntryBytes $direct }
        $parts = $Locator -split '!/', 2
        if ($parts.Count -ne 2) { throw "audit locator entry not found: $Locator" }
        $first = Find-ZipEntry $archive $parts[0]
        $nestedBytes = Read-EntryBytes $first
    }
    finally { $archive.Dispose() }

    $memory = [System.IO.MemoryStream]::new($nestedBytes, $false)
    try {
        $nested = [System.IO.Compression.ZipArchive]::new(
            $memory, [System.IO.Compression.ZipArchiveMode]::Read, $false)
        try { return Read-EntryBytes (Find-ZipEntry $nested $parts[1]) }
        finally { $nested.Dispose() }
    }
    finally { $memory.Dispose() }
}

function Get-TitleStem {
    param([string] $Value)
    if ([string]::IsNullOrWhiteSpace($Value) -or $Value -eq '@raw') { return '' }
    $normalized = $Value.Replace('\', '/')
    if ($normalized.Contains('!/')) { $normalized = ($normalized -split '!/')[-1] }
    $leaf = $normalized.Substring($normalized.LastIndexOf('/') + 1)
    $stem = [System.IO.Path]::GetFileNameWithoutExtension($leaf)
    return ($stem -replace '_', ' ').Trim()
}

function Test-ContainsHan {
    param([string] $Value)
    foreach ($character in $Value.ToCharArray()) {
        $code = [int]$character
        if (($code -ge 0x3400 -and $code -le 0x4dbf) -or
            ($code -ge 0x4e00 -and $code -le 0x9fff) -or
            ($code -ge 0xf900 -and $code -le 0xfaff)) { return $true }
    }
    return $false
}

function Test-ContainsLatin {
    param([string] $Value)
    return $Value -match '[A-Za-z]'
}

function Get-PreferredTitle {
    param([object[]] $Records, [ValidateSet('ZH','EN')] [string] $Language)
    $candidates = New-Object System.Collections.Generic.List[string]
    foreach ($record in $Records) {
        foreach ($value in @((Get-TitleStem $record.sourceRelativePath), (Get-TitleStem $record.locator))) {
            if ([string]::IsNullOrWhiteSpace($value)) { continue }
            $matches = if ($Language -eq 'ZH') {
                Test-ContainsHan $value
            } else {
                (Test-ContainsLatin $value) -and -not (Test-ContainsHan $value)
            }
            if ($matches -and -not $candidates.Contains($value)) { $candidates.Add($value) }
        }
    }
    return @($candidates | Sort-Object Length, { $_ }) | Select-Object -First 1
}

function Get-SafeStem {
    param([string] $Value)
    $invalid = [System.IO.Path]::GetInvalidFileNameChars()
    $builder = [System.Text.StringBuilder]::new()
    foreach ($character in $Value.ToCharArray()) {
        if ($invalid -contains $character -or [char]::IsControl($character)) { [void]$builder.Append(' ') }
        else { [void]$builder.Append($character) }
    }
    $result = (($builder.ToString() -replace '\s+', ' ').Trim()).TrimEnd('.', ' ')
    if ($result.Length -gt 96) { $result = $result.Substring(0, 96).TrimEnd('.', ' ') }
    return $result
}

function Write-PayloadZip {
    param([string] $Path, [string] $EntryName, [byte[]] $Bytes)
    $stream = [System.IO.File]::Create($Path)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new(
            $stream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            $entry = $archive.CreateEntry(
                $EntryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $target = $entry.Open()
            try { $target.Write($Bytes, 0, $Bytes.Length) }
            finally { $target.Dispose() }
        }
        finally { $archive.Dispose() }
    }
    finally { $stream.Dispose() }
}

$resolvedSource = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedAudit = (Resolve-Path -LiteralPath $AuditPayloadsCsv).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputRoot)
$outputParent = [System.IO.Path]::GetDirectoryName($resolvedOutput)
if ([string]::IsNullOrWhiteSpace($outputParent) -or
    $resolvedOutput -eq [System.IO.Path]::GetPathRoot($resolvedOutput)) {
    throw 'OutputRoot must be a specific directory, not a drive root.'
}
if ($resolvedOutput.StartsWith($resolvedSource + [System.IO.Path]::DirectorySeparatorChar,
        [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must not be inside SourceRoot.'
}
if (Test-Path -LiteralPath $resolvedOutput) {
    if (-not $Replace) { throw 'OutputRoot already exists; pass -Replace to replace the organized copy.' }
    $resolvedExisting = (Resolve-Path -LiteralPath $resolvedOutput).Path
    if (-not [string]::Equals($resolvedExisting, $resolvedOutput,
            [StringComparison]::OrdinalIgnoreCase)) { throw 'OutputRoot resolution mismatch.' }
    Remove-Item -LiteralPath $resolvedExisting -Recurse -Force
}
New-Item -ItemType Directory -Force -Path $outputParent | Out-Null
$staging = Join-Path $outputParent (
    ([System.IO.Path]::GetFileName($resolvedOutput)) + '.staging-' + [Guid]::NewGuid().ToString('N'))

try {
    foreach ($bucket in @('NES','FDS','UNF')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $staging $bucket) | Out-Null
    }
    $records = @(Import-Csv -LiteralPath $resolvedAudit | Where-Object {
        $_.status -eq 'accounted' -and $_.format -in @('NES','FDS','UNF') -and
        $_.sha256 -match '^[0-9a-fA-F]{64}$'
    })
    $groups = @($records | Group-Object { $_.sha256.ToLowerInvariant() } | Sort-Object Name)
    $manifest = New-Object System.Collections.Generic.List[object]
    $usedNames = [System.Collections.Generic.HashSet[string]]::new(
        [StringComparer]::OrdinalIgnoreCase)

    foreach ($group in $groups) {
        $hash = $group.Name
        $variants = @($group.Group)
        $extract = $variants | Sort-Object `
            @{Expression={ if ($_.locator -eq '@raw') { 0 } elseif ($_.locator -like '*!/*') { 2 } else { 1 } }}, `
            @{Expression={ $_.sourceRelativePath.Length }}, sourceRelativePath | Select-Object -First 1
        $sourcePath = [System.IO.Path]::GetFullPath((Join-Path $resolvedSource $extract.sourceRelativePath))
        if (-not $sourcePath.StartsWith($resolvedSource + [System.IO.Path]::DirectorySeparatorChar,
                [StringComparison]::OrdinalIgnoreCase)) { throw 'Audit path escaped SourceRoot.' }
        $bytes = Read-AuditedPayload $sourcePath $extract.locator
        $actualHash = Get-Sha256Bytes $bytes
        if ($actualHash -ne $hash) { throw "payload hash mismatch for $($extract.sourceRelativePath)" }

        $titleZh = Get-PreferredTitle $variants 'ZH'
        $titleEn = Get-PreferredTitle $variants 'EN'
        $baseName = Get-SafeStem $(if ($titleZh) { $titleZh } elseif ($titleEn) { $titleEn } else { $hash.Substring(0, 12) })
        if ([string]::IsNullOrWhiteSpace($baseName)) { $baseName = $hash.Substring(0, 12) }
        $uniqueName = $baseName
        if (-not $usedNames.Add($uniqueName)) {
            $uniqueName = Get-SafeStem ($baseName + ' [' + $hash.Substring(0, 8) + ']')
            [void]$usedNames.Add($uniqueName)
        }
        $format = $extract.format.ToUpperInvariant()
        $extension = switch ($format) { 'NES' { '.nes' } 'FDS' { '.fds' } default { '.unf' } }
        $entryStem = Get-SafeStem $(if ($titleEn) { $titleEn } elseif ($titleZh) { $titleZh } else { $hash.Substring(0, 12) })
        $outputRelative = $format + '/' + $uniqueName + '.zip'
        Write-PayloadZip (Join-Path $staging $outputRelative.Replace('/', '\')) ($entryStem + $extension) $bytes
        $manifest.Add([pscustomobject][ordered]@{
            format = $format
            sha256 = $hash
            titleZhHans = [string]$titleZh
            titleEn = [string]$titleEn
            outputRelativePath = $outputRelative
            sourceRelativePath = $extract.sourceRelativePath
            locator = $extract.locator
            duplicateSourceCount = $variants.Count
            bytes = $bytes.Length
        })
    }

    $manifest | Export-Csv -LiteralPath (Join-Path $staging 'manifest.csv') -NoTypeInformation -Encoding utf8
    $summary = [ordered]@{
        schemaVersion = 1
        generatedAtUtc = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
        sourceTopLevelFileCount = @(Get-ChildItem -LiteralPath $resolvedSource -File -Force).Count
        auditedAccountedRecordCount = $records.Count
        organizedUniquePayloadCount = $manifest.Count
        duplicateRecordsRemoved = $records.Count - $manifest.Count
        formats = [ordered]@{}
        sourceModified = $false
        absolutePathsIncluded = $false
    }
    foreach ($bucket in @('NES','FDS','UNF')) {
        $summary.formats[$bucket] = @($manifest | Where-Object format -eq $bucket).Count
    }
    $summary | ConvertTo-Json -Depth 4 | Set-Content -LiteralPath (Join-Path $staging 'summary.json') -Encoding utf8
    Move-Item -LiteralPath $staging -Destination $resolvedOutput
    $summary | ConvertTo-Json -Depth 4
}
catch {
    if (Test-Path -LiteralPath $staging) { Remove-Item -LiteralPath $staging -Recurse -Force }
    throw
}
