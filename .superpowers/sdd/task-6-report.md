# Task 6 Report: Harmony N-API product bridge

## Status

**DONE**

## Summary

Host-tested C++ helper `flynes::harmony::{game_center_filter,control_layout_recommended,control_layout_decode_or_recommended,hit_map_from_layout,pause_commands}` wraps Tasks 1–5 (`flynes_product`). N-API in `napi_init.cpp` is a thin export of those helpers. Existing `playOpen`/`playStep` (and pause-autosave `playSaveCheckpoint`) remain. No Game Center UI.

## Commits

| SHA | Subject |
|-----|---------|
| `a3a9755` | feat(harmony): N-API product bridge over shared contract |

## Files changed

| File | Action |
|------|--------|
| `harmony/entry/src/main/cpp/product_bridge.hpp` | Created — host-testable DTOs and helper API |
| `harmony/entry/src/main/cpp/product_bridge.cpp` | Created — calls `flynes_product` only |
| `harmony/tests/product_bridge_test.cpp` | Created — 魂斗→contra, pause ids, `v1\|JOY`, hit-map geometry |
| `harmony/tests/CMakeLists.txt` | Modified — `flynes_harmony_product_bridge` ctest target |
| `harmony/entry/src/main/cpp/CMakeLists.txt` | Modified — compile `product_bridge.cpp`, link `flynes_product` |
| `harmony/entry/src/main/cpp/napi_init.cpp` | Modified — N-API wrap + keep play* |
| `harmony/entry/src/main/cpp/types/libentry/Index.d.ts` | Modified — product TS exports |
| `harmony/entry/src/main/cpp/play_session.cpp` | Included (already referenced by `napi_init.cpp`) |
| `harmony/entry/src/main/cpp/play_session.hpp` | Included |
| `harmony/tests/play_session_test.cpp` | Included (already referenced by host cmake) |
| `harmony/entry/src/main/cpp/types/libentry/oh-package.json5` | Included with play-session types |

## TDD Evidence

### RED — test before helper existed

Command:

```powershell
$cmake = 'D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$env:FLYNES_ZLIB_ROOT = 'E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port\.artifacts\host-deps\zlib-1.3.1-install'
& $cmake -S harmony/tests -B out/harmony-host -G "Visual Studio 17 2022" -A x64 "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT"
```

Output (expected missing helper):

```text
CMake Error at CMakeLists.txt:117 (add_executable):
  Cannot find source file:

    ../entry/src/main/cpp/product_bridge.cpp

CMake Error at CMakeLists.txt:117 (add_executable):
  No SOURCES given to target: flynes_harmony_product_bridge_test

CMake Generate step failed.  Build files cannot be regenerated correctly.
CMAKE CONFIG EXIT: 1
```

The host test target existed first and failed specifically because the helper source had not been created.

### GREEN — helper implemented, ctest PASS

Command:

```powershell
$cmake = 'D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctest = 'D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
$env:FLYNES_ZLIB_ROOT = 'E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port\.artifacts\host-deps\zlib-1.3.1-install'
& $cmake -S harmony/tests -B out/harmony-host -G "Visual Studio 17 2022" -A x64 "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT"
& $cmake --build out/harmony-host --config Release --target flynes_harmony_product_bridge_test
& $ctest --test-dir out/harmony-host -C Release -R product_bridge --output-on-failure
```

Output:

```text
flynes_harmony_product_bridge_test.vcxproj -> ...\out\harmony-host\Release\flynes_harmony_product_bridge_test.exe
BUILD EXIT: 0

Test project E:/workspace/codes/games/fly-little-games/.worktrees/ios-harmony-port/out/harmony-host
    Start 5: flynes_harmony_product_bridge
1/1 Test #5: flynes_harmony_product_bridge ....   Passed    0.03 sec

100% tests passed, 0 tests failed out of 1
```

Direct binary:

```text
PASS: Harmony product bridge contract
EXE EXIT: 0
```

## TS / helper contract

| Export | Behavior |
|--------|----------|
| `gameCenterFilter` | Same Task 1 items; category `ALL` + query `魂斗` → `contra` |
| `controlLayoutRecommended` | `ControlLayoutV2::recommended().encode()` |
| `controlLayoutDecodeOrRecommended("v1\|JOY")` | Equals recommended encode |
| `hitMapFromLayout` | D-pad bounds + each control center/size/shape; no `PAUSE` |
| `pauseCommands` | `["resume","game_center","settings"]` |
| `playOpen` / `playStep` | Kept for RunGame |
| `playSaveCheckpoint` | Remains for pause autosave only |

`HitMapDto` also forwards `directionMode`, `deadZone`, `joystickRadius`, and `joystickTravelRadius` so ArkTS can draw without reimplementing hit-map math.

Direction mode ints: `0` Joystick, `1` FixedJoystick, `2` DPad.

## Self-review

| Check | Result |
|-------|--------|
| TDD: RED missing `product_bridge.cpp`, then GREEN ctest | Pass |
| Host helper, not ArkTS UI (Task 7 untouched) | Pass |
| Pause ids snake_case exactly as brief | Pass |
| No new HUD Save/Load product APIs | Pass |
| `playOpen`/`playStep` kept | Pass |
| `HitMapDto` has dpad bounds + control center/size/shape | Pass |
| PAUSE not placed by hit map | Pass |
| Filter vector matches Task 1 items | Pass |
| `decode_or_recommended("v1\|JOY")` equals recommended encode | Pass |
| `flynes_product` linked from entry CMakeLists | Pass |
| No extra TS function exports beyond brief + existing play* | Pass |
| No Index.ets HUD, rawfile ROM, HAP, or `.gradle` | Pass |
| `play_session.cpp/hpp` included so `napi_init.cpp` still builds | Pass (allowed) |

N-API wrappers are not compiled on the Windows host (Harmony `napi/native_api.h`). That is the plan: host-test the helper; N-API is a thin wrap.

## Concerns

None that block Task 6. N-API compile is Harmony-SDK-only; overlay/UI consume this bridge in later tasks.

## Fix: drop public playLoadCheckpoint export

**Finding:** Important review — `playLoadCheckpoint` was exported in `Index.d.ts` and `napi_init.cpp` Init descriptors, violating the brief (HUD load is not a product API; `playSaveCheckpoint` remains for pause autosave).

### TDD

**RED** — added `test_public_surface_does_not_export_play_load_checkpoint()` in `harmony/tests/product_bridge_test.cpp`; failed while export still present:

```text
FAIL: Index.d.ts must not export playLoadCheckpoint
FAIL: napi_init.cpp must not register playLoadCheckpoint
2 failure(s)
```

**GREEN** — removed `playLoadCheckpoint` from `Index.d.ts`, removed `PlayLoadCheckpoint` N-API wrapper and Init descriptor from `napi_init.cpp` (kept `play_session` internals). Set ctest `WORKING_DIRECTORY` for product-bridge test in `harmony/tests/CMakeLists.txt`.

### Covering tests

- `harmony/tests/product_bridge_test.cpp` — contract tests + public-surface guard (no `playLoadCheckpoint` in `Index.d.ts` / `napi_init.cpp`)

### Command

```powershell
$env:FLYNES_ZLIB_ROOT = "E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port\.artifacts\host-deps\zlib-1.3.1-install"
cmake --build out/harmony-host --config Release
ctest --test-dir out/harmony-host -C Release -R product_bridge --output-on-failure
```

### Output

```text
1/1 Test #5: flynes_harmony_product_bridge ....   Passed    0.01 sec

100% tests passed, 0 tests failed out of 1
```
