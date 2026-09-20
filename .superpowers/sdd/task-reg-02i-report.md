# REG-02.I — iOS PAIR submit/cancel + N00 devices tool

**Status:** DONE_WITH_CONCERNS

**HEAD:** `6ec6605` on `codex/nearby-ui-acceptance-fixes`  
**Commits created:** none (not authorized)

**Simulator:** NOT_RUN (this machine is Windows; FlyNESUITests / `ios/scripts/run_simulator_tests.py` was not executed). No screenshots were reused or fabricated.

## Files changed (this card)

NEW:

- `tools/quality/tests/test_nearby_ios_pairing_controls.py`
- `.superpowers/sdd/task-reg-02i-report.md`

MODIFIED:

- `ios/app/NearbyPairingView.swift` (join submit disable + in-flight guard; `@Environment(\.dismiss)` on scan/create cancel)
- `ios/app/NearbyFriendsView.swift` (friends tool still `NavigationLink` to manage; devices tool is a distinct disabled find-devices control)

Did not modify Android/Harmony, `NearbyTypography.swift` / tokens, Game Center, `nearby_ui_v1.json`, VERSION, or TYPO-03.I layout/typography. Did not create a second restoration XCTest class.

## TDD RED

Wrote `tools/quality/tests/test_nearby_ios_pairing_controls.py` first. Product still had join `footerButton` always enabled, `submitJoin()` allocating `nearbyNextJoinAttemptID()` with no in-flight guard, scan cancel as a no-op, create cancel clearing invite without dismiss, and the N00 right-pane tool always `NavigationLink` to `NearbyFriendsManageView`.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_pairing_controls.py -v
```

```
FAIL: test_reg_02i_1_join_submit_disabled_and_one_attempt
AssertionError: '.disabled' not found in 'private var footerButton: some View {
        Button(action: footerAction) {
            ...
        }
        .accessibilityIdentifier(footerIdentifier)
    }' : submit always enabled; 5-digit join must disable nearby_join_submit

FAIL: test_reg_02i_2_cancel_dismisses
AssertionError: '@Environment(\.dismiss)' not found in NearbyPairingView.swift
: scan/create cancel must dismiss via Environment(\.dismiss), not a new page

FAIL: test_reg_02i_3_devices_tool_does_not_open_manage
AssertionError: 'dot.radiowaves' unexpectedly found in 'NavigationLink {
                    NearbyFriendsManageView()
                } label: {
                    Image(systemName: tab == .friends ? "person.crop.circle.badge.gearshape" : "dot.radiowaves.left.and.right")
                    ...
                }
                .accessibilityIdentifier("nearby_friends_manage")
                .accessibilityLabel(tab == .friends ? Text("nearby.friends.manage") : Text("nearby.find_devices"))'
: devices tool must not NavigationLink to NearbyFriendsManageView

Ran 3 tests in 0.002s
FAILED (failures=3)
```

RED reason: missing join `.disabled` / in-flight fence, scan/create cancel does not dismiss, devices toolbar is manage `NavigationLink`. Not import typos.

## TDD GREEN

Join submit: `.disabled(joinSubmitDisabled)` so 5-digit / incomplete stays off; valid 6-digit enables; `joinSubmitted` locks the control immediately after a successful attempt-id allocation. `submitJoin()` returns before `nearbyNextJoinAttemptID()` when already in-flight (fail-closed like Android until cancel/dismiss). Scan cancel `dismiss()`es to N00. Create cancel calls `nearbyHostCancelGeneration` then `dismiss()` so a late callback cannot revive the old generation. N00 friends tool keeps `nearby_friends_manage` → `NearbyFriendsManageView`. Devices tool is a disabled Button `nearby_find_devices_tool` labeled `nearby.find_devices` with `nearby.blocked.discovery` value — not a manage `NavigationLink`.

```
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_pairing_controls.py -v
```

```
...
Ran 3 tests in 0.001s
OK
```

```
python -m unittest discover -s tools/quality/tests -p test_nearby_ios_typography.py -v
python tools/quality/check_nearby_typography.py .
python tools/quality/check_nearby_ui_contract.py .
```

```
Ran 4 tests in 0.003s
OK
OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present; no network results generated.
```

Host/static only. Attempt-count evidence is the in-flight guard before `nearbyNextJoinAttemptID()` plus fail-closed `joinSubmitted` (not a live fence delta). FlyNESUITests NOT_RUN.

## Remaining gaps

- Windows cannot run FlyNESUITests, so dismiss-to-N00, disabled submit hit-testing, and devices-tool-does-not-push-manage are unproven on simulator.
- Join footer still has no cancel control (Android join cancel stays `GONE`). After a valid submit, in-flight is not cleared by the fail-closed discovery snapshot; retry requires leaving the page.
- Devices toolbar is disabled with the discovery reason; it does not invoke a live find-devices scan (none exists). In-pane `nearby_find_devices` remains.

**Commits created:** none
