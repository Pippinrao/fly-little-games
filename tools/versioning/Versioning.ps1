Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

function Resolve-FlyNesRepositoryRoot {
    param([string]$RepositoryRoot)
    if ([string]::IsNullOrWhiteSpace($RepositoryRoot)) {
        $RepositoryRoot = Join-Path $PSScriptRoot '..\..'
    }
    return (Resolve-Path -LiteralPath $RepositoryRoot).Path
}

function Get-FlyNesVersion {
    param([Parameter(Mandatory)][string]$RepositoryRoot)
    $value = (Get-Content -Raw -LiteralPath (Join-Path $RepositoryRoot 'VERSION')).Trim()
    if ($value -notmatch '^(?<major>0|[1-9]\d*)\.(?<minor>0|[1-9]\d*)\.(?<patch>0|[1-9]\d*)$') {
        throw "VERSION must be MAJOR.MINOR.PATCH, got '$value'."
    }
    $parts = [pscustomobject]@{
        Text = $value
        Major = [int]$Matches.major
        Minor = [int]$Matches.minor
        Patch = [int]$Matches.patch
    }
    if ($parts.Minor -gt 999 -or $parts.Patch -gt 999) {
        throw 'MINOR and PATCH must be between 0 and 999.'
    }
    return $parts
}

function Get-FlyNesVersionCode {
    param([Parameter(Mandatory)]$Version)
    $code = ([long]$Version.Major * 1000000L) + ([long]$Version.Minor * 1000L) + $Version.Patch
    if ($code -lt 1 -or $code -gt [int]::MaxValue) {
        throw "Version code $code is outside the supported app range."
    }
    return [int]$code
}

function Write-Utf8NoBom {
    param([Parameter(Mandatory)][string]$Path, [Parameter(Mandatory)][string]$Text)
    [IO.File]::WriteAllText($Path, $Text, [Text.UTF8Encoding]::new($false))
}

function Assert-FlyNesMajor {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [Parameter(Mandatory)]$Version,
        [switch]$AllowMajorChange
    )
    $majorPath = Join-Path $RepositoryRoot 'VERSION_MAJOR'
    $protectedMajor = [int](Get-Content -Raw -LiteralPath $majorPath).Trim()
    if ($Version.Major -ne $protectedMajor) {
        if (-not $AllowMajorChange) {
            throw "Major version is protected at $protectedMajor. Only an explicit user-authorized run may change it."
        }
        Write-Utf8NoBom -Path $majorPath -Text "$($Version.Major)`n"
    }
}

function Sync-FlyNesVersion {
    param(
        [Parameter(Mandatory)][string]$RepositoryRoot,
        [switch]$AllowMajorChange
    )
    $RepositoryRoot = Resolve-FlyNesRepositoryRoot $RepositoryRoot
    $version = Get-FlyNesVersion $RepositoryRoot
    Assert-FlyNesMajor -RepositoryRoot $RepositoryRoot -Version $version -AllowMajorChange:$AllowMajorChange
    $code = Get-FlyNesVersionCode $version

    $androidPath = Join-Path $RepositoryRoot 'app\build.gradle'
    $android = Get-Content -Raw -LiteralPath $androidPath
    $androidCodeReplacement = '${1}' + $code + '${2}'
    $androidNameReplacement = '${1}"' + $version.Text + '"${2}'
    $android = $android -replace '(?m)^(\s*versionCode\s+)\d+(\s*)$', $androidCodeReplacement
    $android = $android -replace '(?m)^(\s*versionName\s+)\"[^\"]+\"(\s*)$', $androidNameReplacement
    Write-Utf8NoBom -Path $androidPath -Text $android

    $harmonyPath = Join-Path $RepositoryRoot 'harmony\AppScope\app.json5'
    $harmony = Get-Content -Raw -LiteralPath $harmonyPath
    $harmonyCodeReplacement = '${1}' + $code + '${2}'
    $harmonyNameReplacement = '${1}"' + $version.Text + '"${2}'
    $harmony = $harmony -replace '(?m)^(\s*\"versionCode\"\s*:\s*)\d+(\s*,?)$', $harmonyCodeReplacement
    $harmony = $harmony -replace '(?m)^(\s*\"versionName\"\s*:\s*)\"[^\"]+\"(\s*,?)$', $harmonyNameReplacement
    Write-Utf8NoBom -Path $harmonyPath -Text $harmony
    return $version
}
