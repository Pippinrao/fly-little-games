# FlyNES HarmonyOS validation shell

This directory contains an unsigned HarmonyOS Stage application that validates
only the existing platform-neutral app/catalog ABI. It creates an app from the
application sandbox roots, takes an immutable empty-catalog snapshot, destroys
the app, reads the snapshot generation and count, then releases the snapshot.

The shell deliberately contains no emulator runtime/session API, ROM, core
linkage, network or nearby feature, permission, XComponent, audio, input, or
save-state implementation. Those remain outside this validation boundary.

## Pinned product configuration

- Bundle name: `com.flynes.emu`
- Runtime OS: HarmonyOS
- Compatible SDK: HarmonyOS NEXT `5.0.0(12)`
- Target SDK: `6.0.0(20)`
- Native compiler: BiSheng
- Native ABI: `arm64-v8a`
- N-API library/module/import name: `entry` / `libentry.so`

DevEco Studio 6.0's installed API 20 project template generates
`hvigor/hvigor-config.json5` but does not copy project-local `hvigorw` scripts.
Invoke the wrapper module shipped with DevEco Studio using its bundled Node
runtime:

```powershell
$env:DEVECO_SDK_HOME = "$env:DEVECO_STUDIO_HOME\sdk"
$env:NODE_HOME = "$env:DEVECO_STUDIO_HOME\tools\node"
$Node = "$env:DEVECO_STUDIO_HOME\tools\node\node.exe"
Push-Location harmony
try {
  & $Node "$env:DEVECO_STUDIO_HOME\tools\ohpm\bin\pm-cli.js" install --all
  & $Node "$env:DEVECO_STUDIO_HOME\tools\hvigor\bin\hvigorw.js" assembleHap `
    -p product=default -p module=entry@default -p buildMode=debug
} finally {
  Pop-Location
}
```

No SDK path, signing material, certificate, or local developer profile belongs
in this tree. An unsigned HAP proves compilation, linking, and packaging only;
real-device execution requires a separately provisioned debug signature.

## Private host contract test

The host-only test exercises the same `catalog_smoke.cpp` implementation linked
into `libentry.so`. On Windows it needs the repository's verified zlib 1.3.1
prefix:

```powershell
cmake -S harmony/tests -B out/harmony-host -G Ninja `
  "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT"
cmake --build out/harmony-host
ctest --test-dir out/harmony-host --output-on-failure
```

The host test is not part of the HAP: the module forces `FLYNES_BUILD_TESTS=OFF`.
