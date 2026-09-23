# Game Center cached startup — 200 ms gate

Date: 2026-09-23

## Result

- Gate: nearest-rank P95 at or below 200 ms.
- Result: **170 ms**, passed.
- Fixture: 2,224 synthetic user games plus 7 bundled games (2,231 rows total).
- All 12 measured launches were cache hits.
- Every measured launch reported zero built-in scans, zero external scans, zero
  projection rebuilds, and zero per-game user-state JNI calls before the
  background native catalog became ready.

Measured cold process starts, in milliseconds:

`136, 153, 159, 154, 170, 135, 157, 140, 134, 126, 114, 143`

## Environment

- Disposable emulator: `emulator-5592`
- Model: Android SDK built for x86_64
- API: 35
- ABI: x86_64
- Build: debug APK, package compiled with Android `speed` AOT mode before the
  measured process-cold launches
- Warm-ups: 2 (133 ms, 141 ms)

Raw ignored evidence is under
`out/game-center-fast-start/gate-20260923-224842/run.json`.

The final follow-up change records the package update time so a valid cached
startup does not reopen the bundled manifest on every background native
reconciliation. It executes after the measured visible marker and was verified
by the 2,231-item cold-library instrumentation test. At the user's request, the
12-run performance loop was not repeated after that non-critical-path change.

## What the gate verifies

- A small startup sidecar restores the total row count and the first eight
  cached rows without decoding the multi-megabyte full projection.
- Home paints only four initial cards, then installs the complete RecyclerView
  and decodes remaining rows lazily.
- The full projection and native catalog reconcile on the catalog worker.
- Cache corruption/miss remains recoverable and cache replacement remains
  atomic.
- A package update invalidates the cached bundled-manifest marker; an unchanged
  installation reuses the stored manifest fingerprint.

## Verification

- `gradlew :app:testDebugUnitTest`: passed.
- `gradlew :app:assembleDebug :app:assembleDebugAndroidTest`: passed.
- `AppLanguageTest`: passed using the platform per-app locale path on API 35.
- `AndroidLargeCatalogPerformanceTest#coldLibraryAndRepeatedNavigationWith2224Games`:
  passed in an isolated instrumentation process.
- `FirstRunNavigationTest`: 9/9 passed on a clean install.
- The focused Home continuous-library, multiplayer filter, and catalog launch
  regression groups completed; 20 tests passed before two state-dependent
  groups were rerun independently as listed above.

No physical device was used. Emulator evidence does not certify refresh rate,
power, temperature, or device-specific latency.
