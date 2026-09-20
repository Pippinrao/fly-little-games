# Nearby Backend R2 Product Link Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the production-capable invitation → authenticated provider results → certified bearer → pinned QUIC → ChannelBind → game-less connected-lobby path, while leaving physical-device certification explicitly NOT_RUN.

**Architecture:** The shared engine owns every protocol decision and consumes only fenced asynchronous port events. Discovery, network-path binding, non-exportable keys, secure storage, and socket resources remain platform adapters; Quinn/rustls is a common product QUIC wrapper with product ALPN and externally supplied authenticated TLS material. Host L2 uses two public engines, real cryptography, real loopback QUIC, and deterministic fake radio/path resources without injecting verified evidence.

**Tech Stack:** C++17 shared engine/C ABI V2, CTest, Rust 1.96, Quinn 0.11.11, rustls 0.23.43, ring 0.17.14, Android Keystore/Bluetooth/Wi-Fi APIs, Apple Security/CoreBluetooth/Network.framework, Harmony HUKS/connectivity APIs, Gradle, Hvigor/Hypium, XCTest.

**Scope amendment (2026-09-15):** Per user direction, the first usable
multiplayer release implements DUAL only: both peers emulate/render locally and
exchange authenticated input/state. STREAM media encode/decode/transport and
automatic DUAL→STREAM switching are deferred. Existing mode/capability
extension points remain reserved and must continue to fail closed rather than
advertise STREAM support.

---

### Task 1: Add Discovery and Bearer process ABI tables

**Files:**
- Modify: `shared/include/flynes/flynes_session.h`
- Modify: `shared/src/session/flynes_session_v2.cpp`
- Modify: `shared/src/session/ports/session_ports.hpp`
- Test: `shared/tests/nearby/contract/test_session_v2_provider_tables.cpp`
- Modify: `shared/CMakeLists.txt`

- [x] **Step 1: Write the failing provider-table contract**

Add a C++ contract which instantiates complete Discovery and Bearer tables, passes them through `fly_session_create_v2`, and proves an undersized table, non-zero reserved field, or one missing function returns `FLY_SESSION_V2_ABI_MISMATCH`, `FLY_SESSION_V2_INVALID_ARGUMENT`, or `FLY_SESSION_V2_UNSUPPORTED` before any context retain.

- [x] **Step 2: Run the focused test and verify RED**

Run:

```powershell
& 'C:\Users\pippin\AppData\Local\Android\Sdk\cmake\3.22.1\bin\cmake.exe' --build .artifacts/build/shared-host --config Release --target flynes_session_v2_provider_tables_test
```

Expected: compile failure because `fly_session_discovery_port_v2` and `fly_session_bearer_port_v2` do not exist.

- [x] **Step 3: Add exact asynchronous operations**

Define complete tables for Discovery scan/advertise/stop/connect/disconnect/write/indicate/subscribe and Bearer probe/create/join/resolve/release. Every start accepts `fly_session_op_token_v2` and `fly_session_inbox_v2_t`; immutable bytes use `fly_session_buffer_v2_t`; owned OS resources use non-zero `fly_session_resource_handle_v2`. Append pointers after `quic` in `fly_session_ports_v2` so the R0 prefix remains binary compatible.

- [x] **Step 4: Validate, retain, and release the appended tables**

Use the existing size-gated `optional_port`/`capture_optional` pattern. Reject partial present tables; absent providers remain legal at engine creation but must produce an unavailable capability rather than a fake provider.

- [x] **Step 5: Run focused and ABI tests GREEN**

Run CTest regex `flynes_(session_v2_provider_tables|session_v2_abi|session_v2_contract)` and expect all selected tests to pass.

### Task 2: Freeze typed provider event payloads and BufferHandle ownership

**Files:**
- Modify: `shared/include/flynes/flynes_session.h`
- Create: `shared/src/session/ports/provider_events.hpp`
- Create: `shared/src/session/ports/provider_events.cpp`
- Test: `shared/tests/nearby/contract/test_provider_event_contract.cpp`
- Modify: `shared/CMakeLists.txt`

- [x] **Step 1: Write RED tests for every accepted terminal/non-terminal payload**

