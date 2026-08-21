# FlyNES Alpha Game Library and i18n Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a resilient multi-source game library with recent/favorite state, deterministic ZIP scanning, English/Simplified-Chinese UI, and offline localized game titles/aliases.

**Architecture:** Room stores stable sources and games keyed by ROM SHA-1; scanning returns typed results and never destroys the prior catalog on failure. `GameTitleResolver` combines SHA/name metadata with locale-aware fallback, while all UI strings live in Android resources.

**Tech Stack:** Java 17, Room 2.8.4, RecyclerView 1.4.0, SAF, `java.text.Collator`, `java.text.Normalizer`, Android resources/pseudolocales, JUnit, Room migration tests, Espresso.

---

## File map

- Create `library/db/{GameEntity,GameSourceEntity,GameDao,GameSourceDao,FlyNesDatabase}.java`.
- Create `library/{GameRepository,ScanSource,ScanResult,ScanIssue,ScannedGame,GameScanner,ZipRomEntry}.java`.
- Create `metadata/{LocalizedGameMetadata,GameTitleResolver,SearchNormalizer}.java` and `assets/metadata/game_titles.v1.json`.
- Replace `RomStore`, `RomScanner`, `RomLoader`, and `Popularity` usages.
- Convert `GameLibraryActivity` to AppCompat/RecyclerView and explicit view states.
- Add complete `values` and `values-zh-rCN` resources plus locale config.

### Task 1: Add Room and a stable library schema

**Files:**
- Modify: `app/build.gradle`
- Create: `app/src/main/java/com/flynes/emu/library/db/GameEntity.java`
- Create: `app/src/main/java/com/flynes/emu/library/db/GameSourceEntity.java`
- Create: `app/src/main/java/com/flynes/emu/library/db/GameDao.java`
- Create: `app/src/main/java/com/flynes/emu/library/db/GameSourceDao.java`
- Create: `app/src/main/java/com/flynes/emu/library/db/FlyNesDatabase.java`
- Test: `app/src/androidTest/java/com/flynes/emu/library/db/FlyNesDatabaseTest.java`

- [ ] **Step 1: Add Room dependencies and a failing database test**

```groovy
def room_version = "2.8.4"
implementation "androidx.room:room-runtime:$room_version"
annotationProcessor "androidx.room:room-compiler:$room_version"
androidTestImplementation "androidx.room:room-testing:$room_version"
```

```java
private static final String SHA_A = "1111111111111111111111111111111111111111";

@Test public void favoriteAndRecentSurviveFilenameChange() {
    GameEntity a = GameEntity.create(SHA_A, "Contra (USA).nes", "source-a", "uri-a");
    dao.insert(a);
    dao.setFavorite(SHA_A, true);
    dao.recordLaunch(SHA_A, 1234L);
    dao.renameOriginal(SHA_A, "魂斗罗.nes");
    GameEntity loaded = dao.getById(SHA_A);
    assertTrue(loaded.favorite);
    assertEquals(1, loaded.playCount);
    assertEquals(1234L, loaded.lastPlayedAt);
}
```

