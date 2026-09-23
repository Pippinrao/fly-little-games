# Game Center 200ms Startup Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a complete cached 2,000+ game Android lobby within 200ms P95, avoid startup ROM rescans, rebuild stale cache in the background, and revalidate exact content before launch.

**Architecture:** Persist a checksummed, atomically replaced `GameCenterSnapshot` containing a lightweight pre-ranked UI projection plus encoded full `CatalogState`. `FlyNesApplication` starts two asynchronous phases: publish the lightweight snapshot first, then open the native app and either hydrate the full Java catalog from the matching cached state or rebuild once through a bulk JNI snapshot. `HomeActivity` renders the immutable UI projection with stable-ID diffs and coalesced visible-cover loads; launching waits for native readiness and resolves the selected canonical ID against the live catalog.

**Tech Stack:** Java 17, AndroidX RecyclerView/ListAdapter, Android `AtomicFile`, JNI/C++17 shared app ABI, JUnit 4, Android instrumentation, PowerShell/ADB performance gate.

---

## File map

New files have one responsibility each:

- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshot.java` — immutable cached UI/source projection and its metadata.
- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshotCodec.java` — bounded deterministic binary envelope with SHA-256 verification.
- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshotProjector.java` — project full catalog state into pre-ranked/searchable UI rows.
- `app/src/main/java/com/flynes/emu/catalog/android/AndroidAtomicGameCenterSnapshotStore.java` — atomic last-known-good file storage.
- `app/src/main/java/com/flynes/emu/catalog/android/CatalogStartupPolicy.java` — pure cache/native generation decision table.
- `app/src/main/java/com/flynes/emu/app/NativeCatalogSnapshot.java` — one bulk native snapshot result, including users and sources.
- `app/src/main/java/com/flynes/emu/cover/CoverLoadCoordinator.java` — process-local cover request coalescing.
- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterStartupTrace.java` — startup timestamps and one machine-readable visible marker.
- `tools/quality/run_game_center_startup_perf.ps1` — disposable-emulator 2,000+ game cold-start P95 gate.
- `docs/verification/2026-09-23-game-center-200ms.md` — commands, raw timings, P95 and scope of evidence.

Existing files changed:

- `shared/include/flynes/flynes_app.h`, `shared/src/app/flynes_app.cpp` — indexed snapshot-owned user-state enumeration.
- `shared/tests/test_app.cpp`, `shared/tests/test_catalog_user.cpp` — ABI and semantic coverage.
- `app/src/main/cpp/flynes_app_jni.cpp` — assemble entries, users, titles and source status inside one JNI call.
- `app/src/main/java/com/flynes/emu/app/FlyNesApp.java` — expose `catalogSnapshot()`.
- `app/src/main/java/com/flynes/emu/catalog/android/AndroidCatalogRuntime.java` — lazy native creation, two-phase startup, source epoch and atomic rebuild.
- `app/src/main/java/com/flynes/emu/FlyNesApplication.java` — start the runtime without blocking `onCreate()`.
- `app/src/main/java/com/flynes/emu/HomeActivity.java` — render cached rows, diff updates, canonical-ID launch and visibility marker.
- `app/src/main/java/com/flynes/emu/AndroidGameLaunchService.java` — wait for live readiness and resolve a current launchable variant.
- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterItem.java`, `GameCenterState.java`, `GameTitlePresentation.java` — precomputed search text and cached-title presentation.
- `app/src/main/java/com/flynes/emu/cover/AndroidCoverRepository.java` — async/coalesced cover entry point.
- Existing JVM and instrumentation tests listed in the tasks below.

Do not modify HarmonyOS/iOS UI or nearby protocol files in this plan. Do not read, message or wait on the independent three-platform multiplayer task.

### Task 1: Define and validate the lightweight snapshot envelope

**Files:**
- Create: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshot.java`
- Create: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshotCodec.java`
- Test: `app/src/test/java/com/flynes/emu/gamecenter/GameCenterSnapshotCodecTest.java`

- [ ] **Step 1: Write the failing codec tests**

Cover round-trip fidelity, deterministic bytes, one-byte corruption, truncation, unknown schema, over-limit strings/counts, and a 2,224-row payload. The fixture must use synthetic IDs and filenames only.

```java
@Test public void roundTrips2224RowsDeterministically() throws Exception {
    GameCenterSnapshot value = Snapshots.synthetic(2224, 41L, "00".repeat(32), 7L);
    byte[] first = GameCenterSnapshotCodec.encode(value);
    byte[] second = GameCenterSnapshotCodec.encode(value);
    assertArrayEquals(first, second);
    assertEquals(value, GameCenterSnapshotCodec.decode(first));
}

