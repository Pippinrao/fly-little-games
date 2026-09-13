# iOS Library Stability Implementation Plan

> **For agentic workers:** Use subagent-driven-development for the isolated icon task and inline TDD for the crash and E2E work. Track evidence below.

**Goal:** Fix the confirmed imported-library crash and validate recovery, repeated use and Android icon parity.

**Architecture:** Keep the existing SwiftUI, Objective-C++ bridge, source service and shared catalog. Change only the unsafe presentation enumeration; drive the actual import and persistence APIs from XCTest and real UI from XCUITest.

**Tech Stack:** SwiftUI, Objective-C++, CMake/Xcode 14.3.1, XCTest/XCUITest, Python asset validation, Windows Sideloadly.

**User priority update:** The 1.1.0 crash-fix IPA was installed through Sideloadly and the device query confirmed 1.1.0 (1001000). The user confirmed launch, single-game import, play and reopening all work. SHA-256: `2e822b6cb3bd6f5cdaf9c9e63cb94a3283e67291170235942074a99ee8ff1122`. On 2026-09-13 the user asked to continue simulator basics without waiting for the disconnected phone.

**Scope updates:** Use selected pixel-cartridge option 2 across Android/iOS/Harmony with no exterior frame. Investigate iOS picture quality against Android; pixel tests do not certify real-phone visual parity. Reproduce the reported source-removal issue, including repeated imports and old duplicate source records.

## 1. Recovery and isolation

- [x] Back up phone Documents and Preferences, verify hashes, isolate both catalog files.
- [x] Create `.worktrees/ios-library-stability` from `11a2416` with repository allocator. Root hook configuration is installed, but this old branch lacks the hook implementation; no commit was made.
- [x] Restore Windows Wi-Fi and verify SSH to `apple`.
- [x] Copy tracked source and new tests into a separate Mac directory; build existing simulator tests.

## 2. Regression and minimal fix

Files: `ios/tests/CatalogSourceImportTests.mm`, `ios/tests/catalog_bridge_test.mm`, `ios/app/bridge/FlyNesAppBridge.mm`.

- [x] Add two distinct payloads to the source service, then assert `gameCenterFilteredGamesForCategory:@"ALL" query:@""` returns two cards without throwing. Repeat after reopening the bridge.
- [x] Add 100 distinct payloads with duplicate filenames and verify 100 canonical cards, alias search, favorites and persistence.
- [x] Run the new XCTest alone on the Mac and retain its failing xcresult. Expected: collection mutation exception in the same bridge method as the phone report.
- [x] Replace `for (NSString *canonical in by_id)` at the presentation merge with `for (NSString *canonical in [by_id allKeys])`.
- [x] Run the new tests again and the complete runtime suite; retain results.

## 3. End-to-end coverage

Files: `ios/tests/ProductUITests.mm`, new `ios/tests/ProductImportUITests.mm`, `ios/app/CMakeLists.txt`, fixture staging script under `ios/scripts`.

- [x] Stage licensed NES fixtures in a dedicated simulator's Files provider; import files and a directory through the system picker. ZIP import is covered by the service XCTest.
- [x] Assert library stays alive with multiple canonical games, search and favorites work, source metadata persists and removal updates the grid.
- [x] Exercise repeated launch, pause, settings, resume, return and cold restart, with explicit element predicates and assertions.
- [x] Run existing runtime save/load, renderer, audio and control tests plus UI suite. Record exact counts and failures; fix evidenced defects using TDD.

## 4. Unified pixel icon

- [x] Preserve the selected imagegen master under `assets/branding/` and generate all platform resources reproducibly.
- [x] Use opaque full-bleed iOS/Harmony assets and Android adaptive/monochrome/legacy entries. Inspect system-mask montage; no exterior frame or clipped cartridge.
- [x] Pass 8 icon resource tests and verify all 27 generated resources.
- [x] Compile Android resources and run unit tests: 425 passed, 2 skipped for Windows symlink permission, no failures.
- [x] Run the related Android UI suite on the existing API 35 AVD: 15/15 pass; packaged icon pixels and actual displayed icon verified. The task-started emulator was stopped.
- [x] Harmony Debug CTest 12/12 and SDK icon-resource compilation pass. The unchanged baseline product contract fails; no HDC device, Hypium/device install or full HAP verification. See build/harmony-icon-verification/VERIFICATION.md.

## 4a. Source removal regression

- [x] Real-picker single-file and 100-game-folder import/remove paths pass, including folder removal persistence.
- [x] Reproduce two repeat-import failures: every import creates a new UUID, so removing one leaves the game's duplicate source. Distinct sources sharing one game remain valid independently.
- [x] Reproduce old duplicate-record removal failure (three failing assertions).
- [x] Reuse the same resolved source location on import and remove old duplicate references together, preserving original files.
- [x] Pass all 11 source-service regressions plus 6 actual Metal binary golden comparisons.
- [x] Run the complete UI suite including importing the same file twice, removing it once and restarting.

## 5. Delivery and evidence

- [x] Review diff, run relevant host contracts and simulator suites.
- [x] Build arm64 IPA, record source revision, local signature type and SHA-256 under ignored output.
- [ ] Upgrade the same phone app through Sideloadly, verify installed version, import/relaunch/play and collect new crash reports.
- [x] Document covered paths, remaining device limitations and recovery location; preserve unrelated worktrees.


## Final simulator result (2026-09-13)

- Runtime: 49 tests, zero failures, including 12 source-import tests and 6 Metal binary golden comparisons.
- UI: all 8 tests pass, including 10 launch/pause/settings/resume/return cycles. After the missing-index error-path fix, the repeated-file import/remove/restart E2E was run again and passed.
- Soak: 36,000 frames observed, 28,752,653 audio samples, peak resident 158,613,504 bytes; all bounds passed.
- Missing-index regression: six red assertions before the targeted NOT_FOUND cleanup, green in the final runtime suite.
- New unsigned local test package: FlyNES-1.1.0-libraryfix-pixel.ipa; SHA-256 c95a5b81da5cb415d3f0edd9952b37e737ee283c732641aaad0d92a4066af984. Same version 1.1.0 (1001000), distinguished from the prior crash-only package by hash and filename. This is not a tagged main release and has not been installed on the phone.
- Delivery report and source manifest: build/ios-validation/VERIFICATION.md and source-manifest.json. Device installation and the subjective iPhone/Android picture-quality difference remain unverified; simulator passes are not a hardware qualification.
