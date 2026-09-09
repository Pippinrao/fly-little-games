# Nearby Multiplayer Implementation Progress

Updated: 2026-09-09. Branch: `codex/nearby-multiplayer`.
Worktree: `E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`.

## Baseline and ownership

- Base: committed cross-platform tree `88ddf5636c8149293a326f8f6699929e03f05502`.
- Approved nearby design: main commit `38953b3`, synchronized into this branch; implementation preparation commit `f3faa8e`.
- First implementation commit: `c51fcf9` (capability validation, deterministic plan selection and tests). Branch and worktree retained for subsequent slices; no merge or push performed.
- The approved specification has the same normalized Git blob in both branches: `c88683050f52cb72773917bb6c97573bdddae8af`; working-copy SHA-256 can differ because of Windows line endings.
- The other worktree's uncommitted app/catalog, Harmony, iOS and CMake changes were not copied or modified.
- Pinned Nestopia submodule initialized at `4470a2e99199d8010322eef4bf680fb3760f6eda`.
- Host build: MSVC 19.44 / Visual Studio 2022, CMake 3.22.1, Windows SDK 10.0.26100.0. Reuses the hash-checked read-only zlib 1.3.1 prefix in the main workspace's ignored .artifacts.
- Full shared baseline: 37/37 CTests passed, 16.10 seconds. Existing Nestopia narrowing/boolean warnings appeared during compilation.
- Runtime already supports local four-port stepping/checkpoints; session create/tick/snapshot exists but events/network processing are stubs. Do not treat the earlier M1 “frozen” label as full wire/schema or session qualification.

## Current slice

Canonical PairCapabilitySummaryV1 validation and deterministic exact certified plan intersection is implemented in shared/session/wire/pair_capability.hpp/.cpp, following the adjacent bearer-selection plan. It validates the exact 512-byte summary and sorted 48-byte entries, selects only identical plans by the designed priority, and clears output on failure. Parsing a capability is not authentication or proof of certification. Real radio operations require the authenticated plan lock and local certified profile gates.

Validation evidence:

- TDD RED: new test compiled, then linking failed with exactly the two missing validator/selector symbols (LNK2019/LNK1120).
- GREEN: new focused target passed with strict warnings; full host Release build succeeded.
- Parent independently reran all shared tests: 38/38 passed, 22.08 seconds, including runtime, catalog, session registry, codec, two-process loopback and the new capability target.
- Android NDK 27 / Clang 18, arm64-v8a, API26: flynes_session_codec static library compiled successfully with the new source. This is cross-compilation evidence, not an Android device test or complete APK build.
- Independent Python hashlib and reviewer .NET SHA-256 checks reproduce selected-plan digest ebce62bbf673da4052a6ad921a4d37cad38a90bf17cf5760f0d7f9931d4c07fe.
- A test run started while a full rebuild still held catalog_scan.exe and could not launch that process. The subsequent completed-build run and parent rerun both passed 38/38; no assertion regression was found. A leftover LastTestsFailed.log from that intermediate run is not the final result.
- Independent specification and code-quality reviews both passed without findings. The quality reviewer independently ran 38/38 host CTests, rebuilt the strict-warning capability target and reran its test successfully.

## Cross-task handoff

The approved design §30 is the single handoff contract. An app message to thread `01a06cd1-ea44-73d3-a497-a0308724fa55` was attempted on 2026-09-09. The tool returned “no longer available through dynamic tools”; delivery is **not confirmed**. No repeated messages were sent. Integration currently relies on the inspected committed tree and this documented ownership boundary.

This slice owns pair_capability.hpp/.cpp, test_pair_capability.cpp and its shared/CMakeLists target only. app/catalog and platform product work remain with the cross-platform task. No second shared hierarchy is introduced.

## Device gate

`adb devices -l` returned no attached device. `hdc` and `xcodebuild` are not on this Windows host's PATH. No phone radio, pairing, TLS/QUIC, H.264 or device latency result is claimed.

| Platform/authority direction | Bearer + one-confirmation + QUIC evidence |
|---|---|
| Android → Android | No device evidence |
| Harmony → Harmony | No device evidence |
| iOS → iOS | No device evidence |
| Android → iOS | No device evidence |
| iOS → Android | No device evidence |
| Android → Harmony | No device evidence |
| Harmony → Android | No device evidence |
| iOS → Harmony | No device evidence |
| Harmony → iOS | No device evidence |

For each actual run record both device models/OS versions, selected certified plan bytes/hash, creator/listener/authority/seat roles, system confirmation count, backend version, exact SPKI rejection test, TLS exporter/ChannelBind outcome, QUIC DATAGRAM delivery, and reconnect outcome. Publishing a support entry requires an actual successful run; unit-test fixtures must never populate a production support list.

## Known integration work before real netplay

- Audit the existing runtime's PCM pull: it currently uses the same blocking mutex as frame execution. The approved nonblocking real-time audio requirement still needs implementation/verification.
- The existing session ABI command transition ID is a uint64 field while the designed network transition ID is128 bits. Audit and version the internal/public boundary before wiring commands; do not truncate wire IDs.
- Existing generic fixed-object codec checks do not establish full semantic/child-graph/signature validity. Expand the single schema/codegen/golden gate before using any of those objects as authenticated executable recovery evidence.
- After the selection slice: authenticated plan lock and confirmation budget, session reducer, physical Connectivity/QUIC experiments, then media/input/recovery/downgrade. M0a/M5 remains a release gate.