@Test public void checksumFailureNeverProducesRows() throws Exception {
    byte[] bytes = GameCenterSnapshotCodec.encode(Snapshots.synthetic(3, 1L, "11".repeat(32), 1L));
    bytes[bytes.length / 2] ^= 1;
    CodecException failure = assertThrows(CodecException.class,
            () -> GameCenterSnapshotCodec.decode(bytes));
    assertEquals(ErrorCode.CHECKSUM_MISMATCH, failure.code());
}
```

- [ ] **Step 2: Run the focused test and record RED**

Run:

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.gamecenter.GameCenterSnapshotCodecTest"
```

Expected: compilation fails because `GameCenterSnapshot` and `GameCenterSnapshotCodec` do not exist. Save the relevant failing lines in `out/game-center-fast-start/red/task-1.txt`.

- [ ] **Step 3: Implement the immutable model**

Use these exact public shapes; constructors defensively copy lists and `catalogStateBytes`, and the byte-array accessor returns a clone:

```java
public record GameCenterSnapshot(
        int schemaVersion,
        long nativeGeneration,
        String builtinManifestSha256,
        long sourceEpoch,
        List<Row> rows,
        List<SourceRow> sources,
        byte[] catalogStateBytes) {
    public static final int CURRENT_SCHEMA = 1;

    public record Row(String canonicalId, String titleEn, String titleZhHans,
            String fallbackTitle, String originalFilename, String searchText,
            boolean builtin, boolean favorite,
            long lastPlayedSequence, int playCount, int variantCount,
            boolean launchable, int popularityScore) {
        public GameCenterItem item() {
            return new GameCenterItem(canonicalId, titleEn, titleZhHans, builtin, favorite,
                    lastPlayedSequence, originalFilename, popularityScore, searchText);
        }
    }

    public record SourceRow(String id, RomSource.Type type,
            RomSource.PermissionState permissionState, RomSource.Availability availability,
            SourceScanResult.Completeness completeness, long scanToken, int packageCount) { }
}
```

Reject schema mismatch, blank IDs, non-64-character lowercase SHA-256, negative counters, duplicate canonical/source IDs, and payloads above `24 * 1024 * 1024` bytes. Override record `equals`/`hashCode` so `catalogStateBytes` uses content equality; keep all accessors immutable.

- [ ] **Step 4: Implement the binary codec**

Encode in this fixed order: magic `FNGC`, schema, generation, source epoch, 32 fingerprint bytes, row count and rows, source count and sources, encoded catalog length and bytes, then SHA-256 of every preceding byte. Use explicit UTF-8 lengths, `MAX_COUNT = 100_000`, `MAX_STRING_BYTES = 64 * 1024`, `MAX_BYTES = 24 * 1024 * 1024`, and reject trailing data.

```java
public final class GameCenterSnapshotCodec {
    private static final int MAGIC = 0x464E4743;
    public static byte[] encode(GameCenterSnapshot snapshot) throws CodecException;
    public static GameCenterSnapshot decode(byte[] encoded) throws CodecException;
    public enum ErrorCode { TRUNCATED, BOUNDS, CHECKSUM_MISMATCH,
        INVALID_MAGIC, UNKNOWN_VERSION, INVALID_FIELD, IO }
    public static final class CodecException extends Exception {
        private final ErrorCode code;
        public ErrorCode code() { return code; }
    }
}
```

- [ ] **Step 5: Run GREEN and commit**

Run the focused test again; expected: all `GameCenterSnapshotCodecTest` tests pass.

```powershell
git add app/src/main/java/com/flynes/emu/gamecenter app/src/test/java/com/flynes/emu/gamecenter/GameCenterSnapshotCodecTest.java
git commit -m "feat(catalog): add game center snapshot envelope"
```

### Task 2: Project catalog data once and preserve the last valid file

