# W3 tamper round 2 — task-scoped review

**Spec-compliance verdict: PASS**

**Range:** 697c4a1..5e8e73f (W3 commits `e8375c0`, `5e8e73f` only; W0 merge `08f2afd` excluded)
**Files:** `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp`, `shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp`, `shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp`, `.superpowers/sdd/w3-tamper-report.md`
**Task gate:** requirements first, then build quality. Not a merge review.

---

## Spec compliance

Checked against `w3-tamper-brief.md` and the global constraints. Implementer evidence in `w3-tamper-report.md` was taken as given; the suite was not re-run.

| Requirement | Verdict | Evidence |
|---|---|---|
| Do not touch W0-owned files (`flynes_session.h`, `session_engine.*`, `flynes_session_v2.cpp`, `session_ports.hpp`, `provider_events.cpp`, `shared/CMakeLists.txt`, `shared/schema/`, VERSION hand-edits, `pair_pipeline.cpp`) | PASS | Diff is tests + report only. SAS/pipeline cites are diagnosis in comments. |
| Pair-secure body: v1 / reserved 0 / u64be counter nonzero / ciphertext\|\|16-byte tag / size = inner+28 | PASS | Recorded verbatim in the tamper-matrix comment. `PairSignature` and `KeyConfirm` xor `kLastBodyByte` (last body byte = AEAD tag). GATT domain hash is recomputed so the transport cannot swallow the tamper. `PairCommit` (type 4) is **not** a pair-secure type (`pair_secure_inner_size_v1` is 6, 7, 17, 21, 23–26); commit/reveal correctly tampers plaintext commitment at body 48. |
| Previously fail-open layers: landed AND crossed≥1 AND neither `CONNECTED_LOBBY` AND named state CONNECTING or FAILED AND aead-open-fail or verify-fail on the receiver | PASS | `CommitReveal` / `PairSignature` / `KeyConfirm` now run through `a_tampered_layer_cannot_reach_the_lobby`. Assertions: `injection_landed`; `tampered_groups_delivered >= 1`; neither end lobby; CONNECTING or FAILED on at least one end; `aead_open` or `verify` failure on either pump (direction filter is `None`, so the receiver is whichever end opened the tampered record). Report GREEN: landed=yes, crossed=1, inviter=5 / joiner=9, aead-open-fail=0/1 for all three. Extra “at least one end FAILED” is a conservative tightening of CONNECTING-or-FAILED; GREEN still satisfies it. |
| SAS: diagnose only; do not add SAS bytes to the public action (IF05) | PASS | Confirmed against live code: `session_engine.cpp:5598` calls one-arg `PairSignatureScheduler::approve_local(kind)`; two-arg `PairPipeline::approve_local(kind, displayed_sas)` at `pair_pipeline.cpp:164` is unused; `PairPipeline` is never instantiated under `shared/src/session` except its own files + a friend declaration; public `fly_session_action_choice_v2` is boolean / invite-code / reference only; confirm guard requires `choice_size == 0`. No ABI change. |
| GATT reassembly: mid-message groups after BACKPRESSURE counted, not product failure; only tamper a complete record prefix | PASS | Size check is `logical.size() < 8+body+32` → increment `gatt_reassembly_mismatches_`, return false. Prefix rewrite writes only `record_size` bytes back into fragments. Persist via `RelayDirectionState::remember_replacement` so BACKPRESSURE retries re-offer tampered bytes. `note_tampered_group_delivered` only after `relay_fragment_once` ACCEPTED. |
| TDD if new assertions: watch fail, then green | PASS (as claimed) | Report RED with persist disabled: landed=yes, crossed=0, both lobby 8, aead 0/0, 18 failures. GREEN after restore. Temporary disable is not in the commits, which is the right TDD shape. Suite not re-run here. |
| Commit on this W3 branch; do not amend W0 | PASS | Two new commits on the W3 line. |
| Do not fix `flynes_runtime_pcm_contention` or zip corpus | PASS | Untouched; leftover failures disclosed in the report. |
| Dual-run comment: LoopbackTransport is not Quinn | PASS | `test_two_engine_dual_run.cpp` and the tamper-matrix file header state in-process loopback vs `flynes_loopback_quic_probe` / `flynes_quic_provider_linkage`. |

No planned functionality is missing. No forbidden files were edited.

---

## Strengths

- The round-1 “fail-open” is correctly attributed to a harness bug (tampered copy discarded on BACKPRESSURE), not an engine defect. Persist-in-direction-state is the minimal fix that makes a negative case mean something.
- `tampered_logical_messages` (injection landed) is now distinct from `tampered_logical_delivered` (tampered bytes ACCEPTED by the sink). That is the counter that would have caught round 1.
- Pair-layer fail-closed is asserted at the receiver’s own provider (`crypto_aead_open_failures` / `crypto_verify_failures`), not only at link state.
- SAS diagnosis is end-to-end and accurate: live confirm does not compare SAS bytes; IF05 has no SAS field; residual `PairPipeline` dead-code observation is recorded rather than “fixed” by widening the ABI.
- Asymmetry is documented and tested honestly: sender stays `AUTHENTICATING` (5), receiver `FAILED` (9). Tightening to “both CONNECTING or FAILED” would have been a false requirement.

---

## Issues

No CRITICAL, HIGH, or MEDIUM issues.

Optional polish (not blocking):

- `relay_one_direction` still has the pre-persist comment (“copied only while armed… ordinary path delivers without a copy”) immediately above the persist comment. Harmless leftover.
- `reassembly_mismatches` is still captured on `TamperOutcome` but dropped from the `run_case` printf. The counter exists; a mismatch is just less visible in the log.

---

### Strengths (quality)

Harness-only change, real engines, real wire, no peer-reducer injection, no already-verified evidence. Prefix-only GATT rewrite plus persistent replacements match the failure mode they were asked to close.

### Issues

#### Critical (Must Fix)

None.

#### Important (Should Fix)

None.

#### Minor (Nice to Have)

1. **Stale comment above persist**
   - File: `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp` (~4265)
   - The old “copy only while armed” paragraph is now next to the persist paragraph. Delete the old one if touching the file again.

2. **Reassembly mismatch no longer printed**
   - File: `shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp` `run_case` printf
   - Still counted, not treated as product failure. Printing it would make a mid-message group visible without asserting it.

---

## Review Summary

| Severity | Count | Status |
|----------|-------|--------|
| CRITICAL | 0     | pass   |
| HIGH     | 0     | pass   |
| MEDIUM   | 0     | pass   |
| LOW      | 0     | pass   |

**Spec-compliance: PASS**

Verdict: APPROVE — requirements met; no CRITICAL or HIGH quality issues. Ready to proceed.

**Ready to merge?** Task-scoped: yes, this task is complete. (Not a statement about merging the surrounding W0/W3 branch.)

**Reasoning:** The three previously fail-open pair layers now require landed, crossed, no lobby, CONNECTING/FAILED, and a receiver crypto reject; SAS is diagnosed without an ABI change; GATT persist and prefix rewrite implement the stated harness contract; W0-owned product files were not touched.
