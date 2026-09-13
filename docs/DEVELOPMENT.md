# FlyNES development guide

Repository conventions live in [AGENTS.md](../AGENTS.md); this file is the
factual map: what the product is, how the tree is laid out, and the exact
commands that build, test, and debug each of the three platforms.

Read it before changing platform code or adding a bundled game.

## 1. What the product is

FlyNES is an offline NES/Famicom emulator for **Android, HarmonyOS NEXT, and
iOS**, built from one shared C++ core plus a shared product layer, with a
native UI per platform.

- Emulation core: NestopiaUE, vendored as the git submodule `core/vendor/nestopiaue`.
- One shared product model decides the game library, settings, sessions, save
  state, and rendering policy; each platform only supplies its shell.
- The app bundles **seven licensed homebrew games**, declared once in
  `content/assets/builtin-games.json`. Users import their own ROMs; nothing is
  uploaded anywhere.

Current product surface: bilingual game center (recent / favorites / all /
built-in), source scanning, favorites and play marks, per-game saves, an
editable on-screen gamepad, automatic cover capture, and several display paths
(Nearest, Sharp Bilinear, MMPX, ScaleFX, CRT). High-refresh and motion
compensation are gated behind measured device capability.

## 2. Tree layout

| Path | What lives there |
|---|---|
| `core/` | NestopiaUE wrapper, the `nes_*` C ABI, and its host tests. Must stay platform-free. |
| `shared/` | Cross-platform product layer: catalog, settings, session, rendering policy, `fly_*` app ABI, host tests. |
| `app/` | Android app (`com.flynes.emu`), JNI glue, unit tests, instrumentation tests. |
| `harmony/` | HarmonyOS Stage app, N-API `libentry.so`, ArkTS UI, Hypium + host tests. |
| `ios/` | iOS product app (ObjC++/Swift/Metal), portability probe, XCTest suites. |
| `content/` | **Single source of truth for bundled games**: `assets/builtin-games.json`, `assets/roms/`, `assets/licenses/`, `sources.lock.json`. |
| `tools/content/` | Content pipeline: toolchain bootstrap, ROM build, sync, and the content gate. |
| `tools/versioning/` | Version bump/sync and the worktree allocator. |
| `tools/quality/` | Host/simulator/device quality gates and evidence runners. |
| `docs/` | `COMPLIANCE.md` (SBOM + licensing), `verification/` (dated evidence), design and review notes. |
| `scripts/ci-check.ps1` | Local CI gate: header whitelist, ABI diff, host tests, Android build. |

### Invariants that gates enforce

1. **Never name a bundled game in product source.** Adding a game must be a
   manifest edit plus an asset. `tools/content/verify-builtin-content.ps1`
   fails if any tracked product file contains a bundled game's filename,
   canonical id, or title.
2. **The retired bundled game must stay gone.** Any tracked path matching
   `from[-_ ]below` fails the content gate — binary assets included, which is
   why the rule reads git's index rather than scanning text.
3. **No platform keeps its own ROM copy.** ROMs live once under
   `content/assets/roms/`; platform resource directories are generated and
   git-ignored.
4. **`core/` stays platform-free.** `scripts/ci-check.ps1` scans it for
   Android/JNI/Windows/SDL tokens.
5. **`VERSION` is the only version source.** Never hand-edit a platform
   `versionName`/`versionCode`.

## 3. Prerequisites

| Platform | Toolchain used by this repo |
|---|---|
| Android | JDK 17, Android SDK 36, NDK `27.0.12077973` (SDK `cmake;3.22.1`), Gradle wrapper |
| iOS | macOS + Xcode 26.x, CMake 3.31.8 (see `CMAKE_BIN`), iOS deployment target 16.4 |
| HarmonyOS | DevEco Studio 6.0 with API 20 SDK, `hvigorw` + `ohpm` shipped with DevEco, `hdc` |
| Shared/host | Visual Studio 2022 (x64) or any C++17 toolchain, Python 3, PowerShell 7 |
| Content build | WSL Ubuntu-24.04 as root, cc65 2.19, Millfork 0.3.12, bespoke STB toolchain |

