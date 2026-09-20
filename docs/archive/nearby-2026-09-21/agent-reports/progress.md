> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby DUAL SDD progress (resume 2026-09-16)

Worktree: `.worktrees/nearby-ui-acceptance-fixes` (W0, `codex/nearby-ui-acceptance-fixes`)
Start tip this session: `08f2afd` (provenance docs). Uncommitted: content-port ABI draft in `flynes_session.h` / `session_ports.hpp` / `flynes_session_v2.cpp` / `test_session_v2_abi.c`.

## Completed before this session (do not re-dispatch)

- Task 1 BASE-0212, Task 2 BASE-PARALLEL, Task 3 LINK-WIRE, Task 4 LINK-SCHED
- Task 5 DUAL-INPUT, Task 6 DUAL-RUN
- Task 7 E2E-HARNESS (corrected: crate tests + linkage = real Quinn; two-engine lobby = in-process LoopbackTransport)
- Task 9 MVP-LOBBY (in-process loopback; both engines CONNECTED_LOBBY)
- Task 10 step 1 ABI tail-append (SELECT_CONTENT=42, START_DUAL=43, dual snapshot, dual_runtime port)
- Task 10 STREAM closed, 154-byte input codec, runtime adapter, digest snapshot fields
- §3.1 provenance docs corrected in `08f2afd`; CMake still silently FORCE-OFF if cargo not on PATH (HINTS not landed)

## Completed this session

- W3 tamper round 2: complete (commits `697c4a1`..`5e8e73f` = `e8375c0`+`5e8e73f`, review clean). Pair-layer tampers fail-closed; SAS diagnosed only (IF05, no ABI change).
  Minor (defer to final review): stale “copy only while armed” comment in `two_engine_loopback_fixture.hpp` ~4265; `reassembly_mismatches` captured but no longer printed by `run_case`.

## Completed this session (continued)

- Task 10 MVP-DUAL host E2E green: `flynes_two_engine_dual_mvp passed` (catalog SELECT, START_DUAL UNAVAILABLE/RUNNING, 600-frame local digest match, pause/disconnect freeze). Log: `out/logs/task10-dual-mvp.log`.
- Existing two-engine CONNECTED_LOBBY E2E still green.
- CMake cargo HINTS + WSL cache ON (`FLYNES_CARGO_EXECUTABLE=/home/pippin/.cargo/bin/cargo`).

## In progress

- None. Three-platform emulator gates recorded. L4/STREAM remain NOT_RUN/DEFERRED by design.

## Completed this session (continued 4)

- Task 15 iOS: dedicated Mac checkout `~/Developer/fly-little-games-nearby-w0`. RuntimeTests 50/50. UITests 17/17 after restoring `nearby_multiplayer_filter` AppStorage.
- Task 14 Harmony Hypium 34/34 on `127.0.0.1:5557`.
- Task 16 table filled with host/emulator evidence; physical radio/SAS/QR/latency stay NOT_RUN.

## Completed this session (continued 3)

- Task 14 Harmony: host CTest 12/12; unsigned HAP install after uninstall; Hypium `34/34 pass` on `127.0.0.1:5557` (`aa start TestAbility`). signingConfigs still empty.

## Completed this session (continued 2)

- Task 11 CONTENT-DUAL host GREEN: send-only does not open Rom; both consents transfer on stream_kind 5; GrantRead re-arms until WaitingImport; APPROVE_IMPORT + joiner SELECT.
- Task 12 REC-DUAL host GREEN: watchdog wired into DualSessionController/engine clock; two-engine 300ms freeze; input sequence ledger; ObjectStore root/WAL crash/CAS; REC01–09 reducer tests. STREAM stays UNAVAILABLE.

## Decided (do not re-litigate)

See `docs/handover-2026-09-16-nearby-dual.md` §6.
