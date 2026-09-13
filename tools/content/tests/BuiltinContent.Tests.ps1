[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'

$repoRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..\..')).Path
$toolsContent = Join-Path $repoRoot 'tools\content'
$bootstrapPath = Join-Path $toolsContent 'bootstrap-content-toolchain.ps1'
$buildPath = Join-Path $toolsContent 'build-builtin-roms.ps1'
$verifyPath = Join-Path $toolsContent 'verify-builtin-content.ps1'

function New-ContentFixture {
    <#
        Minimal repository-shaped tree that verify-builtin-content.ps1 must be
        able to gate. Kept tiny on purpose: the gate only needs the manifest,
        the content assets, the three platform loaders and the platform
        resource directories.
    #>
    param(
        [Parameter(Mandatory = $true)][string] $Root,
        [string] $RomSha256,
        [switch] $OmitLicense,
        [switch] $OmitLoader,
        [switch] $WithPlatformRomCopy,
        [switch] $WithFromBelowReference,
        [switch] $WithPlatformGameName,
        [switch] $OmitCoverEligible
    )

    foreach ($relative in @(
            'content\assets\roms',
            'content\assets\licenses',
            'app\src\main\assets\roms',
            'harmony\entry\src\main\resources\rawfile',
            'app\src\main\java\com\flynes\emu\catalog',
            'harmony\entry\src\main\ets\service',
            'ios\app\platform')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $Root $relative) | Out-Null
    }

    $romBytes = [byte[]](0x4E, 0x45, 0x53, 0x1A) + [byte[]](1..64)
    $romPath = Join-Path $Root 'content\assets\roms\thwaite.nes'
    [System.IO.File]::WriteAllBytes($romPath, $romBytes)
    if (-not $PSBoundParameters.ContainsKey('RomSha256')) {
        $RomSha256 = (Get-FileHash -LiteralPath $romPath -Algorithm SHA256).Hash.ToLowerInvariant()
    }

    if (-not $OmitLicense) {
        'GNU GENERAL PUBLIC LICENSE Version 3' |
            Set-Content -LiteralPath (Join-Path $Root 'content\assets\licenses\thwaite.txt') -Encoding utf8
    }

    $manifest = [ordered]@{
        schemaVersion = 1
        games         = @(
            [ordered]@{
                canonicalId    = 'builtin:thwaite'
                assetFilename  = 'thwaite.nes'
                titleEn        = 'Thwaite'
                titleZhHans    = '护村记'
                mapper         = 0
                romSha256      = $RomSha256
                credit         = 'Damian Yerrick'
                sortOrder      = 10
                coverEligible  = if ($OmitCoverEligible) { $false } else { $true }
                license        = [ordered]@{
                    spdx           = 'GPL-3.0-or-later'
                    file           = 'thwaite.txt'
                    sourceUrl      = 'https://github.com/pinobatch/thwaite-nes'
                    sourceRevision = '0123456789abcdef0123456789abcdef01234567'
                    sourceOffer    = 'Corresponding source is published at the pinned revision.'
                }
            }
        )
    }
    $manifest | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $Root 'content\assets\builtin-games.json') -Encoding utf8

    $lock = [ordered]@{
        schemaVersion = 1
        sources       = @(
            [ordered]@{
                canonicalId    = 'builtin:thwaite'
                upstreamRepo   = 'https://github.com/pinobatch/thwaite-nes'
                buildEnv       = 'wsl-ubuntu-24.04'
                outputRomPath  = 'thwaite.nes'
                romSha256      = $RomSha256
                sourceRevision = '0123456789abcdef0123456789abcdef01234567'
            }
        )
    }
    $lock | ConvertTo-Json -Depth 8 |
        Set-Content -LiteralPath (Join-Path $Root 'content\sources.lock.json') -Encoding utf8

    if (-not $OmitLoader) {
        'package com.flynes.emu.catalog; // reads builtin-games.json' |
            Set-Content -LiteralPath (Join-Path $Root 'app\src\main\java\com\flynes\emu\catalog\BuiltinGames.java') -Encoding utf8
        "// reads builtin-games.json" |
            Set-Content -LiteralPath (Join-Path $Root 'harmony\entry\src\main\ets\service\BuiltinGames.ets') -Encoding utf8
        '// reads builtin-games.json' |
            Set-Content -LiteralPath (Join-Path $Root 'ios\app\platform\BuiltinGames.mm') -Encoding utf8
    }

    if ($WithPlatformRomCopy) {
        [System.IO.File]::WriteAllBytes(
            (Join-Path $Root 'app\src\main\assets\roms\thwaite.nes'), $romBytes)
    }

    if ($WithFromBelowReference) {
        'roms/from_below.nes' |
            Set-Content -LiteralPath (Join-Path $Root 'app\src\main\java\com\flynes\emu\RomLoader.java') -Encoding utf8
    }

    if ($WithPlatformGameName) {
        # Product source that names a bundled game: adding another game would now
        # require editing this file, which is exactly what the gate forbids.
        'String asset = "thwaite.nes";' |
            Set-Content -LiteralPath (Join-Path $Root 'app\src\main\java\com\flynes\emu\BundledHelper.java') -Encoding utf8
    }
}

