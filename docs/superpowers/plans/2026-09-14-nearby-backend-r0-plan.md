# Nearby Multiplayer R0 Public Engine Contract Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Deliver the R0 public multiplayer-engine contract: an additive V2 C ABI, injected system-port tables, immutable views and approval tokens, deterministic action/event processing, stale/duplicate completion fencing, and fail-closed shutdown semantics without linking platform headers.

**Architecture:** Keep the existing V1 ABI and invitation compatibility path unchanged. Add a small C ABI facade backed by focused C++ units under `shared/src/session/{engine,ports,view}`; a single engine-owned queue serializes reducer changes, while ref-counted inbox/view/token objects outlive the mutable engine safely. R0 deliberately stops before BLE, cryptographic pairing, QUIC, gameplay, media, or durable recovery effects; unavailable capabilities remain explicit in the snapshot.

**Tech Stack:** C11 public ABI, C++17 implementation, CMake/CTest, MSVC host tests, existing Android JNI/Harmony N-API/Objective-C++ link consumers.

---

## File map

- Modify `shared/include/flynes/flynes_session.h`: additive V2 opaque handles, result codes, common IDs/scopes/fences, port-table prefixes, action/view/inbox entry points.
- Create `shared/src/session/ports/session_ports.hpp`: validated retained copy of port tables and context lifetime.
- Create `shared/src/session/view/session_view.hpp` and `.cpp`: immutable snapshot, descriptor, approval-token and pagination ownership.
- Create `shared/src/session/engine/session_engine.hpp` and `.cpp`: single serialized state owner, bounded action/notice queues, event idempotency and shutdown.
- Create `shared/src/session/flynes_session_v2.cpp`: exception-safe C ABI translation only.
- Create `shared/tests/nearby/contract/test_session_v2_abi.c`: C layout/export consumer.
- Create `shared/tests/nearby/contract/test_session_v2_contract.cpp`: API01–API10 and API15 host contract tests.
- Create `shared/tests/nearby/contract/test_session_v2_fencing.cpp`: API04/API05/API14 stale, duplicate and conflicting completion tests.
- Create `shared/tests/nearby/harness/deterministic_executor.hpp`: test-only FIFO task/clock driver; never linked into production.
- Modify `shared/CMakeLists.txt`: build the focused units; Task 1 registers the ABI target without a label so Task 7 can first prove the label gate is red, then registers non-empty `nearby_abi`/`nearby_ports` CTest labels.
- Modify platform composition roots only after the host contract is green: `app/src/main/cpp/flynes_app_jni.cpp`, `harmony/entry/src/main/cpp/napi_init.cpp`, and `ios/app/bridge/FlyNesAppBridge.mm` compile against V2 but retain V1 UI behavior until R2.

### Task 1: Freeze the additive ABI prefix and reject invalid construction

**Files:**

- Modify: `shared/include/flynes/flynes_session.h`
- Create: `shared/tests/nearby/contract/test_session_v2_abi.c`
- Modify: `shared/CMakeLists.txt`

- [x] **Step 1: Write the failing C ABI test**

The C consumer must instantiate only public types and assert the frozen prefix:

```c
_Static_assert(sizeof(fly_session_result_v2) == 4, "result width");
_Static_assert(offsetof(fly_session_config_v2, struct_size) == 0, "size prefix");
_Static_assert(offsetof(fly_session_config_v2, abi_version) == 4, "version prefix");
_Static_assert(FLY_SESSION_ABI_VERSION_2 == 2, "ABI version");
```

Register this one executable and test without assigning a `nearby_*` label yet; label registration belongs to Task 7.

- [x] **Step 2: Run the target and verify RED**

Run: `cmake --build .artifacts/build/shared-host --config Release --target flynes_session_v2_abi_test`

Expected: compile failure because the V2 names do not exist.

- [x] **Step 3: Add the minimal common ABI types**

Add distinct V2 result constants and opaque handles without changing any V1 value or layout:

```c
typedef int32_t fly_session_result_v2;
typedef struct fly_session_v2_handle fly_session_v2_t;
typedef struct fly_session_inbox_v2_handle fly_session_inbox_v2_t;
typedef struct fly_session_view_v2_handle fly_session_view_v2_t;
typedef struct fly_session_approval_token_v2_handle fly_session_approval_token_v2_t;

#define FLY_SESSION_ABI_VERSION_2 UINT32_C(2)
enum fly_session_result_code_v2 {
  FLY_SESSION_V2_OK = 0, FLY_SESSION_V2_ACCEPTED = 1,
  FLY_SESSION_V2_EMPTY = 2, FLY_SESSION_V2_DUPLICATE = 3,
  FLY_SESSION_V2_INVALID_ARGUMENT = -1, FLY_SESSION_V2_ABI_MISMATCH = -2,
  FLY_SESSION_V2_STALE = -3, FLY_SESSION_V2_INVALID_STATE = -4,
  FLY_SESSION_V2_UNSUPPORTED = -5, FLY_SESSION_V2_PERMISSION_DENIED = -6,
  FLY_SESSION_V2_BACKPRESSURE = -7, FLY_SESSION_V2_BUFFER_TOO_SMALL = -8,
  FLY_SESSION_V2_CLOSED = -9, FLY_SESSION_V2_CANCELLED = -10,
  FLY_SESSION_V2_TIMEOUT = -11, FLY_SESSION_V2_IO_FAILED = -12,
  FLY_SESSION_V2_AUTH_FAILED = -13, FLY_SESSION_V2_PROTOCOL_VIOLATION = -14,
  FLY_SESSION_V2_CONTRACT_VIOLATION = -15, FLY_SESSION_V2_BUSY = -16,
  FLY_SESSION_V2_OUT_OF_MEMORY = -17
};
```

