> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# REG-01.A — Android PAIR paste 7-digit + submit once

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
Do **not** create another worktree. Do **not** git commit. Physical device forbidden. Pin `ANDROID_SERIAL=emulator-5554` after confirming emulator.

## Scene

TYPO-02.A just restored Nearby TextAppearance and 320-width PAIR. **Do not revert those.** This card is plan §6 REG-01.A.1 / REG-01.A.2 (audit R01).

Current: `activity_nearby_pairing.xml` has `android:maxLength="6"` on `nearby_join_code_input`. Paste `1234567` is silently truncated to 6 digits so submit can enable. C05 `NearbyInviteCode.normalize` already rejects length != 6 — keep that rule. Do **not** change normalize to accept 7 digits.

`onSubmit` sets `requestInFlight` then clears it at the end of the same method, so a second click can call `submitCode` again.

Existing `NearbyInviteCodeTest.sevenDigitsArePreservedAndRejectedWithoutSilentTruncation` uses `replaceText("0123456")`. If maxLength still applies, that test is the RED (or write a paste of `1234567`). Do not delete existing tests.

## Vectors (must fail first, then pass)

REG-01.A.1: paste/replace `1234567` — field still contains `1234567` (not `123456`); format reason visible after submit attempt or as disabled+error policy already in C05; submit **disabled**; `NearbySession.submitCode` / join attempt count stays 0.

REG-01.A.2: `12345` / empty / `12A456` never send; `123456` sends **once** even if submit is triggered twice quickly. Leading-zero `012345` still valid (C05). Do not invent a new code rule.

Count attempts via existing `NearbySession.snapshot()` joinAttemptId / nextJoinAttemptId, or a production-visible generation already on the session — do not add a test-only `getSubmitCount()` if snapshot already works. If you need a hook, keep it ABI-honest (attempt id fence).

## Files

- `activity_nearby_pairing.xml` (remove silent truncation; keep TYPO styles/minHeight)
- `NearbyPairingActivity.java` (submit disabled for non-6; in-flight stays until cancel/failure snapshot; double-click one attempt)
- Tests: extend `NearbyInviteCodeTest.java` and/or `NearbyUxRestorationTest.java` — do not create a second restoration class

Do not change Harmony/iOS, themes.xml, Material version, `NearbyInviteCode.normalize` acceptance rules, original HTML.

## TDD

Write/adjust failing assertions first. Run:

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyInviteCodeTest"
```

Also re-run `com.flynes.emu.ui.NearbyUxRestorationTest` so TYPO-02.A stays green.

Keep `python tools/quality/check_nearby_ui_contract.py .` and typography checker green.

## Report

`.superpowers/sdd/task-reg-01a-report.md` with RED/GREEN, attempt-count evidence, files.

Return under 15 lines: Status, commits (none), tests, concerns, report path.
