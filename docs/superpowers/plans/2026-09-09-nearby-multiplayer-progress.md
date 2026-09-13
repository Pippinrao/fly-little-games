# Nearby Multiplayer Implementation Progress

Updated: 2026-09-11. Branch: `codex/nearby-multiplayer`.
Worktree: `E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`.

## Latest: real transport experiment (2026-09-10)

Earlier device-absence notes below are historical. Both Android and Harmony now appear in ADB/HDC; Android briefly went offline and recovered after a targeted reconnect. Both phones answered local-network ICMP, which is not gameplay evidence.

`887852a` adds the standalone Quinn/rustls M0a candidate under `tools/nearby-quic-spike/`. Parent actually ran Android as QUIC listener and Windows as connector on the existing Wi-Fi: both directions' streams and DATAGRAM verified, exact public SPKI pin callback executed, matching exporter digests, both exits zero. A second valid-but-wrong SPKI failed during TLS on both ends, with no VERIFIED result. Windows-listener reverse-role experiment timed out and is not recorded as successful; no firewall/security change was made. Exact evidence and executable hash are in `docs/acceptance/2026-09-10-nearby-quic-spike.md`.

`ca8c711` fixes an independently reviewed completion-versus-retry race. Parent and reviewers reran all 18 probe tests, and the spec/quality follow-ups passed. Existing unchanged shared build's 41 CTests also passed again (14.98s). Android and OHOS native probe builds/linking succeeded using their respective SDKs; this does not prove Harmony execution.

`63dc22b` implements the isolated Harmony runner and actual native bridge; `b1a4487` fixes latest-Want precedence. Fresh parent verification passed 20 Rust tests, 6 Python tests, 5 actual TypeScript tests, fmt/clippy and both mobile builds. The actual unsigned HAP built. Updated Android binary was deployed to its owned temporary path and physically retested against Windows with matching exporter digests and zero exits. Evidence and hashes are recorded in the acceptance document.

The owner reported automatic signing completed on 2026-09-11; rebuilding the isolated `.artifacts/nearby-quic-harmony-app` still reports no default-product signing configuration and produces no signed HAP. The owner was asked to verify that exact project rather than the product `harmony` directory. Preserve installed `com.flynes.emu` and use `com.flynes.nearbyprobe`. No phone-to-phone QUIC, production handshake, input, frame, PCM or playable multiplayer result is claimed. Approved design/schema are unchanged; unrelated product/signing edits in the worktree are preserved untouched. **Do not stop subsequent work merely because these component/probe tests pass.**

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

## Initial capability slice

Canonical PairCapabilitySummaryV1 validation and deterministic exact certified plan intersection is implemented in shared/session/wire/pair_capability.hpp/.cpp, following the adjacent bearer-selection plan. It validates the exact 512-byte summary and sorted 48-byte entries, selects only identical plans by the designed priority, and clears output on failure. Parsing a capability is not authentication or proof of certification. Real radio operations require the authenticated plan lock and local certified profile gates.

Validation evidence:

- TDD RED: new test compiled, then linking failed with exactly the two missing validator/selector symbols (LNK2019/LNK1120).
- GREEN: new focused target passed with strict warnings; full host Release build succeeded.
- Parent independently reran all shared tests: 38/38 passed, 22.08 seconds, including runtime, catalog, session registry, codec, two-process loopback and the new capability target.
- Android NDK 27 / Clang 18, arm64-v8a, API26: flynes_session_codec static library compiled successfully with the new source. This is cross-compilation evidence, not an Android device test or complete APK build.
- Independent Python hashlib and reviewer .NET SHA-256 checks reproduce selected-plan digest ebce62bbf673da4052a6ad921a4d37cad38a90bf17cf5760f0d7f9931d4c07fe.
- A test run started while a full rebuild still held catalog_scan.exe and could not launch that process. The subsequent completed-build run and parent rerun both passed 38/38; no assertion regression was found. A leftover LastTestsFailed.log from that intermediate run is not the final result.
- Independent specification and code-quality reviews both passed without findings. The quality reviewer independently ran 38/38 host CTests, rebuilt the strict-warning capability target and reran its test successfully.