- [x] **Step 4: Build and verify GREEN**

Run the target again. Expected: compile and link success.

### Task 2: Define scopes, fences, operation tokens and validated port prefixes

**Files:**

- Modify: `shared/include/flynes/flynes_session.h`
- Create: `shared/src/session/ports/session_ports.hpp`
- Create: `shared/src/session/flynes_session_v2.cpp`
- Create: `shared/tests/nearby/contract/test_session_v2_contract.cpp`

- [x] **Step 1: Write failing API01/API02 tests**

Cover undersized structs, unknown ABI, non-zero reserved fields, missing required Clock/Executor/PlatformState functions, and an absent optional Camera. Assert that invalid create leaves `out_engine == NULL` and calls no retain function; missing Camera creates a LOADING engine but exposes scan as unavailable.

- [x] **Step 2: Verify RED**

Run: `ctest --test-dir .artifacts/build/shared-host -C Release -R flynes_session_v2_contract --output-on-failure`

Expected: target absent or compilation failure.

- [x] **Step 3: Implement exact value types and table validation**

Use fixed-width fields and zero-reserved validation:

```c
typedef struct fly_session_scope_v2 {
  uint32_t struct_size, abi_version, kind, reserved_zero;
  uint8_t link_id[16], branch_id[16];
} fly_session_scope_v2;

typedef struct fly_session_op_token_v2 {
  uint32_t struct_size, abi_version;
  uint8_t engine_instance_id[16];
  fly_session_scope_v2 scope;
  uint64_t connection_generation, config_revision, authority_term;
  uint64_t writer_generation, timeline_epoch, seat_revision;
  uint64_t mode_generation, media_generation, operation_id;
  uint8_t transition_id[16];
} fly_session_op_token_v2;
```

Define every R0 table with `struct_size`, `abi_version`, `context`, `retain`, and `release`. Require Clock `read_continuous`, Executor `post/arm_timer/cancel_timer`, and PlatformState `watch/stop`; keep Camera optional. `SessionPorts` copies function tables and retains each accepted context exactly once.

- [x] **Step 4: Verify GREEN and V1 compatibility**

Run the V2 contract target and existing `flynes_session`/`flynes_session_public_path` tests.

### Task 3: Implement immutable views, pagination and token lifetime

**Files:**

- Create: `shared/src/session/view/session_view.hpp`
- Create: `shared/src/session/view/session_view.cpp`
- Modify: `shared/include/flynes/flynes_session.h`
- Modify: `shared/tests/nearby/contract/test_session_v2_contract.cpp`

- [x] **Step 1: Write failing API08/API15 tests**

Acquire view revision 1, publish revision 2, begin shutdown and destroy the engine, then read and paginate the old view. Retain one approval token past view release; prove it stays memory-safe but becomes STALE after its bound generation changes and cannot be submitted to another engine.

- [x] **Step 2: Verify RED**

Expected: missing acquire/read/copy/release symbols.

- [x] **Step 3: Implement ref-counted immutable storage**

`SessionView` owns copied snapshot rows and token references. Copy functions validate caller prefixes and return a single revision only; `offset > total` is INVALID_ARGUMENT, `offset == total` returns OK with `written=0`. Approval tokens store engine instance, descriptor ID, action kind, scope, bound revision/hash and a revocation generation; retaining never changes authorization expiry.

- [x] **Step 4: Verify GREEN under ordinary and engine-destroyed lifetimes**

Run the contract target; then rebuild the public C header consumer.

### Task 4: Add bounded actions/notices and deterministic consumption

**Files:**

- Create: `shared/src/session/engine/session_engine.hpp`
- Create: `shared/src/session/engine/session_engine.cpp`
- Create: `shared/src/session/flynes_session_v2.cpp`
- Create: `shared/tests/nearby/harness/deterministic_executor.hpp`
- Modify: `shared/tests/nearby/contract/test_session_v2_contract.cpp`

- [x] **Step 1: Write failing API03/API06/API09 tests**

