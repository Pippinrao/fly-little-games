# Task 12 Report: iOS same contract

## Status

**DONE_WITH_CONCERNS**

## Summary

The iOS product shell now consumes the shared Android contract that Harmony already uses: Game Center four categories, overlay geometry from `flynes::product::GamepadHitMap` + `ControlLayoutV2`, pause drawer exactly Resume / Game Center / Settings via `kPauseDrawerCommands`, five settings sections in Android order, and layout persist through `fly_control_layout_get/apply`. Bookmarks stay a platform UUID to bookmark map. This Windows host verified python contracts and `flynes_product` ctest only — not simulator/device parity.

## Commits

| SHA | Subject |
|-----|---------|
| `54015f7` | feat(ios): consume shared product contract for overlay and pause |
| `d031c8c` | fix(ios): pause returns to Game Center and layout drag uses stage space |
| `7b81704` | fix(ios): shared Game Center filter and paused Settings overlay |

## Files changed

| File | Action |
|------|--------|
| `ios/tests/test_product_android_parity_contract.py` | Created — Android/Harmony parity file gates |
| `ios/tests/test_product_shell_contract.py` | Modified — REQUIRED editor; Settings from Game Center; `flynes_product` link |
| `ios/tests/CMakeLists.txt` | Modified — register parity contract ctest |
| `ios/app/CMakeLists.txt` | Modified — link `flynes_product`; compile layout editor |
| `ios/app/run/GamepadOverlayView.mm` | Modified — place/hit via `GamepadHitMap::from_layout` |
| `ios/app/run/GamepadOverlayView.h` | Modified — `layoutUtf8`, `deadZone` |
| `ios/app/run/RunSurfaceViewController.mm` | Modified — `OPEN_PAUSE` + `kPauseDrawerCommands` drawer |
| `ios/app/run/RunSurfaceViewController.h` | Modified — `onPauseCommand` |
| `ios/app/SettingsView.swift` | Modified — five `section.*` roots in Android order |
| `ios/app/ControlLayoutEditorView.swift` | Created — D_PAD/A/B/SELECT/START editor to `controlLayoutApply` |
| `ios/app/CatalogLibraryView.swift` | Modified — Game Center IA, four filters, no extra tabs |
| `ios/app/FlyNESApp.swift` | Modified — cold start Game Center only (no Settings tab) |
| `ios/app/RunGameView.swift` | Modified — pause Game Center / Settings routing |
| `ios/app/CatalogGameDetailView.swift` | Modified — host `RunGameContainer` |
| `ios/app/bridge/FlyNesAppBridge.h/.mm` | Modified — `controlLayoutGet` / `controlLayoutApply` |
| `ios/app/platform/FlyNesBookmarkStore.h/.mm` | Modified — UUID to bookmark map (no FLYCAT01) |
| `ios/app/en.lproj/Localizable.strings` | Modified — Game Center / section / pause strings |
| `ios/app/zh-Hans.lproj/Localizable.strings` | Modified — zh labels |
| `ios/app/Info.plist.in` | Modified — landscape-first orientations |

## TDD Evidence

### RED

Command (repo root, before overlay/product wiring):

```powershell
python ios/tests/test_product_android_parity_contract.py
```

Output (expected fail: overlay still used local CGRect placement, not `GamepadHitMap`):

```text
flynes_ios_product_android_parity_contract: FAIL: overlay must place controls via GamepadHitMap / flynes::product
EXIT:1
```

Exit code: 1. Failed because `GamepadOverlayView.mm` did not reference `GamepadHitMap` / `flynes::product`.

### GREEN

Command (repo root, after implementation):

```powershell
python ios/tests/test_product_android_parity_contract.py
python ios/tests/test_product_shell_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: PASS
flynes_ios_product_shell_contract: PASS
```

Exit codes: 0 / 0.

Shared product ctest (`ZLIB_ROOT` already configured into `out/shared`):

```powershell
D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/shared -C Release -R flynes_product --output-on-failure
```

Output:

```text
    Start 3: flynes_product_game_center .......   Passed
    Start 4: flynes_product_control_layout ....   Passed
    Start 5: flynes_product_gamepad_hit_map ...   Passed
    Start 6: flynes_product_pause_actions .....   Passed
100% tests passed, 0 tests failed out of 4
```

## Self-review

- Overlay placement and hits go through `GamepadHitMap::from_layout` + `ControlLayoutV2::decode_or_recommended`. NES START is `Control::Start` / `NES_START` only; `OPEN_PAUSE` lives on a separate pause button in the run controller.
- Pause drawer iterates `flynes::product::kPauseDrawerCommands` (resume / game_center / settings). No HUD Save/Load.
- Settings section keys appear in Android order: `section.display`, `section.controls`, `section.audio`, `section.game_language`, `section.about`. Layout editor writes the v2 wire string via `fly_control_layout_apply`.
- Bookmark store is UUID string to `NSData` bookmark in `NSUserDefaults`; FLYCAT01 is not referenced there.
- `ios/app/CMakeLists.txt` links `flynes_product`. Mach-O/IPA still CI-only on this Windows host.
- Cold start is Game Center (`CatalogLibraryView`) with four category filters and no bottom Settings tab.

## Concerns

1. **No simulator/device checklist.** Host python + `flynes_product` ctest only. Do not claim iOS product parity with Android until a Task-10-style simulator/device pass exists.
2. **Catalog has no separate zh title field.** iOS follows Harmony: `title_en` and `original_filename` come from display name (else relative-path basename); `title_zh_hans` is empty until catalog grows a zh title.
3. **GitHub Actions `ios-product.yml` does not run the new parity contract.** Commit scope was `ios/app` and `ios/tests` only. CI will keep running the older host contracts until the workflow is updated.
4. **Stage-1 `test_stage1_contract.py` currently fails** (`flynes_app` symbol baseline vs public header, including `fly_control_layout_*`). That mismatch predates this task (Task 5 ABI) and was not modified here.

## Verification not claimed

- iOS Simulator or device operation checklist (landscape overlay, START not pause, layout editor moves A, and so on)
- Xcode/Mach-O/IPA build on this Windows host

## Review fix (Important, Task 12)

Three host contract gaps: pause Game Center used `dismiss()` (Library → Detail → Run, so 游戏中心 landed on detail); layout drag used control-local `DragGesture.location`; pause `UITapGestureRecognizer` sat on `pauseLayer_` which also hosts the drawer.

### RED

Command (repo root, before the three source fixes):

```powershell
python ios/tests/test_product_android_parity_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: FAIL: Game Center must own navigationDestination so pause can pop Library → Detail → Run; pause Game Center must pop to the library root, not dismiss() one NavigationLink; pause Game Center must not dismiss() a single NavigationLink; Play must push run as a path value, not a nested NavigationLink destination; layout drag must use named coordinateSpace on the stage or translation from the start normalized point; DragGesture.location without a named stage space is the control's local point; pause tap must use a sibling scrim beside the drawer, or shouldReceiveTouch only when touch.view is the scrim; pause tap must not be installed on pauseLayer_; that layer includes the drawer
```

Exit code: 1.

### GREEN

Command (repo root):

```powershell
python ios/tests/test_product_android_parity_contract.py
python ios/tests/test_product_shell_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: PASS
flynes_ios_product_shell_contract: PASS
```

Exit codes: 0 / 0.

```powershell
D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/shared -C Release -R flynes_product --output-on-failure
```

Output:

```text
    Start 3: flynes_product_game_center .......   Passed
    Start 4: flynes_product_control_layout ....   Passed
    Start 5: flynes_product_gamepad_hit_map ...   Passed
    Start 6: flynes_product_pause_actions .....   Passed
100% tests passed, 0 tests failed out of 4
```

### Code

- `CatalogLibraryView` owns `NavigationPath` + `navigationDestination(for: LibraryRoute.self)`. Pause Game Center sets `path = NavigationPath()` (library root). Play is `NavigationLink(value: LibraryRoute.run)`.
- Layout editor drag uses `.coordinateSpace(name: "layoutStage")` and `DragGesture(coordinateSpace: .named("layoutStage"))`.
- Pause tap is on a sibling `UIView *scrim` constrained beside the drawer, not on `pauseLayer_`.
- No HUD Save/Load. No simulator/device parity claim.