Cover discovery candidate/connection/MTU/raw bytes/end, bearer capabilities/path/endpoint/end, key/public/sign/agreement, TLS material, secure-store revision, QUIC connection/handshake/exporter/stream/data/budget/stats/end. Mutate size, version, reserved bytes, handle zero, terminal bit, scope, generation, operation, and buffer ownership one field at a time.

- [x] **Step 2: Verify the new parser is missing**

Build `flynes_provider_event_contract_test`; expected compile failure for missing `parse_provider_event_v2`.

- [x] **Step 3: Implement strict typed parsing**

Parse the fixed 64-byte event payload into internal values only after validating the outer event and exact operation kind. Retain an incoming BufferHandle exactly once on acceptance and release it on duplicate, stale, conflict, reducer rejection, cancellation, or successful consumption.

- [x] **Step 4: Run RED mutations and GREEN lifetime assertions**

Expect all field mutations to fail closed and the test buffer destructor count to equal one for every path.

### Task 3: Add the common product Quinn/rustls wrapper

**Files:**
- Create: `shared/nearby-quic-provider/Cargo.toml`
- Create: `shared/nearby-quic-provider/Cargo.lock`
- Create: `shared/nearby-quic-provider/include/flynes_quic_provider.h`
- Create: `shared/nearby-quic-provider/src/lib.rs`
- Create: `shared/nearby-quic-provider/src/tls.rs`
- Create: `shared/nearby-quic-provider/src/runtime.rs`
- Create: `shared/nearby-quic-provider/tests/transport.rs`
- Create: `shared/nearby-quic-provider/tests/ffi.rs`

- [x] **Step 1: Write real-transport RED tests**

Specify product ALPN `flynes-nearby/2`, exact DER-SPKI verifier invocation, full TLS 1.3 only, disabled client resumption/server tickets/0-RTT, exact exporter label/context/32-byte output, bounded uni/bidi streams, DATAGRAM payload budget, cancellation, FIN/reset, and buffer completion. The first build must fail because no product wrapper exists.

- [x] **Step 2: Implement TLS policy without copying probe success claims**

Adapt the reviewed rustls verifier and bounded Quinn runtime into a new product crate. Accept TLS key/certificate material only through an opaque provider callback; do not generate a hidden replacement key, accept system CAs, weaken the pin, log secrets, or use the probe ALPN.

- [x] **Step 3: Implement the C callback boundary**

Expose create/retain/release plus non-blocking listen/connect/inspect/exporter/open/write/finish/reset/read-credit/datagram/query/close. Each accepted call owns copied input/retained BufferHandles until exactly one terminal callback. Synchronous failure emits no callback.

- [x] **Step 4: Run locked Rust quality gates**

Run `cargo test --locked`, `cargo fmt -- --check`, and `cargo clippy --all-targets -- -D warnings`; expect all tests and checks to pass.

### Task 4: Drive provider cryptography into authenticated evidence

**Files:**
- Create: `shared/src/session/link/pair_pipeline.hpp`
- Create: `shared/src/session/link/pair_pipeline.cpp`
- Modify: `shared/src/session/pair_auth_reducer.*`
- Test: `shared/tests/nearby/integration/test_real_pair_pipeline.cpp`
- Modify: `shared/CMakeLists.txt`

- [ ] **Step 1: Write a two-party real-crypto RED test**

Create two independent test key providers backed by ring P-256 test keys. Start from PairContext and commit/reveal bytes, invoke key agreement/sign/verify through the public port operations, make both users approve SAS, verify capability/plan/key-confirm, and assert only the complete provider receipts yield sealed `VerifiedPairEvidence` with the same transcript.

- [ ] **Step 2: Add the required negative matrix**

Cover reflection, wrong role/engine/link/generation/operation, wrong purpose/scope, replay, commitment, AEAD, transcript, double-hash/high-S signature, SAS, QR proof, key-confirm, capability, plan, credential, and stale provider completion. Assert no Bearer or QUIC start occurs.

- [ ] **Step 3: Implement the operation scheduler and reducer bridge**

