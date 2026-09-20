> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# TYPO-02.A — Android local styles + narrow PAIR layout

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
HEAD: `6ec6605` on `codex/nearby-ui-acceptance-fixes` plus dirty UX/typography.
Do **not** create another worktree. Do **not** git commit. Do **not** push.
Do **not** install on a physical device.

## Scene

Consume `shared/schema/nearby_typography_v1.json`. F02/F05. Do not globally rewrite Material theme. Do not bump/downgrade Material. Do not restyle non-Nearby pages.

**Existing work already in this dirty tree — do not duplicate files:**

- `app/src/main/res/values/nearby_typography.xml` (roles 18/21/15/14/12/14/14/11/29/28)
- Layouts already reference `TextAppearance.FlyNes.Nearby.*`
- `NearbyPairingActivity.layoutPairingColumns` stacks when content-after-insets `<=580`, split when `>580`
- `NearbyFriendsActivity` same 580 rule
- `app/src/androidTest/java/com/flynes/emu/ui/NearbyUxRestorationTest.java` already has `TYPO_A_headline_resolves21` and `TYPO_A_pair_width320`

Plan: 已有修复若符合任务卡，只补验证，不推倒重写. **Do not create a second NearbyUxRestorationTest.**

## Remaining gaps to close (TDD)

1. **Host static check** (new `tools/quality/tests/test_nearby_android_typography.py`): assert XML `android:textSize` for PaneTitle=21sp, Muted=12sp, Action=14sp, InviteCode=29sp, CodeInput=28sp, Kicker letterSpacing 0.13, InviteCode 0.17. Negative: HeadlineSmall 24 / InviteCode 28 must FAIL. Layouts must reference the Nearby styles, not `textAppearanceHeadlineSmall`. Missing `nearby_typography.xml` FAIL. Watch RED if you first introduce a negative fixture; GREEN against the frozen file.

2. **Instrumentation on emulator** (this is the card's native evidence; source review is not PASS):
   - Confirm `adb devices` target is an emulator (currently `emulator-5554` product `sdk_phone64_x86_64` — emulator, OK). Pin `ANDROID_SERIAL`.
   - `.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug`
   - `.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"`
   - If tests FAIL, fix the smallest product/test issue. Do not delete assertions.
   - If emulator gone / physical only: NOT_RUN, do not claim visual PASS.

3. **Font scale 1 / 1.3 / 2** if not already asserted: add instrumentation (or Configuration override) that paneTitle stays role 21sp **base** and actual px grows; minHeight 48/60 still met; no shrinking tokens to pass. Android 14 nonlinear: measure actual, do not assume `sp * fontScale`. Do not disable accessibility.

4. **N00 320/580/640** if friends page still overflows: same content-width breakpoint as PAIR. QR 170 must fit; no HorizontalScrollView workaround.

5. Confirm `themes.xml` does **not** set app-wide HeadlineSmall to 21. Nearby styles stay local.

6. Keep Material dependency version. Keep join `minHeight` 60 as minimum that can grow.

## REG-01.A is NEXT card, same PAIR files

This card must **not** also rewrite paste/submit guards except if a typography/layout edit would reintroduce `maxLength=6`. If `maxLength=6` is present, remove it here only because it silently truncates (audit R01) — add a failing test first. Full paste/submit matrix is REG-01.A.

## Commands

```
python -m unittest discover -s tools/quality/tests -p test_nearby_android_typography.py
python tools/quality/check_nearby_typography.py .
python tools/quality/check_nearby_ui_contract.py .
$env:ANDROID_SERIAL = 'emulator-5554'   # or the actual emulator serial
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"
```

## Report

`.superpowers/sdd/task-typo-02a-report.md` with RED/GREEN, gradle exit codes, ANDROID_SERIAL, emulator vs physical, remaining gaps.

Return under 15 lines: Status, commits (none), test summary, concerns, report path.