## Review fix (Important, Task 12 — shared filter + paused Settings)

Pause Settings no longer dismisses the drawer while leaving `paused_ == YES` with a live overlay. Game Center filter/search now maps catalog rows into `GameCenterItem` and calls `GameCenterState::filtered` (Harmony mapping: `title_en` / empty `title_zh_hans` / `original_filename`). No HUD Save/Load. No device parity claim.

### RED

Command (repo root, gates added before source fixes):

```powershell
python ios/tests/test_product_android_parity_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: FAIL: iOS must filter Game Center through flynes::product::GameCenterState, not a Swift fork
EXIT:1
```

Exit code: 1. Failed because `FlyNesAppBridge` did not use `GameCenterState` / `GameCenterItem` / `filtered`. Additional gates (not reached on this run) require pause Settings to keep or reopen the drawer and `reloadProductSettings` when the settings sheet closes (`viewWillAppear` does not fire).

### GREEN

Command (repo root):

```powershell
python ios/tests/test_product_android_parity_contract.py
python ios/tests/test_product_shell_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: PASS
flynes_ios_product_shell_contract: PASS
```

Exit codes: 0 / 0.

```powershell
D:\vs2022\ide\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe --test-dir out/shared -C Release -R flynes_product --output-on-failure
```

Output:

```text
    Start 3: flynes_product_game_center .......   Passed
    Start 4: flynes_product_control_layout ....   Passed
    Start 5: flynes_product_gamepad_hit_map ...   Passed
    Start 6: flynes_product_pause_actions .....   Passed
100% tests passed, 0 tests failed out of 4
```

### Code

- `FlyNesAppBridge.gameCenterFilteredGamesForCategory:query:` maps catalog rows to `GameCenterItem` and calls `GameCenterState::filtered` / `items_for` (via `filtered`). Categories remain RECENT / FAVORITES / ALL / BUILTIN.
- `CatalogLibraryView` displays that helper result; Swift no longer filters on `displayName` / `canonicalId`.
- Pause Settings keeps the drawer up under the sheet; session stays paused. Sheet `onDismiss` increments a generation so `RunGameView` calls `reloadProductSettings` (layout Apply reaches the overlay without `viewWillAppear`).
- No HUD Save/Load. No simulator/device parity claim.

Commit: `7b81704` `fix(ios): shared Game Center filter and paused Settings overlay`

## Review fix (Important, Task 12 — pause auto-checkpoint)

Opening the pause drawer now stops stepping, calls `FlyNesRuntimeBridge.saveCheckpoint` (`fly_runtime_save_checkpoint`), and shows localized `pause.checkpoint_failed` when save fails. Resume and Game Center still proceed (Harmony parity). No HUD Save/Load. No device parity claim.

### RED

Command (repo root, gates added before source fixes):

```powershell
python ios/tests/test_product_android_parity_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: FAIL: runtime bridge must expose saveCheckpoint
EXIT:1
```

Exit code: 1. Failed because `openPauseDrawer` did not call checkpoint save and `FlyNesRuntimeBridge` had no `saveCheckpoint`.

### GREEN

Command (repo root):

```powershell
python ios/tests/test_product_android_parity_contract.py
python ios/tests/test_product_shell_contract.py
```

Output:

```text
flynes_ios_product_android_parity_contract: PASS
flynes_ios_product_shell_contract: PASS
```

Exit codes: 0 / 0.

### Code

- `FlyNesRuntimeBridge.saveCheckpoint` wraps the two-pass `fly_runtime_save_checkpoint` sizing call (same pattern as Harmony `PlaySession::save_checkpoint`).
- `RunSurfaceViewController.openPauseDrawer` steps with buttons 0, saves checkpoint, and adds a drawer label with `accessibilityIdentifier` `pause_checkpoint_failed` when save fails.
- Localized strings: `pause.checkpoint_failed` (en/zh-Hans). No HUD Save/Load.
