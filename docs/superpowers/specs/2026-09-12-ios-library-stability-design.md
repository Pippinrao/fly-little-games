# iOS imported-library stability

The user approved the full scope on 2026-09-12: recover the phone app, fix the imported-library crash, run simulator regression and E2E coverage, use the Android icon, and validate a new phone installation.

## Evidence and change

The physical-device crash reports show `__NSFastEnumerationMutationHandler` in `gameCenterFilteredGamesForCategory:query:`. The presentation merge enumerates `by_id` while replacing its entries. Enumerate an immutable snapshot of its keys. Preserve the canonical merge, titles, search aliases, and ordering.

Phone catalog files have been backed up with verified hashes and isolated inside its Documents directory. Saves, preferences, and external ROM downloads remain intact. Recovery is reversible; clearing state alone does not fix the installed binary.

## Validation

Add a real XCTest through the source service and bridge using multiple distinct fixture payloads, duplicate variants, a 100-game batch, category/search/favorite operations, and reopening the persisted catalog. Demonstrate failure before the fix and success afterwards. Extend UI automation through actual import, library, pause/resume, settings, source removal and restart paths. Exercise save/load through runtime integration when the UI has no such action. Run the existing simulator runtime and UI suites, and report unsupported or untested paths explicitly.

Use only the repository's licensed fixture for committed tests. Private ROMs, phone data, packages and test evidence remain in ignored directories. Use a dedicated simulator and copied source directory on the Mac to avoid other work.

## Icon and delivery

The user subsequently selected imagegen option 2: a teal pixel cartridge with a coral star. Preserve that master, with no exterior frame, across Android, iOS and Harmony. Fit Android adaptive artwork to the safe circle and generate opaque iOS/Harmony images without a baked-in system mask. Validate asset dimensions, inspect mask previews, and compile with platform resource tools. Build an unsigned arm64 package and apply the existing local development sideload signature. Preserve the signed bundle identity for upgrade. Verify the physical device separately from iOS 16.4 simulator evidence; do not claim exhaustive stability or iOS 26 hardware qualification from simulator tests.


## Source removal follow-up (2026-09-13)

The reported source-manager removal problem is now represented by explicit regressions: repeated selection of one file/folder must create one logical source; a single Remove must clear old duplicate UUIDs for that exact file-provider location and scope. Independent files with identical ROM content remain independent sources. A missing catalog record must not prevent cleanup of known source metadata/bookmarks or other old duplicate records. Original ROM files are preserved. When bookmarks cannot resolve, only the explicitly selected UUID is removable; never guess other source identities.

The user asked to finish simulator basics without requiring a phone connection. Real-phone verification of the updated source-removal build and current rendering settings remains separate from simulator evidence.