- [ ] **Step 2: Run and observe missing schema**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.library.db.FlyNesDatabaseTest --console=plain`

Expected: compile FAIL.

- [ ] **Step 3: Implement schema version 1**

`games` primary key is `romSha1`; fields include sourceId, documentUri, zipEntry, originalFilename, normalizedFilename, size, mapper, PRG/CHR, region, favorite, lastPlayedAt, playCount, builtIn, and lastSeenScan. `sources` primary key is sourceId; fields include treeUri, displayName, permissionState, lastScanAt, and enabled. Export Room schemas to `app/schemas`.

- [ ] **Step 4: Run DAO and schema export tests**

Run: `./gradlew.bat :app:connectedDebugAndroidTest --console=plain`

Expected: identity-based favorite/recent test PASS and schema JSON exists.

- [ ] **Step 5: Commit the database**

```powershell
git add app/build.gradle app/src/main/java/com/flynes/emu/library/db app/src/androidTest/java/com/flynes/emu/library/db app/schemas
git commit -m "feat: add Room-backed game library schema"
```

### Task 2: Migrate legacy RomStore without dead SAF entries

**Files:**
- Create: `app/src/main/java/com/flynes/emu/library/LegacyLibraryMigrator.java`
- Test: `app/src/androidTest/java/com/flynes/emu/library/LegacyLibraryMigratorTest.java`
- Modify: `app/src/main/java/com/flynes/emu/RomStore.java`

- [ ] **Step 1: Write valid, corrupt, and revoked-permission migration tests**

The test seeds current `game_library` SharedPreferences JSON, grants or withholds a test DocumentsProvider URI, runs migration, and asserts: valid entries import once; corrupt JSON remains backed up; revoked sources become `NEEDS_REAUTHORIZE` instead of disappearing.

- [ ] **Step 2: Run and observe no migration path**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.library.LegacyLibraryMigratorTest --console=plain`

Expected: FAIL for missing migrator.

- [ ] **Step 3: Implement idempotent migration**

Read old JSON, normalize every URI, derive a deterministic source ID from tree URI, insert legacy rows in one Room transaction, and set `legacy_library_migration=success` only after commit. Keep the raw preference under `games_legacy_backup` for one release. Do not invent ROM SHA-1; mark imported rows `identityPending=true` until first validated scan.

- [ ] **Step 4: Run migration tests twice**

Expected: same row counts after the second run, no duplicate built-in game, and actionable revoked-source state.

- [ ] **Step 5: Commit migration**

```powershell
git add app/src/main/java/com/flynes/emu/library/LegacyLibraryMigrator.java app/src/main/java/com/flynes/emu/RomStore.java app/src/androidTest/java/com/flynes/emu/library/LegacyLibraryMigratorTest.java
git commit -m "feat: migrate legacy game library into Room"
```

### Task 3: Return structured scan results and deterministic ZIP entries

**Files:**
- Create: `app/src/main/java/com/flynes/emu/library/ScanResult.java`
- Create: `app/src/main/java/com/flynes/emu/library/ScanIssue.java`
- Create: `app/src/main/java/com/flynes/emu/library/ScanSource.java`
- Create: `app/src/main/java/com/flynes/emu/library/ScannedGame.java`
- Create: `app/src/main/java/com/flynes/emu/library/GameScanner.java`
- Create: `app/src/main/java/com/flynes/emu/library/ZipRomEntry.java`
- Test: `app/src/test/java/com/flynes/emu/library/GameScannerTest.java`

- [ ] **Step 1: Write scanner fixture tests**

```java
private static final byte[] ROM_A = inesRom((byte) 0x11);
private static final byte[] ROM_B = inesRom((byte) 0x22);
private final GameScanner scanner = new GameScanner(64L * 1024 * 1024, 100.0);

@Test public void zipWithTwoRomsCreatesTwoDeterministicEntries() throws Exception {
    ScanResult result = scanner.scan(zip(Map.of("b.nes", ROM_B, "a.nes", ROM_A)));
    assertEquals(List.of("a.nes", "b.nes"), result.entries().stream()
            .map(ScannedGame::zipEntry).toList());
    assertTrue(result.fatalError().isEmpty());
}

@Test public void permissionFailurePreservesPreviousCatalog() {
    ScanResult result = scanner.scan(new ScanSource() {
        @Override public String name() { return "revoked"; }
        @Override public InputStream open() { throw new SecurityException("revoked"); }
    });
    assertEquals(ScanIssue.Code.PERMISSION_REVOKED, result.fatalError().orElseThrow().code());
    assertFalse(result.replaceExistingCatalog());
}

private static byte[] inesRom(byte marker) {
    byte[] rom = new byte[16 + 16 * 1024];
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1;
    rom[16] = marker;
    return rom;
}

private static ScanSource zip(Map<String, byte[]> entries) throws IOException {
    ByteArrayOutputStream bytes = new ByteArrayOutputStream();
    try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
        for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
            zip.putNextEntry(new ZipEntry(entry.getKey()));
            zip.write(entry.getValue());
            zip.closeEntry();
        }
    }
    byte[] archive = bytes.toByteArray();
    return new ScanSource() {
        @Override public String name() { return "games.zip"; }
        @Override public InputStream open() { return new ByteArrayInputStream(archive); }
    };
}
```

