> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Authenticated plan-lock execution plan

Branch: `codex/nearby-multiplayer`. Approved product contract: nearby design §9.2.
This continues implementation, not a new product-design approval checkpoint.

## Task 1 — deterministic plan-lock reducer

- [x] Add internal `shared/src/session/initial_plan_lock.hpp/.cpp` and `shared/tests/test_initial_plan_lock.cpp`, register with the existing session library/test suite.
- [x] Tests first: no bearer action before mutual lock; initiator ACK persistence then FINAL persistence then successful current-generation ordered send then mutual-lock persistence; responder plan persistence before ACK and verified FINAL persistence before mutual-lock persistence.
- [x] Bind exact pair transcript, both reveal logical hashes, selected 48-byte entry, plan/ACK/FINAL logical hashes and connection generation. Use the existing exact-intersection selector. Reject zero binding hashes, wrong roles, changed plans, stale completions and out-of-order input. Any command completion is matched to a monotonically increasing local command ID and current generation, never a truncated wire transition ID.
- [x] Bounded one-outstanding-command reducer with explicit persistence/send/action commands, retry by returning the same pending command, no implicit success. Failures fail closed. Physical ACK is not a semantic event. Initiator and responder lock gates are deliberately different.
- [x] Only locked creator may create; only locked noncreator with verified durable matching credentials may join. System confirmation authorization must be durably consumed before any action which may prompt, only on the designated role, and never issued twice. Refuse a reconnect action that requests another prompt or a different plan (initial attempt invalidation is terminal; game reconnect remains unimplemented).
- [x] Keep it internal and transport-independent: inputs are already cryptographically verified immutable evidence supplied by a future authenticated decoder, not raw packets. Document that this reducer does not implement signature/AEAD/HMAC verification or certify fixture plans. Do not expose a public network bypass or radio backend.
- [x] Build focused tests, record RED/GREEN, run independent specification then quality reviews.

Task 1 committed as `e1ea62d`. Independent spec review passed; quality review's C++17 usage-requirement fix was applied and re-reviewed. Parent full host suite passed 39/39 (16.58s). Android/Harmony arm64 libraries compiled. Tests cover all role/creator/prompt combinations, every command failure, stable snapshots and noncopyable command ownership. Creator credential generation is a separate trusted-adapter step after CreateBearer; exact bytes must be supplied and persisted before PublishCredentials.

## Task 2 — session ownership / adapter seam

- [x] Audit existing C session and platform consumers before ABI changes. Connect the reducer through a private session-owned interface that preserves the stub public ABI until authenticated decoding is available. Exercise ownership and command flow from an actual session handle in an integration test; no global singleton, unsafe cast, raw-packet approval or ambiguous 64-bit wire ID.
- [x] Preserve the fail-closed public receive path for unimplemented authenticated messages. Report pending effects/snapshots through the private seam without duplicating reducer state.
- [x] Test two session instances with distinct roles/generations, durable command acknowledgement, stale command rejection, creator/join ordering and confirmation count. Review spec then quality.

Concrete seam: `shared/src/session/session_initial_plan.hpp`, implementation in the existing `flynes_session.cpp`, test `shared/tests/test_session_initial_plan.cpp`. Use null-safe free functions taking `fly_session_t*`; the opaque handle owns one reducer directly. Do not return a mutable reducer reference. `start_initial_pair_attempt` latches the original PairContext send/receive continuous-clock start and GATT generation once, before accepting verified capability evidence; the later evidence must match that generation. The caller supplies the original context timestamp, not a fresh time after capability exchange. `fly_session_tick` enforces nondecreasing local time and the non-extending 60-second attempt deadline with overflow-safe elapsed subtraction. Once expired/cancelled/invalidated, pending effects vanish and this handle cannot begin another attempt. An unchanged repeated tick does not extend time. A new user pairing uses a fresh session handle; game reconnect is a separate future reducer. Test exact deadline, backwards tick, near-UINT64_MAX timestamps, null handles, repeated begin, expiry with pending persist/send/prompt commands, and no public raw-packet route to trusted evidence.

Task 2 committed as `b58851c`. Behavioral RED on starting an attempt through inert wrappers, then GREEN. Independent spec review's exact-credential integration assertions were added and re-reviewed; quality review found no remaining issues. Parent independently ran the reducer/session/integration tests (3/3), rebuilt and ran the Harmony host adapter test, and cross-compiled the actual session library for Android/Harmony arm64. Public C ABI and unimplemented raw packet paths remain unchanged; platform executors and authenticated decoding are still absent.

## Task 3 — verification and next implementation boundary

- [x] Completed-build host CTest, Android/Harmony arm64 cross-compile, inspect diff and preserve unrelated device-audit file. Final normal host build succeeded and 41/41 tests passed (16.43s).
- [x] Update progress with exact implemented surfaces and remaining authentication/radio/media/recovery work. Native component test success is not a phone-to-phone qualification.
- [x] Continue toward the next feasible implementation boundary: the separately planned PCM lock-wait fix was implemented, independently reviewed, cross-compiled and verified in the same continuation. Device certification remains a release gate, not a reason to leave host-side session work idle.