## Continued implementation: initial plan locking

Commit `e1ea62d` adds the internal noncopyable `InitialPlanLock` reducer and its test target. This is trusted-evidence state control, **not an authenticated decoder or a radio backend**. It independently selects the exact common plan, orders PLAN/ACK/FINAL persistence and generation-fenced sends, and emits at most one pending effect. Creator and noncreator paths are separate. Designated system-prompt allowance must be durably consumed before its bearer action. Creator credentials must resolve to exact bytes that are persisted before the corresponding publish command; a hash alone does not satisfy storage success.

Evidence: behavioral RED→GREEN for initialization, exact creator credentials and noncopyable ownership; role/creator/confirmation combinations and every command failure exercised. Independent specification review passed. Independent quality review's C++17 transitive compilation requirement was corrected and re-reviewed. Parent completed the full host build then ran 39/39 tests successfully (16.58s). Android API26/arm64 and Harmony arm64 reducer libraries compiled; Harmony's SDK emits an unused `--gcc-toolchain` warning. Existing Harmony host adapter regression and iOS schema-contract Python check passed. These do not establish iOS native compilation or phone-to-phone interoperability.

Commit `b58851c` connects that reducer to each actual opaque session handle through the private `session_initial_plan.hpp` seam. The original route-defined pairing start and local GATT generation are latched once; late capability verification cannot extend the 60-second deadline. Expiry, backwards active clock, cancellation, stale completions and failed storage revoke pending effects permanently for that initial attempt. Historical durable flags remain diagnostic, never permission to act after failure. Public C ABI struct layouts and raw/event/command/snapshot stubs remain unchanged. Callers must serialize access, tick before callbacks/dispatch, retain handles until callbacks drain, and deduplicate/revalidate commands.

Integration evidence: behavioral RED then focused GREEN; parent independently ran reducer/session/integration tests 3/3 and Harmony host adapter regression 1/1; actual session libraries cross-compiled for Android/Harmony arm64. Independent spec and quality reviews passed after adding exact credential identity assertions to the integration test.

## PCM contention fix and final continuation verification

Commit `5e86fcf` removes PCM pull's blocking wait on the core mutex. A failed single try-lock uses the existing silence fallback without touching protected PCM state; a successful acquisition retains normal sample and sequence behavior. The regression includes the real implementation and emulator dependency, holds the actual mutex on an owner thread and calls the public API on a callback thread. The old code failed safely after 1.56s; the fix preserved the queued nonzero samples and cursors and passed the subsequent partial-drain checks. This test is not a latency benchmark. API layout, mute policy, canonical producer sequence and normal partial-output semantics did not change.

Final parent verification after all three code changes:

- Normal full Windows Release build succeeded; then **41/41 CTests passed**, 16.43s. Test execution started only after the build completed.
- Android NDK27/API26/arm64 and Harmony SDK/arm64: actual `flynes_runtime` and `flynes_session` static libraries compiled with the existing real core dependencies. Outputs are under `.artifacts/nearby-runtime-android-arm64` and `.artifacts/nearby-runtime-harmony-arm64`.
- Existing Harmony host nearby adapter regression passed after session linking; iOS schema-contract check passed. No iOS native SDK build is claimed.
- All three bounded tasks passed independent specification and code-quality review. Review-driven fixes added correct C++17 propagation and exact credential integration assertions; PCM API wording clarifies zero-valued fallback samples.
- Approved spec remains unchanged: normalized Git blob `c88683050f52cb72773917bb6c97573bdddae8af`. No app/catalog or platform product source was modified. The pre-existing untracked device-audit document was preserved and excluded from commits.

