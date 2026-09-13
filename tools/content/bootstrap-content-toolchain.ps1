<#
.SYNOPSIS
    Bootstrap the pinned toolchain used to build the bundled homebrew ROMs.

.DESCRIPTION
    Pins come from content/sources.lock.json; this script never invents a
    version. It:
      1. installs the WSL-side packages (the cc65 pin, Pillow, a headless JRE)
         and checks the observed versions against the lock,
      2. downloads every pinned archive on the Windows side (WSL's access to
         GitHub is unreliable on this machine) and verifies its SHA-256,
      3. compiles/installs the small toolchain tree under .artifacts/content,
      4. writes .artifacts/content/content-toolchain.json for later auditing.

    Exit code 0 only when every pin matched.
#>
[CmdletBinding()]
param(
    [string] $Root = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..\..')).Path,
    [string] $WslDistro = 'Ubuntu-24.04',
    [string] $ArchiveCache = '',
    [switch] $Force
)

$ErrorActionPreference = 'Stop'

function Write-Step { param([string] $Message) Write-Host "==> $Message" -ForegroundColor Cyan }
function Fail { param([string] $Message) [Console]::Error.WriteLine("BOOTSTRAP_FAIL $Message"); exit 1 }

$lockPath = Join-Path $Root 'content\sources.lock.json'
if (-not (Test-Path -LiteralPath $lockPath -PathType Leaf)) { Fail "missing content/sources.lock.json" }
$lock = Get-Content -Raw -LiteralPath $lockPath | ConvertFrom-Json
$tc = $lock.toolchain
if ($null -eq $tc) { Fail 'the lock file has no toolchain pin block' }

$artifacts = Join-Path $Root '.artifacts\content'
if ([string]::IsNullOrWhiteSpace($ArchiveCache)) { $ArchiveCache = Join-Path $artifacts 'toolchain-cache' }
$install = Join-Path $artifacts 'toolchain'
New-Item -ItemType Directory -Force -Path $ArchiveCache, $install | Out-Null

$forward = {
    # WSL sees this repository under /mnt/<drive>/..., never as a Windows path.
    param([string] $p)
    $full = [System.IO.Path]::GetFullPath($p)
    $drive = $full.Substring(0, 1).ToLowerInvariant()
    return '/mnt/' + $drive + ($full.Substring(2) -replace '\\', '/')
}

Write-Step "checking the WSL distro '$WslDistro'"
$distros = (wsl.exe -l -q) -split "`r?`n" | Where-Object { $_.Trim() } | ForEach-Object { $_.Trim() }
if ($distros -notcontains $WslDistro) { Fail "WSL distro '$WslDistro' is not installed (found: $($distros -join ', '))" }

function Invoke-Wsl {
    param([Parameter(Mandatory = $true)][string] $Script, [string[]] $Arguments = @())
    $wslArgs = @('-d', $WslDistro, '-u', 'root', '--', 'bash', '-lc', $Script)
    $output = & wsl.exe @wslArgs @Arguments 2>&1
    return [pscustomobject]@{ ExitCode = $LASTEXITCODE; Output = ($output -join "`n") }
}

Write-Step 'installing WSL-side packages and checking the cc65 pin'
$aptScript = @"
export DEBIAN_FRONTEND=noninteractive
command -v ca65 >/dev/null 2>&1 || apt-get install -y -qq cc65 >/tmp/flynes-apt.log 2>&1
python3 -c 'import PIL' >/dev/null 2>&1 || apt-get install -y -qq python3-pil >>/tmp/flynes-apt.log 2>&1
command -v java >/dev/null 2>&1 || apt-get install -y -qq default-jre-headless >>/tmp/flynes-apt.log 2>&1
echo "cc65=`$(dpkg-query -W cc65 2>/dev/null | cut -f2)"
echo "ca65=`$(ca65 --version 2>&1 | head -1)"
echo "pillow=`$(python3 -c 'import PIL; print(PIL.__version__)' 2>/dev/null || echo none)"
echo "java=`$(command -v java >/dev/null 2>&1 && echo present || echo none)"
"@
$apt = Invoke-Wsl -Script $aptScript
Write-Host $apt.Output
if ($apt.ExitCode -ne 0) { Fail "WSL package installation failed: $($apt.Output)" }

