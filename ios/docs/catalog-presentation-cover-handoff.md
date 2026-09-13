# iOS catalog titles and automatic covers — checkpoint handoff

Date: 2026-09-11. Work stopped at the user's requested checkpoint before product integration.

## Completed, independently verified

- `ios/app/platform/CatalogPresentation.h/.mm`: Foundation-only presentation helper. Trusted built-in metadata supplies **From Below / 来自下方** exactly as Android. External filenames lose only their final extension; underscores and region tags remain. Han script takes Chinese precedence; Latin letters identify English; other scripts remain unclassified. ZIP outer filename candidates take precedence over same-language entry names. Trusted candidates win canonical merges. Locale presentation and case-insensitive filename/title alias searches are implemented.
- `ios/app/platform/GameCoverPolicy.hpp`: compact little-endian RGB565 quality scoring matching Android `FrameQuality`, samples at offsets 120/240/360/480 from the first observed sequence, acceptance at score >= 18 and improvement strictly greater than 1.
- `ios/tests/catalog_presentation_test.mm` and `ios/tests/game_cover_policy_test.cpp`: observed RED on expected title/quality assertions, then GREEN. Both run on macOS with `xcrun clang++ -std=c++17`; title test links Foundation. Both pass `-Wall -Wextra -Werror`. Title helper also passed iOS simulator 16.4 SDK syntax checking.

No existing AppBridge, RunSurface, library, model, row, or detail view was modified by this subtask. There is **no automatic cover capture or new title rendering in the product yet**. No product Mac mirror was modified; compilation used isolated `/tmp` files.

## Remaining product integration

1. Build title fields in `FlyNesAppBridge.mm::catalog_row_dictionary`. Raw files use `displayName` as the outer filename; ZIP snapshots put the decoded entry path in `displayName` and outer package path in `relativePath`. Treat only the actual built-in manifest source as trusted; never infer a translation solely from an external filename. Aggregate candidate fields and filename aliases alongside canonical rows without changing the chosen playable physical source.
2. Map title fields into `CatalogGame`, render locale primary/secondary in row and detail, and search all title/filename aliases. The current shared search normalizes ASCII only; use the helper for Unicode-aware alias matching or extend that path deliberately. Adjust UI expectations from `from_below.nes` to the localized trusted title while retaining filename search.
3. Implement a capture coordinator using the policy. Observe **every produced native game frame**, including steps skipped by display presentation. A local session sequence counter can represent the offsets; the runtime bridge currently does not expose its sequence. Copy RGB565 pixels only when a sample is due; evaluate and persist on a serial background queue. Never capture the UI, controls, CRT output, or pause drawer.
4. Implement a private no-backup `covers/v1/<SHA256(canonicalID)>.png` repository with 320x240 nearest-neighbor conversion, atomic durable writes, and a 24-image memory cache. Android restarts its best-score gate per play session; it does not compare the existing persisted cover score.
5. Add cover loading/refresh to row and left detail, with a title placeholder until an acceptable sample exists. Register the new store/view source files and ImageIO/CoreGraphics only after those files actually exist. They do **not** exist at this checkpoint.
6. Run product simulator tests centrally through the parent workflow; verify black-frame rejection, cover creation after accepted samples, updated list/detail covers, restart persistence, bilingual display, and original filename alias search.

## Android sources used

- `app/src/main/java/com/flynes/emu/catalog/android/AndroidBuiltinCatalogAdapter.java`
- `app/src/main/java/com/flynes/emu/catalog/scan/RomPackageScanner.java`
- `app/src/main/java/com/flynes/emu/catalog/CanonicalGame.java`
- `app/src/main/java/com/flynes/emu/gamecenter/GameTitlePresentation.java`
- `app/src/main/java/com/flynes/emu/cover/CoverCaptureCoordinator.java`
- `app/src/main/java/com/flynes/emu/cover/FrameQuality.java`
- `app/src/main/java/com/flynes/emu/cover/AndroidCoverRepository.java`