**Files:**
- Create: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterSnapshotProjector.java`
- Create: `app/src/main/java/com/flynes/emu/catalog/android/AndroidAtomicGameCenterSnapshotStore.java`
- Modify: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterItem.java`
- Modify: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterState.java`
- Modify: `app/src/main/java/com/flynes/emu/gamecenter/GameTitlePresentation.java`
- Test: `app/src/test/java/com/flynes/emu/gamecenter/GameCenterSnapshotProjectorTest.java`
- Test: `app/src/test/java/com/flynes/emu/gamecenter/GameCenterStateTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidAtomicGameCenterSnapshotStoreTest.java`

- [ ] **Step 1: Write failing projection/search tests**

Assert that projection preserves all 2,224 canonical IDs, precomputes popularity, aliases and original names into `searchText`, produces deterministic popularity order, summarizes source package counts, and embeds `CatalogStateCodec.encode(state)` without ROM bytes.

```java
@Test public void cachedRowsSearchAliasesWithoutGameCatalogTraversal() throws Exception {
    GameCenterSnapshot snapshot = projector.project(9L, FINGERPRINT, 3L,
            Fixtures.stateWithAlias("content-id", "Hidden Alias"));
    GameCenterState state = new GameCenterState();
    state.setQuery("hidden alias");
    assertEquals(List.of("content-id"), state.filtered(
            snapshot.rows().stream().map(GameCenterSnapshot.Row::item).toList())
            .stream().map(GameCenterItem::canonicalId).toList());
}
```

Add `GameCenterItem.searchText` as the final record component. Keep the existing seven- and eight-argument constructors source-compatible and make them derive `searchText` from both titles and the original filename.

- [ ] **Step 2: Run the JVM tests and record RED**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.gamecenter.GameCenterSnapshotProjectorTest" --tests "com.flynes.emu.gamecenter.GameCenterStateTest"
```

Expected: the new projector test does not compile or the alias assertion fails on the old search path.

- [ ] **Step 3: Implement deterministic projection**

`GameCenterSnapshotProjector.project(...)` accepts `CatalogState`, projects it through a temporary `GameCatalog`, and emits immutable rows and source summaries. Normalize search text once with NFKC/lowercase and include every title candidate, alias and distinct original filename. Select the fallback title from the first UNKNOWN title candidate; never expose it as metadata.

```java
public GameCenterSnapshot project(long nativeGeneration, String builtinFingerprint,
        long sourceEpoch, CatalogState state) throws CatalogStateCodec.CodecException {
    GameCatalog catalog = new GameCatalog();
    catalog.publishPersistentState(packages(state), state.userStates(), state.lastPlayedSequence());
    List<GameCenterSnapshot.Row> rows = rows(catalog.canonicalEntries(), state.builtinSourceId());
    rows.sort(Comparator.comparingInt(GameCenterSnapshot.Row::popularityScore).reversed()
            .thenComparing(GameCenterSnapshot.Row::canonicalId));
    return new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, nativeGeneration,
            builtinFingerprint, sourceEpoch, rows, sourceRows(state), CatalogStateCodec.encode(state));
}
```

`GameCenterState.filtered(...)` checks `item.searchText()` rather than nesting `GameCatalog.search()` and a second scan of all rows.

- [ ] **Step 4: Write and run the atomic-store RED test**

Copy the isolation pattern from `AndroidAtomicCatalogStateStoreTest`. Write a good snapshot, inject an oversized write and an interrupted/failed temporary write, and assert the good bytes remain readable.

```powershell
.\gradlew.bat :app:assembleDebug :app:assembleDebugAndroidTest
& "$env:ANDROID_HOME\platform-tools\adb.exe" -s emulator-5554 shell am instrument -w -r -e class com.flynes.emu.catalog.android.AndroidAtomicGameCenterSnapshotStoreTest com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner
```

Expected before implementation: class-not-found or compilation failure.

- [ ] **Step 5: Implement the atomic store and run GREEN**

Use Android `AtomicFile`; read `.bak` without repairing it, bound both reads and writes to `GameCenterSnapshotCodec.MAX_BYTES`, `flush()` and `getFD().sync()` before `finishWrite()`, and call `failWrite()` on any failure.

Expected: focused JVM and instrumentation tests all pass.

- [ ] **Step 6: Commit**

```powershell
git add app/src/main/java/com/flynes/emu/gamecenter app/src/main/java/com/flynes/emu/catalog/android/AndroidAtomicGameCenterSnapshotStore.java app/src/test/java/com/flynes/emu/gamecenter app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidAtomicGameCenterSnapshotStoreTest.java
git commit -m "feat(game-center): persist ranked lobby projection"
```

### Task 3: Replace per-game JNI user queries with one bulk boundary call

**Files:**
- Modify: `shared/include/flynes/flynes_app.h`
- Modify: `shared/src/app/flynes_app.cpp`
- Modify: `shared/tests/test_app.cpp`
- Modify: `shared/tests/test_catalog_user.cpp`
- Create: `app/src/main/java/com/flynes/emu/app/NativeCatalogSnapshot.java`
- Modify: `app/src/main/java/com/flynes/emu/app/FlyNesApp.java`
- Modify: `app/src/main/cpp/flynes_app_jni.cpp`
- Test: `app/src/androidTest/java/com/flynes/emu/app/FlyNesAppBulkSnapshotTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java`

- [ ] **Step 1: Add failing shared ABI tests**

