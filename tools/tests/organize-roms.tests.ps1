[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.IO.Compression.FileSystem

function New-InesPayload {
    param([byte] $Marker)
    $bytes = New-Object byte[] (16 + 16384)
    $bytes[0] = 0x4e; $bytes[1] = 0x45; $bytes[2] = 0x53; $bytes[3] = 0x1a
    $bytes[4] = 1
    $bytes[$bytes.Length - 1] = $Marker
    return $bytes
}

function Get-Hash {
    param([byte[]] $Bytes)
    $sha = [System.Security.Cryptography.SHA256]::Create()
    try { return ([BitConverter]::ToString($sha.ComputeHash($Bytes))).Replace('-', '').ToLowerInvariant() }
    finally { $sha.Dispose() }
}

function Write-SingleEntryZip {
    param([string] $Path, [string] $EntryName, [byte[]] $Bytes)
    $stream = [System.IO.File]::Create($Path)
    try {
        $archive = [System.IO.Compression.ZipArchive]::new(
            $stream, [System.IO.Compression.ZipArchiveMode]::Create, $false)
        try {
            $entry = $archive.CreateEntry($EntryName, [System.IO.Compression.CompressionLevel]::Optimal)
            $target = $entry.Open()
            try { $target.Write($Bytes, 0, $Bytes.Length) } finally { $target.Dispose() }
        }
        finally { $archive.Dispose() }
    }
    finally { $stream.Dispose() }
}

$root = Join-Path ([System.IO.Path]::GetTempPath()) ('flynes-organizer-' + [Guid]::NewGuid().ToString('N'))
$source = Join-Path $root 'source'
$output = Join-Path $root 'organized'
$audit = Join-Path $root 'payloads.csv'
New-Item -ItemType Directory -Path $source | Out-Null
try {
    $same = New-InesPayload 1
    $different = New-InesPayload 2
    $bangDirectory = New-InesPayload 3
    [System.IO.File]::WriteAllBytes((Join-Path $source '魂斗罗.nes'), $same)
    Write-SingleEntryZip (Join-Path $source '魂斗罗副本.zip') 'Contra (U).nes' $same
    Write-SingleEntryZip (Join-Path $source '魂斗罗日版.zip') 'Contra (J).nes' $different
    Write-SingleEntryZip (Join-Path $source 'Goal!.zip') 'Goal!/Goal! (E).nes' $bangDirectory
    $before = Get-ChildItem -LiteralPath $source -File | ForEach-Object {
        [pscustomobject]@{ Name = $_.Name; Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    }

    @(
        [pscustomobject]@{sourceRelativePath='魂斗罗.nes';locator='@raw';format='NES';status='accounted';bucket='NES';sha256=(Get-Hash $same);bytes=$same.Length;note=''},
        [pscustomobject]@{sourceRelativePath='魂斗罗副本.zip';locator='Contra (U).nes';format='NES';status='accounted';bucket='NES';sha256=(Get-Hash $same);bytes=$same.Length;note=''},
        [pscustomobject]@{sourceRelativePath='魂斗罗日版.zip';locator='Contra (J).nes';format='NES';status='accounted';bucket='NES';sha256=(Get-Hash $different);bytes=$different.Length;note=''},
        [pscustomobject]@{sourceRelativePath='Goal!.zip';locator='Goal!/Goal! (E).nes';format='NES';status='accounted';bucket='NES';sha256=(Get-Hash $bangDirectory);bytes=$bangDirectory.Length;note=''}
    ) | Export-Csv -LiteralPath $audit -NoTypeInformation -Encoding utf8

    & (Join-Path $PSScriptRoot '..\organize-roms.ps1') -SourceRoot $source -AuditPayloadsCsv $audit -OutputRoot $output
    if ($null -ne $LASTEXITCODE -and $LASTEXITCODE -ne 0) {
        throw "organizer exited with $LASTEXITCODE"
    }

    $manifest = @(Import-Csv -LiteralPath (Join-Path $output 'manifest.csv'))
    if ($manifest.Count -ne 3) { throw "expected 3 unique payloads, got $($manifest.Count)" }
    if (@(Get-ChildItem -LiteralPath (Join-Path $output 'NES') -File -Filter '*.zip').Count -ne 3) {
        throw 'organized NES output count differs from manifest'
    }
    if (@($manifest.sha256 | Sort-Object -Unique).Count -ne 3) { throw 'manifest hashes are not unique' }
    if (-not ($manifest.titleZhHans -contains '魂斗罗')) { throw 'Chinese display title was not preserved' }

    $after = Get-ChildItem -LiteralPath $source -File | ForEach-Object {
        [pscustomobject]@{ Name = $_.Name; Hash = (Get-FileHash -LiteralPath $_.FullName -Algorithm SHA256).Hash }
    }
    if ((Compare-Object $before $after -Property Name,Hash).Count -ne 0) {
        throw 'source fixtures were modified'
    }
    Write-Output 'PASS organize-roms exact-hash dedupe, distinct-variant retention, bilingual metadata, source immutability'
}
finally {
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
