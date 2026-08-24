[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [string] $SourceRoot,

    [string] $OutputRoot = (Join-Path $PSScriptRoot '..\local-data\roms\audit'),

    [long] $MaxPayloadBytes = 67108864
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function Get-Sha256Bytes {
    param([byte[]] $Bytes)
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        return ([BitConverter]::ToString($algorithm.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Read-BoundedBytes {
    param(
        [System.IO.Stream] $Stream,
        [long] $Limit
    )
    $buffer = New-Object byte[] 65536
    $memory = [System.IO.MemoryStream]::new()
    try {
        while (($count = $Stream.Read($buffer, 0, $buffer.Length)) -gt 0) {
            if (($memory.Length + $count) -gt $Limit) {
                throw [System.IO.InvalidDataException]::new('payload_limit_exceeded')
            }
            $memory.Write($buffer, 0, $count)
        }
        return $memory.ToArray()
    }
    finally {
        $memory.Dispose()
    }
}

function Test-Prefix {
    param([byte[]] $Bytes, [int] $Offset, [byte[]] $Prefix)
    if ($Offset -lt 0 -or ($Bytes.Length - $Offset) -lt $Prefix.Length) { return $false }
    for ($index = 0; $index -lt $Prefix.Length; $index++) {
        if ($Bytes[$Offset + $index] -ne $Prefix[$index]) { return $false }
    }
    return $true
}

function Get-PayloadKind {
    param([byte[]] $Bytes)
    if (Test-Prefix $Bytes 0 ([byte[]](0x4e, 0x45, 0x53, 0x1a))) { return 'NES' }
    if (Test-Prefix $Bytes 0 ([byte[]](0x46, 0x44, 0x53, 0x1a))) { return 'FDS' }
    if (Test-Prefix $Bytes 0 ([byte[]](0x01, 0x2a, 0x4e, 0x49, 0x4e, 0x54, 0x45, 0x4e, 0x44, 0x4f, 0x2d, 0x48, 0x56, 0x43, 0x2a))) { return 'FDS' }
    if (Test-Prefix $Bytes 0 ([byte[]](0x55, 0x4e, 0x49, 0x46))) { return 'UNF' }
    if (Test-Prefix $Bytes 0 ([byte[]](0x50, 0x4b, 0x03, 0x04))) { return 'ZIP' }
    if (Test-Prefix $Bytes 0 ([byte[]](0x4d, 0x5a))) { return 'EXE' }
    $gbLogo = [byte[]](0xce,0xed,0x66,0x66,0xcc,0x0d,0x00,0x0b,0x03,0x73,0x00,0x83,0x00,0x0c,0x00,0x0d,0x00,0x08,0x11,0x1f,0x88,0x89,0x00,0x0e,0xdc,0xcc,0x6e,0xe6,0xdd,0xdd,0xd9,0x99,0xbb,0xbb,0x67,0x63,0x6e,0x0e,0xec,0xcc,0xdd,0xdc,0x99,0x9f,0xbb,0xb9,0x33,0x3e)
    if (Test-Prefix $Bytes 0x104 $gbLogo) { return 'GB' }
    return 'UNKNOWN'
}

function Get-PayloadDecision {
    param([byte[]] $Bytes, [string] $Kind)
    if ($Kind -eq 'NES') {
        if ($Bytes.Length -lt 16) { return @{ Status = 'error'; Bucket = 'NeedsReview'; Note = 'nes_header_invalid' } }
        $trainer = (($Bytes[6] -band 0x04) -ne 0)
        $expected = 16L + $(if ($trainer) { 512L } else { 0L }) + ([long]$Bytes[4] * 16384L) + ([long]$Bytes[5] * 8192L)
        if ($Bytes[4] -eq 0) { return @{ Status = 'error'; Bucket = 'NeedsReview'; Note = 'nes_zero_prg' } }
        if ($Bytes.Length -lt $expected) { return @{ Status = 'error'; Bucket = 'NeedsReview'; Note = 'nes_truncated' } }
        $notes = New-Object System.Collections.Generic.List[string]
        if ($Bytes.Length -gt $expected) { $notes.Add('trailing_data') }
        if (($Bytes[7] -band 0x0c) -ne 0x08) {
            for ($index = 12; $index -lt 16; $index++) {
                if ($Bytes[$index] -ne 0) { $notes.Add('dirty_header'); break }
            }
        }
        return @{ Status = 'accounted'; Bucket = 'NES'; Note = ($notes -join '+') }
    }
    if ($Kind -eq 'FDS') { return @{ Status = 'accounted'; Bucket = 'FDS'; Note = 'requires_fds_bios' } }
    if ($Kind -eq 'UNF') { return @{ Status = 'accounted'; Bucket = 'UNF'; Note = 'product_disabled' } }
    if ($Kind -eq 'GB') { return @{ Status = 'skipped'; Bucket = 'NeedsReview'; Note = 'game_boy_payload' } }
    if ($Kind -eq 'EXE') { return @{ Status = 'skipped'; Bucket = 'NeedsReview'; Note = 'executable_never_run' } }
    if ($Kind -eq 'ZIP') { return @{ Status = 'skipped'; Bucket = 'NeedsReview'; Note = 'nested_zip_depth_exceeded' } }
    return @{ Status = 'skipped'; Bucket = 'NeedsReview'; Note = 'unknown_format' }
}

function Test-SafeEntryPath {
    param([string] $Path)
    if ([string]::IsNullOrWhiteSpace($Path) -or [System.IO.Path]::IsPathRooted($Path)) { return $false }
    return -not (($Path -split '[\\/]') -contains '..')
}

function New-PayloadRecord {
    param(
        [string] $PackagePath,
        [string] $Locator,
        [byte[]] $Bytes
    )
    $kind = Get-PayloadKind $Bytes
    $decision = Get-PayloadDecision $Bytes $kind
    return [pscustomobject][ordered]@{
        sourceRelativePath = $PackagePath
        locator = $Locator
        format = $kind
        status = $decision.Status
        bucket = $decision.Bucket
        sha256 = Get-Sha256Bytes $Bytes
        bytes = $Bytes.Length
        note = $decision.Note
    }
}

function Read-ZipPayloads {
    param(
        [System.IO.Compression.ZipArchive] $Archive,
        [string] $PackagePath,
        [string] $Prefix,
        [bool] $AllowNested
    )
    $records = New-Object System.Collections.Generic.List[object]
    foreach ($entry in $Archive.Entries) {
        if ([string]::IsNullOrEmpty($entry.Name)) { continue }
        $locator = if ($Prefix) { "$Prefix!/$($entry.FullName)" } else { $entry.FullName }
        if (-not (Test-SafeEntryPath $entry.FullName)) {
            $records.Add([pscustomobject][ordered]@{sourceRelativePath=$PackagePath;locator=$locator;format='UNKNOWN';status='skipped';bucket='NeedsReview';sha256='';bytes=$entry.Length;note='unsafe_entry_path'})
            continue
        }
        if ($entry.FullName.EndsWith('.exe', [StringComparison]::OrdinalIgnoreCase)) {
            $records.Add([pscustomobject][ordered]@{sourceRelativePath=$PackagePath;locator=$locator;format='EXE';status='skipped';bucket='NeedsReview';sha256='';bytes=$entry.Length;note='executable_never_run'})
            continue
        }
        if ($entry.Length -gt $MaxPayloadBytes) {
            $records.Add([pscustomobject][ordered]@{sourceRelativePath=$PackagePath;locator=$locator;format='UNKNOWN';status='error';bucket='NeedsReview';sha256='';bytes=$entry.Length;note='payload_limit_exceeded'})
            continue
        }
        try {
            $stream = $entry.Open()
            try { $bytes = Read-BoundedBytes $stream $MaxPayloadBytes } finally { $stream.Dispose() }
            $kind = Get-PayloadKind $bytes
            if ($kind -eq 'ZIP' -and $AllowNested) {
                $memory = [System.IO.MemoryStream]::new($bytes, $false)
                try {
                    $nested = [System.IO.Compression.ZipArchive]::new($memory, [System.IO.Compression.ZipArchiveMode]::Read, $false)
                    try {
                        foreach ($nestedRecord in (Read-ZipPayloads $nested $PackagePath $entry.FullName $false)) { $records.Add($nestedRecord) }
                    }
                    finally { $nested.Dispose() }
                }
                finally { $memory.Dispose() }
            }
            else {
                $records.Add((New-PayloadRecord $PackagePath $locator $bytes))
            }
        }
        catch {
            $records.Add([pscustomobject][ordered]@{sourceRelativePath=$PackagePath;locator=$locator;format='UNKNOWN';status='error';bucket='NeedsReview';sha256='';bytes=$entry.Length;note=('entry_read_failed:' + $_.Exception.GetType().Name)})
        }
    }
    return $records
}

$resolvedSource = (Resolve-Path -LiteralPath $SourceRoot).Path
$resolvedOutput = [System.IO.Path]::GetFullPath($OutputRoot)
if ($resolvedOutput.StartsWith($resolvedSource, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'OutputRoot must not be inside SourceRoot.'
}
New-Item -ItemType Directory -Force -Path $resolvedOutput | Out-Null

$packageRecords = New-Object System.Collections.Generic.List[object]
$payloadRecords = New-Object System.Collections.Generic.List[object]
$files = @(Get-ChildItem -LiteralPath $resolvedSource -File -Force | Sort-Object Name)
foreach ($file in $files) {
    $relative = [System.IO.Path]::GetRelativePath($resolvedSource, $file.FullName)
    $packageStatus = 'skipped'
    $packageNote = ''
    $physicalSha = ''
    $before = $payloadRecords.Count
    try {
        $physicalSha = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
        $fileStream = [System.IO.File]::OpenRead($file.FullName)
        try {
            $header = New-Object byte[] ([Math]::Min(512L, $file.Length))
            [void]$fileStream.Read($header, 0, $header.Length)
        }
        finally { $fileStream.Dispose() }
        $kind = Get-PayloadKind $header
        if ($kind -eq 'ZIP') {
            $archive = [System.IO.Compression.ZipFile]::OpenRead($file.FullName)
            try { foreach ($record in (Read-ZipPayloads $archive $relative '' $true)) { $payloadRecords.Add($record) } }
            finally { $archive.Dispose() }
        }
        elseif ($kind -eq 'EXE' -or $file.Extension.Equals('.exe', [StringComparison]::OrdinalIgnoreCase)) {
            $payloadRecords.Add([pscustomobject][ordered]@{sourceRelativePath=$relative;locator='@raw';format='EXE';status='skipped';bucket='NeedsReview';sha256='';bytes=$file.Length;note='executable_never_run'})
        }
        elseif ($file.Length -gt $MaxPayloadBytes) {
            $payloadRecords.Add([pscustomobject][ordered]@{sourceRelativePath=$relative;locator='@raw';format=$kind;status='error';bucket='NeedsReview';sha256='';bytes=$file.Length;note='payload_limit_exceeded'})
        }
        else {
            $payloadRecords.Add((New-PayloadRecord $relative '@raw' ([System.IO.File]::ReadAllBytes($file.FullName))))
        }
        $own = @($payloadRecords | Select-Object -Skip $before)
        if ($own.status -contains 'accounted') { $packageStatus = 'accounted' }
        elseif ($own.status -contains 'error') { $packageStatus = 'error' }
        else { $packageStatus = 'skipped' }
        $packageNote = if ($own.Count -eq 0) { 'empty_package' } else { '' }
    }
    catch {
        $packageStatus = 'error'
        $packageNote = 'package_read_failed:' + $_.Exception.GetType().Name
    }
    $packageRecords.Add([pscustomobject][ordered]@{
        relativePath = $relative
        status = $packageStatus
        physicalSha256 = $physicalSha
        bytes = $file.Length
        payloadCount = $payloadRecords.Count - $before
        note = $packageNote
    })
}

$timestamp = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
$summary = [ordered]@{
    schemaVersion = 1
    generatedAtUtc = $timestamp
    dryRun = $true
    sourceFileCount = $files.Count
    packageOutcomeCount = $packageRecords.Count
    packageOutcomes = [ordered]@{
        accounted = @($packageRecords | Where-Object status -eq 'accounted').Count
        skipped = @($packageRecords | Where-Object status -eq 'skipped').Count
        error = @($packageRecords | Where-Object status -eq 'error').Count
    }
    payloadCount = $payloadRecords.Count
    accountedPayloadCount = @($payloadRecords | Where-Object status -eq 'accounted').Count
    skippedPayloadCount = @($payloadRecords | Where-Object status -eq 'skipped').Count
    errorPayloadCount = @($payloadRecords | Where-Object status -eq 'error').Count
    payloadFormats = [ordered]@{}
    uniquePhysicalSha256 = @($packageRecords.physicalSha256 | Where-Object { $_ } | Sort-Object -Unique).Count
    uniquePayloadSha256 = @($payloadRecords.sha256 | Where-Object { $_ } | Sort-Object -Unique).Count
    uniqueAccountedPayloadSha256 = @($payloadRecords | Where-Object status -eq 'accounted' | Select-Object -ExpandProperty sha256 | Where-Object { $_ } | Sort-Object -Unique).Count
    scannerMode = 'forensic_container_dry_run'
    entryNameDecoder = 'dotnet_ziparchive'
    productionCatalogParity = $false
    absolutePathsIncluded = $false
    romBytesCopied = $false
}
foreach ($group in ($payloadRecords | Group-Object format | Sort-Object Name)) { $summary.payloadFormats[$group.Name] = $group.Count }

$packageRecords | Export-Csv -LiteralPath (Join-Path $resolvedOutput 'packages.csv') -NoTypeInformation -Encoding utf8
$payloadRecords | Export-Csv -LiteralPath (Join-Path $resolvedOutput 'payloads.csv') -NoTypeInformation -Encoding utf8
$summary | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $resolvedOutput 'summary.json') -Encoding utf8
$summary | ConvertTo-Json -Depth 5