Specify snapshot-owned user enumeration so a captured generation remains immutable after later favorite/play mutations:

```cpp
uint64_t users = 99;
check(fly_catalog_snapshot_user_count(snapshot, &users) == FLY_RESULT_OK && users == 1,
      "snapshot exposes one user row");
fly_catalog_user_state state{FLY_CATALOG_USER_STATE_V1_SIZE,
    FLY_CATALOG_USER_STATE_VERSION_1};
uint32_t required = 0;
check(fly_catalog_snapshot_user_get(snapshot, 0, nullptr, 0, &required, &state)
          == FLY_RESULT_BUFFER_TOO_SMALL,
      "snapshot user sizes canonical id");
```

Also cover nulls, small structs, wrong versions, out-of-range indices and a snapshot remaining unchanged after `fly_catalog_mark_played` mutates the app.

- [ ] **Step 2: Run the host target and record RED**

```powershell
$cmake = "$env:ANDROID_HOME/cmake/3.22.1/bin/cmake.exe"
& $cmake -S shared -B out/game-center-fast-start/host -G "Visual Studio 17 2022" -A x64 -DFLYNES_BUILD_TESTS=ON
& $cmake --build out/game-center-fast-start/host --config Debug --parallel 2 --target flynes_app_contract_test flynes_catalog_user_test
```

Expected: compilation fails because the two snapshot-user functions are undeclared.

- [ ] **Step 3: Add the append-only shared ABI**

Add functions, not fields in the V1 catalog-entry struct:

```c
FLYNES_API fly_result fly_catalog_snapshot_user_count(
    const fly_catalog_snapshot_t* snapshot, uint64_t* count_out);
FLYNES_API fly_result fly_catalog_snapshot_user_get(
    const fly_catalog_snapshot_t* snapshot, uint64_t index,
    char* canonical_id_utf8, uint32_t canonical_id_capacity,
    uint32_t* canonical_id_required, fly_catalog_user_state* state_out);
```

`user_count` reads `snapshot->catalog->users.size()`. `user_get` uses indexed access, validates the V1 state prefix/version, reports the NUL-inclusive required ID size, returns `BUFFER_TOO_SMALL` without partially changing state, then copies the ID and counters. It never reads the mutable app.

- [ ] **Step 4: Run shared GREEN**

```powershell
& $cmake --build out/game-center-fast-start/host --config Debug --parallel 2 --target flynes_app_contract_test flynes_catalog_user_test
& "$env:ANDROID_HOME/cmake/3.22.1/bin/ctest.exe" --test-dir out/game-center-fast-start/host -C Debug --output-on-failure -R "^(flynes_app_contract|flynes_catalog_user)$"
```

Expected: both tests pass.

- [ ] **Step 5: Write the failing Android bulk-snapshot regression**

Add package-private Java call counters in `FlyNesApp`, visible to `FlyNesAppBulkSnapshotTest` in the same package. Seed at least 2,000 entries and multiple user states, call once, and assert entry count, generation, source rows and user fields match while Java observes exactly one bulk native invocation and zero `nativeUserStateGet` calls.

```java
FlyNesApp.resetCatalogCallCountsForTest();
NativeCatalogSnapshot snapshot = app.catalogSnapshot();
assertEquals(2000, snapshot.entries().size());
assertArrayEquals(new long[]{1L, 0L}, FlyNesApp.catalogCallCountsForTest());
```

- [ ] **Step 6: Implement the one-call JNI projection**

Expose only this Java API to production:

```java
public record NativeCatalogSnapshot(long generation,
        List<NativeCatalogEntry> entries,
        Map<String, CanonicalUserState> userStates,
        List<NativeSourceStatus> sources) { }

public NativeCatalogSnapshot catalogSnapshot() {
    NativeCatalogSnapshot value = nativeCatalogSnapshot(app);
    if (value == null) throw new IllegalStateException("native catalog snapshot failed");
    return value;
}
```

Inside `nativeCatalogSnapshot`, acquire one immutable `fly_catalog_snapshot_t`, enumerate its entries/titles and indexed users, enumerate source status, construct the Java collections/result, and release in one cleanup path. Delete `AndroidCatalogRuntime` usage of `catalogEntries()` followed by `userState(id)`; retain the old public methods only where existing tests or non-startup callers still require them.

- [ ] **Step 7: Run Android native regression and commit**

Run `:app:testDebugUnitTest`, assemble both APKs, and invoke `FlyNesAppBulkSnapshotTest` plus `AndroidCatalogLaunchRegressionTest` directly on the pinned emulator. Expected: all assertions pass and the bulk/user counter is `1/0`.

