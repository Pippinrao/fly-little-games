# TYPO-01.H — Harmony restore roles + scale entry

Work from: `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`
HEAD: `6ec6605de0f5275295fc08b5905fed3d329f37a4` on `codex/nearby-ui-acceptance-fixes`
Do **not** create another worktree. Do **not** git commit. Do **not** push.
Do **not** install on a physical device. This plan does not authorize physical install.

## Scene

TYPO-00.S froze `shared/schema/nearby_typography_v1.json`. Consume it. Do not invent sizes. Do not globally multiply fonts. Do not restyle GameCenter. Dirty Nearby pages in this worktree are in-progress UX restoration by this same product worktree — restore typography **into** them; do not revert layout restoration.

Audit F01/F04/F05. Also covers UX-01.H / UX-04–07.H typography parts.

hdc currently lists `127.0.0.1:5557`. Previous sessions treated this as a Harmony emulator. **Verify it is an emulator** (`hdc shell getprop` / device info) before any HAP install. If it is physical or unsure: do not install; mark Hypium NOT_RUN.

## Frozen roles (CSS px targets; Harmony uses fp for fontSize)

From `shared/schema/nearby_typography_v1.json`:

- paneTitle 21 / 600 / line box 27.3
- muted 12 / 400 / 17.4
- action 14 / 400 (tabs + ordinary buttons)
- primaryAction 14 / 600
- kicker 11 / 400 / tracking .13em → equivalent letterSpacing ≈ 0.13 * 11 = 1.43 (current 1.6 is wrong)
- inviteCode 29 / 600 / tracking .17em → equivalent letterSpacing ≈ 0.17 * 29 = 4.93 (current Bold + letterSpacing 4 is wrong)
- codeInput 28 / 400 / tracking .16em
- sectionTitle 15 / 600
- pageTitle 18 / 600
- body 14 / 400

Do not change icon glyph sizes (radar/gear/info). Do not shrink 21/29/11.

## Current source (must FAIL tests first)

`NearbyFriends.ets` leftActions/tabRow:
- headline 21 Medium, lineHeight 28 (weight should be 600; line box target 27.3)
- subtitle **14** / lineHeight 20 → must become muted **12**
- action buttons **16** / height 48 → font **14**; 48 is **minimum** tap height, may grow with large type
- tabs **16** → **14**

`NearbyPairing.ets` inviteLeft:
- headline 21 Medium
- subtitle 14 → 12
- invite code 29 **Bold** letterSpacing **4** → 600 and .17em equivalent
- regenerate button 16 → 14

Same pattern on join/scan/other pairing panes. Apply helper everywhere in:
- `pages/NearbyFriends.ets`
- `pages/NearbyPairing.ets`
- `pages/NearbyLobby.ets`
- `pages/NearbyFriendsManage.ets`

NEW `harmony/entry/src/main/ets/ui/NearbyTypography.ets` — product pages must **import and use** it. Tests reading it is not enough.

`EntryAbility.ets` currently accepts only `1 / 1.5 / 2` for `flynes.test.fontSizeScale`. 1.3 is ignored. Allow set `{1, 1.3, 1.5, 2}`. Keep 1.5 for compatibility. Invalid values: log explicit rejection. Release (`!debug`) must still ignore the Want override. Do **not** turn off `followSystem`.

## TDD (mandatory)

NO product font/helper/scale-parser changes without a failing test first.

1. Create `harmony/entry/src/ohosTest/ets/test/NearbyUxRestoration.test.ets` if missing (do not create a second file with another name). Register it in `List.test.ets`.
2. Follow existing Hypium style in `NearbyService.test.ets` (`describe`/`it`/`expect` from `@ohos/hypium`).
3. First test `TYPO_H_N00_roles`: N00 paneTitle 21/600, muted 12, action/primaryAction 14. Against **current** helper-or-source values this must FAIL (subtitle 14 / button 16).
   - Practical split allowed: export role tokens from `NearbyTypography.ets` and assert those tokens, **plus** a static/host check that the four Nearby pages import the helper and do not hardcode action 16 / muted 14 on N00 copy/buttons. If you add a Python check, put it in `tools/quality/tests/` and run it; do not skip Hypium registration.
4. Watch RED. Then implement the helper and wire pages. Watch GREEN for role tests.
5. Then `TYPO_H_scale13_applied`: debug Want 1.3 must apply; current ignore must fail first. Extract a small parse/apply function if needed so the reject/accept table is unit-testable (`1`, `1.3`, `1.5`, `2` accept; `1.2` / garbage reject). On emulator, read `config.fontSizeScale` / measured text to prove it is not still 1.
6. Then `TYPO_H_scale2_reachable`: zh+en, 640×360, scale 2: long reason, primary button, back are fully readable / scrollable / tappable. No ellipsis-as-pass. No shrinking fonts to pass. 48vp is min tap height; large type may grow. Necessary content scrolls vertically.
7. Fix fixed height 48 / maxLines clipping only with evidence from tests/layout. Confirm layout updates when system/test scale changes, not only text.

## Commands

Harmony HAP (from DEVELOPMENT §6):

```
$env:DEVECO_SDK_HOME = "D:\soft\DevEco Studio\sdk"
$Node = "D:\soft\DevEco Studio\tools\node\node.exe"
$Hvigor = "D:\soft\DevEco Studio\tools\hvigor\bin\hvigorw.js"
```

ohosTest HAP assemble. Host CTest is C++ — not a substitute for these ArkTS tests.

If emulator confirmed: install **signed** debug HAP only using the developer's existing local signing; never print or commit credentials. Physical device: NOT_RUN.

Also keep `python tools/quality/check_nearby_typography.py .` and `python tools/quality/check_nearby_ui_contract.py .` green.

## Must not

- GameCenter global restyle, native engine, original HTML/UX docs
- Android/iOS pages (TYPO-02/03)
- nearby_ui_v1.json / nearby_ui_state (UX-00.S)
- VERSION hand-edit
- Commit / stash / reset / delete others' files
- Claim L3 playable or real-device PASS
- Disable followSystem or globally scale all sizes by 0.875

## Report

Write `.superpowers/sdd/task-typo-01h-report.md` with TDD RED/GREEN evidence, files changed, emulator vs NOT_RUN, HAP exit codes, remaining gaps.

Return under 15 lines: Status, commits (none), test summary, concerns, report path.
