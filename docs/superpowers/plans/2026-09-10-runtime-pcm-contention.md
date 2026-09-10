# Runtime PCM contention fix

Scope: fix the existing blocking wait in `fly_runtime_pull_pcm`, required before the nearby audio pipeline can use it. This does not implement PublishedAudioBlock staging, local playout correction/status, networking or A/V scheduling.

## Evidence and boundary

Before this fix, `shared/src/runtime/flynes_runtime.cpp` took the same blocking `runtime->mutex` in frame stepping, checkpoint operations and PCM pull. The approved nearby design forbids waiting for core execution in an audio callback. Existing `test_pcm_pull` checked an uncontended empty queue and labeled it nonblocking, but never contended that mutex.

## Execution

- [x] Add `shared/tests/test_runtime_pcm_contention.cpp` and a focused CMake test target. Compile the actual runtime implementation into this test translation unit and link the real `nes_abi`, not `flynes_runtime` a second time. This lets the test hold the real private mutex without adding a test-only production API or replacing the emulator with a mock.
- [x] Use a synchronization barrier with the runtime mutex held on the owner thread and the real public PCM pull on a callback thread. Observe behavioral RED: the pull cannot finish until the owner releases the mutex. Use a bounded wait and always release/join even on test failure; give CTest an outer timeout. Do not treat this as a device latency benchmark.
- [x] Change only PCM pull to a non-waiting lock attempt. On contention use the existing empty-queue silence result convention without reading or modifying protected runtime state. On successful acquisition retain normal queue and sequence behavior. No spin, sleeping, allocation, logging or callback to the emulator.
- [x] Test sentinel output clearing and initialized metadata for contention, subsequent exact queued sample/sequence preservation after release, normal partial drains and validation failures. Existing runtime checkpoint tests must still pass.
- [x] Document the contention-silence behavior in the public API comment without changing layout or inventing canonical sample sequence for silence. Consumer-side status and correction accounting remains a subsequent audio-pipeline requirement.
- [x] Independent spec then quality review; full host suite and Android/Harmony runtime cross-compile where available. Keep this focused fix separate from broader media architecture changes.

## Evidence

The old implementation failed with `PCM pull returns while the core mutex remains held` after a safe 1.56-second test run. The two-line production fix then passed both focused tests after explicit recompilation of runtime and the test translation units. Independent specification and quality reviews passed. Parent used the normal full host build and then ran 41/41 CTests successfully (16.43s). Android API26/arm64 and Harmony arm64 runtime/session static libraries compiled with the real core dependencies. No phone latency or complete audio-pipeline qualification is inferred.

Cross-build harness is the ignored `.artifacts/nearby-runtime-cross-src/CMakeLists.txt`, adding the existing core and shared subdirectories with tests disabled. Outputs are `.artifacts/nearby-runtime-android-arm64` and `.artifacts/nearby-runtime-harmony-arm64`; target SDK zlib is used. The SDK CMake is required for Harmony's OHOS platform module. Existing Nestopia MSVC warnings and the Harmony SDK unused `--gcc-toolchain` warning remain; no vendor or SDK workaround was added to production code.