```powershell
git add shared/include/flynes/flynes_app.h shared/src/app/flynes_app.cpp shared/tests/test_app.cpp shared/tests/test_catalog_user.cpp app/src/main/cpp/flynes_app_jni.cpp app/src/main/java/com/flynes/emu/app app/src/androidTest/java/com/flynes/emu/app/FlyNesAppBulkSnapshotTest.java app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java
git commit -m "perf(catalog): bulk native catalog projection"
```

### Task 4: Make catalog startup two-phase and skip unchanged scans

**Files:**
- Create: `app/src/main/java/com/flynes/emu/catalog/android/CatalogStartupPolicy.java`
- Modify: `app/src/main/java/com/flynes/emu/catalog/android/AndroidCatalogRuntime.java`
- Modify: `app/src/main/java/com/flynes/emu/FlyNesApplication.java`
- Test: `app/src/test/java/com/flynes/emu/catalog/android/CatalogStartupPolicyTest.java`
- Modify test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogRuntimeTest.java`
- Modify test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java`

- [ ] **Step 1: Write the pure decision-table RED tests**

Use this exact policy output:

```java
enum NativeAction { HYDRATE_CACHED_STATE, REBUILD_PROJECTION, RESCAN_BUILTIN }
record CacheFacts(boolean usable, long generation, String manifestSha256, long sourceEpoch) { }
record NativeFacts(long generation, String manifestSha256, long sourceEpoch) { }

@Test public void exactMetadataMatchHydratesWithoutEnumerationOrScan() {
    assertEquals(NativeAction.HYDRATE_CACHED_STATE,
            decide(new CacheFacts(true, 8, HASH, 4), new NativeFacts(8, HASH, 4)));
}
@Test public void manifestChangeRequiresOnlyBuiltinRescan() {
    assertEquals(NativeAction.RESCAN_BUILTIN,
            decide(new CacheFacts(true, 8, HASH, 4), new NativeFacts(8, OTHER_HASH, 4)));
}
@Test public void generationOrEpochChangeRebuildsWithoutRomDirectoryScan() {
    assertEquals(NativeAction.REBUILD_PROJECTION,
            decide(new CacheFacts(true, 7, HASH, 4), new NativeFacts(8, HASH, 5)));
}
@Test public void invalidCacheRescansBuiltinOnce() {
    assertEquals(NativeAction.RESCAN_BUILTIN,
            decide(new CacheFacts(false, 0, HASH, 0), new NativeFacts(8, HASH, 4)));
}
```

- [ ] **Step 2: Run RED, implement policy, run GREEN**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.catalog.android.CatalogStartupPolicyTest"
```

The decision order is: missing/invalid cache -> `RESCAN_BUILTIN`; changed manifest -> `RESCAN_BUILTIN`; generation or source epoch mismatch -> `REBUILD_PROJECTION`; exact match -> `HYDRATE_CACHED_STATE`. Permission-set changes increment source epoch but do not scan ROM files.

- [ ] **Step 3: Add failing runtime tests with injected slow native creation**

Introduce package-private injection seams for `NativeAppFactory`, snapshot store, executor and clock. The old production constructor remains unchanged to callers.

```java
AndroidCatalogRuntime.Startup startup = runtime.start();
assertEquals(CacheStatus.HIT, startup.cacheReady().get(200, MILLISECONDS).status());
assertEquals(2224, runtime.gameCenterSnapshot().rows().size());
assertFalse("fast phase must not wait for native", nativeFactory.opened.await(1, MILLISECONDS));
nativeFactory.release.countDown();
startup.nativeReady().get(30, SECONDS);
assertEquals(0, nativeFactory.builtinScans.get());
assertEquals(0, nativeFactory.bulkSnapshots.get());
```

Add separate cases for missing/corrupt/old-schema cache, generation mismatch, manifest mismatch, source epoch mismatch, native failure preserving cached rows, and concurrent `start()` calls returning the same futures.

- [ ] **Step 4: Implement idempotent two-phase startup**

The runtime API is:

```java
public record Startup(CompletableFuture<FastResult> cacheReady,
        CompletableFuture<BootstrapResult> nativeReady) { }
