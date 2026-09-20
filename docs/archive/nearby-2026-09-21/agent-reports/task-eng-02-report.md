> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# ENG-02 — freeze min seams, no platform-invented state

Worktree: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`  
Status: **PASS** (ABI freeze only; not playable)  
Commits: **none**

## Scene

Plan ENG-02. UX-00.S already projects screens. This card freezes the read/write seams so platforms cannot invent `connected` / `confirmed`.

## Reproduce (before)

WSL Ubuntu-24.04, `out/nearby-playable/shared-linux`.

Appended `_Static_assert` for `FLY_SESSION_SNAPSHOT_V2_R2_SIZE` / `pending_config_id` to `test_session_v2_abi.c` while the header still ended at `dual_pcm_digest`.

```
cmake --build out/nearby-playable/shared-linux --target flynes_session_v2_abi_test
```

Compile **RED**: `‘fly_session_snapshot_v2’ has no member named ‘pending_config_id’`.

## What landed

- Tail-append on `fly_session_snapshot_v2`: `pending_config_id[32]`, local/peer confirm u32, `pending_config_revision` u64. `R2 = offsetof(pending_config_id) = R1+96`; `SIZE = R2+48`. `fly_session_view_read_v2` still copies `min(declared, SIZE)`.
- `docs/superpowers/specs/2026-09-17-nearby-playable-seams.md` maps wait-host / SAS / connected friend / select / pending config / dual confirm / pause-end onto existing action kinds. No STREAM. `START_DUAL=43` has no UX `ActionId`.
- `session_action_kind(ActionId)` reuses frozen kinds (`SelectGame` → `SELECT_CONTENT=42`, `ConfirmConfig` → `24`, not `43`).
- Did **not** change `dual_seats_confirmed = selected_` (ENG-05). Did **not** enable N09 confirm. Did **not** implement ENG-06/08/09.

## Verify (after)

```
cmake --build out/nearby-playable/shared-linux --target flynes_session_v2_abi_test flynes_session_v2_contract_test flynes_product_nearby_ui_test
ctest --test-dir out/nearby-playable/shared-linux -R session_v2_abi
ctest --test-dir out/nearby-playable/shared-linux -R session_v2_contract
ctest --test-dir out/nearby-playable/shared-linux -R nearby_product_ui
```

- `flynes_session_v2_abi` **Passed 0.00s**
- `flynes_session_v2_contract` **Passed 0.01s** (R2 prefix canary + empty pending_config)
- `nearby_product_ui` **Passed 0.00s**

Not L2/L3/L4. Hypium / iOS XCTest still NOT_RUN.
