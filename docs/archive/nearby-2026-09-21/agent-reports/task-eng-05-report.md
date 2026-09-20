> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# ENG-05 — pending config from catalog

Worktree: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`  
Status: **PARTIAL** (fingerprint + invalidation tightened; not complete, not playable)  
Commits: **none**

## Reproduce (before)

Independent diagnosis, then failing consent cases:

- A→B→A: revision 1→3, old A confirm still accepted (`apply_peer` compared id only).
- Same content, different local `catalog_revision` → different `pending_config_id`.
- Fingerprint omitted core/profile/options.

## What landed

- `pending_config_id` = domain hash `flynes-pending-config-v1` of content_id + local/authority seat stubs + core_id + profile_id + options_id (136 bytes). Local `catalog_revision` is not identity.
- Zero core/profile/options means unbound, not "don't care"; both ends must agree.
- Config change zeros both confirms and bumps revision. Rebinding A after B keeps the A fingerprint but a new revision; stale `(id, rev1)` is `FLY_SESSION_V2_STALE`.
- `SELECT` still does not write `dual_seats_confirmed`.

## Still open (do not mark complete)

- Catalog choice records still leave core/profile/options as zeros; confirm is refused until they are bound.
- Seat map / negotiated mode are not the D8 assignment yet.
- Confirm wire and dual runtime-ready remain ENG-06 / ENG-08 / ENG-09.

## Verify

Re-run `flynes_game_config_consent` after this correction. Not L2/L3/L4.
