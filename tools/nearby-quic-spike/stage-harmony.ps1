[CmdletBinding()]
param([string]$Archive = '', [string]$Destination = '', [string]$DevEcoHome = 'D:/soft/DevEco Studio')
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
if (!$Archive) { $Archive = "$repoRoot/.artifacts/nearby-quic-ohos/aarch64-unknown-linux-ohos/debug/libnearby_quic_spike.a" }
if (!$Destination) { $Destination = "$repoRoot/.artifacts/nearby-quic-harmony-app" }
if (Test-Path -LiteralPath $Destination) { throw 'Stage already exists; refusing overwrite to preserve local signing. Choose a new destination.' }
if (!(Test-Path -LiteralPath $Archive -PathType Leaf)) { throw "Missing OHOS archive: $Archive" }
$null = New-Item -ItemType Directory -Path $Destination
Copy-Item -Path "$PSScriptRoot/harmony/*" -Destination $Destination -Recurse
$deps = Join-Path $Destination 'entry/src/main/cpp/deps'
$null = New-Item -ItemType Directory -Path $deps
Copy-Item -LiteralPath $Archive -Destination "$deps/libnearby_quic_spike.a"
Copy-Item -LiteralPath "$PSScriptRoot/include/nearby_quic_spike.h" -Destination $deps
Write-Output "Staged independent com.flynes.nearbyprobe at $Destination"
Write-Output "Open this project in DevEco Studio for local automatic signing; SDK root: $DevEcoHome/sdk"