function Invoke-ContentGate {
    param([Parameter(Mandatory = $true)][string] $Root)

    $output = & $verifyPath -Root $Root 2>&1 | Out-String
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = $output }
}

Describe 'content pipeline scripts are pinned and present' {
    It 'ships the three content pipeline scripts' {
        Test-Path -LiteralPath $bootstrapPath -PathType Leaf | Should Be $true
        Test-Path -LiteralPath $buildPath -PathType Leaf | Should Be $true
        Test-Path -LiteralPath $verifyPath -PathType Leaf | Should Be $true
    }

    It 'pins the cc65 toolchain and the Millfork jar by URL and SHA-256' {
        Test-Path -LiteralPath $bootstrapPath -PathType Leaf | Should Be $true
        $source = Get-Content -Raw -LiteralPath $bootstrapPath
        $source | Should Match 'cc65'
        $source | Should Match 'millfork'
        $source | Should Match 'sha256'
        # No moving refs: every download must be a pinned release URL.
        $source | Should Not Match 'releases/latest'
        $source | Should Not Match 'master\.zip'
    }

    It 'records the build lock with a per-game room for the build environment' {
        Test-Path -LiteralPath $buildPath -PathType Leaf | Should Be $true
        $source = Get-Content -Raw -LiteralPath $buildPath
        $source | Should Match 'sources\.lock\.json'
        $source | Should Match 'wsl'
        $source | Should Match 'AcceptNewHash'
        $source | Should Match 'Reason'
    }
}

Describe 'verify-builtin-content.ps1 gates the single source of truth' {
    BeforeEach {
        $script:fixture = Join-Path ([System.IO.Path]::GetTempPath()) ("flynes-content-" + [guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Force -Path $script:fixture | Out-Null
    }
    AfterEach {
        if ($script:fixture -and (Test-Path -LiteralPath $script:fixture)) {
            Remove-Item -Recurse -Force -LiteralPath $script:fixture -ErrorAction SilentlyContinue
        }
    }

    It 'accepts a well-formed content tree' {
        New-ContentFixture -Root $script:fixture
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Be 0
    }

    It 'rejects a ROM whose bytes no longer match the manifest hash' {
        New-ContentFixture -Root $script:fixture
        Add-Content -LiteralPath (Join-Path $script:fixture 'content\assets\roms\thwaite.nes') -Value 'tampered'
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'hash'
    }

    It 'rejects a game whose license text is missing' {
        New-ContentFixture -Root $script:fixture -OmitLicense
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'license'
    }

    It 'rejects a platform that keeps its own committed ROM copy' {
        New-ContentFixture -Root $script:fixture -WithPlatformRomCopy
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'source of truth'
    }

    It 'rejects a platform that does not read the shared manifest' {
        New-ContentFixture -Root $script:fixture -OmitLoader
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'BuiltinGames'
    }

    It 'rejects any surviving from_below reference in shipped source' {
        New-ContentFixture -Root $script:fixture -WithFromBelowReference
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'retired bundled game'
    }

    It 'rejects a platform that names a bundled game in its own source' {
        New-ContentFixture -Root $script:fixture -WithPlatformGameName
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'adding one would need a code change'
    }

    It 'rejects a bundle where no game can be observed capturing a cover' {
        New-ContentFixture -Root $script:fixture -OmitCoverEligible
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'coverEligible'
    }

    It 'rejects the retired game still checked in as a ROM binary' {
        # The text scan cannot see a binary asset, so this rule reads git's index.
        New-ContentFixture -Root $script:fixture
        & git -C $script:fixture init -q 2>&1 | Out-Null
        & git -C $script:fixture config user.email 'gate@example.invalid' 2>&1 | Out-Null
        & git -C $script:fixture config user.name 'Content Gate' 2>&1 | Out-Null
        # git ls-files is index-based, so an intent-to-add entry is enough. The
        # fixture writes only the manifest's ROM, so the legacy asset is created here.
        $legacy = Join-Path $script:fixture 'content\assets\roms\from_below.nes'
        [System.IO.File]::WriteAllBytes($legacy, [byte[]](0x4E, 0x45, 0x53, 0x1A))
        & git -C $script:fixture add -f -N -- 'content/assets/roms/from_below.nes' 2>&1 | Out-Null
        $result = Invoke-ContentGate -Root $script:fixture
        $result.ExitCode | Should Not Be 0
        $result.Output | Should Match 'retired bundled game is still checked in'
    }
}