Issue monotonic operation IDs, record exact expected completion kind, and publish provider results to the reducer only after fence and journal validation. Remove every test path that directly constructs authenticated receipts.

- [ ] **Step 4: Run the complete pair suite GREEN**

Run tests carrying labels `nearby_auth` and `nearby_protocol`; expect zero failures.

Progress (2026-09-14): the provider-only signature/HMAC reducer bridge,
canonical PairContext/Commit/Contribution codecs, shared P-256 point validation,
typed asynchronous failure terminals, and a fenced local-material scheduler are
implemented. The scheduler obtains purpose-separated identity/ECDH/TLS handles,
X9.63 public keys, exact DER-SPKI pin, TLS material, and a CSPRNG contribution
nonce only from public provider completions. A CNG-backed two-party component
test and the public-completion tests pass. The responder public-engine path now
also accepts the initiator commit, sends the canonical responder commit, obtains
the ECDH secret and separate i2r/r2i reveal keys through fenced Key/Crypto port
completions, verifies the initiator AEAD reveal against the earlier commitment,
and sends the responder's 352-byte AEAD reveal through ordered DiscoveryPort
fragments. It remains AUTHENTICATING and releases all three derived secret handles
on a later protocol failure. This is intentionally not checked off: the initiator
public-engine path, encrypted pair signatures, human approval/key-confirm,
capability/plan exchange, and the full negative matrix are not complete. The
current shared regression count is recorded by the latest verification run below,
not proof of a connected product lobby.

Progress (2026-09-15): exact 880-byte PairTranscriptV1 construction and object
hashing are implemented. PairSignatureScheduler now retains the two verified
role-ordered signatures and emits ObjectStore.put_immutable(kind=0x0213); SAS
and user approval remain blocked until a typed durable completion returns the
same object hash. The public engine and its host integration fixture use the
new size-gated ObjectStore port. This closes the persist-before-KEY_CONFIRM
gate but does not complete Task 4's full negative matrix.

### Task 5: Implement the shared game-less link orchestrator

**Files:**
- Create: `shared/src/session/link/link_orchestrator.hpp`
- Create: `shared/src/session/link/link_orchestrator.cpp`
- Modify: `shared/src/session/engine/session_engine.*`
- Modify: `shared/src/session/ports/session_ports.hpp`
- Test: `shared/tests/nearby/integration/test_two_engine_empty_lobby.cpp`
- Test: `shared/tests/nearby/race/test_link_races.cpp`
- Modify: `shared/CMakeLists.txt`

- [ ] **Step 1: Write the public-engine RED scenario**

Using two `fly_session_v2_t` instances, submit create-invite and join-code actions, forward raw Discovery fragments, satisfy the provider crypto/SAS path, lock one bearer plan, establish real loopback QUIC, exchange ChannelBind and both LINK_READY records, then assert both immutable snapshots enter `CONNECTED_LOBBY` without loading a ROM.

- [ ] **Step 2: Enumerate cancellation and stale races**

Exercise bounded traces of length seven for page exit, cancel, expiry, regeneration, discovery disconnect, bearer terminal, QUIC terminal, LINK_READY, and shutdown. Old generations may release resources but never advance a newer link.

- [ ] **Step 3: Implement actions, snapshots, operation journal, and cleanup**

Add the approved LINK actions/descriptors and state projection. The orchestrator starts each next port only after authenticated evidence and plan gates; any failure cancels outstanding operations, releases exact owned handles, discards pre-bind records, and publishes the shared reason key.

- [ ] **Step 4: Run the two-engine and race suites GREEN**

Require identical semantic state sequence on both engines and no leaked provider resources.

### Task 6: Implement platform Key/Crypto/TLS/SecureStore adapters

**Files:**
- Create: `app/src/main/java/com/flynes/emu/nearby/AndroidSecureProvider.java`
- Create: `app/src/main/cpp/nearby/android_secure_provider_jni.cpp`
- Create: `ios/app/platform/nearby/AppleSecureProvider.mm`
- Create: `harmony/entry/src/main/ets/service/NearbySecureProvider.ets`
- Create: `harmony/entry/src/main/cpp/nearby_secure_provider.cpp`
- Modify: the three platform composition roots and build files
- Test: platform-local provider contract suites

