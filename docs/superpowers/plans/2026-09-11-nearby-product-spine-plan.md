# Nearby multiplayer — product spine continuation plan

Date: 2026-09-11. Branch `codex/nearby-multiplayer`. Worktree `E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`.

Supersedes the "what next" advice in `docs/handoffs/2026-09-11-nearby-multiplayer-handoff.md` §8 only in ordering and granularity;
that document's product decisions, evidence boundary and prohibitions remain authoritative.
Approved design (unchanged): `docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md`, blob `c88683050f52cb72773917bb6c97573bdddae8af`.

Baseline re-verified on 2026-09-11 (this plan's first round):

- Host build `cmake --build .artifacts/nearby-host --config Release` exit 0; **41/41 CTests passed, 14.07s**. A second, independent clean configure into `.artifacts/build/shared-host` also gave **41/41 passed, 13.71s**.
- `cmake`/`ctest` are NOT on PATH. Use `C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\{cmake,ctest}.exe`.
- **Environment quirk:** MSBuild fails with `MSB6001 … 字典中的关键字:"NO_PROXY"所添加的关键字:"no_proxy"` because this shell exports case-variant duplicates (`HTTP_PROXY`/`http_proxy`/`https_proxy`, `NO_PROXY`/`no_proxy`). Remove the lower-case duplicates before any `cmake --build`:

```powershell
$keys = [Environment]::GetEnvironmentVariables('Process').Keys
$keys | Group-Object { $_.ToUpperInvariant() } | Where-Object { $_.Count -gt 1 } |
  ForEach-Object { $_.Group | Select-Object -Skip 1 | ForEach-Object { Remove-Item "Env:$_" -ErrorAction SilentlyContinue } }
```
- No device attached: `adb devices` empty, `D:\soft\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe list targets` = `[Empty]`.
- Harmony probe signing still absent: `.artifacts/nearby-quic-harmony-app/entry/build/default/outputs/default/` holds only `entry-default-unsigned.hap`.

## Verified completion snapshot

| Milestone (design §26) | State | Evidence |
|---|---|---|
| M0a disposable vertical slice | Partial. Only Android↔Windows QUIC positive + wrong-SPKI negative. No phone↔phone, no H.264, no secure identity storage. | `docs/acceptance/2026-09-10-nearby-quic-spike.md` |
| M0b runtime | `flynes_runtime.h` frozen; four-port bundle, checkpoint, 12-slot rollback ring exist. | `shared/tests/test_runtime.cpp` |
| M1 session | schema/codec/goldens real; **public session path is stubs**; no product QUIC contract; no fuzz. | see "Session public path" below |
| M2 platform adapters | Host-only fakes + platform-local DTOs. No radio, no Keystore/Keychain/HUKS. | `docs/superpowers/specs/m2-m5-status.md` |
| M3 HOST_STREAM | Not implemented (encoder command returns `UNSUPPORTED_VERSION`). | `docs/superpowers/specs/m2-m5-status.md:45-50` |
| M4 DUAL | Runtime rollback ring only; no input exchange/digest/resync/downgrade transaction. | `docs/superpowers/specs/m2-m5-status.md:52-58` |
| M5 device certification | Not started. | `docs/superpowers/specs/m2-m5-status.md:60-64` |
| Product UI spine | **Zero on all three platforms.** | below |

## Verified product-UI insertion points

| Platform | Game-center host | Insertion point | New pages must be registered |
|---|---|---|---|
| Android | `app/src/main/java/com/flynes/emu/HomeActivity.java:54,89` | `app/src/main/res/layout/activity_home.xml:3-4`; nav wiring `HomeActivity.java:172` | `app/src/main/AndroidManifest.xml:21-55` |
| Harmony | `harmony/entry/src/main/ets/pages/GameCenter.ets:9`, `build()` `:33` | `@Builder navButtons()` `:154-168` | `harmony/entry/src/main/resources/base/profile/main_pages.json:2-9` |
| iOS | `ios/app/CatalogLibraryView.swift:29` | `ToolbarItemGroup(.topBarTrailing)` `:70-81`; `enum LibraryRoute` `:3-6`; `navigationDestination` `:83-90` | add new `.swift` to `ios/app/CMakeLists.txt:41-52` (no Xcode project in tree; CMake requires `-G Xcode`) |

Current permissions are insufficient for any nearby path: Android manifest declares **VIBRATE only**; Harmony `module.json5:36-47` declares **FILE_ACCESS_PERSIST only**; iOS `ios/app/Info.plist.in` has no Bluetooth/Camera/LocalNetwork keys and no `NSBonjourServices`.

All existing nearby code is unreachable dead code: three DTO headers (`app/src/main/cpp/nearby/nearby_dtos.hpp`, `ios/app/platform/nearby/nearby_dtos.hpp`, `harmony/nearby/nearby_dtos.hpp`) plus `harmony/nearby/{nearby_adapter,friend_store}` compiled **only** into `harmony/tests/CMakeLists.txt:141-144` and absent from `harmony/entry/src/main/cpp/CMakeLists.txt`. `flynes_session.h` is referenced by no shipping platform code.

## Session public path — exact current state

`shared/src/session/flynes_session.cpp` — `fly_session_t` (`:9-16`) owns only `last_tick_ns`, `has_tick`, `pair_generation`, `original_context_start_ns` and `initial_plan` (by value). No event/command queue, no codec state, no seat/mode/authority/media state.

| Function | Line | Today |
|---|---|---|
| `fly_session_create` / `destroy` | 151 / 180 | Implemented |
| `fly_session_submit_event` | 185 | Stub — envelope validated, then `return FLY_RESULT_INVALID_STATE;` (`:208`) |
| `fly_session_receive_stream` | 211 | Stub — `return FLY_RESULT_INVALID_STATE;` (`:224`); bytes never reach `wire::check` |
| `fly_session_receive_datagram` | 227 | Delegates to the stream stub (`:232`) |
| `fly_session_poll_command` | 235 | Shape-only — always `kind = FLY_SESSION_COMMAND_NONE` (`:255`), never calls `initial_plan.poll()` |
| `fly_session_complete_command` | 259 | Stub — `return FLY_RESULT_INVALID_STATE;` (`:275`) |
| `fly_session_tick` | 278 | Implemented (monotonic + 60 s attempt deadline) |
| `fly_session_get_snapshot` | 301 | Hardcoded `UI_IDLE` + genesis cursor (`:321-323`) |

The private seam `shared/src/session/session_initial_plan.hpp:40-51` (10 free functions) is **fully implemented** in `flynes_session.cpp:39-102`, but its only callers are tests. The reducer is complete and unreachable from the public path.

`shared/src/session/wire/` is real: `session_codec.cpp` validates + domain-hashes ~30 kinds by string name (`:279-384`); `pair_capability.cpp` is real. Neither decodes into DTOs, and nothing routes `fly_session_receive_*` into the codec.

Tests currently **lock in the stubs**: `shared/tests/test_session.cpp:46` (UI_IDLE), `:55` (COMMAND_NONE), `:62,66-70,78` (INVALID_STATE). Wiring the public path requires deliberately updating these.

Header gap vs design §12 (the product UI cannot be honestly built without these): only `FLY_SESSION_COMMAND_NONE` and `FLY_SESSION_UI_IDLE` exist; no lifecycle states or FROZEN/UNCERTAIN/DOWNGRADE_REQUIRED/RECOVERY_LOCKED flags (§2 table, spec:200-238); `fly_session_event` has no payload (spec:712); no real command kinds or cancel token (spec:714, 221); no `FLY_SESSION_MODE_*`, seat/confirmation/quality fields (spec:717); no media/mute; no friend identity; no side-effect-free size query (spec:525). `transition_id` is `uint64_t` (`flynes_session.h:126,139`) while the wire transition ID is 16 bytes (spec:470, 1021, 1050, 1058).

Shared session tests are **not in CI**: `scripts/ci-check.ps1:228-250` and `.github/workflows/stage0.yml:28-30` configure `core` only. Hypium/ohosTest is absent repo-wide.

## Slice order

Ordering rule: only slices that produce verifiable progress without a device come first; everything device-gated is named as such and not started.

### C1 — probe script preference leak — DONE

Commit `59d633f`. `build-mobile.ps1` no longer leaves the caller's `$ErrorActionPreference` at `Stop`; regression assertion added to `tools/nearby-quic-spike/tests/test_harmony_runner.py:55-59`. RED observed (exit 8), GREEN observed (6/6 Python tests).

### C2 — session public path (shared, offline) — NEXT

Scope, in this order:

1. Route `fly_session_receive_stream` / `_datagram` into `wire::check` for the declared channels; on codec success produce the corresponding internal reducer event, on failure keep failing closed. Unauthenticated content must still be rejected: a validated-and-hashed envelope is not authentication (design §12.6, spec:713).
2. Bridge `fly_session_poll_command` → `poll_initial_plan_command` and `fly_session_complete_command` → `complete_initial_plan_command`, mapping the local monotonically increasing command id; keep the 64-bit public `transition_id` explicitly unused/zero rather than truncating a wire id.
3. Project the real snapshot in `fly_session_get_snapshot` from `initial_plan_snapshot()` instead of the hardcoded `UI_IDLE`.
4. Preserve every public struct size. Any needed ABI addition is versioned through `struct_size` / `abi_version` per §12.1 (spec:523-528) — never by changing an existing field's width or meaning.
5. Update the stub-locking assertions in `shared/tests/test_session.cpp` deliberately; add new behavioural tests. TDD: show the failing assertion before the fix.

Acceptance: host Release build + full CTest green (≥41 tests, no test deleted); RED→GREEN recorded; Android arm64 and Harmony arm64 cross-compile of `flynes_session` still succeed; public struct sizes unchanged or version-gated.

### C3 — CI coverage for the shared session suite — DONE

`scripts/ci-check.ps1` previously built only `core`, so the shared session/ABI suite could regress silently. Added **Check 4/5 "shared session host test"**: it reuses the canonical toolchain that Check 3 already bootstraps (`$hostTools` / `$hostCMake` / `$hostCTest` / `$hostZlibRoot`, so no extra bootstrap and no extra network), configures `shared` with `FLYNES_BUILD_TESTS=ON` into `.artifacts/build/shared-host`, builds it and requires `100% tests passed`. The Android build was renumbered to Check 5/5.

Verified 2026-09-11: a fresh configure of `shared` with the canonical generator/toolset (`Visual Studio 17 2022`, x64, `ZLIB_ROOT=<pinned zlib 1.3.1 install>`, `TrackFileAccess=false`, `CMAKE_TRY_COMPILE_CONFIGURATION=Release`) succeeded, the full build succeeded, and **41/41 CTests passed in 13.71s** from that clean directory. Script parse check: 0 errors.

Not verified end-to-end: the whole `ci-check.ps1` gate was not run, because Check 3 first bootstraps the pinned zlib/toolchain into `.artifacts/host-deps` of this worktree (that directory is currently absent; the worktree build dirs point `ZLIB_ROOT` at the main checkout's `.artifacts/host-deps/zlib-1.3.1-install`). The verification above substituted that same zlib install, so the only unexercised link is Check 3's pre-existing bootstrap step.

### A1 — three-platform entry + friends/nearby + pairing + lobby + in-game status pages

Decomposed per platform so the three can proceed independently: A1a Android, A1b Harmony, A1c iOS.

Each platform slice delivers: the "附近联机" entry on the game-center screen; pages for 好友/附近设备, 配对, 大厅, 游戏中状态; and the permission/Info.plist declarations the path needs. Every page must render the **actual** session/platform state, and every unimplemented capability must display the exact blocking stage with its reason (design §22.2 last bullet: 权限/发现/认证/Wi-Fi/QUIC/版本/codec) — no fake friends, no fake connections, no navigate-only buttons (handoff §8.3).

Acceptance per platform: that platform's build command succeeds and the new pages are registered in its navigation mechanism; all new user-visible strings exist in that platform's i18n resources.

### Device-gated (do not start until devices are attached and Harmony signing is configured)

- M0a phone↔phone QUIC (needs 2 phones + signed `com.flynes.nearbyprobe`).
- M2 real BLE/QR/Wi-Fi bearer + Keystore/Keychain/HUKS.
- M3 HOST_STREAM, M4 DUAL, M5 nine-direction matrix, p95 latency gates (§25.3).

## Rules carried forward

- No merge, no push, no force-clean. Preserve `harmony/build-profile.json5`, `harmony/entry/oh-package-lock.json5`, `harmony/.clang-tidy`, `harmony/.clangd` and `docs/acceptance/2026-09-09-nearby-device-audit.md` exactly as found.
- Do not weaken the approved design; ABI conflicts go back to §30 review instead of being frozen unilaterally.
- Do not advertise 三端联机首版 while M5 is unpassed.
