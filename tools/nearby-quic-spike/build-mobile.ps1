[CmdletBinding()]
param(
    [ValidateSet('ohos', 'android', 'all')][string]$Platform = 'all',
    [string]$DevEcoHome = 'D:/soft/DevEco Studio',
    [string]$AndroidNdk = "$env:LOCALAPPDATA/Android/Sdk/ndk/27.0.12077973",
    [switch]$Release
)
$ErrorActionPreference = 'Stop'
$probeRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $probeRoot '../..')).Path
$savedEnvironment = @{}
$variables = @('CC_aarch64_unknown_linux_ohos','AR_aarch64_unknown_linux_ohos','CFLAGS_aarch64_unknown_linux_ohos','CARGO_TARGET_AARCH64_UNKNOWN_LINUX_OHOS_LINKER','CC_aarch64_linux_android','AR_aarch64_linux_android','CFLAGS_aarch64_linux_android','CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER','CC_SHELL_ESCAPED_FLAGS','CARGO_ENCODED_RUSTFLAGS')
foreach ($name in $variables) { $savedEnvironment[$name] = [Environment]::GetEnvironmentVariable($name, 'Process') }
try {
    foreach ($targetPlatform in @('ohos', 'android')) {
        if ($Platform -ne 'all' -and $Platform -ne $targetPlatform) { continue }
        if ($targetPlatform -eq 'ohos') {
            $native = Join-Path $DevEcoHome 'sdk/default/openharmony/native'
            $clang = Join-Path $native 'llvm/bin/clang.exe'
            $env:CC_aarch64_unknown_linux_ohos = $clang
            $env:AR_aarch64_unknown_linux_ohos = Join-Path $native 'llvm/bin/llvm-ar.exe'
            $env:CFLAGS_aarch64_unknown_linux_ohos = "--target=aarch64-linux-ohos --sysroot=`"$native/sysroot`" -D__MUSL__"
            $env:CARGO_TARGET_AARCH64_UNKNOWN_LINUX_OHOS_LINKER = $clang
            $env:CARGO_ENCODED_RUSTFLAGS = @('-C','link-arg=--target=aarch64-linux-ohos','-C',"link-arg=--sysroot=$native/sysroot") -join [char]31
            $target = 'aarch64-unknown-linux-ohos'
        } else {
            $native = Join-Path $AndroidNdk 'toolchains/llvm/prebuilt/windows-x86_64/bin'
            $clang = Join-Path $native 'clang.exe'
            $env:CC_aarch64_linux_android = $clang
            $env:AR_aarch64_linux_android = Join-Path $native 'llvm-ar.exe'
            $env:CFLAGS_aarch64_linux_android = '--target=aarch64-linux-android26'
            $env:CARGO_TARGET_AARCH64_LINUX_ANDROID_LINKER = $clang
            $env:CARGO_ENCODED_RUSTFLAGS = @('-C','link-arg=--target=aarch64-linux-android26') -join [char]31
            $target = 'aarch64-linux-android'
        }
        if (!(Test-Path -LiteralPath $clang)) { throw "Missing compiler: $clang" }
        $env:CC_SHELL_ESCAPED_FLAGS = '1'
        $cargoArgs = @('build','--locked','--manifest-path',"$probeRoot/Cargo.toml",'--target',$target,'--target-dir',"$repoRoot/.artifacts/nearby-quic-$targetPlatform")
        if ($Release) { $cargoArgs += '--release' }
        & cargo @cargoArgs
        if ($LASTEXITCODE -ne 0) { throw "Cargo $target failed ($LASTEXITCODE)" }
    }
} finally {
    foreach ($name in $variables) { [Environment]::SetEnvironmentVariable($name, $savedEnvironment[$name], 'Process') }
}
