> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# ENG-06 — dual confirm barrier before START_DUAL

Worktree: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`  
Status: **PARTIAL** (local BOOLEAN shortcut removed; peer confirm wire not landed; not playable)  
Commits: **none**

## Reproduce (before)

Production `session_engine` accepted `CONFIRM_GAME_CONFIG` + `CHOICE_BOOLEAN value=1` and set `pending_config_peer_confirmed` with no peer message and no test-build isolation. Docs saying "harness only" did not constrain the binary. `dual_mvp` then reached GAME_RUNNING / 600-frame / REC04 through that shortcut.

## What landed

- Engine: empty choice → `confirm_local_pending()` only. Non-empty choice (including BOOLEAN) → `INVALID_ARGUMENT`, peer bit unchanged.
- `START_DUAL` still requires local **and** peer pending-config confirms.
- Peer ingest is `DualSessionController::apply_peer_pending_confirm(id, revision)` after a verified message; mismatch is `STALE`.
- `test_two_engine_dual_mvp` asserts START unpublished / `peer_confirmed=0` / not GAME_RUNNING after local confirms. It does not BOOLEAN-start.

## Still open (do not mark complete)

- No authenticated QUIC/GATT confirm payload yet, so two public engines cannot both become runtime-ready.
- 600-frame / pause / REC04 paths are blocked on that wire; do not record them as L1 PASS.

## Verify

Re-run `flynes_game_config_consent` and `flynes_two_engine_dual_mvp` after this correction. Not L2/L3/L4.
