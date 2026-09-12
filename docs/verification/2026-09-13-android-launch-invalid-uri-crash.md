# Android launch crash — invalid SAF document URI (2026-09-13)

Device: vivo `V2324A` (PD2324, MediaTek, Android 16), package `com.flynes.emu`.

## Symptom

Tapping a game in the library killed the process (no ANR dialog, whole app closed), reproducibly
on every launch of a ROM:

```
09-13 00:23:59.838 29653 29946 E AndroidRuntime: FATAL EXCEPTION: flynes-launch
09-13 00:23:59.838 29653 29946 E AndroidRuntime: Process: com.flynes.emu, PID: 29653
09-13 00:23:59.838 29653 29946 E AndroidRuntime: java.lang.IllegalArgumentException: Invalid URI:
    content://com.android.externalstorage.documents/tree/primary%3AROMs/七宝奇谋2.zip
    at android.content.ContentResolver.openInputStream(ContentResolver.java:1551)
    at com.flynes.emu.catalog.android.AndroidCatalogStreamOpener.open(AndroidCatalogStreamOpener.java:60)
    at com.flynes.emu.launch.ExactRomLoader.load(ExactRomLoader.java:40)
    at com.flynes.emu.launch.LaunchCoordinator.launch(LaunchCoordinator.java:54)
    at com.flynes.emu.AndroidGameLaunchService.lambda$launch$3(AndroidGameLaunchService.java:52)
```

## Root cause

`AndroidPackageLocatorMap` (tree/source UUID + relative path → document URI) is process-local and
only filled by a scan. At a cold start it is empty, so `NativeCatalogProjector.addVariant` fell
back to

```java
locator = source.uri() + "/" + relative;   // <treeLocator>/<file>
```

which is a **tree URI with an appended path**, not a document URI. `ContentResolver` only opens
document URIs (`…/tree/<treeId>/document/<documentId>` or `…/document/<documentId>`), so the
storage provider answered with `IllegalArgumentException: Invalid URI`. `ExactRomLoader` catches
`IOException | SecurityException` but not `IllegalArgumentException`, so the unchecked fault
escaped `AndroidGameLaunchService`'s bare `executor.execute(...)` and killed the process.

Two aggravating facts:

- The launch path therefore crashed on *every* cold start: the first crash killed the process, and
  the next cold start started with an empty locator map again.
- `scanSourceNative` passed only the flattened display name as the catalog's canonical relative
  path, so even a rebuilt locator could not address a nested library (`/sdcard/ROMs/NES/*.zip`).

## Fix

| Layer | Change |
| --- | --- |
| Crash boundary | `AndroidCatalogStreamOpener` validates the shape and converts provider `IllegalArgumentException`/`SecurityException` into the typed `SourceOpenException` (`LOCATOR_INVALID` / `PERMISSION_LOST`) that `ExactRomLoader` already maps. |
| Root cause | `NativeCatalogProjector` no longer invents a locator: a `LocatorResolver` seam derives a real document URI, and an unresolvable package is projected `PRESERVED_STALE` (unavailable, not launchable) with a `unresolved://` placeholder instead of a fake content URI. |
| Derivation | New `AndroidDocumentLocators` rebuilds `…/tree/<treeId>/document/<treeId>/<relative>` with `DocumentsContract.buildDocumentUriUsingTree`; `documentUriFor` / `relativePathFor` are guarded by the pure-JVM `DocumentLocatorShape` and `SourceRelativePath`. |
| Nested libraries | `AndroidCatalogRuntime.scanSourceNative` now stores the canonical tree-relative path (e.g. `NES/game.nes`) as the native relative path, matching the native `is_safe_relative_path` contract, with the display name only as fallback. |
| Last resort | `LaunchCoordinator` reports an unexpected `RuntimeException` from the opener as `SOURCE_OPEN_FAILED`, and `AndroidGameLaunchService` no longer lets anything escape the launch thread (`LaunchResult.unexpectedFailure`). |
| Migration | `AndroidCatalogRuntime.openPackage` validates and wraps the same provider fault as `IOException`. |

## Verification

| Gate | Command | Result |
| --- | --- | --- |
| Android JVM unit | `gradlew :app:testDebugUnitTest` | 438 tests, 0 failures |
| On-device e2e (new) | `gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.catalog.android.AndroidCatalogLaunchRegressionTest` | 5/5 on `V2324A - 16` |
| On-device catalog suite | `gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.package=com.flynes.emu.catalog` | 16/16 on `V2324A - 16` |
| Device `crash` buffer after cold start | `adb logcat -b crash -d` | empty (no FATAL) |

New regression cover:

- `DocumentLocatorShapeTest` — the verbatim crash locator is rejected; document/tree-scoped document
  URIs are accepted.
- `SourceRelativePathTest` — canonical relative-path rules (rejects `..`, `/x`, `C:/x`, `a//b`).
- `NativeCatalogProjectorTest` — an unresolvable locator must never look openable and must project
  as unavailable; a resolver-provided locator is used and stays `FRESH`.
- `LaunchCoordinatorTest.unexpectedSourceFailureIsReportedInsteadOfKillingTheLaunchThread`.
- `AndroidCatalogLaunchRegressionTest` (on device) — a provider that rejects exactly what
  `DocumentsContract.getDocumentId` rejects; cold-start projection of a nested package then a real
  launch through `ContentResolver`; and a catalog that still carries the crash locator failing with
  `SOURCE_OPEN_FAILED` instead of killing the process.

## Known limits

- Catalog entries written by builds before this fix keep a flattened relative path, so packages
  inside sub-directories stay unavailable until the source is scanned again (they never launched
  before either). Flat libraries are fixed by the derivation alone.
- Providers whose child document ids are not rooted at the tree id fall back to the display name,
  which keeps them from crashing but may fail to reopen a nested file.

## Tooling note

`connectedDebugAndroidTest` uninstalls the app (and its private data) after the run unless
`-Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true` is passed. Installing with
`adb install -r` and driving `am instrument` directly does not do this.
