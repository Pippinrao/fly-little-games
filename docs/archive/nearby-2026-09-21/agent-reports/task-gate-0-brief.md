> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# GATE-0 — dual_mvp REC04 300ms freeze (ENG-01 leftover / ENG-10 pre)

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
Do **not** git commit. Do **not** create another worktree. Do **not** delete `flynes_runtime_pcm_contention` or `flynes_zip_payload_fixture_corpus_check`.

## Scene

Plan §9 GATE-0. ENG-01 left `flynes_two_engine_dual_mvp` FAIL on REC04 freeze (5 assertions). Do not mark L2/L3. Runtime is still Fake DualRuntime (L1). Transport is loopback.

Reproduce first. Distinguish **fixture clock** vs **engine watchdog**. Do **not** widen 300ms to make the test pass. Do **not** refresh peer liveness with a local send that fakes a healthy link.

WSL Ubuntu-24.04. Git inside WSL needs:
`GIT_DIR=/mnt/e/workspace/codes/games/fly-little-games/.git/worktrees/nearby-ui-acceptance-fixes`
`GIT_WORK_TREE=/mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`

Build dir: `out/nearby-playable/shared-linux` (already configured with Quinn ON).

```
wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes && cmake --build out/nearby-playable/shared-linux --target flynes_two_engine_dual_mvp_test --parallel 2 && ctest --test-dir out/nearby-playable/shared-linux -R "^flynes_two_engine_dual_mvp$" --output-on-failure --no-tests=error'
```

Expected current: FAIL. After fix: 1/1 PASS. Log the 5 assertions before and after.

TDD: if you change product/engine, keep the existing REC04 assertions; they must fail for the right reason first if you temporarily revert — the test already exists and is red.

Likely files (only if needed): `shared/src/session/dual/dual_session_controller.cpp`, `shared/src/session/engine/session_engine.cpp`, `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp`, `shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp` (do not weaken assertions).

Tamper matrix must stay meaningful (do not disable fail-closed).

## Report

`.superpowers/sdd/task-gate-0-report.md` with root cause, commands, exit codes, assertion text.

Return under 15 lines: Status, commits (none), ctest 1/1 or still FAIL, root cause, report path.