public enum CacheStatus { HIT, MISS, RECOVERY_NEEDED }
public synchronized Startup start();
public Startup startup();
public GameCenterSnapshot gameCenterSnapshot();
public CompletableFuture<BootstrapResult> nativeReady();
```

Production construction must not call `FlyNesApp.create`. `start()` queues cache read/decode first, atomically assigns the snapshot, completes `cacheReady`, and only then queues native creation on the catalog executor. `FlyNesApplication.onCreate()` constructs runtime/services and calls `catalogRuntime.start()` without awaiting either future.

Use `files/catalog/game-center-snapshot.bin`; leave `catalog-state.bin` solely for legacy FNCA migration. Keep native settings/control-layout access behind a small deferred backend that joins `nativeReady` only when those screens request it.

- [ ] **Step 5: Implement cache-match hydration and rebuild**

For `HYDRATE_CACHED_STATE`, decode only `snapshot.catalogStateBytes()` in the background and call `repository.loadProjection`; do not call native catalog enumeration. For `REBUILD_PROJECTION`, call `nativeApp.catalogSnapshot()` once, project with `NativeCatalogProjector`, then create/write/publish a new `GameCenterSnapshot`. For `RESCAN_BUILTIN`, run `scanBuiltinNative()` once, then the same one-call rebuild.

Remove the trailing `refreshNativeView()` from `scanBuiltinNative()` so no path projects twice. Replace every mutation's old `refreshNativeView()` with one `rebuildProjectionAndCache()`.

Store `sourceEpoch` in `flynes_catalog_projection` preferences. Increment exactly once after successful add, remove, explicit rescan, or detected persisted-permission-set change. Do not increment and do not enumerate directories on ordinary startup.

- [ ] **Step 6: Run runtime GREEN and prove no startup scan**

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest
& "$env:ANDROID_HOME\platform-tools\adb.exe" -s emulator-5554 shell am instrument -w -r -e class com.flynes.emu.catalog.android.AndroidCatalogRuntimeTest,com.flynes.emu.catalog.android.AndroidLargeCatalogPerformanceTest com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner
```

Expected: cache is published before injected native release; unchanged restart reports zero built-in scans, zero external scans, zero bulk snapshots and all 2,224 rows; mismatch performs one bulk snapshot; corruption leaves bytes untouched until a successful replacement exists.

- [ ] **Step 7: Commit**

```powershell
git add app/src/main/java/com/flynes/emu/catalog/android app/src/main/java/com/flynes/emu/FlyNesApplication.java app/src/test/java/com/flynes/emu/catalog/android app/src/androidTest/java/com/flynes/emu/catalog/android
git commit -m "perf(android): load cached lobby before native catalog"
```

### Task 5: Render cached rows lazily and coalesce cover work

**Files:**
- Create: `app/src/main/java/com/flynes/emu/cover/CoverLoadCoordinator.java`
- Create: `app/src/main/java/com/flynes/emu/gamecenter/GameCenterStartupTrace.java`
- Modify: `app/src/main/java/com/flynes/emu/cover/AndroidCoverRepository.java`
- Modify: `app/src/main/java/com/flynes/emu/HomeActivity.java`
- Modify: `app/src/main/java/com/flynes/emu/gamecenter/GameTitlePresentation.java`
- Test: `app/src/test/java/com/flynes/emu/cover/CoverLoadCoordinatorTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/ui/HomeContinuousLibraryTest.java`
- Modify test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java`

- [ ] **Step 1: Write cover-coalescing RED tests**

Use a blocked fake decoder and assert two requests for one canonical ID create one decode future, both receive the same result, cancellation of one waiter preserves the other, and a completed memory hit does no executor work.

```java
CompletionStage<Bitmap> first = coordinator.load("same-id");
CompletionStage<Bitmap> second = coordinator.load("same-id");
assertEquals(1, decoder.calls.get());
decoder.release.countDown();
assertSame(first.toCompletableFuture().get(), second.toCompletableFuture().get());
```

- [ ] **Step 2: Run RED, implement coalescing, run GREEN**

`CoverLoadCoordinator` owns a `ConcurrentHashMap<String, CompletableFuture<Bitmap>> inFlight` and removes with `inFlight.remove(id, future)` in `whenComplete`. `AndroidCoverRepository.loadAsync(id)` returns a memory hit immediately or delegates one disk decode. `HomeActivity` keeps its holder tag check but removes its independent fixed thread pool.

- [ ] **Step 3: Write the Home cached-render RED assertions**

Update instrumentation to inject a 2,224-row `GameCenterSnapshot` without a ready native backend. Assert adapter count is complete, only visible holders exist, selected row survives a background replacement, and no full `onChanged()` notification occurs.

```java
assertEquals(2224, grid.getAdapter().getItemCount());
assertTrue(grid.getChildCount() < 40);
assertEquals(0, observer.fullRefreshes.get());
```

- [ ] **Step 4: Replace main-thread reprojection with immutable rows**

`HomeActivity` awaits `startup.cacheReady()` and calls `applySnapshot(runtime.gameCenterSnapshot())`; it separately observes `nativeReady()` and applies a newer snapshot if its identity changed. `applySnapshot` assigns already-built rows/source summaries and never calls `gameCatalog().canonicalEntries()`, `gameCatalog().search()`, or `Popularity.scorePackage()`.

Convert `GameCardAdapter` to `ListAdapter<GameCenterSnapshot.Row, GameCardHolder>` with canonical-ID identity and value equality content checks. Call `setHasStableIds(true)` and return a stable 64-bit digest of canonical ID. Keep the existing selection payload update. Use the submit commit callback plus `ViewTreeObserver.OnPreDrawListener` to record visibility only when adapter count equals snapshot row count and the first card has non-empty bounds.

```java
gameAdapter.submitList(snapshot.rows(), () -> grid.getViewTreeObserver()
        .addOnPreDrawListener(trace.visibleOnNextPreDraw(grid, snapshot.rows().size(), cacheStatus)));