- [ ] **Step 2: Run and observe current empty-list error collapse**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*GameScannerTest' --console=plain`

Expected: FAIL because the old scanner returns only `List<GameEntry>`.

- [ ] **Step 3: Implement validation and limits**

Use this JVM-testable input contract:

```java
public interface ScanSource {
    String name();
    java.io.InputStream open() throws java.io.IOException, SecurityException;
}
```

`GameScanner(long maxDecompressedBytes, double maxCompressionRatio)` validates iNES/NES2 magic before creating entries, enumerates ZIP names in locale-independent sorted order, caps total decompressed bytes and compression ratio, reports encrypted/corrupt/oversize/no-ROM reasons, and treats `SecurityException` as `PERMISSION_REVOKED`. The Android adapter requests read-only SAF permission and implements `ScanSource`. `ScanResult` contains entries, skipped issues, warnings, optional fatal error, and `replaceExistingCatalog`.

- [ ] **Step 4: Run corrupt/empty/multi-ROM/zip-bomb tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*GameScannerTest' --console=plain`

Expected: zero fake entries; deterministic multi-ROM output; fatal errors never request catalog replacement.

- [ ] **Step 5: Commit structured scanning**

```powershell
git add app/src/main/java/com/flynes/emu/library app/src/test/java/com/flynes/emu/library
git commit -m "feat: scan ROM sources with structured deterministic results"
```

### Task 4: Implement source management and repository transactions

**Files:**
- Create: `app/src/main/java/com/flynes/emu/library/GameRepository.java`
- Create: `app/src/main/java/com/flynes/emu/library/SourceManager.java`
- Test: `app/src/androidTest/java/com/flynes/emu/library/SourceManagementTest.java`

- [ ] **Step 1: Write add/refresh/rebind/remove tests**

Add source A, add source B, refresh A after adding/removing ROMs, revoke A, rebind A, then remove A. Assert B is unchanged throughout and fatal refresh does not erase A's last good entries.

- [ ] **Step 2: Run and observe missing repository**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.library.SourceManagementTest --console=plain`

Expected: compile FAIL.

- [ ] **Step 3: Implement transactional reconciliation**

`GameRepository.refresh(sourceId)` scans outside Room, then in one transaction upserts seen identities, preserves favorite/recent metadata, removes only entries absent from a successful scan of that source, and updates lastScanAt. Rebind updates the source URI then reconciles by RomIdentity. Remove deletes only that source's rows after user confirmation.

- [ ] **Step 4: Run source-management tests**

Expected: all transitions pass without clearing application data.

- [ ] **Step 5: Commit repository/source management**

```powershell
git add app/src/main/java/com/flynes/emu/library app/src/androidTest/java/com/flynes/emu/library/SourceManagementTest.java
git commit -m "feat: add resilient multi-source library management"
```

### Task 5: Add localized title metadata and Unicode search

**Files:**
- Create: `app/src/main/assets/metadata/game_titles.v1.json`
- Create: `app/src/main/java/com/flynes/emu/metadata/LocalizedGameMetadata.java`
- Create: `app/src/main/java/com/flynes/emu/metadata/SearchNormalizer.java`
- Create: `app/src/main/java/com/flynes/emu/metadata/GameTitleResolver.java`
- Test: `app/src/test/java/com/flynes/emu/metadata/GameTitleResolverTest.java`

- [ ] **Step 1: Write Chinese alias, Turkish locale, and fallback tests**

```java
@Test public void contraMatchesChineseAndEnglishQueries() {
    GameDisplay game = resolver.resolve(null, "Contra (USA) (Rev A).nes", Locale.SIMPLIFIED_CHINESE);
    assertEquals("魂斗罗", game.displayTitle());
    assertTrue(game.matches("魂斗罗"));
    assertTrue(game.matches("contra"));
}

