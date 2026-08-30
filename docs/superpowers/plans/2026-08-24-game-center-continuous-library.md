# FlyNES Continuous Game Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace filename-first paginated browsing with localized-title-first, compact, two-row continuous horizontal browsing, then deploy the exact-hash-deduplicated NES collection to the connected phone.

**Architecture:** Keep the existing catalog and RecyclerView stack. Add a pure title presentation policy, classify scanner title candidates by script, remove page state from `GameCenterState`, and configure `GridLayoutManager` with two spans and horizontal orientation. A separate PowerShell organizer consumes the read-only audit manifest, extracts one playable NES payload per SHA-256 into an ignored staging directory, writes a bilingual manifest, and never mutates the source tree.

**Tech Stack:** Java 17, Android Views/RecyclerView, JUnit 4, PowerShell 7, Gradle/ADB.

---

### Task 1: Localized title policy

**Files:**
- Create: `app/src/main/java/com/flynes/emu/gamecenter/GameTitlePresentation.java`
- Modify: `app/src/main/java/com/flynes/emu/catalog/scan/RomPackageScanner.java`
- Test: `app/src/test/java/com/flynes/emu/gamecenter/GameTitlePresentationTest.java`
- Test: `app/src/test/java/com/flynes/emu/catalog/scan/RomPackageScannerTest.java`

- [ ] Write failing tests proving CJK outer names become `ZH_HANS`, Latin ZIP entries become `EN`, Chinese UI prefers Chinese, and filenames remain searchable but are never presentation metadata.
- [ ] Run the focused tests and confirm failures are caused by missing policy/language classification.
- [ ] Implement the smallest script classifier and presentation policy that satisfy the tests.
- [ ] Run the focused tests and then the full JVM suite.
- [ ] Commit the title behavior.

### Task 2: Continuous horizontal library

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterState.java`
- Modify: `app/src/main/java/com/flynes/emu/HomeActivity.java`
- Modify: `app/src/main/res/layout/activity_home.xml`
- Modify: `app/src/main/res/layout/item_game_center_card.xml`
- Modify: `app/src/main/res/values/strings.xml`
- Modify: `app/src/main/res/values-zh-rCN/strings.xml`
- Test: `app/src/test/java/com/flynes/emu/gamecenter/GameCenterStateTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/HomeActivityTest.java`

- [ ] Replace pagination tests with a failing assertion that all filtered items are submitted at once and selection repairs without page state.
- [ ] Add failing instrumentation assertions for horizontal-only scrolling and absence of page controls.
- [ ] Remove page persistence, buttons, indicators, page gestures, and page-sized adapter submission.
- [ ] Configure a two-span horizontal `GridLayoutManager`; set compact fixed card width while RecyclerView assigns row height from available height.
- [ ] Change the split to 30/70, remove filename text, and retain accessible title/selected semantics.
- [ ] Run focused JVM/instrumentation tests and commit.

### Task 3: Read-only exact-hash organizer

**Files:**
- Create: `tools/organize-roms.ps1`
- Create: `tools/tests/organize-roms.tests.ps1`
- Output ignored: `local-data/roms/organized/NES/`
- Output ignored: `local-data/roms/organized/manifest.csv`

- [ ] Write a failing fixture test with duplicate raw/ZIP payloads, a distinct regional variant, unsafe ZIP entries, and non-NES payloads.
- [ ] Implement extraction to temporary files, SHA-256 verification, atomic rename, and one output per unique playable NES hash.
- [ ] Generate bilingual display metadata from cleaned outer/inner titles without changing source files.
- [ ] Run fixture tests, then organize `D:\FCgames\roms` and verify source file hashes/count remain unchanged.
- [ ] Verify output count equals unique playable NES hashes in the generated manifest.

### Task 4: Release and physical-device acceptance

**Files:**
- Output ignored: `app/build/outputs/apk/release/app-release-local-test.apk`
- Output ignored: `.artifacts/device-install/`

- [ ] Run the full JVM and connected Android test suites.
- [ ] Build Release, sign with the existing local test certificate, and verify APK signatures/hash.
- [ ] Install as an in-place upgrade on the explicitly verified physical serial.
- [ ] Resolve the current persisted SAF tree. Clear only the app-managed phone ROM copy, push the organized NES files, and rescan.
- [ ] Compare phone file count/catalog count/manifest unique hash count.
- [ ] Capture screenshots and hierarchy showing localized names, no pagination, horizontal-only continuous scrolling, and no filename metadata.
- [ ] Test first/middle/last entries, rapid scroll non-launch, card selection, launch, pause/resume, and crash/ANR logs.