`local.properties` points at the Android SDK and is **not** committed
(`sdk.dir=...`). Signing material, keystores, device credentials, and private
ROMs must never be printed or committed.

## 4. Android

### Build and unit tests

```powershell
.\gradlew.bat :app:testDebugUnitTest
.\gradlew.bat :app:assembleDebug
```

Native code is built through the SDK CMake inside Gradle; run the whole local
CI gate with the ABI check included:

```powershell
.\scripts\ci-check.ps1
```

### Instrumentation tests

The runner is `SingleDeviceCertificationRunner`; tests wait on a real device or
emulator, so always pin the serial:

```powershell
$env:ANDROID_SERIAL = 'emulator-5570'
.\gradlew.bat :app:connectedDebugAndroidTest
# one class:
.\gradlew.bat :app:connectedDebugAndroidTest `
  "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.cover.CoverCaptureIntegrationTest"
```

Existing AVDs: `MIT_Phone_API35`, `FlyNES_BuiltinContent`,
`FlyNES_Loading_20260913_5564`. An AVD is not a substitute for a physical
device: **emulators never certify refresh rate, power, temperature, or
latency**, and this repo's software-rendered AVDs expose only
OpenGL ES 3.0, so the ES 3.1 compute and native-presenter tests fail there for
environmental reasons.

### Debugging

```powershell
$adb = "$env:ANDROID_HOME\platform-tools\adb.exe"
& $adb -s emulator-5570 logcat -s FlyNES:*          # catalog, scan, launch logs
& $adb -s emulator-5570 shell run-as com.flynes.emu ls -la files
& $adb -s emulator-5570 shell run-as com.flynes.emu ls -la no_backup/covers/v1
& $adb -s emulator-5570 shell am start -n com.flynes.emu/.HomeActivity
& $adb -s emulator-5570 shell uiautomator dump /sdcard/ui.xml
```

Notes that save time:

- Cover PNGs live under `no_backup/covers/v1`, named by SHA-256 of the
  canonical id.
- The bundled catalog is scanned at every cold start; `bundled scan committed
  N game(s)` in logcat is the quickest proof the manifest was read.
- `pm clear com.flynes.emu` resets catalog state between manual runs.

## 5. iOS

The product app is `ios/app` (CMake target `FlyNES`, bundle id
`com.flynes.app`). iOS work runs on a Mac; keep a synced checkout there and use
the repo's scripts instead of hand-rolled `xcodebuild` invocations.

```sh
# build the product app for the simulator
CMAKE_BIN=~/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake \
  bash ios/scripts/build_simulator.sh

# install + launch on a specific simulator
bash ios/scripts/run_product_simulator.sh <SIMULATOR_UDID>

# test suites through the official runner (installs the app and exports the
# app's data container to the tests)
python3 ios/scripts/run_simulator_tests.py <SIMULATOR_UDID> FlyNESRuntimeTests
python3 ios/scripts/run_simulator_tests.py <SIMULATOR_UDID> FlyNESUITests