@Test public void normalizationIsIndependentOfTurkishDefaultLocale() {
    Locale previous = Locale.getDefault();
    try {
        Locale.setDefault(Locale.forLanguageTag("tr-TR"));
        assertEquals("ice climber", SearchNormalizer.normalize("ICE CLIMBER"));
    } finally { Locale.setDefault(previous); }
}
```

- [ ] **Step 2: Run and observe old filename-only matching**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*metadata*' --console=plain`

Expected: FAIL.

- [ ] **Step 3: Add versioned metadata and fallback order**

The JSON schema contains `schemaVersion`, SHA-1 aliases when legally known, canonical normalized-name patterns, `titles.en`, `titles.zh-Hans`, aliases, region, and review source. Seed the existing curated common-title list without ROM bytes. Resolve in order: user title → locale title → canonical title → normalized filename → original filename. Normalize with NFKC, whitespace collapse, tag removal, and `Locale.ROOT` case folding; sort display titles with `Collator` for the active locale.

- [ ] **Step 4: Run metadata and search tests**

Expected: Contra is searchable by both names, Turkish locale is stable, and unknown titles preserve original filenames.

- [ ] **Step 5: Commit offline metadata**

```powershell
git add app/src/main/assets/metadata app/src/main/java/com/flynes/emu/metadata app/src/test/java/com/flynes/emu/metadata
git commit -m "feat: add offline localized game titles and aliases"
```

### Task 6: Move all UI text into English and Simplified-Chinese resources

**Files:**
- Replace: `app/src/main/res/values/strings.xml`
- Create: `app/src/main/res/values-zh-rCN/strings.xml`
- Create: `app/src/main/res/xml/locales_config.xml`
- Modify: `app/src/main/AndroidManifest.xml`
- Test: `app/src/androidTest/java/com/flynes/emu/i18n/LocaleCoverageTest.java`

- [ ] **Step 1: Add a failing locale coverage test**

The test loads both locales and asserts non-empty values for every key used by Home, Game, Pause, Settings, Library, scanner errors, save status, and Licenses. It also formats 0/1/2 game counts through plurals.

- [ ] **Step 2: Run and confirm current hardcoded strings fail**

Run: `./gradlew.bat :app:lintDebug :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.i18n.LocaleCoverageTest --console=plain`

Expected: hardcoded/SetTextI18n warnings or coverage failure.

- [ ] **Step 3: Add the complete resource contract**

Required keys include: app/home/library titles, built-in/local/recent/favorites, add/refresh/manage/rebind/remove source, search/clear/no results, sort name/recent/size, scan progress/completed/skipped/fatal messages, play/loading/load failed, open pause/continue/settings/exit, autosave saving/saved/failed/retry, aspect/filter/refresh/control/haptic/audio/language labels and values, license/source/privacy, and all confirmation dialogs. Define `game_count` and `scan_result_count` plurals in both locales; mark ROM names and source URLs non-translatable.

- [ ] **Step 4: Run Lint and pseudolocale screenshots**

Run: `./gradlew.bat :app:lintDebug :app:connectedDebugAndroidTest --console=plain`

Expected: `SetTextI18n` and `DefaultLocale` counts are 0; en, zh-Hans, en-XA, and ar-XB screens have no clipping.

- [ ] **Step 5: Commit i18n resources**

