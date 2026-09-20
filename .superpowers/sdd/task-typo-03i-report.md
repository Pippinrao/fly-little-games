# TYPO-03.I — iOS unified dynamic type + reachability

**Status:** DONE_WITH_CONCERNS

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

**Simulator:** NOT_RUN (this machine is Windows; `ios/scripts/run_simulator_tests.py` was not executed). No screenshots were reused or fabricated.

## Files changed (this card)

NEW:

- `ios/app/NearbyTypography.swift` (in `FLYNES_PRODUCT_SWIFT`)
- `ios/tests/NearbyUxRestorationTests.mm` (in `FlyNESUITests MODULE` next to `NearbyUiParityTests.mm`)
- `tools/quality/tests/test_nearby_ios_typography.py`
- `.superpowers/sdd/task-typo-03i-report.md`

MODIFIED:

- `ios/app/CMakeLists.txt`
- `ios/app/NearbyFriendsView.swift`
- `ios/app/NearbyPairingView.swift`
- `ios/app/NearbyLobbyView.swift`
- `ios/app/NearbyFriendsManageView.swift`

Did not modify Game Center, `nearby_ui_v1.json`, VERSION, Harmony/Android, submit-disable / cancel-pop / devices-toolbar-vs-manage (REG-02.I), or footerAction/submitJoin logic.

## TDD RED

Wrote `tools/quality/tests/test_nearby_ios_typography.py` first (helper missing; N00 `.title3` vs N01 unscaled `system(size: 21)` / subtitle 14; wide N00 has no ScrollView; custom `Text("nearby.title")` stacked with `navigationTitle`; pairing left panes lock `frame(width: 224)` on narrow; segmented Picker). `NearbyUxRestorationTests.mm` was added as the FlyNESUITests case names before production wiring.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_typography.py -v
```

```
FAIL: test_typo_i_roles_and_scaling
AssertionError: False is not true : NearbyTypography.swift missing; N00 still uses title3 vs N01 size 21/subtitle 14 (n00='Text("nearby.entry.headline")\n                .font(.title3.weight(.semibold))\n ' n01='Text("nearby.invite.headline")\n                .font(.system(size: 21, weight: .')

FAIL: test_typo_i_wide_scroll
AssertionError: False is not true : N00 wide branch has no ScrollView; last action is unreachable at 640x360 large type because the footer clips the body

FAIL: test_typo_i_single_header
AssertionError: 1 != 0 : N00 must not stack a custom HStack title with navigationTitle; keep system back semantics

FAIL: test_typo_i_narrow_224_and_underline_tabs
AssertionError: Regex didn't match: 'wide|isWide' ... narrow must cancel left pane frame(width: 224) lock

Ran 4 tests in 0.002s
FAILED (failures=4)
```

RED reason: missing helper / unscaled pairing / clipped wide N00 / stacked header / 224 lock, not an import typo.

## TDD GREEN

`NearbyTypography` roles (paneTitle 21/600, muted 12, action/primaryAction 14, inviteCode 29 tracking .17em → 4.93 not 4, kicker 11/.13em → 1.43 not 1.6, codeInput 28/.16em, pageTitle 18, sectionTitle 15, body 14) with `ScaledMetric` + `UIFontMetrics`. Four Nearby views consume `nearbyRole` / `nearbyMinTap` (48 minimum, may grow). N00 wide path scrolls; one system `navigationTitle` + visible toolbar (no custom title HStack); pairing 224 only when `wide`; underline tabs (not segmented Picker); footer `ViewThatFits` wrap. Button order/enabled state unchanged.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_typography.py -v
```

```
....
Ran 4 tests in 0.004s
OK
```

```
python tools/quality/check_nearby_typography.py .
```

```
OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
```

```
python tools/quality/check_nearby_ui_contract.py .
```

```
OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present; no network results generated.
```

```
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
```

```
Ran 8 tests in 0.036s
OK
```

FlyNESUITests cases: `testTYPO_I_roles_and_scaling`, `testTYPO_I_wide_scroll`, `testTYPO_I_single_header`.

## Simulator / Mac

**FlyNESUITests: NOT_RUN.** Windows host cannot run `cmake --build build/ios-simulator --config Debug` or `python3 ios/scripts/run_simulator_tests.py <UDID> FlyNESUITests`. No 640×360 large-type screenshot. No Dynamic Type frame measurements from XCUITest.

## Remaining gaps

- On-simulator `TYPO_I_*` not executed; host/static evidence only.
- System navigation title still uses UIKit bar metrics, not `pageTitle` 18 via `nearbyRole` (keeps system back).
- REG-02.I submit disable / scan-create cancel pop / devices toolbar vs friends manage left unchanged.
- Do not claim L3 playable or simulator PASS.