```

- [ ] **Step 5: Add the machine-readable startup marker**

`GameCenterStartupTrace` uses `Process.getStartElapsedRealtime()` as its origin and logs once per process:

```text
I/FlyNesStartup: GAME_CENTER_VISIBLE elapsedMs=173 count=2231 cache=HIT
```

Register a root-view pre-draw callback immediately after `setContentView()` and log `GAME_CENTER_SHELL_VISIBLE` once, even on cache miss. Also log `CACHE_READ`, `LIST_SUBMIT`, `NATIVE_READY`, `BUILTIN_SCAN_COUNT`, `EXTERNAL_SCAN_COUNT`, `BULK_SNAPSHOT_COUNT`, and `USER_STATE_JNI_COUNT`. Do not log file paths, titles, locators or credentials.

- [ ] **Step 6: Run UI GREEN and commit**

Run the unit tests plus `HomeContinuousLibraryTest` and `AndroidLargeCatalogPerformanceTest` directly. Expected: full cached count, visible-only holders, stable selection, no full refresh, and duplicate cover decoding count `1`.

```powershell
git add app/src/main/java/com/flynes/emu/HomeActivity.java app/src/main/java/com/flynes/emu/gamecenter app/src/main/java/com/flynes/emu/cover app/src/test/java/com/flynes/emu/cover app/src/androidTest/java/com/flynes/emu/ui/HomeContinuousLibraryTest.java app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java
git commit -m "perf(game-center): render cached rows and coalesce covers"
```

### Task 6: Gate launch on live native identity

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/AndroidGameLaunchService.java`
- Modify: `app/src/main/java/com/flynes/emu/HomeActivity.java`
- Modify: `app/src/main/java/com/flynes/emu/catalog/android/AndroidCatalogRuntime.java`
- Test: `app/src/test/java/com/flynes/emu/AndroidGameLaunchServiceTest.java`
- Modify test: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java`

- [ ] **Step 1: Write launch-before-ready RED tests**

Replace UI launch by variant ID with canonical ID. Block native readiness, click a cached row, assert no ROM open/session staging occurs, then release native and assert the service resolves the current preferred launchable variant. Add deletion, permission-revocation and payload-identity-change cases; each must stay in the lobby with `launch_failed` and publish a corrected snapshot.

```java
service.launchCanonical("cached-id", callback);
assertEquals(0, opener.calls.get());
nativeReady.complete(resultWithLiveCatalog());
assertEquals("live-variant-id", opener.lastVariantId.get());
```

- [ ] **Step 2: Run RED**

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests "com.flynes.emu.AndroidGameLaunchServiceTest"
```

Expected: `launchCanonical` does not exist.

- [ ] **Step 3: Implement canonical launch resolution after readiness**

On the existing single launch executor:

```java
runtime.nativeReady().get();
GameCatalogEntry live = runtime.gameCatalog().canonicalEntries().stream()
        .filter(entry -> entry.canonicalGame().id().equals(canonicalId))
        .findFirst().orElse(null);
GameVariant variant = preferredLaunchableVariant(live);
if (variant == null) return LaunchResult.catalogChanged();
return coordinatorFor(runtime).launch(variant.variantId());
```

Do not trust a cached path or cached variant ID. Keep `LaunchCoordinator` exact hash/source/access checks. Serialize requests on the existing single executor; a newer not-yet-started UI choice replaces only the pending canonical ID, never an already staged session. Apply the same readiness wait before the `nearby_choose_game` path loads exact content.

- [ ] **Step 4: Run launch GREEN and commit**

Run `AndroidGameLaunchServiceTest`, all JVM tests, then direct instrumentation for `AndroidCatalogLaunchRegressionTest`. Expected: blocked launch performs no I/O, live launch succeeds, stale/deleted/revoked entries fail without staging.

