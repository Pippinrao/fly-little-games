> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby UI Reacceptance and Backend R1 Checkpoint — 2026-09-14

## Verdict

- UI automated reacceptance: **PASS** for the scoped Android, HarmonyOS, iOS,
  shared, and parity suites listed below.
- Backend R1 shared protocol/ABI checkpoint: **PARTIAL PASS**.
- Product multiplayer end-to-end acceptance: **NOT ACCEPTED**.
- Physical-device authentication, bearer, QUIC, media, latency, power, and
  recovery certification: **NOT_RUN**, by explicit user direction until the
  backend is complete.

This report does not infer physical-device behavior from host tests, Android or
Harmony emulators, an iOS simulator, or the disposable QUIC probe.

## Automated reacceptance evidence

| Scope | Result | Evidence |
|---|---:|---|
| Shared host | PASS | CMake 3.22.1 Release build; CTest 67/67 |
| Android unit/build | PASS | `:app:testDebugUnitTest :app:assembleDebug` |
| Android Nearby instrumentation | PASS | 28/28 with `SingleDeviceCertificationRunner` |
| HarmonyOS product + ohosTest packages | PASS | both HAP builds succeeded; packages are unsigned |
| HarmonyOS Hypium | PASS | 34/34; NearbyService 10/10 |
| iOS Runtime | PASS | 50/50; `tests-20260914-044935.xcresult` |
| iOS Nearby UI | PASS | 4/4; `tests-20260914-045254.xcresult` |
| Nearby UI parity checker | PASS | 5/5 |
| iOS product shell contract | PASS | contract runner exited 0 |
| iOS/Android parity contract | PASS | contract runner exited 0 |

The full Android instrumentation suite still contains five environment/GLES or
animation failures outside the Nearby runner and one skip. It is not represented
as a full-suite pass here.

## Reacceptance repairs confirmed

- Invitation generations and join attempts are process-monotonic; cancelled or
  expired generations cannot be revived by late callbacks.
- iOS route disappearance cancels only route-owned invitation state.
- Harmony catalog canonical deduplication preserves the maximum popularity
  score rather than whichever duplicate happened to be encountered first.
- The iOS parity contract recognizes both the SwiftUI Toggle form and the
  accessible native UISwitch wrapper while still requiring the exact persisted
  binding and accessibility identifier.
- The parity evidence matrix groups by surface, dimensions, orientation, font,
  safe area, and keyboard state, and enforces the required C17/C18 coverage.

## Backend R1 implemented

- Process ABI V2 lifecycle, fencing, ports, immutable BufferHandle read/retain/
  release contract, and strict optional provider-table validation.
- Authoritative GATT V2 invitation lookup registry and exact codec; superseded
  V1 invitation kinds removed from the active registry and goldens regenerated.
- Bounded physical GATT fragmentation/reassembly and finite invitation
  coordinator with fixed deadlines, rate/candidate bounds, regeneration,
  cancellation, and mutually consumed code/QR routes.
- Pair commit/reveal transcript construction, HMAC/HKDF/SAS helpers, sealed
  authenticated evidence, exact engine/link/generation/operation fencing, and
  fail-closed negative tests.
- Incremental QUIC application framing, FNR1/FNB1 separation, bounded pre-bind
  buffering, full TLS/pin/exporter/ChannelBind gate, and two-peer game-less link
  state tests.
- QUIC contract covers full TLS 1.3, exact `flynes-nearby/2` ALPN, exact DER-SPKI
  pin evidence, exporter binding, resumption/0-RTT prohibition, stream ownership,
  cancellation, payload budgets, and buffer lifetime.

The generated session schema registry hash is
`b8583a3baeb053f9617546d7985f0d338bf9056759b8623bc7c0f241a5bbd719`.

## Real QUIC candidate evidence

The separately scoped Quinn 0.11.11 / rustls 0.23.43 probe passed its locked
test, formatting, and Clippy gates on this checkpoint: 7 library tests, 2 FFI
tests, and 11 transport tests. These tests use real UDP/TLS and cover correct and
wrong pins, exporter agreement, streams, DATAGRAM, FIN, bounds, deadlines, and
resumption policy.

The probe is still **not** the product provider. It uses a probe-only ALPN,
in-memory generated listener credentials, and a blocking test FFI. The approved
design explicitly prohibits linking it unchanged and calling that product
completion.

## Blocking gaps before product E2E acceptance

1. The common Quinn/rustls product wrapper has not been implemented against
   `fly_session_quic_port_v2`; no production composition root injects a real
   QuicPort yet.
2. Android Keystore, iOS Keychain/Secure Enclave, and Harmony HUKS providers are
   not wired to Key/Crypto/TLS/SecureStore tables. Current composition roots only
   compile the ABI prefix and therefore cannot create authenticated evidence from
   OS-owned keys.
3. DiscoveryPort and BearerPort are not present in the V2 composition, so the
   real BLE invitation path and certified offline network path cannot execute.
4. The shared engine does not yet orchestrate the whole invitation → provider
   crypto → bearer → QUIC → ChannelBind → LINK_READY pipeline.
5. R2–R7 game configuration, HOST_STREAM input/media, DUAL/degrade, durable
   recovery, content transfer, and cross-device support matrices are not
   implemented.

Consequently B03 and all product-level end-to-end multiplayer acceptance rows
remain unproven. No screen or report may claim “双人联机中” from these host-only
tests.