$observed = @{}
foreach ($line in ($apt.Output -split "`n")) {
    if ($line -match '^(cc65|ca65|pillow|java)=(.*)$') { $observed[$Matches[1]] = $Matches[2].Trim() }
}
if ($observed['cc65'] -ne $tc.cc65) {
    Fail "cc65 version drift: lock pins '$($tc.cc65)' but WSL has '$($observed['cc65'])'"
}
if ($observed['pillow'] -ne $tc.pythonPillow) {
    Fail "Pillow version drift: lock pins '$($tc.pythonPillow)' but WSL has '$($observed['pillow'])'"
}
if ($observed['java'] -ne 'present') { Fail 'no JRE is available in WSL (required by millfork)' }

$downloads = [ordered]@{
    'millfork-0.3.12.zip' = $tc.millfork
    'gcc-6502.zip'        = $tc.gcc6502
    'xa65-stb.tar.gz'     = $tc.xa65stb
    'huffmunch.tar.gz'    = $tc.huffmunch
}
foreach ($name in $downloads.Keys) {
    $pin = $downloads[$name]
    $target = Join-Path $ArchiveCache $name
    $expected = ([string]$pin.sha256).ToLowerInvariant()
    if ($expected -notmatch '^[0-9a-f]{64}$') { Fail "the lock has no sha256 pin for $name" }
    if ((Test-Path -LiteralPath $target) -and -not $Force) {
        $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
        if ($actual -eq $expected) { Write-Host "    $name cached and verified"; continue }
        Write-Host "    $name cached copy is stale; refetching" -ForegroundColor Yellow
    }
    Write-Step "downloading $name"
    try {
        Invoke-WebRequest -Uri $pin.url -OutFile $target -TimeoutSec 900
    } catch {
        Fail "download failed for $name ($($pin.url)): $($_.Exception.Message)"
    }
    $actual = (Get-FileHash -LiteralPath $target -Algorithm SHA256).Hash.ToLowerInvariant()
    if ($actual -ne $expected) {
        Fail "SHA-256 mismatch for ${name}: pinned $expected but downloaded $actual"
    }
}

Write-Step 'building and installing the toolchain in WSL'
$bootstrapSh = (& $forward (Join-Path $Root 'tools\content\bootstrap-toolchain-in-wsl.sh'))
$cachePath = (& $forward $ArchiveCache)
$installPath = (& $forward $install)
$build = Invoke-Wsl -Script "bash '$bootstrapSh' '$cachePath' '$installPath'"
Write-Host $build.Output
if ($build.ExitCode -ne 0 -or $build.Output -notmatch 'BOOTSTRAP_OK') {
    Fail "toolchain bootstrap failed: $($build.Output)"
}

$manifest = [ordered]@{
    wslDistro     = $WslDistro
    cc65          = $observed['cc65']
    ca65          = $observed['ca65']
    pillow        = $observed['pillow']
    installedAt   = (Get-Date).ToUniversalTime().ToString('yyyy-MM-ddTHH:mm:ssZ')
    installDir    = (& $forward $install)
    archives      = @()
}
foreach ($name in $downloads.Keys) {
    $path = Join-Path $ArchiveCache $name
    $manifest.archives += [ordered]@{
        name   = $name
        sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
        url    = $downloads[$name].url
    }
}
$manifestPath = Join-Path $artifacts 'content-toolchain.json'
$manifest | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath $manifestPath -Encoding utf8

Write-Host ''
Write-Host 'PASS content toolchain bootstrap' -ForegroundColor Green
Write-Host "    toolchain : $install"
Write-Host "    manifest  : $manifestPath"
exit 0
