> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# TYPO-02.A — Android local Nearby TextAppearance + 320-width PAIR layout

**Status:** DONE_WITH_CONCERNS

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

**ANDROID_SERIAL:** `emulator-5554`  
**Emulator confirmation:** `adb devices -l` → `emulator-5554 device product:sdk_phone64_x86_64 model:Android_SDK_built_for_x86_64 device:emu64x`. `ro.kernel.qemu=1`, `ro.boot.qemu=1`, `ro.hardware=ranchu`, SDK 35, `wm size` 1080x2340 @ 440dpi (~393dp portrait), `font_scale=1.0`. Physical device not used.

## Files changed (this card)

NEW:

- `app/src/main/res/values/nearby_typography.xml`
- `app/src/androidTest/java/com/flynes/emu/ui/NearbyUxRestorationTest.java`
- `.superpowers/sdd/task-typo-02a-report.md`

MODIFIED:

- `app/src/main/res/layout/activity_nearby_friends.xml`
- `app/src/main/res/layout/activity_nearby_pairing.xml`
- `app/src/main/res/layout/activity_nearby_lobby.xml`
- `app/src/main/res/layout/activity_nearby_friends_manage.xml`
- `app/src/main/res/layout/view_nearby_lobby_row.xml`
- `app/src/main/java/com/flynes/emu/NearbyFriendsActivity.java` (content-width after insets, minHeight growth only)
- `app/src/main/java/com/flynes/emu/NearbyPairingActivity.java` (same)

Did not modify `themes.xml` global textAppearance, Material version in `app/build.gradle`, Game Center, Harmony/iOS, `nearby_ui_v1.json`, VERSION, original HTML, `android:maxLength`, or join-submit rules.

## TDD RED

Wrote `NearbyUxRestorationTest` first. Production Nearby TextAppearance / PAIR stack did not exist. Headline still `textAppearanceHeadlineSmall` (24sp); PAIR stayed horizontal (224+18 left, ~46dp remaining for 170dp QR at 320).

```
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"
```

```
Starting 2 tests on FlyNES_BuiltinContent(AVD) - 15

com.flynes.emu.ui.NearbyUxRestorationTest > TYPO_A_pair_width320[...] FAILED
	java.lang.AssertionError: QR 170 must be fully on-screen at content 320.0dp after insets; inner=288.0 left=224.0 remaining=46.0 (224+18+16*2+170=444 needed for side-by-side QR)

com.flynes.emu.ui.NearbyUxRestorationTest > TYPO_A_headline_resolves21[...] FAILED
	java.lang.AssertionError: paneTitle must be 21sp, not HeadlineSmall 24sp expected:<21.0> but was:<24.0>

Finished 2 tests on FlyNES_BuiltinContent(AVD) - 15
Total tests 2, failure 2
BUILD FAILED
```

RED reason: missing local paneTitle 21 / inviteCode 29 and PAIR still side-by-side at 320, not an import typo. Instrumentation discovered 2 tests (`0 tests` was not used as PASS).

## TDD GREEN

Nearby-only TextAppearance roles (paneTitle 21/600/27.3, muted 12, action/primaryAction 14, inviteCode 29 tracking .17em, codeInput 28 tracking .16em, kicker 11/.13em, pageTitle 18, sectionTitle 15, body 14). Layouts reference those styles; includeFontPadding false on Nearby text. PAIR/N00: content width after insets `<=580` vertical stack, `>580` 224/18 split. Buttons `wrap_content` + minHeight 48; code input minHeight 60. `android:maxLength="6"` unchanged.

```
python tools/quality/check_nearby_typography.py .
python tools/quality/check_nearby_ui_contract.py .
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"
```

```
OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present; no network results generated.
```

```
BUILD SUCCESSFUL  (:app:testDebugUnitTest :app:assembleDebug)
unit tests=468 failures=0 skipped=2
```

Skipped 2 are pre-existing `UnsupportedPayloadClassifierFixtureParityTest`, unrelated to Nearby typography. Not deleted.

```
Starting 2 tests on FlyNES_BuiltinContent(AVD) - 15
Finished 2 tests on FlyNES_BuiltinContent(AVD) - 15
BUILD SUCCESSFUL

instrumentation tests=2 failures=0 errors=0 skipped=0
  TYPO_A_pair_width320 time=4.698
  TYPO_A_headline_resolves21 time=1.664
```

Existing `NearbyFriendsTest` / `NearbyPairingTest` / `NearbyUiParityTest` still compile (androidTest javac). They did not lock HeadlineSmall/24sp.

## Remaining gaps

- Optional fontScale 1/1.3/2 Configuration overlay was not added (brief marked optional; emulator font_scale left at 1.0).
- `NearbyUiParityTest.wideLayoutUsesTheShared224By18Split` still requires content `>580dp`. This AVD is ~393dp in portrait; that suite was not re-run on-device in this card (compile only).
- REG-01.A still owns paste-7-digit / `maxLength`; this card did not change join-submit validation.
