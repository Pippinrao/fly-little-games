> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# GATE-0 — dual_mvp REC04 300ms freeze

Worktree: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`  
Status: **PASS**  
Commits: **none** (per brief)  
ctest: **1/1 Passed** (`flynes_two_engine_dual_mvp`)

## Scene

Plan §9 GATE-0 / ENG-01 leftover. Runtime remains Fake DualRuntime (L1). Transport is loopback. Did not mark L2/L3. Did not delete `flynes_runtime_pcm_contention` or `flynes_zip_payload_fixture_corpus_check`.

## Reproduce (before)

WSL Ubuntu-24.04, existing `out/nearby-playable/shared-linux` (Quinn ON).

```
wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes && cmake --build out/nearby-playable/shared-linux --target flynes_two_engine_dual_mvp_test --parallel 2 && ctest --test-dir out/nearby-playable/shared-linux -R "^flynes_two_engine_dual_mvp$" --output-on-failure --no-tests=error'
```

- cmake build: success (`flynes_two_engine_dual_mvp_test`)
- **CTEST_EXIT: 8**
- 1/1 Test #100: `flynes_two_engine_dual_mvp` **Failed** 0.47 sec

REC04 assertions **before** (5 failures; earlier dual mvp cases printed OK):

```
dual mvp: REC04 300ms freeze / 30s deadline
FAIL: 300ms without peer activity freezes both engines
FAIL: freeze reason is authenticated-activity timeout
FAIL: local send after freeze does not resume stepping
FAIL: local send does not slide the freeze
FAIL: 30s deadline expires still frozen
5 failure(s)
```

## Root cause — fixture clock, not engine watchdog

Two clocks exist in the loopback harness:

| Clock | Where | What engines sample |
|---|---|---|
| `g_loopback_clock_ns` + `set_loopback_clock_ns()` | leftover global in `two_engine_loopback_fixture.hpp` | **No.** `EngineFixture` does not wire this. |
| `ClockFixtureV1` (`clock_fixture.continuous_ns`, default 1) | per-engine provider clock (W3 recovery matrix) | **Yes.** `clock.read_continuous = ClockFixtureV1::read` |

REC04 jumped `g_loopback_clock_ns` to `1 + 300ms` (then `+ 30s`). Each engine kept reporting `continuous_ns = 1`. `SessionEngine::run_work` already samples the clock port and calls `DualSessionController::on_clock` → `LinkActivityWatchdogV1` (`kLinkFreezeNsV1` = 300ms, `>=`). The watchdog never saw elapsed silence, so both ends stayed `GAME_RUNNING`.

Not:

- a 300ms threshold that needed widening
- missing watchdog wiring (reset at `start_dual`, freeze reason `FLY_SESSION_DUAL_FREEZE_AUTHENTICATED_ACTIVITY_TIMEOUT_V2` already exist)
- local send faking peer liveness (`on_local_send_complete` is a no-op; only `on_verified_peer_activity` / authenticated ACK refresh)

## Fix

Keep the five REC04 assertions and the 300ms / 30s numbers. Jump **both engines’** `ClockFixtureV1` to the same absolute timestamps the test already used. Local pad submit still only wakes `run_work`; it does not call `on_verified_peer_activity`.

- `shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp` — REC04 clock stimulus
- `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp` — comment that the global is leftover

No product/engine change. Tamper matrix untouched.

## Verify (after)

Same cmake + ctest command.

- cmake: rebuilt `test_two_engine_dual_mvp.cpp.o`, linked `flynes_two_engine_dual_mvp_test`
- **CTEST_EXIT: 0**
- 1/1 Test #100: `flynes_two_engine_dual_mvp` **Passed** 0.39 sec

```
100% tests passed, 0 tests failed out of 1
```

Direct binary stdout (after pass):

```
dual mvp: REC04 300ms freeze / 30s deadline
flynes_two_engine_dual_mvp passed
```

TDD: the existing REC04 checks were red for the right reason (clock port stayed at 1 ns). Reverting the `ClockFixtureV1` jump restores the same five FAILs. Threshold was not widened. Peer liveness was not refreshed from local send.