# import-flow UI tests need their fixtures staged first
python3 ios/scripts/stage_import_fixtures.py --udid <SIMULATOR_UDID>
```

Two traps cost this repository real time:

1. **`run_simulator_tests.py` does `test-without-building`; it never rebuilds.**
   Always `cmake --build build/ios-simulator --config Debug` first, or a stale
   test bundle will make a correct fix look broken.
2. **Plain `xcodebuild test-without-building` does not install the app, and does
   not set `FLYNES_TEST_APPLICATION_CONTAINERS`.** Tests then run against
   whatever old build is installed. Use the script.

When syncing sources onto the Mac with `scp`, note that `scp` preserves mtimes:
CMake may decide the copied file is older than its object file and **skip the
rebuild**. `touch` the file after copying.

Debugging:

```sh
xcrun simctl get_app_container <UDID> com.flynes.app data   # Library/Caches/covers/v1 lives here
xcrun simctl spawn <UDID> log stream --predicate 'process == "FlyNES"'
```

Local-only helpers for exercising a real (private) ROM corpus live in
`ios/scripts/local/`; they are git-ignored by design. See its README.

## 6. HarmonyOS

```powershell
$env:DEVECO_SDK_HOME = "D:\soft\DevEco Studio\sdk"
$Node = "D:\soft\DevEco Studio\tools\node\node.exe"
$Hvigor = "D:\soft\DevEco Studio\tools\hvigor\bin\hvigorw.js"
Push-Location harmony
try {
  & $Node "$env:DEVECO_SDK_HOME\..\tools\ohpm\bin\pm-cli.js" install --all
  & $Node $Hvigor --mode module -p product=default assembleHap --no-daemon
  # ohosTest module
  & $Node $Hvigor --mode module -p product=default -p module=entry@ohosTest -p buildMode=debug assembleHap --no-daemon
} finally { Pop-Location }
```

Host tests (the private C++ suite; not part of the HAP):

```powershell
cmake -S harmony/tests -B .artifacts/harmony-host -G "Visual Studio 17 2022" -A x64 -DFLYNES_BUILD_TESTS=ON
cmake --build .artifacts/harmony-host --config Debug --parallel 2
ctest --test-dir .artifacts/harmony-host -C Debug --parallel 2
```

Device/emulator:

```powershell
& "$env:DEVECO_SDK_HOME\default\openharmony\toolchains\hdc.exe" list targets
& "...\hdc.exe" install -r <signed>.hap
```

Boundaries:

- **An unsigned HAP proves compilation, linking, and packaging only.** Installing
  it needs a debug signature; signing material stays in the developer's local
  environment and is never committed or printed.
- A missing `signingConfigs` in `harmony/build-profile.json5` blocks device
  install; ask instead of copying credentials.
- DevEco ships `hvigorw` in its own `tools/` tree — invoke it through the
  bundled Node runtime rather than expecting a project-local wrapper.
- On Windows, a duplicated `NO_PROXY`/`no_proxy` environment pair breaks MSBuild
  with `MSB6001`; build host trees through `Start-Process -UseNewEnvironment`,
  or run the build from a shell without the duplicate.

## 7. Bundled content

`content/assets/builtin-games.json` is the single source of truth; every
platform reads it at runtime through its own `BuiltinGames` loader. Licence
texts and pinned upstream revisions are recorded per game, and
`content/sources.lock.json` holds the build recipe.

```powershell
# stage the manifest/ROMs/licenses into platform resource dirs (HarmonyOS rawfile)
.\tools\content\sync-builtin-content.ps1

# the gate: schema, hashes, licences, platform loaders, decoupling, legacy assets
.\tools\content\verify-builtin-content.ps1

# rebuild the ROMs from pinned upstream source (WSL, root)
.\tools\content\build-builtin-roms.ps1
```

`concentration_room` is **not byte-reproducible** upstream (its build shuffles
memory layout); its `hashPolicy` is `artifact` and `docs/COMPLIANCE.md` states
this explicitly. The other six rebuild byte-identically.

Adding a game requires: a manifest entry, its ROM and licence text under
`content/assets/`, a `sources.lock.json` record, and nothing else. If any
platform source needs an edit, the decoupling invariant is broken and the gate
will fail.

## 8. Verification expectations

- Test-drive product behavior: show the failing assertion, make the smallest
  fix, then re-run the affected suites.
- Android shared/native change → host tests + Android unit tests; UI change →
  instrumentation suite.
- Harmony change → host CTest + Hypium (+ signed-device install when available).
- iOS change → the relevant `FlyNESRuntimeTests` / `FlyNESUITests` suite on a
  simulator.
- Record dated evidence under `docs/verification/`, and state plainly what was
  verified versus merely compiled.