Use action capacity 2 and notice capacity 2. Submit two valid actions without draining results, assert a third returns BACKPRESSURE without consuming its token, then execute tasks deterministically. Deliver from inside a port start callback and from another thread; assert reducer depth never exceeds one and replaying the ordered events produces the same revisions.

- [x] **Step 2: Verify RED**

Expected: submit/read-notice symbols or behavior missing.

- [x] **Step 3: Implement the minimal serialized engine**

The public call copies typed choice bytes, retains the token, reserves one notice slot and posts one session task. Consumption checks token binding again, returns exactly one ACTION_RESULT notice, and never calls a reducer synchronously from a port callback. R0 actions are limited to visibility/discovery/invite descriptors whose required providers exist; unsupported R1+ actions remain disabled with shared reason keys.

- [x] **Step 4: Verify GREEN and deterministic replay**

Run the contract target three times with identical seeds; ordered snapshots/notices must match byte-for-byte.

### Task 5: Fence duplicate, stale and conflicting port completions

**Files:**

- Create: `shared/tests/nearby/contract/test_session_v2_fencing.cpp`
- Modify: `shared/src/session/engine/session_engine.hpp`
- Modify: `shared/src/session/engine/session_engine.cpp`
- Modify: `shared/src/session/flynes_session_v2.cpp`

- [x] **Step 1: Write failing API04/API05/API14 tests**

Deliver an accepted completion twice with identical bytes, then the same key with different bytes; cancel generation `g`, start `g+1`, and deliver a late `g` result. Advance authority term and writer generation between queueing and consumption. Assert DUPLICATE for exact replay, CONTRACT_VIOLATION plus affected-operation freeze for conflict, STALE for old fences, and no mutation of the current view/root counters.

- [x] **Step 2: Verify RED**

Run only `flynes_session_v2_fencing`; expected missing behavior.

- [x] **Step 3: Implement bounded completion journal**

Journal accepted operations by the full `OpToken`, terminal payload hash and exact result. Re-check current generation/term/writer guard at effect time. Keep monotonic high-water tombstones after bounded payload eviction so operation IDs are never reused.

- [x] **Step 4: Verify GREEN**

Run fencing plus contract tests.

### Task 6: Implement fail-closed shutdown and inbox lifetime

**Files:**

- Modify: `shared/src/session/engine/session_engine.hpp`
- Modify: `shared/src/session/engine/session_engine.cpp`
- Modify: `shared/src/session/flynes_session_v2.cpp`
- Modify: `shared/tests/nearby/contract/test_session_v2_contract.cpp`

- [x] **Step 1: Write failing API10 tests**

Begin shutdown with one normal port operation and one deliberately stuck provider. Assert new actions/inputs are CLOSED, the inbox safely returns CLOSED to late callbacks, destroy returns BUSY while a provider owns resources, and destroy succeeds only after terminal cleanup.

- [x] **Step 2: Verify RED**

Expected: destroy frees early or shutdown symbols are absent.

- [x] **Step 3: Implement two-phase shutdown**

Revoke new effect permits first, cancel live operations, keep the ref-counted inbox closed but allocated for retained providers, publish SHUTTING_DOWN, and publish SHUTDOWN_COMPLETE only after every accepted operation reaches terminal and every buffer/resource reference is settled.

- [x] **Step 4: Verify GREEN**

Run contract/fencing tests and verify reference counters return to zero.

### Task 7: Register real CTest labels and prove platform-neutral linkage

**Files:**

- Modify: `shared/CMakeLists.txt`
- Modify: `app/src/main/cpp/flynes_app_jni.cpp`
- Modify: `harmony/entry/src/main/cpp/napi_init.cpp`
- Modify: `ios/app/bridge/FlyNesAppBridge.mm`

- [x] **Step 1: Write failing build/static assertions**

Require `ctest -N -L nearby_abi` and `ctest -N -L nearby_ports` to list at least one target. Add compile-only calls that construct the required V2 tables; release builds must have no fake-provider selector and shared sources must include no JNI/ArkTS/Foundation/Android/OHOS header.

- [x] **Step 2: Verify RED**

Run label enumeration and three platform compile targets; labels are initially empty and composition roots lack V2 construction.

- [x] **Step 3: Wire compile-only production tables**

Provide real clock/executor/platform-state table functions at each composition root, but do not switch existing UI invitation behavior until R2. Missing R1 transport/crypto/store ports must keep R0 view capabilities unavailable rather than install a fake.

- [x] **Step 4: Run the R0 completion gate**

Run:

```text
ctest --test-dir .artifacts/build/shared-host -C Release -L nearby_ --output-on-failure
gradlew.bat :app:testDebugUnitTest :app:assembleDebug
hvigor assembleHap (product and entry@ohosTest)
bash ios/scripts/build_simulator.sh
```

Expected: every registered R0 target passes; three products compile; V1 tests remain green. No statement about BLE, QUIC, gameplay, media, persistence or real-device multiplayer is made at R0.