```powershell
git add app/src/main/res/values app/src/main/res/values-zh-rCN app/src/main/res/xml/locales_config.xml app/src/main/AndroidManifest.xml app/src/androidTest/java/com/flynes/emu/i18n/LocaleCoverageTest.java
git commit -m "feat: localize FlyNES UI in English and Simplified Chinese"
```

### Task 7: Replace the library UI with explicit view states

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GameLibraryActivity.java`
- Create: `app/src/main/java/com/flynes/emu/library/GameListAdapter.java`
- Create: `app/src/main/java/com/flynes/emu/library/LibraryViewState.java`
- Create: `app/src/main/res/layout/activity_game_library.xml`
- Create: `app/src/main/res/layout/item_game.xml`
- Create: `app/src/main/res/layout/view_library_empty.xml`
- Create: `app/src/androidTest/java/com/flynes/emu/library/LibraryStatesTest.java`

- [ ] **Step 1: Write state tests for first run, no result, error, and content**

Assert first run displays the built-in row and add-source action; a query with zero matches displays clear-search; revoked permission displays rebind; a successful non-empty scan displays content plus always-visible source management.

- [ ] **Step 2: Run and observe contradictory current states**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.library.LibraryStatesTest --console=plain`

Expected: first-run or no-result assertion FAIL.

- [ ] **Step 3: Implement state-driven RecyclerView UI**

`LibraryViewState` is one of Loading, Content, BuiltinOnly, EmptySource, NoSearchResult, PermissionLost, or FatalError. The Activity renders exactly one state, never derives visibility from “has user games.” Rows show one localized title, one built-in badge, recent/favorite state, original filename as secondary information, and accessible loading/pressed/selected states.

- [ ] **Step 4: Run state, locale, and 200% font tests**

Expected: every state has an actionable CTA; no blank screen; no repeated `(内置) [内置]` labels; all content scrolls at 200% font.

- [ ] **Step 5: Commit the product library**

```powershell
git add app/src/main/java/com/flynes/emu/GameLibraryActivity.java app/src/main/java/com/flynes/emu/library app/src/main/res/layout/activity_game_library.xml app/src/main/res/layout/item_game.xml app/src/main/res/layout/view_library_empty.xml app/src/androidTest/java/com/flynes/emu/library/LibraryStatesTest.java
git commit -m "feat: rebuild the game library around explicit states"
```

### Task 8: Remove legacy library code and record acceptance

**Files:**
- Delete: `app/src/main/java/com/flynes/emu/RomScanner.java`
- Delete: `app/src/main/java/com/flynes/emu/RomLoader.java`
- Delete: `app/src/main/java/com/flynes/emu/Popularity.java`
- Delete after migration window: `app/src/main/java/com/flynes/emu/RomStore.java`
- Create: `docs/acceptance/alpha/library-i18n.md`

- [ ] **Step 1: Remove all production references to legacy classes**

Run: `rg "RomScanner|RomLoader|Popularity|RomStore" app/src/main/java`

Expected before deletion: only `LegacyLibraryMigrator` may reference `RomStore`; after the migration window, no matches.

- [ ] **Step 2: Run the complete library matrix**

Test 0/1/N ROMs, two sources, add/delete/rename, permission revoke/rebind, invalid/multi-ROM ZIP, Chinese/English search, favorites/recent, force-stop, and en/zh/tr/pseudolocales.

- [ ] **Step 3: Record evidence**

`library-i18n.md` records database schema hash, migration result, fixture matrix, state screenshots, locale coverage, and search examples including “魂斗罗/Contra.”

- [ ] **Step 4: Run global verification**

Run: `./gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest :app:connectedDebugAndroidTest --console=plain`

Expected: `BUILD SUCCESSFUL`; no i18n or library test failure.

- [ ] **Step 5: Commit cleanup and evidence**

```powershell
git add -A app/src/main/java/com/flynes/emu app/src/main/res docs/acceptance/alpha/library-i18n.md
git commit -m "test: verify resilient localized game library flows"
```
