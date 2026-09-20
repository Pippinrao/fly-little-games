> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby Backend R1 Wire, Authentication, and Route Plan

> Scope: implement R1 from the approved 2026-09-13 backend/interface/test designs. Keep process ABI V2, wire major 2, object encoding V1, and repository VERSION as separate axes. Do not claim device certification from host tests.

## Task 1: Freeze the authoritative lookup registry and exact V2 codec

- [x] Add a failing independent golden/codec test for GATT V2 type 27 request (32-byte body, 72-byte logical message) and type 28 match (136-byte body, 176-byte logical message).
- [x] Assert the literal SHA-256 domain construction, field offsets, non-zero nonce, PairContext hash, wire major 2, BLE_ANONYMOUS route, direction, reserved bytes, and exact lengths.
- [x] Assert rejection of V1 type 27/28/29, V2 type 29, swapped direction/type, outer/body version mismatch, trailing/truncated bytes, nonce mismatch, and context mismatch.
- [x] Implement the smallest shared codec/registry needed to pass, without interpreting V1 object kinds 0x0214/0x0215 as this protocol.
- [x] Remove the superseded V1 invite object kinds from the active schema/app-frame registry and regenerate reviewed schema/goldens.

## Task 2: Implement bounded GATT physical fragmentation/reassembly

- [x] Add failing tests for asymmetric ATT value caps 20/182/244, arbitrary ordering allowed by the specified fragment indices, exact duplicate fragments, and two concurrent incomplete messages.
- [x] Add failing tests for cap <20, zero/reused message id, three concurrent messages, >8192 buffered bytes, 4097-byte logical messages, overlap/gap/conflicting duplicate, header/type/count/FIRST/LAST mismatch, stale connection generation, and five-second no-progress expiry.
- [x] Implement the 14-byte physical header, sender fragmentation, per-generation/direction reassembler, bounded allocation, exact logical verification, and terminal error results.
- [x] Prove type 16 remains only a physical acknowledgement and cannot produce authentication evidence.

## Task 3: Implement the finite invitation coordinator

- [x] Add failing deterministic-clock tests for uniform six-digit generation input mapping, 60-second fixed deadline, trim/preserve-leading-zero validation, explicit refresh/cancel, and non-sliding expiry.
- [x] Add failing tests for at most eight anonymous candidates, two seconds per candidate, sixteen seconds total, five submits per local 60-second window, twenty host lookups per process window, one in-flight lookup per GATT connection, and multiple-context ambiguity.
- [x] Add failing tests showing code and QR child routes have distinct ids/nonces/commitments, share only the product deadline, and are mutually consumed by the first host acceptance.
- [x] Implement `InviteCoordinator` and typed route outcomes; no route match may produce verified identity, credentials, bearer startup, or connected-lobby state.

## Task 4: Replace private verified-evidence seams with an authenticated reducer boundary

- [ ] Add failing scenario tests that drive commit/reveal, contribution transcript, signatures, user approval, SAS/QR confirmation, key confirmation, capability reveal, and plan lock through provider-produced cryptographic results.
- [ ] Add reflection, replay, wrong-role, wrong-generation, commitment, signature, transcript, SAS, QR proof, key-confirm, plan, and credential negative cases; assert zero downstream side effects.
- [x] Add the required Key/Crypto/TLS/SecureStore provider tables and typed events to the process ABI with strict purpose/scope fencing and no private-key byte export.
- [x] Implement the reducer so `VerifiedPairEvidence` is constructible only inside the authenticated pipeline after the complete current-generation chain and required user permissions.

## Task 5: Add bounded incremental QUIC application framing

- [x] Add failing tests for every split/coalescing point, interleaved stream cursors, OPEN/DATA/FIN/RESET, FIN with partial prefix/body, oversized prefix before allocation, duplicate bind stream, wrong opener/kind/channel, and pre-bind data.
- [x] Implement per-stream incremental assemblers and the stream-kind/FNR1/FNB1 separation rules using the single generated registry.
- [x] Buffer pre-bind legal app records within a fixed budget and release them only after full TLS/pin/exporter/ChannelBind/LINK gates; otherwise discard without reducer effects.

## Task 6: Define and verify the real QUIC provider contract

- [x] Add contract tests for full TLS 1.3, ALPN `flynes-nearby/2`, exact DER-SPKI pin, pin-verifier invocation, exporter label/context/length, no PSK/ticket/0-RTT, DATAGRAM budget, stream ownership, completion, cancellation, and buffer lifetime.
- [x] Add the QUIC port table/events to the process ABI and engine validation without a fake release fallback.
- [ ] Wire one independently buildable provider wrapper target per platform; unsupported primitives must return an explicit unavailable reason.

## Task 7: R1 regression and evidence

- [ ] Run new protocol tests repeatedly and under the available host sanitizer/toolchain where supported.
- [x] Run the complete shared CTest suite and Android unit build.
- [x] Compile Android, HarmonyOS, and iOS production composition roots after ABI changes.
- [x] Record real-device authentication/QUIC as NOT_RUN until compatible signed devices are available; do not infer it from host or simulator results.
