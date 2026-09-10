# FlyNES HarmonyOS product shell

This directory contains the HarmonyOS Stage application for FlyNES. Cold start
opens **Game Center** (`pages/GameCenter`): Recent / Favorites / All / Built-in,
search, launch into landscape play, plus Sources and Settings. The leftover
`pages/Index` debug HUD is not the default entry.

Play (`pages/RunGame`) shows the NES picture with an overlay (D-pad/joystick, A,
B, SELECT, START) and a separate pause control. Pause is Resume / Game Center /
Settings only; there is no on-screen Save/Load. Settings follow Android’s five
roots (Display, Controls, Audio, Game & Language, About). The layout editor
writes the shared ControlLayoutV2 string. FilePicker URIs stay in
`HarmonySourceMap` app preferences, never in FLYCAT01.

The bundled ROM is Matt Hughson's *From Below* (MIT). See
`entry/src/main/resources/rawfile/LICENSE-from-below.txt`.

## Pinned product configuration

- Bundle name: `com.flynes.emu`
- Runtime OS: HarmonyOS
- Compatible SDK: HarmonyOS NEXT `5.0.0(12)`
- Target SDK: `6.0.0(20)`
- Native compiler: BiSheng
- Native ABI: `arm64-v8a` (devices) and `x86_64` (official Phone emulator)
- N-API library/module/import name: `entry` / `libentry.so`

DevEco Studio 6.0's installed API 20 project template generates
`hvigor/hvigor-config.json5` but does not copy project-local `hvigorw` scripts.
Invoke the wrapper module shipped with DevEco Studio using its bundled Node
runtime:

```powershell
$env:DEVECO_STUDIO_HOME = "D:\soft\DevEco Studio"
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
emulator or real-device execution requires a separately provisioned debug
signature.

The completion work has one stage-aware test entry point. It writes revision,
target, dirty-tree state, and each command log into a distinct evidence directory:

```powershell
.\tools\quality\run_harmony_completion_gate.ps1 -Stage Host `
  -CMakeExe $CMake -CTestExe $CTest
.\tools\quality\run_harmony_completion_gate.ps1 -Stage HarmonyEmulator `
  -NodeExe $Node -HvigorScript "$env:DEVECO_STUDIO_HOME\tools\hvigor\bin\hvigorw.js" `
  -DevEcoSdkHome $env:DEVECO_SDK_HOME `
  -HdcExe "$env:DEVECO_SDK_HOME\default\openharmony\toolchains\hdc.exe"
.\tools\quality\run_harmony_completion_gate.ps1 -Stage AndroidEmulator
```

`HarmonyDevice` and `AndroidDevice` require an explicit target serial. Their
logs remain separate from emulator evidence and do not certify physical timing
by themselves.

## Private host contract test

The host-only tests exercise `catalog_smoke.cpp`, `runtime_smoke.cpp`, and
`play_session.cpp`. On Windows they need the repository's verified zlib 1.3.1
prefix:

```powershell
cmake -S harmony/tests -B out/harmony-host -G "Visual Studio 17 2022" -A x64 `
  "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT"
cmake --build out/harmony-host --config Release
ctest --test-dir out/harmony-host -C Release --output-on-failure
```

The host tests are not part of the HAP: the module forces `FLYNES_BUILD_TESTS=OFF`.
