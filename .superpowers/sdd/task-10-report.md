# Task 10 remainder — W0 DUAL MVP wiring

**Status:** DONE_WITH_CONCERNS

## Commits

Uncommitted in this worktree at report time. Focused increment is green on host.

## Test

```
wsl -e bash -lc 'export PATH="$HOME/.cargo/bin:$PATH"; cd /mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes; cmake -S shared -B out/nearby-host-linux/shared/build -DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=$HOME/.cargo/bin/cargo; cmake --build out/nearby-host-linux/shared/build --target flynes_two_engine_dual_mvp_test; ./out/nearby-host-linux/shared/build/flynes_two_engine_dual_mvp_test'
```

CMake cache: `FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=ON`, cargo `/home/pippin/.cargo/bin/cargo`.

Output:

```
dual mvp: no content port, lobby unchanged
dual mvp: content catalog and SELECT_CONTENT
dual mvp: START_DUAL gates
dual mvp: 600-frame DUAL run
dual mvp: pause and disconnect freeze
flynes_two_engine_dual_mvp passed
```

EXIT:0. Evidence: `out/logs/task10-dual-mvp.log`.

## What landed

- Gap A: CONNECTED_LOBBY catalog walk via content port; EMPTY is a synchronous query return; SELECT_CONTENT uses 16-byte `source_choice_ref`.
- Gap C: DualSessionController owns catalog, START_DUAL, DualRunScheduler, State Commit bidi, local/remote CanonicalInputBundleV1, pause/disconnect freeze.
- START_DUAL without dual_runtime is UNAVAILABLE; without selection is REJECTED.
- Engine action reducer handles SELECT_CONTENT / START_DUAL / PAUSE / DISCONNECT.
- CMake cargo HINTS; dual sources in `flynes_session`; `flynes_two_engine_dual_mvp_test` registered.

## Concerns

- Digest acknowledgement and resume stay fail-closed (Task 12).
- Android unit / three-platform emulator not part of this binary; still required before Task 16.
- Existing lobby E2E and platform suites not re-run in this report.
