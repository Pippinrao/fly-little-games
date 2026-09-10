# FlyNES nearby multiplayer — M2–M5 status

**Date:** 2026-09-05  
**Spec:** `docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md` §26  
**Rule:** do **not** advertise 三端联机首版. M5 device-matrix certification is not done. A Harmony HAP package is not a device run.

## Frozen shared headers

| Milestone | Header / artifact | Status |
|-----------|-------------------|--------|
| M0 | `shared/include/flynes/flynes_runtime.h` | Frozen. Host `flynes_runtime` test covers step/PCM/RGB565/checkpoint and the 12-slot rollback ring. |
| M1 | `shared/schema/flynes_session_v1.schema` + `shared/include/flynes/flynes_session.h` | Frozen. Codec goldens exist. The session reducer is still a stub (`submit_event` / `receive_*` / `complete_command` return `FLY_RESULT_INVALID_STATE`). |

BLE, QR, Wi-Fi, GATT, and friend DTOs are **not** in `shared/include/flynes/`.

## QUIC channel IDs (shared wire, already assigned)

These values are in `flynes_session.h` and the session codec. No QUIC backend is linked.

| ID | Enum | Intended use | Implemented? |
|----|------|----------------|--------------|
| 1 | `FLY_SESSION_QUIC_CONTROL` | Control / ChannelBind | IDs + goldens only |
| 2 | `FLY_SESSION_QUIC_INPUT` | Input datagrams | IDs + goldens only |
| 3 | `FLY_SESSION_QUIC_STATE_COMMIT` | State commit | IDs only |
| 4 | `FLY_SESSION_QUIC_BULK` | Bulk state / ROM | IDs only |
| 5 | `FLY_SESSION_QUIC_ROM` | ROM transfer | IDs only |
| 6 | `FLY_SESSION_QUIC_VIDEO` | HOST_STREAM H.264 | IDs only; no encoder |
| 7 | `FLY_SESSION_QUIC_AUDIO` | HOST_STREAM PCM | IDs only |

## M2 — platform adapters (host fakes only)

Present as host-only stubs. They do **not** open radios.

| Piece | Where | Status |
|-------|-------|--------|
| Command mapping | `harmony/nearby/nearby_adapter.*` | Maps poll `NONE`; executes start/stop discovery, show QR, confirm SAS, create bearer, open QUIC, persist friend as local kinds. Submits `fly_session_submit_event` only. |
| Fake transport | Harmony host test | Forwards `shared/schema/golden/channel_bind_v1_initial/legal.bin` through `fly_session_receive_stream`. Does not decode seats. |
| Friend storage | `harmony/nearby/friend_store.*` | In-memory fake. Production backends are Android Keystore / iOS Keychain / Harmony HUKS — not wired. |
| BLE/QR/Wi-Fi DTOs | `app/src/main/cpp/nearby/`, `ios/app/platform/nearby/`, `harmony/nearby/` | Platform-local structs only. |

Blocked: real BLE/GATT, QR camera, Wi-Fi Aware/P2P/hotspot, QUIC/TLS backends, SPKI pin, ChannelBind on a live path.

## M3 — HOST_STREAM

| Piece | Status |
|-------|--------|
| Start-encoder command | Host stub returns `FLY_RESULT_UNSUPPORTED_VERSION` (`NearbyCommandKind::StartHostStreamEncoder`). |
| H.264 encode/decode | Not implemented. |
| Published video/audio datagrams | Not implemented. |
| ROM P2P transfer | Not implemented. |

## M4 — DUAL_SIMULATION

| Piece | Status |
|-------|--------|
| 12 rollback slots | Implemented in runtime. Host coverage is `shared/tests/test_runtime.cpp` `test_rollback_and_checkpoint` (captures slots 0–11, restores slot 1, then steps). Do not treat this as a netplay rollback protocol. |
| Input exchange / digest / resync | Not implemented. |
| DUAL → STREAM transaction | Not implemented. |

## M5 — device matrix and certification

Not started. No hdc, no nine authority combinations, no 30-minute soak, no support-list sign-off.

Harmony catalog/runtime **host** tests on Windows are not a HarmonyOS device run. Packaging a HAP, if done later, is also not M5 evidence.

## Must not claim

- Cross-platform multiplayer v1 / 三端联机首版
- Working nearby discovery, pairing, or Wi-Fi bearer
- HOST_STREAM or DUAL over the network
- That M2 adapters are production Connectivity
