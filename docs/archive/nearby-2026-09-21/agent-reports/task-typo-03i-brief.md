> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# TYPO-03.I — iOS unified dynamic type + reachability

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
Do **not** git commit. Do **not** create another worktree. This machine is Windows: **do not claim simulator PASS**. If you cannot run `ios/scripts/run_simulator_tests.py`, mark FlyNESUITests **NOT_RUN**. Do not reuse old screenshots.

## Scene

F03/F05; UX-01.I / UX-04–07.I typography. Consume `shared/schema/nearby_typography_v1.json`.

N00 uses `.title3.semibold` / `.subheadline` (dynamic). N01–03 uses fixed `.system(size: 21/14/29)` with tracking 4 / 1.6. Unify via NearbyTypography: **base role + Dynamic Type** (`ScaledMetric` / `UIFontMetrics`). Do not freeze N00 to unscaled 21. Do not leave N01–03 unscaled.

**Do not implement REG-02.I here** (submit disable, scan/create cancel pop, devices toolbar vs friends manage). Leave footerAction/submitJoin logic for REG-02.I. This card is type roles, scroll, single header, narrow layout, tab look.

## Files

NEW:

- `ios/app/NearbyTypography.swift` — must be in `FLYNES_PRODUCT_SWIFT` in `ios/app/CMakeLists.txt` so the product app compiles it, not tests-only.
- `ios/tests/NearbyUxRestorationTests.mm` if missing — add to `FlyNESUITests MODULE` in `ios/app/CMakeLists.txt` next to `NearbyUiParityTests.mm`. Do not create a second class name.

MODIFY: `NearbyFriendsView.swift`, `NearbyPairingView.swift`, `NearbyLobbyView.swift`, `NearbyFriendsManageView.swift`.

Python host check allowed in `tools/quality/tests/test_nearby_ios_typography.py`: helper exists, four views import/use it, N00 and N01 share paneTitle/muted tokens, no leftover `.title3` on N00 headline vs fixed 21 only on pairing.

## Roles

paneTitle 21/600, muted 12, action/primaryAction 14, inviteCode 29/600 tracking .17em (~4.93 not 4), kicker 11/.13em (~1.43 not 1.6), codeInput 28/.16em, pageTitle 18, sectionTitle 15, body 14.
min tap 48. 48 is minimum; large type may grow.

## TDD

1. `TYPO_I_roles_and_scaling`: N00 and N01 same role measurement; default title 21/600, muted 12; raising Dynamic Type changes **both** pages. On Windows this is helper + static import tests first (must RED: N00 title3 vs N01 size 21/subtitle 14).
2. `TYPO_I_wide_scroll`: 640×360, large type, English, last action reachable; wide branch has ScrollView or equivalent; footer does not cover body.
   Current N00 wide branch has **no ScrollView** (`NearbyFriendsView` ~49–55).
3. `TYPO_I_single_header`: only one mockup title/back. N00 currently has custom HStack title **and** `.navigationTitle("nearby.title")` — must not stack both. Keep system back semantics.
4. Narrow: cancel left pane `frame(width: 224)` lock; footer wraps by available width, keep button order/state. Restore underline tabs, not segmented Picker as “equal”.

## Commands

```
python tools/quality/check_nearby_typography.py .
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_typography.py
```

Mac-only (NOT_RUN here): `cmake --build build/ios-simulator --config Debug` then `python3 ios/scripts/run_simulator_tests.py <UDID> FlyNESUITests`.

Keep `check_nearby_ui_contract.py` green.

## Report

`.superpowers/sdd/task-typo-03i-report.md` with RED/GREEN, Mac NOT_RUN if applicable.

Return under 15 lines: Status, commits (none), tests, concerns, report path.
