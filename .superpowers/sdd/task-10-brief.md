# Task 10 remainder — W0 DUAL MVP wiring

You work ONLY in:
`E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`

Branch: `codex/nearby-ui-acceptance-fixes`. Do not edit main or other worktrees.

## Skills

TDD: failing test first, watch it fail, then minimal code. Verification before any PASS claim: run the command, read output.

C++ tests run in WSL Ubuntu-24.04 only. Windows only `cmake --build`. Always:

```
wsl -e bash -lc 'export PATH="$HOME/.cargo/bin:$PATH"; cd /mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes; ...'
```

ctest: long options only (`--tests-regex`, `--build-config`, `--test-dir`, `--label-regex`).
Configure with `-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=$HOME/.cargo/bin/cargo` and abort if cache is OFF or cargo NOTFOUND.

Do not "fix" `flynes_runtime_pcm_contention` or `flynes_zip_payload_fixture_corpus_check`.
Do not amend commits `1bfabf6`, `2128e7f`, `d3f1e2d`. Append only.
Do not hand-edit VERSION / versionName / versionCode; pre-commit hook bumps PATCH.
Commit when a focused increment is green. Message: why, not what.

## Already present (uncommitted)

Content port ABI draft: `fly_session_content_port_v2` tail-appended on `fly_session_ports_v2` with `FLY_SESSION_PORTS_V2_R2_SIZE = offsetof(content)`. Missing port is legal. `SessionPorts` captures it. ABI static asserts exist in `test_session_v2_abi.c`. **Keep and complete this; do not delete.**

## Gap A — read-only content port (user-approved)

1. Add `FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2 = 290` as a **hash** payload (terminal=1), same form as OBJECT_IMMUTABLE. Register in `provider_events.cpp` `contract_for`.
2. `SessionPorts::query_content(token, index, inbox)` / `cancel_content(token)`.
3. Canonical record (already documented in the header): version u16be=1, reserved 2 bytes zero, source_choice_ref[16], content_id[32], display_name_size u32be, name UTF-8. Hash = SHA256("flynes-content-choice-v1" || u32be(len) || bytes).
4. Out-of-range index: **synchronous** `query()` returns `FLY_SESSION_V2_EMPTY` (value 2). Do NOT send EMPTY as an async event result — `parse_provider_event_v2` rejects `result > OK`.
5. SELECT_CONTENT_V2 uses `FLY_SESSION_CHOICE_REFERENCE_V2` with **choice_id[16] = source_choice_ref**. Never a raw 32-byte hash in the action (IF05).
6. On CONNECTED_LOBBY, if `has_content()`, walk index 0,1,... until EMPTY. Fill `view->game_choices` and publish SELECT_CONTENT. No content port: identical to today (empty choices, no SELECT_CONTENT). Existing lobby E2E must stay green.

## Gap C — engine DUAL path

Today:
- Action catch-all `session_engine.cpp` ~5642 rejects PROPOSE_GAME/START_DUAL/SELECT_CONTENT/PAUSE/... as INVALID_STATE.
- `publish_link_view_locked` hardcodes `GAME_NOT_STARTED` and never sets `SCOPE_GAME_V2`.
- `submit_input` requires SCOPE_GAME_V2 + GAME_RUNNING → unreachable.
- `DualRuntimePort` never called.
- Only bind stream + one Control bidi stream. Control is opened as **bidi** (`open_quic_stream(true, ... Control)`). Implement State Commit the same way (bidi, `wire::QuicChannel::StateCommit`). Uni streams are `unavailable` in the fixture; do not block MVP on uni.
- `FLY_SESSION_PROVIDER_QUIC_DATA_V2` is non-terminal: parse via `parse_provider_event_v2`, **never** `ProviderOperationJournal`.
- Do **not** put `DUAL_RUN_FENCE_V1` (0x0201) on the State Commit allow-list.

