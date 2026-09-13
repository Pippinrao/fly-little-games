# Android import, Chinese titles, and ZIP launch follow-up

Base revision: `168bb1699836c0cd8d1223eb2639d7fc8aefd9ab` plus the uncommitted fixes described here.
The earlier loading investigation used `1.0.1`; the subsequent shared workspace is on `1.1.4`
after an existing concurrent merge. This investigation did not change VERSION or create a
release, commit, or tag. Existing HomeActivity/source
diagnostic logs and unrelated Harmony/local evidence were preserved.

## Reproduced failures

1. Device log at 00:58:28: registering `primary:ROMs` succeeded, then scanning raised
   `IllegalArgumentException: unknown source ID`. Registration changed the Java repository
   and UUID map, but immediately rebuilt the view from a native catalog that did not yet
   contain the new source. The source vanished before the first scan.
2. After fixing registration, a real SAF scan on the emulator committed successfully but
   constructing `SourceScanResult` raised `scan source or candidate accounting is invalid`.
   The native path returned zero package outcomes with a nonzero candidate count.
3. A ZIP containing `readme.txt` before a nested ROM scanned successfully but failed cold
   launch with `ZIP exact locator is missing`. The native catalog persisted exact raw name
   bytes and local-header offset, but its Android projection recreated UTF-8 bytes from a
   display name and hardcoded offset zero.
4. A Chinese outer archive name such as `魂斗罗.zip` with an English ROM entry produced an
   empty Chinese title. Native projection ignored the outer filename and treated the inner
   filename, including extension, as verified English metadata.
5. Review identified a blank-basename edge case in the title fix: valid ` .nes` must retain
   its original filename instead of throwing while projecting the entire persisted catalog.

Each failure was demonstrated before its corresponding fix. ZIP fixtures contain synthesized
iNES data or the repository's MIT-licensed From Below asset; no private ROM data is committed.

## Changes

- Persist a new empty native source before refreshing the Java view; check native source
  status first so reauthorization cannot empty-scan an existing library.
- Preserve native per-file scan outcomes through the existing JNI result array and return
  matching indexed packages and candidate accounting.
- Add `fly_catalog_snapshot_get_zip_locator`, without changing the V1 entry structure or
  persistence format. JNI carries exact raw ZIP name bytes and real local-header offsets
  into `NativeCatalogEntry` and the existing exact/hash-verified Java ROM loader.
- Build low-confidence title candidates from outer filename and inner ZIP filename using
  the scanner's existing language classification policy. Chinese outer names remain usable
  as titles independently of the bytes used to identify an archive entry.
- Preserve nonblank original names when stripping an extension leaves a blank title.

## Verification and local evidence

Evidence directory: `out/android-loading-20260913/` (ignored).

| Check | Evidence/result |
|---|---|
| Registration RED on vivo V2324A / Android 16 | `source-red.txt`: source vanished immediately after registration |
| Scan accounting RED on API 35 emulator | `emulator-source-green.txt`: accounting assertion failed after native commit |
| Native ZIP cold-start RED | `zip-fixture-red.txt`: exact locator missing for the non-first entry |
| Chinese title RED | `red-build.log`: expected Chinese title, got empty string |
| Blank title RED | `blank-title-red.log`: title candidate must not be blank |
| Android JVM | `final-build.log`: 440 tests, zero failures |
| Shared native host | `shared-final-tests.log`: 38/38 passed; includes ZIP raw-name, offset and buffer-boundary checks |
| Emulator catalog integration | `emulator-catalog-final.txt`: 18/18 passed, including real SAF registration/restart/scan and ZIP reopen |
| Physical-device deterministic launch regressions | `device-zip-final.txt`: 6/6 passed |

The opt-in real-source test takes an already granted `sourceTree` instrumentation argument.
It isolates its catalog and preferences from the application library and does not delete ROMs
or revoke grants. Without that argument it is skipped. For large libraries its scan budget is
10 minutes; on timeout it waits for native scanning to finish before destroying its owner.

Initial full emulator testing found four motion-rendering failures (EGL context unavailable /
shadow presenter never advances). All four reproduced using the earlier `dist/1.0.0` sideload
package: see `compute-baseline.txt` and `motion-baseline.txt`. Later full-suite attempts also
encountered UiAutomation connection conflicts; these attempts are not passing evidence.
No hardware-qualified mode was enabled and nearby multiplayer was not changed.

The first whole-device library tests exceeded their original 120-second harness timeout.
One attempt was also frozen while a system fingerprint prompt appeared. These are recorded as
failed attempts, not successful whole-library validation. The library contains 2,224 candidates.

The normal physical-device UI scan subsequently completed all 2,224 candidates at 01:31:29
(about 236 seconds). 大金刚3 displayed live gameplay at 01:32:54. Returning to Game Center,
force-stopping/restarting the application and launching again restored gameplay and autosave
at 01:34:59 (`autosave restore rc=0`). Evidence: `device-donkey-kong3-playing.png`.
The later sorting/performance work uses emulators only at the user's request; see
`2026-09-13-game-center-popularity-performance.md` for its separate evidence.

## Reproduction commands

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest
adb -s <device> install -r app/build/outputs/apk/debug/app-debug.apk
adb -s <device> install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s <device> shell am instrument -w -r -e package com.flynes.emu.catalog `
  -e sourceTree <already-granted-tree-uri> `
  com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner
```

Use explicit serials when both the phone and emulator are connected. Direct `am instrument`
avoids Gradle's post-test uninstall/data removal behavior. Avoid concurrent UI automation on
a device being instrumented. The debug APK uses the local development/test signature, not a
production/store certificate.