```powershell
git add app/src/main/java/com/flynes/emu/AndroidGameLaunchService.java app/src/main/java/com/flynes/emu/HomeActivity.java app/src/main/java/com/flynes/emu/catalog/android/AndroidCatalogRuntime.java app/src/test/java/com/flynes/emu/AndroidGameLaunchServiceTest.java app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java
git commit -m "fix(launch): revalidate cached selection against live catalog"
```

### Task 7: Add and pass the 200ms cold-process gate

**Files:**
- Modify: `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java`
- Create: `tools/quality/run_game_center_startup_perf.ps1`
- Create: `docs/verification/2026-09-23-game-center-200ms.md`

- [ ] **Step 1: Split fixture seeding from measurement**

Add an instrumentation method `seedDisposable2224GameSnapshot()` that writes synthetic valid NES fixtures through the real native scanner, waits for `rebuildProjectionAndCache()`, asserts the snapshot has `2224 + builtinCount` rows, then exits. It must require runner argument `flynes.disposablePerformanceFixture=true`; otherwise fail before writing.

- [ ] **Step 2: Implement the external cold-start runner**

The script takes `-Serial`, `-Runs 12` and `-Warmups 2`; validates the serial is an emulator, builds release-like debug and test APKs, replace-installs them, runs the explicit seeding method, and then for each measured run:

```powershell
& $adb -s $Serial logcat -c
& $adb -s $Serial shell am force-stop com.flynes.emu
& $adb -s $Serial shell am start -W -n com.flynes.emu/.HomeActivity
# poll only FlyNesStartup until GAME_CENTER_VISIBLE or a 10s deadline
```

Parse `elapsedMs`, `count`, `cache`, and scan/JNI counters. Sort measured values and calculate nearest-rank P95 at `Ceiling(0.95 * count) - 1`. Fail unless every count is complete, every cache status is `HIT`, every startup scan count is zero, every `USER_STATE_JNI_COUNT` is zero, and P95 is at most 200ms. Write logs and `run.json` under `out/game-center-fast-start/gate-<timestamp>/`; never commit them.

- [ ] **Step 3: Run the gate and optimize only measured hot spots**

```powershell
.\tools\quality\run_game_center_startup_perf.ps1 -Serial emulator-5554 -Runs 12 -Warmups 2
```

Expected: 12 measured values, all complete 2,000+ row cache hits, P95 ≤200ms. If the first run fails, use the phase markers to optimize the measured phase only. If decode is dominant, read the UI section without decoding `catalogStateBytes`; if list submission is dominant, confirm rows were pre-ranked and the initial diff does not bind off-screen holders; if layout is dominant, remove startup-only animations. Do not weaken the timer origin, drop rows, add a delay/warm process, or raise the threshold.

- [ ] **Step 4: Verify cache miss and corruption behavior**

On the disposable install, remove only `files/catalog/game-center-snapshot.bin`, cold launch, and assert `GAME_CENTER_SHELL_VISIBLE` is ≤200ms while one background rebuild occurs. Corrupt a copied test snapshot through instrumentation, cold launch, assert it is rejected, last-good backup is not overwritten, and the next successful rebuild restores cache-hit behavior.

- [ ] **Step 5: Run the complete affected regression matrix**

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug :app:assembleDebugAndroidTest
.\scripts\ci-check.ps1
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest
.\tools\content\verify-builtin-content.ps1
```

Use a disposable AVD for `connectedDebugAndroidTest`; it uninstalls the app. Confirm Gradle reports all tests executed with zero failures, not only a zero shell exit code. Host/shared changes must pass CTest through `ci-check.ps1`; UI behavior must pass the full instrumentation suite.

- [ ] **Step 6: Record evidence and self-audit scope**

Write the commit SHA, emulator API/ABI, fixture count, build type, every measured timing, nearest-rank P95, cache/scan/JNI counters, test commands and results. State that simulator evidence does not certify physical storage, power, temperature or latency. Run:

```powershell
git diff --check
git status --short
git ls-files | Select-String -Pattern '\.(nes|zip|apk|aab|hap|p12|jks|keystore)$'
```

Inspect staged content so no generated package, private ROM, credential, signing file or ignored evidence is committed.

- [ ] **Step 7: Commit, push and verify remote equality**

```powershell
git add tools/quality/run_game_center_startup_perf.ps1 docs/verification/2026-09-23-game-center-200ms.md app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidLargeCatalogPerformanceTest.java
git commit -m "test(perf): gate 200ms cached game center startup"
git push origin main
git status --short --branch
git rev-parse HEAD
git rev-parse origin/main
```

Expected: clean `main`, local HEAD equals `origin/main`, content gate passes, full affected tests pass, and the evidence file reports P95 ≤200ms.