Wiring:
1. After both ends SELECT_CONTENT the same content_id, publish START_DUAL (defaults: inviter seat 0 / authority, joiner seat 1).
2. START_DUAL: require `has_dual_runtime()`, non-zero matching `DualContentRefV1.content_hash`, `DualRunSchedulerV1::begin(Dual, ...)`. Attach `dual_runtime_adapter`.
3. Open State Commit bidi stream; send/receive 154-byte `CanonicalInputBundleV1` (`canonical_input_wire.hpp`).
4. `submit_input`: accept when GAME_RUNNING + SCOPE_GAME_V2; normalize dpad via existing helper; `accept_local_input`; encode and write to State Commit.
5. Incoming bundles → `accept_remote_input` then `step_next_frame` as the window allows.
6. Publish snapshot dual_* fields including digests each view.
7. PAUSE / DISCONNECT_LINK / QUIC terminal: freeze, no new step, no silent single-player, no STREAM.
8. `acknowledge_peer_digest` / resume-to-RUNNING: leave fail-closed, do not fake. Tests compare **local** snapshot digests every 60 frames.

Math fact: with only local seat input, `commit_frontier_` freezes at prediction depth 10. Peer input MUST actually cross State Commit.

## Fixture

`shared/tests/nearby/harness/two_engine_loopback_fixture.hpp` (W0 copy):
- Optional content catalog (default off). One deterministic choice when on.
- Pump answers CONTENT_CHOICE hash completions with exact record bytes (`deliver_provider_hash_buffer`).
- Optional dual_runtime C port backed by a deterministic fake (see `test_dual_runtime_seam.cpp`) so 600 frames do not need a real ROM.
- Add `submit` helper that copies `choice_id[16]` for SELECT_CONTENT (existing `submit_reference` leaves choice_id zero — that is for JOIN_CANDIDATE).

## Tests (RED then GREEN)

New focused binary, register in `shared/CMakeLists.txt` Task 10 block with labels `nearby_integration`:
1. No content port: lobby still CONNECTED_LOBBY, `game_choice_count==0`, no SELECT_CONTENT/START_DUAL.
2. Content port: after lobby, both engines publish one game_choice; SELECT_CONTENT with published source_choice_ref APPLIED; wrong 16-byte ref REJECTED; raw-looking 32-byte hash in choice_id rejected (only 16 bytes exist).
3. START_DUAL without selection REJECTED; without dual_runtime UNAVAILABLE; with both → GAME_RUNNING, SCOPE_GAME_V2, dual_mode=DUAL.
4. 600 frames, P1 and P2 each ≥100 distinguishable edges, snapshot digests equal every 60 frames, STREAM never selected.
5. Pause and disconnect freeze both ends.

Reuse the compact lobby driver from W3 `test_two_engine_dual_run.cpp` `two_public_engines_reach_the_lobby()` (copy into W0 test; do not edit W3 files).

Also pin `FLYNES_ENABLE_RUST_QUIC_PROVIDER`: `find_program(cargo HINTS $ENV{HOME}/.cargo/bin $ENV{USERPROFILE}/.cargo/bin)`. Do not FATAL_ERROR on Windows if cargo missing (Windows only compiles). If the option is ON and cargo is found, never FORCE OFF.

## CMake cargo

```
find_program(FLYNES_CARGO_EXECUTABLE cargo
    HINTS "$ENV{HOME}/.cargo/bin" "$ENV{USERPROFILE}/.cargo/bin")
```

## Android

After ABI changes: `Set-Location` to THIS worktree, one gradle process, `:app:testDebugUnitTest`. Read results from THIS worktree `app/build/test-results/testDebugUnitTest`, not the main repo.

## Report

Write `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes\.superpowers\sdd\task-10-report.md` with: status (DONE/DONE_WITH_CONCERNS/BLOCKED), commits, test command + output summary, remaining gaps.

Return only: status, commits, one-line test summary, concerns.
