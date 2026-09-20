> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# W3 round 2 — tamper matrix fail-open + SAS diagnosis

Worktree: `nearby-dual-e2e-harness`
Branch: `codex/nearby-dual-e2e-harness`

## Status

GREEN. Round-1 "fail-open" on commit/reveal, pair signature, and key-confirm was a harness bug (tampered GATT copies discarded on BACKPRESSURE). Persist + prefix-only rewrite already lived on this branch; this round added the assertions that would have caught that bug, watched them fail with persist disabled, restored persist, and re-ran green.

W0-owned files were not touched. `flynes_runtime_pcm_contention` and zip corpus were not fixed.

## What this round added

- Fail-closed assertions for the three previously fail-open pair-secure layers:
  injection landed, `tampered_logical_delivered >= 1`, neither engine
  `CONNECTED_LOBBY`, documented named states with CONNECTING or FAILED on at
  least one end, and `aead-open-fail` or `verify-fail` on the receiver.
- SAS diagnosis recorded in the test comment (and below): live engine does not
  compare SAS bytes; two-arg `PairPipeline::approve_local` is unused; public
  action has no SAS field (IF05). Not "fixed" by adding SAS bytes to the action.
- GATT persist (`RelayDirectionState::remember_replacement`) and prefix-only
  rewrite already present; reassembly mismatch is counted, not treated as a
  product failure.

## TDD evidence

### RED

Persist (`remember_replacement`) temporarily disabled. WSL:

```
export PATH="$HOME/.cargo/bin:$PATH"
cmake --build out/w3-linux/shared/build --target flynes_two_engine_tamper_matrix_test -j 24
ctest --test-dir out/w3-linux/shared/build --output-on-failure --tests-regex flynes_two_engine_tamper_matrix
```

Observed (exit 8):

```
  commit/reveal          landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
FAIL: a tampered commit/reveal actually CROSSED (observed 0)
FAIL: a tampered commit/reveal is rejected by the RECEIVER's own provider (aead-open-fail=0/0 verify-fail=0/0)
FAIL: a tampered commit/reveal NEVER reaches FLY_SESSION_LINK_CONNECTED_LOBBY_V2 (inviter=8 joiner=8)
  pair signature         landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
  key-confirm            landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
18 failure(s)
```

Landed-without-crossed plus both ends in lobby (8) is the round-1 harness bug.

### GREEN

Persist restored. Same ctest: `1/1 Test #91: flynes_two_engine_tamper_matrix ... Passed`. Direct binary:

```
  CONTROL (no tamper)    landed=yes crossed=0 inviter=8 joiner=8 aead-open-fail=0/0
  ... 10 other layers fail-closed, at least one end FAILED ...
  commit/reveal          landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
  pair signature         landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
  key-confirm            landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
  SAS layer: confirm offered=yes, inviter stage=1 joiner stage=1, SAS agrees=yes
flynes_two_engine_tamper_matrix passed
```

`flynes_two_engine_dual_run` also Passed (0.07 s). LoopbackTransport is the fixture's in-process transport, not Quinn.

## Pair-secure body layout (verbatim)

```
body[0]=version 1, [1..3]=reserved 0, [4..11]=message_counter u64be nonzero,
body[12..]=ciphertext || 16-byte AEAD tag
body_size = pair_secure_inner_size_v1(type) + 28
```

AEAD tag is the last 16 body bytes. `kLastBodyByte` xor therefore hits the tag; the GATT trailing domain hash is recomputed so the transport cannot eat the tamper.

## SAS diagnosis

The live engine at `session_engine.cpp:5598` calls `PairSignatureScheduler::approve_local(kind)`, which does not compare SAS bytes — it only forwards the approval kind. Two-arg `PairPipeline::approve_local(kind, displayed_sas)` (the only compare of `displayed_sas != *expected`) is unused; `PairPipeline` is never instantiated by the engine. Single-arg `PairPipeline::approve_local(kind)` reject-path is `entry_mode==1 && !known_path` (`pair_pipeline.cpp:174`), and even that overload is not on the live confirm branch. The public action has no SAS field (IF05): `fly_session_action_choice_v2` is boolean / invite-code / reference only, and `CONFIRM_SAS` is submitted with `choice_size == 0`; the confirm guard rejects any choice payload before apply. This is not exploitable through the public ABI and is not a "confirms a SAS it was never shown" defect: the engine derives SAS from the transcript and the human compares the two displayed codes. Do not "fix" by adding SAS bytes to the public action. Residual design observation: if confirmation was intended to be cryptographically bound to the displayed SAS, that binding is dead code (`PairPipeline`), not the shipped engine.

## Concerns

- Pair-layer sender stays `AUTHENTICATING` (5) because it never observes the peer's AEAD rejection; the receiver is `FAILED` (9). The named-state check is CONNECTING or FAILED on at least one end, not both. Tightening to "both CONNECTING or FAILED" would fail this honest asymmetry.
- GATT reassembly groups may start mid-message after BACKPRESSURE; mismatches are counted (`logical_tamper_reassembly_mismatches`) and are not treated as product failure. Only a complete record prefix is tampered.
- Full `ctest` in `out/w3-linux` still reports `flynes_runtime_pcm_contention` (Not Run / subobject-linkage `-Werror`) and `flynes_zip_payload_fixture_corpus_check` (Failed). Out of scope per brief.
- These tests prove engines drove every QUIC port primitive over in-process LoopbackTransport and rejected tampered bytes. They do not prove bytes went through Quinn (`flynes_loopback_quic_probe` / `flynes_quic_provider_linkage` own that).
