# Game Center popularity, exact-content deduplication and large-library loading

User scope: sort the **All** category by the existing curated popularity ranking on Android,
HarmonyOS and iOS; merge only ROMs with identical full decompressed bytes. Keep different-region,
translated and modified payloads separate. Optimize Android's already-indexed 2,000+ game library.
Use emulators and stop within one hour (started 2026-09-13 01:41:58 Asia/Shanghai).

Working-tree base: `9af12ba` (`merge: retain concurrent Android launch fix from main`). VERSION is
`1.1.4`; no version edit, commit, tag or production release was made by this work. Unrelated
Harmony signing/configuration changes and local evidence remain untouched.

## Sorting contract

- `shared/data/popularity.inc` is the single source for all 131 original curated ranking entries.
  Android's Gradle task generates Java ranks from it; the shared C++ product code includes it.
  These are offline curated scores, not live usage analytics.
- Score both the outer filename and inner ZIP entry's **leaf** filename, taking the maximum over
  all copies of the same ROM. Directory names, locale-specific display choices and search aliases
  do not affect the score. Android, Harmony N-API/ArkTS and iOS Objective-C++ propagate that score
  explicitly before presentation deduplication.
- All sorts descending by score, then ascending by canonical content ID. Native canonical IDs
  derive from SHA-256 of full decompressed ROM bytes, including the header. Different ZIP wrappers
  or source folders containing identical bytes produce one card; different bytes remain separate.
- Recent retains play-history ordering. Search retains the chosen category's ordering.

## Android performance changes

- Replace per-byte `String.format` calls in hash/UUID conversion with direct nibble encoding,
  preserving every byte, leading zero and existing upper/lowercase convention.
- Project the full native catalog once after the builtin scan. On builtin/cache failure, abort
  that scan, still publish the existing catalog and check source permissions, then report failure.
- Publish the native projection directly instead of encoding it as legacy FNCA, cloning it and
  decoding it again. Snapshot construction validates before atomic assignment; failed validation
  preserves the live repository and game catalog. Durable Java-store commits retain their preflight.
- Cache All ordering against a copied immutable-row input. A changed title, score, source list or
  user state invalidates the cache. Card selection updates only the old/new selection payloads;
  catalog refresh still rebinds presentation metadata.
- A full isolated run exposed a separate native startup race: `thread_` was started in the
  EglPresenter member initializer before the command deque and later flags were constructed.
  Move thread startup into the constructor body, after all members initialize. The original
  SIGSEGV was symbolized to `commands_.front()` with matching native Build ID
  `5384dddce05236147acbff4f66d48c7945509706` (`isolated-crash.log`). Shutdown behavior is unchanged.

## Verification

Evidence is under ignored `out/android-loading-20260913/`.

- Sorting RED: Java assertions failed for original insertion order/duplicate cards; native checks
  failed for heat order, duplicate content and reversed scan input. Final Java and C++ cases cover
  Chinese/English names, non-first ZIP entries, excluded directory names, distinct translations,
  deterministic ties and cache invalidation.
- Performance baseline, same API 35 hardware configuration and 2,224 unique synthetic ROMs plus
  the bundled game: cold loads **6,010 / 5,722 / 5,620 ms**; 20 repeated list selections
  **1,457 / 1,434 / 1,407 ms**. `perf-baseline.txt` and captured `FlyNesCatalogPerf` logs.
- Performance assertions failed before optimization: `perf-red.txt`, `perf-bootstrap-red.txt`.
  Selection's full-list invalidation failed in `perf-ui-red.txt`.
- Builtin failure RED: `builtin-fallback-red.txt` showed **0** visible games instead of persisted
  **2,225** when asset copying failed. The final test preserves all 2,225 and reports the failure.
- JVM: **446 tests, zero failures/errors, 2 skipped** (`verified-final-build.log` plus JUnit XML).
- Shared native host CTest: **38/38** (`verified-final-host.txt`).
- Harmony host Debug CTest: **12/12** (`popularity-harmony-host-tests.log`).
- Harmony app and test HAP builds succeeded, installed on the existing Pura 70 Pro **emulator**;
  Hypium **22/22** (`popularity-hypium.log`). No physical-device installation was attempted.
- Android original emulator: non-motion UI suite **103/103**; later targeted changes **10/10**.
  After another installation replaced that emulator's app with version 1.0.4, a subsequent
  performance run was discarded (`final-visual.txt`). It is not final-build evidence.
- Final isolated Android AVD `FlyNES_Loading_20260913_5564`, explicit serial `emulator-5564`, same
  API 35 / x86_64 / 4 CPU / 2 GB / 1080×2340 configuration, verified app version **1.1.4**:
  targeted tests **10/10** (`isolated-targeted.txt`). Cold loads **1,076 / 852 / 819 ms**;
  20 list selections **3 / 0 / 0 ms**. This measures cached-catalog startup, not initial SAF scanning
  or physical-phone performance. UI checks reach the last card, return to the first, check visible
  bounds/alpha and capture `isolated-catalog-2225.png` after animations settle.
- The first isolated broad run crashed in native presenter construction (`isolated-full-ui.txt`)
  and is not passing evidence. A new 2,000-construction stress test supplements the full-suite
  regression; that microstress also passed before the fix, so it alone is not a reproducer.
- After fixing presenter construction, the final isolated non-motion instrumentation suite passed
  **104/104** in 120.182 seconds (`verified-final-ui.txt`) on the artifact hashed below. Its three
  2,225-row cold loads measured **817 / 749 / 697 ms**, with 20 repeated list selections each
  below the millisecond timer resolution. Average cached-catalog loading dropped from 5,784 ms
  to 754 ms (about 87%). These emulator measurements do not certify physical-phone performance.
- Final-build manual launch of the bundled From Below game reached active gameplay on the isolated
  emulator (`verified-playing.png`); native logs confirm ROM loaded and autosave restore succeeded
  (`verified-final-performance-and-launch.log`). Verification concluded at approximately
  2026-09-13 02:26 Asia/Shanghai, within the user's one-hour limit.

Four unrelated motion-rendering tests were excluded from the broad Android suite because they
also fail with the pre-fix baseline package; their baseline evidence is documented in the loading
report. No hardware-qualified display mode was enabled. iOS's shared native logic was host-tested;
the Objective-C++ adapter still requires Xcode build/simulator verification on macOS.

## Local test artifact

`out/android-loading-20260913/verified-debug.apk` is the copied, version-verified **local debug test**
build used on the isolated emulator, not a production/store release.

SHA-256: `8F31253CAFCD0CD23CCD5DE0D7FF96BCD3AB9CD9A52DB347DA8E727188DB5EEE`.

The synthetic performance fixture stays in its own private cache/preferences namespace and
does not select or hardcode a user's ROM directory. Source locators in product code continue to
come from the system picker and persisted permission grants.
