# W3 round 2 — tamper matrix fail-open + SAS diagnosis

Work ONLY in:
`E:\workspace\codes\games\fly-little-games\.worktrees\nearby-dual-e2e-harness`

Branch: `codex/nearby-dual-e2e-harness`.

## Do not touch (W0 owns)

`shared/include/flynes/flynes_session.h`
`shared/src/session/engine/session_engine.*`
`shared/src/session/flynes_session_v2.cpp`
`shared/src/session/ports/session_ports.hpp`
`shared/src/session/ports/provider_events.cpp`
`shared/CMakeLists.txt`
`shared/schema/`
`VERSION` and platform version metadata
`pair_pipeline.cpp` / `session_engine.cpp` — diagnosis only.

## Already uncommitted (continue, do not revert)

Fixture: persist tampered GATT replacements across BACKPRESSURE retries (`RelayDirectionState::remember_replacement`); fix reassembly prefix vs whole-group rewrite; count `tampered_logical_delivered`.
Tamper matrix: move CommitReveal / PairSignature / KeyConfirm into the fail-closed loop; print aead-open/verify counters.
Dual-run comment: LoopbackTransport is not real Quinn.

Pair-secure body layout (do not guess last byte):
```
body[0]=version 1, [1..3]=reserved 0, [4..11]=message_counter u64be nonzero,
body[12..]=ciphertext || 16-byte AEAD tag
body_size = pair_secure_inner_size_v1(type) + 28
```
AEAD tag is last 16 body bytes. Round-1 fail-open was harness throwing away tampered copies on BACKPRESSURE.

## Required

1. TDD if you add new assertions: watch fail, then green.
2. Run the tamper matrix in WSL with cargo on PATH (`bash -lc`, `~/.cargo/bin`). Long ctest options.
3. Every previously fail-open layer: injection landed AND crossed>=1 AND neither engine CONNECTED_LOBBY AND named state CONNECTING or FAILED AND aead-open-fail or verify-fail on the receiver.
4. SAS: live engine calls `PairSignatureScheduler::approve_local(kind)` (`session_engine.cpp` ~5598) which does NOT compare SAS bytes. Two-arg `PairPipeline::approve_local(kind, displayed_sas)` is unused. Single-arg PairPipeline reject-path is `entry_mode==1 && !known_path`. Public action has no SAS field (IF05). Write diagnosis in the test comment / a report file. Do not "fix" by adding SAS bytes to the public action.
5. GATT reassembly: group may start mid-message after BACKPRESSURE; count mismatch, do not treat as product failure; only tamper a complete record prefix.
6. Commit on this W3 branch when green. Do not amend W0 commits.
7. Do not fix `flynes_runtime_pcm_contention` or zip corpus.

## Report

Write `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-dual-e2e-harness\.superpowers\sdd\w3-tamper-report.md`.
Return: status, commits, test summary, SAS diagnosis one paragraph, concerns.