The worktree remains on `codex/nearby-multiplayer`; no merge, push, device security change or installed app replacement was performed. This continuation implements tested internal state control and a runtime fix, **not playable phone-to-phone multiplayer**.

## Cross-task handoff

The approved design §30 is the single handoff contract. An app message to thread `01a06cd1-ea44-73d3-a497-a0308724fa55` was attempted on 2026-09-09. The tool returned “no longer available through dynamic tools”; delivery is **not confirmed**. No repeated messages were sent. Integration currently relies on the inspected committed tree and this documented ownership boundary.

This slice owns pair_capability.hpp/.cpp, test_pair_capability.cpp and its shared/CMakeLists target only. app/catalog and platform product work remain with the cross-platform task. No second shared hierarchy is introduced.

## Device gate

At the original implementation check, `adb devices -l` returned no attached device and `hdc`/`xcodebuild` were not on PATH. A subsequent independently produced `docs/acceptance/2026-09-09-nearby-device-audit.md` records an Android 16/API36 native component test pass and a Harmony native executable denied by device policy (exit 126). That file is preserved as found, not rewritten by this implementation. Neither historical result qualifies a physical bearer or phone-to-phone session. The newer Android↔Windows QUIC evidence is recorded above; pairing, H.264 and device latency qualification remain absent.

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

### Continuation audit (2026-09-10)

The public session C API is consumed by the shared C/C++ ABI tests and Harmony's host-only nearby adapter. The latter submits payload-free events and maps only NONE commands; Android/iOS nearby files are local DTO declarations. These do not provide an authenticated transport path. The next implementation uses a session-owned private C++ seam, preserving public C struct sizes and rejecting unimplemented raw receive calls. Internal local command IDs are not network transition IDs. The current 64-bit public transition placeholder remains unused; a later versioned integration must preserve the full 128-bit wire value.

Execution plan: `2026-09-10-nearby-plan-lock.md`. Durable storage acknowledgements and current connection-generation send acknowledgement will be separate effects; none may be inferred from a physical GATT ACK. Adapter effects must be executed once per command ID, not once per poll.

Fresh continuation preflight: ADB and the SDK's full-path HDC both currently list no targets. Harmony cross-configuration is now available at `.artifacts/nearby-harmony-arm64`: use `D:/soft/DevEco Studio/sdk/default/openharmony/native/build-tools/cmake/bin/cmake.exe` with the adjacent SDK Ninja and `native/build/cmake/ohos.toolchain.cmake`. The Android CMake 3.22 distribution lacks `Platform/OHOS` and initially failed to find the existing sysroot zlib header; the SDK CMake includes that platform module and configuration succeeds without bypassing sysroot checks. This is configuration evidence only until the new targets are built. A Windows Harmony adapter regression build is configured separately at `.artifacts/nearby-harmony-host`.

- PCM's blocking core-lock wait is fixed, but V1 silence metadata (sequence/time zero) alone cannot distinguish fallback from legitimate first-block content. Explicit source/status accounting, shared PublishedAudioBlock staging, local playout/correction and device A/V verification remain required before media qualification; do not treat the current pull API as a qualified canonical network audio stream.
- The existing session ABI command transition ID is a uint64 field while the designed network transition ID is128 bits. Audit and version the internal/public boundary before wiring commands; do not truncate wire IDs.
- Existing generic fixed-object codec checks do not establish full semantic/child-graph/signature validity. Expand the single schema/codegen/golden gate before using any of those objects as authenticated executable recovery evidence.
- Initial plan-lock/prompt policy and session-owned deadline control now exist. Still implement authenticated message decoding and exact-byte storage/executors before connecting these trusted-evidence methods to a real transport. Then physical Connectivity/QUIC experiments and media/input/recovery/downgrade remain; M0a/M5 is still an unpassed release gate. Public receive/event/command/snapshot paths remain explicitly unimplemented, not a working nearby UI.