- [ ] **Step 1: Write platform contract RED tests**

Assert non-exportable P-256 purposes, prehashed canonical-low-S signing, exact digest verification, ECDH, CSPRNG/HKDF/AES-GCM, ThisDeviceOnly/non-backup secure records, exact TLS SPKI restore, revision conflicts, lock/unavailable/not-found distinction, and stale completion cleanup.

- [ ] **Step 2: Implement Android Keystore and no-backup storage**

Use Android Keystore handles for identity/session/ECDH/TLS purposes and `noBackupFilesDir` for encrypted metadata. Never return private bytes or silently regenerate on open failure.

- [ ] **Step 3: Implement Apple Security/Keychain provider**

Use Secure Enclave where supported with `kSecAttrAccessibleWhenPasscodeSetThisDeviceOnly`, non-synchronizable keys/records, complete file protection, and backup exclusion. Report explicit unavailable on simulators lacking required secure hardware.

- [ ] **Step 4: Implement Harmony HUKS provider**

Use HUKS aliases bound to installation/purpose/scope and application-private non-backup records. Locked, unavailable, not-found, and revoked results remain distinct.

- [ ] **Step 5: Run platform unit/host tests and build all production roots**

No adapter test may inject `authenticated=true`; real cryptographic outputs must be independently verified.

### Task 7: Implement platform Discovery and Bearer adapters

**Files:**
- Create platform-local nearby discovery/bearer implementations under the existing Android, iOS, and Harmony nearby directories
- Modify platform manifests/entitlements and composition roots
- Test platform-local adapter contract suites

- [ ] **Step 1: Write lifecycle/fencing RED tests**

Test fixed service identity, anonymous advertising, candidate bounds, GATT raw byte fidelity, MTU changes, one terminal per start, background/permission transitions, exact plan role, endpoint resolution, and owned-resource release.

- [ ] **Step 2: Implement adapters with explicit capability results**

Implement only OS-supported primitives. Simulator/emulator or unsupported hardware returns the shared unavailable reason; it must never substitute LAN, hotspot, fake friends, or fake authentication for a certified plan.

- [ ] **Step 3: Compile and run all non-device adapter tests**

Run Android JVM/instrumentation where emulator-valid, Harmony host/Hypium, and iOS simulator XCTest. Mark physical radio/path checks NOT_RUN.

### Task 8: Integrate product UI with the public engine

**Files:**
- Modify the Android, Harmony, and iOS nearby services/bridges/views
- Test the existing three platform Nearby UI suites

- [ ] **Step 1: Write RED tests proving UI no longer fabricates session progress**

Create/join/cancel/SAS/disconnect actions must carry the current public descriptor token and expected revision. Connected text may appear only from a `CONNECTED_LOBBY` snapshot.

- [ ] **Step 2: Replace local invite/stage state with immutable session views**

Keep drafts and navigation local; move invitation generations, deadlines, peer facts, stage progress, reasons, and actions to the public engine projection.

- [ ] **Step 3: Re-run parity and platform suites**

Require shared 67+ CTests, Android unit/build/Nearby instrumentation, Harmony Hypium/build, iOS Runtime/Nearby UI, and parity checkers to pass.

### Task 9: Record the R2 acceptance boundary

**Files:**
- Create: `docs/acceptance/2026-09-14-nearby-backend-r2-acceptance.md`
- Modify: `docs/superpowers/plans/2026-09-14-nearby-backend-r2-plan.md`

- [ ] **Step 1: Run all non-device R2 gates**

Record exact commands, counts, artifact hashes, toolchain versions, simulator/emulator scope, and failures. Do not omit unrelated failures from a suite that is described as complete.

- [ ] **Step 2: Keep physical evidence NOT_RUN**

Bluetooth/GATT radio behavior, certified no-router bearer creation, hardware-backed key properties, cross-device QUIC, latency, power, and thermal evidence remain NOT_RUN until the user resumes device acceptance.

- [ ] **Step 3: Gate the next phase**

Proceed to R3 only after both public engines reach the same empty-lobby semantic state through real provider crypto and real loopback QUIC with zero injected verified evidence.
