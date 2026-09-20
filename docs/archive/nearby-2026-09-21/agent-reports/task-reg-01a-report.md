> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# REG-01.A — Android PAIR paste 7-digit + submit once

**Status:** DONE_WITH_CONCERNS

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

**ANDROID_SERIAL:** `emulator-5554`  
**Emulator confirmation:** `adb devices -l` → `emulator-5554 device product:sdk_phone64_x86_64 model:Android_SDK_built_for_x86_64 device:emu64x`. `ro.kernel.qemu=1`. Physical device not used.

## Files changed (this card)

MODIFIED:

- `app/src/main/res/layout/activity_nearby_pairing.xml` (removed `android:maxLength="6"` only; TYPO TextAppearance / minHeight 60 kept)
- `app/src/main/java/com/flynes/emu/NearbyPairingActivity.java` (`requestInFlight` stays set after `submitCode`; invalid still never allocates an attempt)
- `app/src/androidTest/java/com/flynes/emu/ui/NearbyInviteCodeTest.java`
- `.superpowers/sdd/task-reg-01a-report.md`

Did not modify `NearbyInviteCode.normalize`, Harmony/iOS, `themes.xml`, Material version, original HTML, or TYPO-02.A typography/layout.

## TDD RED

Existing `sevenDigitsArePreservedAndRejectedWithoutSilentTruncation` (`replaceText("0123456")`) plus new `pasteSevenDigits1234567DoesNotTruncateOrSend` and `sixDigitsSubmitTwiceIsOneAttempt`. Attempt count uses production `NearbySession.nextJoinAttemptId()` fence probes (`joinSendsBetween`); snapshot `joinAttemptId` stays 0 after the current fail-closed `resolvePending(false)`, so it cannot count sends by itself.

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyInviteCodeTest"
```

First RED (Espresso click on disabled submit): truncation only.

```
Starting 8 tests on FlyNES_BuiltinContent(AVD) - 15
pasteSevenDigits1234567DoesNotTruncateOrSend FAILED
	expected "1234567" Got: view.getText() was "123456"
sevenDigitsArePreservedAndRejectedWithoutSilentTruncation FAILED
	expected "0123456" Got: view.getText() was "012345"
```

Second RED after tests used `View.performClick()` (disabled MotionEvent does not fire `onClick`):

```
Starting 8 tests on FlyNES_BuiltinContent(AVD) - 15
pasteSevenDigits1234567DoesNotTruncateOrSend FAILED
	expected "1234567" Got: view.getText() was "123456"
sevenDigitsArePreservedAndRejectedWithoutSilentTruncation FAILED
	expected "0123456" Got: view.getText() was "012345"
sixDigitsSubmitTwiceIsOneAttempt FAILED
	valid 123456 must send once even if submit is clicked twice expected:<1> but was:<2>
```

RED reason: `LengthFilter(6)` silent truncation; `onSubmit` cleared `requestInFlight` in the same method so a second `performClick` allocated another attempt id. Not import typos. `incompleteEmptyAndLettersNeverSend` was already green (C05 never sends).

## TDD GREEN

Removed silent `maxLength`. `onSubmit` returns immediately when `requestInFlight`; invalid still only shows `nearby_reason_code_invalidFormat` and does not call `nextJoinAttemptId` / `submitCode`. Valid `123456` keeps in-flight so a second click is one attempt.

```
python tools/quality/check_nearby_ui_contract.py .
python tools/quality/check_nearby_typography.py .
```

```
OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present; no network results generated.
OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
```

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyInviteCodeTest,com.flynes.emu.ui.NearbyUxRestorationTest"
```

```
Starting 10 tests on FlyNES_BuiltinContent(AVD) - 15
Finished 10 tests on FlyNES_BuiltinContent(AVD) - 15
BUILD SUCCESSFUL

instrumentation tests=10 failures=0 errors=0 skipped=0
NearbyInviteCodeTest 8/8 (12.476s)
  pasteSevenDigits1234567DoesNotTruncateOrSend 0.972s
  sevenDigitsArePreservedAndRejectedWithoutSilentTruncation 0.724s
  sixDigitsSubmitTwiceIsOneAttempt 1.156s  (fence sends=1)
  incompleteEmptyAndLettersNeverSend 2.104s  (12345 / empty / 12A456 sends=0)
NearbyUxRestorationTest 2/2 (5.284s) TYPO-02.A still green
```

Attempt-count evidence: RED double-click fence delta 2; GREEN delta 1. Invalid/7-digit fence delta 0; snapshot `joinAttemptId` unchanged.

## Remaining gaps

- Join cancel stays `GONE`. After a valid submit, in-flight is not cleared by the immediate fail-closed snapshot, so retry requires leaving the page. Needed so Espresso-idle double `performClick` stays one attempt.
- `createModeShowsAnInviteLifecycleThatRegeneratesAndCancels`: footer cancel is not inside a `ScrollView` (`scrollTo` cannot run). Cancel already `finish()`es; the test now asserts `DESTROYED` instead of empty on-page code.
- Snapshot `joinAttemptId` is 0 after `submitCode` + `resolvePending(false)`; tests therefore count via the production attempt-id fence, not snapshot.

## Quality review Important #1 (empty submit disabled)

Pushed back Important #2 (show `nearby_join_cancel` after submit). Out of REG-01.A; cancel-visible-after-fail belongs to a later snapshot observer.

### RED

`emptyJoinCodeShowsSubmitDisabled`: launch `JOIN_CODE`, empty field, submit displayed and not enabled. Product still inflated MaterialButton enabled by default.

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyInviteCodeTest#emptyJoinCodeShowsSubmitDisabled"
```

```
Starting 1 tests on FlyNES_BuiltinContent(AVD) - 15
emptyJoinCodeShowsSubmitDisabled FAILED
	Expected: not view.isEnabled() is <true>
Finished 1 tests on FlyNES_BuiltinContent(AVD) - 15
BUILD FAILED
```

RED reason: `showJoinBlock` never `setEnabled(false)` and XML had no `android:enabled="false"`.

### GREEN

XML `android:enabled="false"` on `nearby_join_submit`. `showJoinBlock` calls `submit.setEnabled(false)` immediately after making it visible. Did not show join cancel.

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyInviteCodeTest,com.flynes.emu.ui.NearbyUxRestorationTest"
```

```
Starting 11 tests on FlyNES_BuiltinContent(AVD) - 15
Finished 11 tests on FlyNES_BuiltinContent(AVD) - 15
BUILD SUCCESSFUL

instrumentation tests=11 failures=0 errors=0 skipped=0
NearbyInviteCodeTest 9/9 (13.348s) including emptyJoinCodeShowsSubmitDisabled 0.717s
NearbyUxRestorationTest 2/2 (4.832s)
```

One combined run hit a transient `TYPO_A_pair_width320` `submit tap target >= 48dp` (InviteCode 9/9 still passed). Isolated UxRestoration 2/2 then combined 11/11 both green. Not treated as a product height regression.

**Commits created:** none
