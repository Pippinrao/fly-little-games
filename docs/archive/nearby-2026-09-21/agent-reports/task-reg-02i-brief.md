> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# REG-02.I — iOS PAIR submit/cancel + N00 devices tool

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
Do **not** git commit. Windows: FlyNESUITests **NOT_RUN**. Do not revert TYPO-03.I typography.

## Scene

Plan §6 REG-02.I.1–3 (audit R02/R03/R04). Current `NearbyPairingView`:
- `footerButton` has **no disabled** for join; `submitJoin()` has no in-flight guard before `nearbyNextJoinAttemptID()`.
- `footerAction` scan cancel is a no-op; create cancel clears invite but **does not dismiss**.
- `NearbyFriendsView` right-pane tool is always `NavigationLink` to `NearbyFriendsManageView` for both tabs.

Do not change Android/Harmony. Do not change NearbyTypography tokens.

## Vectors (fail first)

REG-02.I.1: 5-digit submit control disabled; after valid 6-digit submit, immediately disabled; two triggers → one attempt (`nearbyNextJoinAttemptID` / fence). Fail/cancel restore from real snapshot — if fail-closed like Android, keep in-flight until cancel/dismiss, don't allocate a second attempt.

REG-02.I.2: scan cancel **dismisses** to N00; create cancel **revokes generation then dismisses**; late callback cannot re-show old invite (generation already cancelled). Use `@Environment(\.dismiss)` or equivalent; do not invent a new page.

REG-02.I.3: devices tool triggers find-devices / unavailable reason only — **must not** navigate to friends manage. Friends tool still opens `NearbyFriendsManageView`. Distinct accessibility ids.

## Files

- `ios/app/NearbyPairingView.swift` (submit disable, dismiss on cancel)
- `ios/app/NearbyFriendsView.swift` (devices vs friends toolbar)
- Tests: python in `tools/quality/tests/` and/or `ios/tests/NearbyUxRestorationTests.mm` — do not create a second restoration class. Host python may assert disabled modifiers / dismiss / NavigationLink only on friends tab.

TDD: RED python/static (submit always enabled; scan cancel no dismiss; devices NavigationLink to manage). Then minimal Swift.

Keep typography python tests green.

## Report

`.superpowers/sdd/task-reg-02i-report.md`

Return under 15 lines: Status, commits (none), tests, concerns, report path.
