package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.util.Log;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.app.FlyNesApp;
import com.flynes.emu.app.NativeCatalogEntry;
import com.flynes.emu.catalog.BuiltinGames;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.migration.LegacyLibraryMigrator;
import com.flynes.emu.catalog.persistence.CanonicalUserState;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateCodec;
import com.flynes.emu.catalog.persistence.CatalogStateStore;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;
import com.flynes.emu.catalog.source.DocumentLocatorShape;
import com.flynes.emu.catalog.source.SourceEnumerator;
import com.flynes.emu.catalog.source.SourceRegistry;
import com.flynes.emu.settings.ControlLayoutRepository;
import com.flynes.emu.settings.DualSettingsStore;
import com.flynes.emu.settings.NativeSettingsStore;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Isolated catalog bootstrap/runtime seam for the later Game Center UI. */
public final class AndroidCatalogRuntime implements AutoCloseable {
    private static final String UUID_PREFS = "flynes_source_uuids";
    private static final String MIGRATION_PREFS = "flynes_migration_log";

    private final Context context;
    private final boolean nativeCatalog;
    private final AndroidAtomicCatalogStateStore store;
    private final GameCatalog catalog;
    private final CatalogRepository repository;
    private final RomPackageScanner scanner;
    private final PersistedReadPermissionGateway permissions;
    private final SourceRegistry sources;
    private final ExecutorService executor;
    private final AndroidBuiltinCatalogAdapter builtin;
    private final BuiltinGames builtinGames;
    private final FlyNesApp nativeApp;
    private final AndroidUuidSafMap uuidMap;
    private final AndroidRetryableMigrationLog migrationLog;
    private final AndroidPackageLocatorMap locators;
    private final SettingsRepository settingsRepository;
    private final File dataRoot;
    private final File cacheRoot;

    public AndroidCatalogRuntime(Context context) {
        this(context, defaultStateFile(context), true);
    }

    public AndroidCatalogRuntime(Context context, File stateFile) {
        this(context, stateFile, false);
    }

    private AndroidCatalogRuntime(Context context, File stateFile, boolean nativeCatalog) {
        if (context == null || stateFile == null) throw new NullPointerException();
        this.context = context.getApplicationContext();
        this.nativeCatalog = nativeCatalog;
        File parent = stateFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IllegalStateException("catalog directory unavailable");
        }
        catalog = new GameCatalog();
        scanner = new RomPackageScanner(ScanLimits.defaults());
        permissions = new PersistedReadPermissionGateway(this.context.getContentResolver());
        executor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "flynes-catalog");
            thread.setDaemon(true);
            return thread;
        });
        // The bundled game list comes from the shared manifest. A missing or
        // broken manifest must not take the whole library down: the builtin
        // source simply ends up with nothing to show.
        BuiltinGames games;
        try {
            games = BuiltinGames.fromAssets(this.context);
        } catch (IOException failure) {
            Log.w("FlyNES", "bundled game manifest is unreadable", failure);
            games = BuiltinGames.empty();
        }
        builtinGames = games;
        builtin = new AndroidBuiltinCatalogAdapter(
                builtinGames, assetPath -> this.context.getAssets().open(assetPath));
        locators = new AndroidPackageLocatorMap();
        if (nativeCatalog) {
            dataRoot = nativeDataRoot(this.context);
            cacheRoot = nativeCacheRoot(this.context);
            if (!dataRoot.exists() && !dataRoot.mkdirs() && !dataRoot.isDirectory()) {
                throw new IllegalStateException("native catalog data root unavailable");
            }
            if (!cacheRoot.exists() && !cacheRoot.mkdirs() && !cacheRoot.isDirectory()) {
                throw new IllegalStateException("native catalog cache root unavailable");
            }
            store = new AndroidAtomicCatalogStateStore(stateFile);
            repository = new CatalogRepository(
                    CatalogState.empty(AndroidBuiltinCatalogAdapter.SOURCE), new MemoryStore(), catalog);
            nativeApp = FlyNesApp.create(dataRoot.getAbsolutePath(), cacheRoot.getAbsolutePath());
            SharedPreferences uuidPrefs = this.context.getSharedPreferences(
                    UUID_PREFS, Context.MODE_PRIVATE);
            uuidMap = new AndroidUuidSafMap(
                    key -> uuidPrefs.getString(key, null),
                    (key, value) -> uuidPrefs.edit().putString(key, value).commit(),
                    key -> uuidPrefs.edit().remove(key).commit());
            SharedPreferences logPrefs = this.context.getSharedPreferences(
                    MIGRATION_PREFS, Context.MODE_PRIVATE);
            migrationLog = new AndroidRetryableMigrationLog(
                    key -> logPrefs.getString(key, null),
                    (key, value) -> logPrefs.edit().putString(key, value).commit());
            settingsRepository = new SettingsRepository(new DualSettingsStore(
                    new NativeSettingsStore(nativeApp),
                    new SharedPreferencesSettingsStore(this.context)));
        } else {
            dataRoot = null;
            cacheRoot = null;
            store = new AndroidAtomicCatalogStateStore(stateFile);
            repository = new CatalogRepository(
                    CatalogState.empty(AndroidBuiltinCatalogAdapter.SOURCE), store, catalog);
            nativeApp = null;
            uuidMap = null;
            migrationLog = null;
            settingsRepository = new SettingsRepository(
                    new SharedPreferencesSettingsStore(this.context));
        }
        sources = new SourceRegistry(
                repository, permissions, new AndroidPendingReleaseStore(this.context));
    }

    public SettingsRepository settingsRepository() {
        return settingsRepository;
    }

    public ControlLayoutRepository.Backend controlLayoutBackend() {
        return nativeApp;
    }

    public Future<BootstrapResult> bootstrap() {
        return submit(this::bootstrapOnCatalogThread);
    }

    public Future<RomSource> addOrReauthorizeTree(String locator, int resultFlags) {
        return submit(() -> {
            RomSource source = sources.addOrReauthorize(locator, resultFlags);
            if (nativeCatalog) {
                byte[] uuid = uuidMap.uuidForLocator(locator);
                if (uuid == null) {
                    uuid = randomUuid();
                    uuidMap.put(uuid, locator);
                }
                boolean registered = false;
                for (var status : nativeApp.sourceStatuses()) {
                    if (java.util.Arrays.equals(uuid, status.sourceUuid())) {
                        registered = true;
                        break;
                    }
                }
                // Persist a new, empty source before replacing the Java view. Never empty-scan
                // an existing source: that would remove its games during reauthorization.
                if (!registered) {
                    int begun = nativeApp.scanBegin(uuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY);
                    if (begun != FlyCatalogCommands.OK) {
                        throw new IOException("source registration begin failed: " + begun);
                    }
                    int committed = nativeApp.scanCommit(FlyCatalogCommands.SCAN_FULL);
                    if (committed != FlyCatalogCommands.OK) {
                        nativeApp.scanAbort();
                        throw new IOException("source registration commit failed: " + committed);
                    }
                }
                refreshNativeView();
            }
            return source;
        });
    }

    public Future<Void> removeSource(String sourceId) {
        return submit(() -> {
            if (nativeCatalog) {
                SourceCatalogState existing = repository.state().sources().get(sourceId);
                if (existing != null && existing.source().type() == RomSource.Type.SAF_TREE) {
                    byte[] uuid = uuidMap.uuidForLocator(existing.source().uri());
                    if (uuid != null) {
                        int begun = nativeApp.scanBegin(
                                uuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY);
                        if (begun == FlyCatalogCommands.OK) {
                            nativeApp.scanCommit(FlyCatalogCommands.SCAN_FULL);
                        } else {
                            nativeApp.scanAbort();
                        }
                        uuidMap.remove(uuid);
                    }
                }
            }
            sources.remove(sourceId);
            if (nativeCatalog) refreshNativeView();
            return null;
        });
    }

    public Future<SourceScanResult> scanSource(String sourceId) {
        return submit(() -> scanSourceOnCatalogThread(sourceId));
    }

    public Future<Boolean> setFavorite(String canonicalId, boolean favorite) {
        return submit(() -> {
            if (nativeCatalog) {
                int result = nativeApp.favoriteSet(canonicalId, favorite);
                refreshNativeView();
                return result == FlyCatalogCommands.OK && repository.state()
                        .userStates().getOrDefault(canonicalId, CanonicalUserState.EMPTY).favorite()
                        == favorite;
            }
            return repository.setFavorite(canonicalId, favorite);
        });
    }

    public Future<Boolean> recordSuccessfulLaunch(String canonicalId) {
        return submit(() -> {
            if (nativeCatalog) {
                int result = nativeApp.markPlayed(canonicalId);
                refreshNativeView();
                return result == FlyCatalogCommands.OK;
            }
            return repository.recordSuccessfulLaunch(canonicalId);
        });
    }

    public CatalogState stateSnapshot() { return repository.state(); }
    public GameCatalog gameCatalog() { return catalog; }

    public AndroidCatalogStreamOpener streamOpener() {
        return new AndroidCatalogStreamOpener(
                context, repository, permissions::hasPersistedRead);
    }

    private BootstrapResult bootstrapOnCatalogThread() throws Exception {
        if (nativeCatalog) {
            migrateSettingsIfNeeded();
            migrateFncaIfNeeded();
            // Commit the bundled source before projecting the complete native catalog once.
            // Previously both sides of this scan rebuilt every external game on each startup.
            Exception builtinFailure = null;
            try {
                scanBuiltinNative();
            } catch (Exception failure) {
                nativeApp.scanAbort();
                builtinFailure = failure;
            }
            CatalogRepository.LoadResult load = refreshNativeView();
            try {
                sources.retryPendingReleases();
            } catch (SourceRegistry.PendingReleaseException ignored) {
            }
            sources.verifyPersistedPermissions();
            // A bundled-asset/cache failure must still leave the existing library usable.
            // Preserve the failure for the UI after publishing and checking source permissions.
            if (builtinFailure != null) throw builtinFailure;
            return new BootstrapResult(load, null);
        }
        boolean storeExisted = store.baseFile().exists()
                || new File(store.baseFile().getPath() + ".bak").exists();
        CatalogRepository.LoadResult load = repository.load();
        if (load.status() == CatalogRepository.LoadStatus.RECOVERY_NEEDED) {
            return new BootstrapResult(load, null);
        }
        try {
            sources.retryPendingReleases();
        } catch (SourceRegistry.PendingReleaseException ignored) {
            // Tombstone remains durable and will be retried on the next bootstrap.
        }
        sources.verifyPersistedPermissions();
        LegacyLibraryMigrator.Result migration = migrateLegacyIfEligible(!storeExisted);
        if (migration.status() == LegacyLibraryMigrator.Status.RECOVERY_NEEDED
                || migration.hasFatalIssue()) {
            return new BootstrapResult(load, migration);
        }
        ScanResult scanned = builtin.scan();
        SourceCatalogState builtinState = repository.state().sources().get(
                AndroidBuiltinCatalogAdapter.SOURCE.id());
        repository.commitScan(SourceScanResult.from(
                AndroidBuiltinCatalogAdapter.SOURCE, repository.state().revision(),
                Math.addExact(builtinState.lastScanToken(), 1),
                SourceScanResult.Completeness.FULL, scanned,
                // One candidate per bundled game: the scan accounting must match
                // the outcome list, so it cannot be a fixed number any more.
                scanned.packageOutcomes().size()));
        return new BootstrapResult(load, migration);
    }

    private void migrateSettingsIfNeeded() {
        File flyset = new File(dataRoot, "settings.flyset01");
        if (flyset.exists() || !migrationLog.shouldRetry(
                AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4)) {
            return;
        }
        try {
            boolean saved = settingsRepository.save(
                    new SettingsRepository(new SharedPreferencesSettingsStore(context)).load());
            migrationLog.record(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4, saved,
                    saved ? "applied schema 4" : "native apply failed");
        } catch (RuntimeException failure) {
            migrationLog.record(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4, false,
                    String.valueOf(failure.getMessage()));
        }
    }

    private void migrateFncaIfNeeded() {
        File flycat = new File(dataRoot, "catalog.flycat01");
        File fnca = defaultStateFile(context);
        if (flycat.exists() || !fnca.exists() || !migrationLog.shouldRetry(
                AndroidRetryableMigrationLog.Kind.CATALOG_FNCA)) {
            return;
        }
        try {
            byte[] encoded = new AndroidAtomicCatalogStateStore(fnca).read();
            if (encoded == null) return;
            CatalogState state = CatalogStateCodec.decode(encoded);
            new FncaNativeMigrator().migrate(state, uuidMap, nativeApp, this::openPackage, migrationLog);
        } catch (Exception failure) {
            migrationLog.record(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA, false,
                    String.valueOf(failure.getMessage()));
        }
    }

    private void scanBuiltinNative() throws Exception {
        byte[] uuid = uuidMap.builtinUuid();
        int begun = nativeApp.scanBegin(uuid, FlyCatalogCommands.SOURCE_SCOPE_BUILTIN);
        if (begun != FlyCatalogCommands.OK) {
            Log.w("FlyNES", "bundled scan could not begin: rc=" + begun);
            nativeApp.scanAbort();
            return;
        }
        for (BuiltinGames.Entry game : builtinGames.all()) {
            File copied = copyBuiltinAsset(game.assetPath());
            try (ParcelFileDescriptor pfd = ParcelFileDescriptor.open(
                    copied, ParcelFileDescriptor.MODE_READ_ONLY)) {
                int added = nativeApp.scanAddFile(
                        game.assetFilename, game.assetFilename, pfd.getFd(), null);
                if (added != FlyCatalogCommands.OK) {
                    Log.w("FlyNES", "bundled scan rejected " + game.assetFilename + ": rc=" + added);
                }
            } finally {
                // noinspection ResultOfMethodCallIgnored
                copied.delete();
            }
            locators.put(uuid, game.assetFilename,
                    AndroidBuiltinCatalogAdapter.assetLocator(game.assetFilename));
        }
        int committed = nativeApp.scanCommit(FlyCatalogCommands.SCAN_FULL);
        if (committed != FlyCatalogCommands.OK) {
            Log.w("FlyNES", "bundled scan commit failed: rc=" + committed);
        } else {
            Log.i("FlyNES", "bundled scan committed " + builtinGames.all().size() + " game(s)");
        }
        refreshNativeView();
    }

    private CatalogRepository.LoadResult refreshNativeView() throws Exception {
        List<NativeCatalogEntry> entries = nativeApp.catalogEntries();
        LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>();
        long sequence = 0;
        HashSet<String> seen = new HashSet<>();
        for (NativeCatalogEntry entry : entries) {
            if (!seen.add(entry.canonicalId())) continue;
            CanonicalUserState user = nativeApp.userState(entry.canonicalId());
            if (!user.equals(CanonicalUserState.EMPTY)) users.put(entry.canonicalId(), user);
            sequence = Math.max(sequence, user.lastPlayedSequence());
        }
        CatalogState projected = NativeCatalogProjector.project(
                entries, nativeApp.sourceStatuses(), users, sequence, uuidMap, locators,
                AndroidDocumentLocators::documentUriFor, builtinGames);
        // The repository owns durability; a second atomic write here would race it.
        return repository.loadProjection(projected);
    }

    private LegacyLibraryMigrator.Result migrateLegacyIfEligible(boolean newStoreAbsent)
            throws Exception {
        AndroidLegacyRomStoreReader reader = new AndroidLegacyRomStoreReader(context);
        LegacyLibraryMigrator.LegacySnapshot legacy = reader.read();
        boolean granted = legacy.treeLocator() != null
                && permissions.hasPersistedRead(legacy.treeLocator());
        SourceEnumerator.Result enumerated = null;
        if (granted) {
            RomSource source = new RomSource(
                    com.flynes.emu.catalog.StableIds.safSourceId(legacy.treeLocator()),
                    RomSource.Type.SAF_TREE, legacy.treeLocator(),
                    RomSource.PermissionState.GRANTED);
            enumerated = new SourceEnumerator(16, 20_000).enumerate(
                    source, new AndroidDocumentTreeGateway(
                            context.getContentResolver(), legacy.treeLocator()));
            if (enumerated.completeness() == SourceEnumerator.Completeness.FATAL) {
                boolean permissionRevoked = false;
                for (ScanIssue issue : enumerated.issues()) {
                    permissionRevoked |= issue.code() == ScanIssue.Code.PERMISSION_REVOKED;
                }
                if (!permissionRevoked) {
                    return new LegacyLibraryMigrator.Result(
                            LegacyLibraryMigrator.Status.RECOVERY_NEEDED,
                            java.util.Collections.emptyList());
                }
                granted = false;
            }
        }
        return new LegacyLibraryMigrator(scanner).migrate(
                legacy, granted,
                enumerated == null ? java.util.Collections.emptyList() : enumerated.candidates(),
                repository, new AndroidLegacyMigrationMarker(context), newStoreAbsent);
    }

    private SourceScanResult scanSourceOnCatalogThread(String sourceId) throws Exception {
        sources.verifyPersistedPermissions();
        SourceCatalogState current = repository.state().sources().get(sourceId);
        if (current == null) throw new IllegalArgumentException("unknown source ID");
        RomSource source = current.source();
        SourceEnumerator.Result enumeration = new SourceEnumerator(16, 20_000).enumerate(
                source, new AndroidDocumentTreeGateway(
                        context.getContentResolver(), source.uri()));
        if (nativeCatalog) {
            return scanSourceNative(source, current, enumeration);
        }
        ScanResult scanned = scanner.scan(source, enumeration.candidates());
        ArrayList<ScanIssue> issues = new ArrayList<>(scanned.issues());
        issues.addAll(enumeration.issues());
        ScanResult combined = new ScanResult(
                scanned.packages(), scanned.packageOutcomes(), scanned.entryOutcomes(), issues);
        SourceScanResult.Completeness completeness = combined.hasFatalIssue()
                ? SourceScanResult.Completeness.FATAL : SourceScanResult.Completeness.FULL;
        SourceScanResult result = SourceScanResult.from(
                source, repository.state().revision(), Math.addExact(current.lastScanToken(), 1),
                completeness, combined, enumeration.candidateCount());
        repository.commitScan(result);
        return result;
    }

    private SourceScanResult scanSourceNative(
            RomSource source, SourceCatalogState current, SourceEnumerator.Result enumeration)
            throws Exception {
        byte[] uuid = uuidMap.uuidForLocator(source.uri());
        if (uuid == null) {
            uuid = randomUuid();
            uuidMap.put(uuid, source.uri());
        }
        int begun = nativeApp.scanBegin(uuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY);
        android.util.Log.i("FlyNesSources", "scanBegin " + source.uri() + " -> " + begun
                + " candidates=" + enumeration.candidates().size());
        if (begun != FlyCatalogCommands.OK) {
            nativeApp.scanAbort();
            throw new IOException("fly_scan_begin failed: " + begun);
        }
        HashSet<String> usedNames = new HashSet<>();
        ArrayList<PackageOutcome> outcomes = new ArrayList<>();
        try {
            for (PackageCandidate candidate : enumeration.candidates()) {
                String relative = sourceRelative(candidate, source.uri(), usedNames);
                locators.put(uuid, relative, candidate.contentLocator());
                try (ParcelFileDescriptor pfd = context.getContentResolver().openFileDescriptor(
                        Uri.parse(candidate.contentLocator()), "r")) {
                    if (pfd == null) throw new IOException("provider returned no fd");
                    int[] outcome = new int[3];
                    int added = nativeApp.scanAddFile(relative, fileName(relative), pfd.getFd(), null, outcome);
                    PackageOutcome.Status status = added != FlyCatalogCommands.OK
                            ? PackageOutcome.Status.ERROR : switch (outcome[0]) {
                        case 1 -> PackageOutcome.Status.INDEXED;
                        case 2 -> PackageOutcome.Status.SKIPPED;
                        default -> PackageOutcome.Status.ERROR;
                    };
                    PackageOutcome.Reason reason = switch (outcome[1]) {
                        case 1 -> PackageOutcome.Reason.INDEXED;
                        case 2 -> PackageOutcome.Reason.NO_SUPPORTED_PAYLOADS;
                        case 4 -> PackageOutcome.Reason.PACKAGE_LIMIT_EXCEEDED;
                        case 6 -> PackageOutcome.Reason.INVALID_ZIP;
                        default -> PackageOutcome.Reason.IO_ERROR;
                    };
                    outcomes.add(new PackageOutcome(StableIds.packageId(source.id(), relative), status, reason));
                    if (added != FlyCatalogCommands.OK) {
                        android.util.Log.w("FlyNesSources", "scanAddFile rejected relative="
                                + relative + " code=" + added);
                    }
                }
            }
        } catch (Exception failure) {
            nativeApp.scanAbort();
            throw failure;
        }
        int completeness = enumeration.completeness() == SourceEnumerator.Completeness.FATAL
                ? FlyCatalogCommands.SCAN_FATAL : FlyCatalogCommands.SCAN_FULL;
        int committed = nativeApp.scanCommit(completeness);
        android.util.Log.i("FlyNesSources", "scanCommit " + source.uri() + " -> " + committed);
        if (committed != FlyCatalogCommands.OK && committed != FlyCatalogCommands.CONFLICT) {
            throw new IOException("fly_scan_commit failed: " + committed);
        }
        refreshNativeView();
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        SourceCatalogState refreshed = repository.state().sources().get(source.id());
        if (refreshed != null && completeness != FlyCatalogCommands.SCAN_FATAL) {
            for (PackageOutcome outcome : outcomes) {
                if (outcome.status() == PackageOutcome.Status.INDEXED) {
                    var indexed = refreshed.packages().get(outcome.packageId());
                    if (indexed != null) packages.add(indexed.physicalPackage());
                }
            }
        }
        ScanResult scanned = new ScanResult(
                packages, outcomes,
                java.util.Collections.emptyList(), enumeration.issues());
        return SourceScanResult.from(
                source, repository.state().revision(), Math.addExact(current.lastScanToken(), 1),
                completeness == FlyCatalogCommands.SCAN_FATAL
                        ? SourceScanResult.Completeness.FATAL : SourceScanResult.Completeness.FULL,
                scanned, enumeration.candidateCount());
    }

    private FncaNativeMigrator.OpenedFile openPackage(PhysicalPackage pkg) throws Exception {
        if (pkg.source().type() == RomSource.Type.BUILTIN) {
            File copied = copyBuiltinAsset(builtinAssetPath(pkg.originalFilename()));
            ParcelFileDescriptor pfd = ParcelFileDescriptor.open(
                    copied, ParcelFileDescriptor.MODE_READ_ONLY);
            return new FncaNativeMigrator.OpenedFile(
                    pkg.originalFilename(), pkg.originalFilename(), pfd.getFd(),
                    shaBytes(pkg.physicalPackageSha256()),
                    () -> {
                        pfd.close();
                        // noinspection ResultOfMethodCallIgnored
                        copied.delete();
                    });
        }
        if (!DocumentLocatorShape.isOpenableDocumentLocator(pkg.sourceUri())) {
            throw new IOException("package locator is not an openable document URI");
        }
        ParcelFileDescriptor pfd;
        try {
            pfd = context.getContentResolver().openFileDescriptor(
                    Uri.parse(pkg.sourceUri()), "r");
        } catch (IllegalArgumentException malformed) {
            throw new IOException("provider rejected the package locator", malformed);
        }
        if (pfd == null) throw new IOException("provider returned no fd");
        locators.put(
                uuidMap.uuidForLocator(pkg.source().uri()) == null
                        ? uuidMap.builtinUuid() : uuidMap.uuidForLocator(pkg.source().uri()),
                pkg.originalFilename(), pkg.sourceUri());
        return new FncaNativeMigrator.OpenedFile(
                pkg.originalFilename(), pkg.originalFilename(), pfd.getFd(),
                shaBytes(pkg.physicalPackageSha256()), pfd);
    }

    /** Asset path of a bundled ROM, resolved from the shared manifest by filename. */
    private String builtinAssetPath(String assetFilename) throws IOException {
        for (BuiltinGames.Entry game : builtinGames.all()) {
            if (game.assetFilename.equals(assetFilename)) return game.assetPath();
        }
        throw new IOException("no bundled game declares the asset " + assetFilename);
    }

    private File copyBuiltinAsset(String assetPath) throws IOException {
        File copied = File.createTempFile("flynes-builtin-", ".nes", cacheRoot);
        try (InputStream input = context.getAssets().open(assetPath);
             FileOutputStream output = new FileOutputStream(copied)) {
            byte[] buffer = new byte[8192];
            while (true) {
                int count = input.read(buffer);
                if (count < 0) break;
                output.write(buffer, 0, count);
            }
            output.flush();
        } catch (IOException | RuntimeException failure) {
            // noinspection ResultOfMethodCallIgnored
            copied.delete();
            throw failure;
        }
        return copied;
    }

    private <T> Future<T> submit(Callable<T> operation) {
        return executor.submit(operation);
    }

    @Override public void close() {
        executor.shutdownNow();
        if (nativeApp != null) nativeApp.close();
    }

    public static File defaultStateFile(Context context) {
        return new File(new File(context.getApplicationContext().getFilesDir(), "catalog"),
                "catalog-state.bin");
    }

    static File nativeDataRoot(Context context) {
        return new File(context.getApplicationContext().getFilesDir(), "flynes");
    }

    static File nativeCacheRoot(Context context) {
        return new File(context.getApplicationContext().getCacheDir(), "flynes");
    }

    public record BootstrapResult(
            CatalogRepository.LoadResult loadResult,
            LegacyLibraryMigrator.Result migrationResult) {
    }

    private static byte[] randomUuid() {
        java.util.UUID value = java.util.UUID.randomUUID();
        byte[] bytes = new byte[16];
        long high = value.getMostSignificantBits();
        long low = value.getLeastSignificantBits();
        for (int index = 0; index < 8; index++) {
            bytes[index] = (byte) (high >>> (8 * (7 - index)));
            bytes[8 + index] = (byte) (low >>> (8 * (7 - index)));
        }
        return bytes;
    }

    private static byte[] shaBytes(String hex) {
        byte[] bytes = new byte[32];
        for (int index = 0; index < 32; index++) {
            bytes[index] = (byte) Integer.parseInt(hex.substring(index * 2, index * 2 + 2), 16);
        }
        return bytes;
    }

    /**
     * Prefers the canonical tree-relative path so a cold start can rebuild the document locator
     * for nested libraries, and falls back to the display name for providers whose document ids
     * are not rooted at the tree.
     */
    private static String sourceRelative(
            PackageCandidate candidate, String treeLocator, HashSet<String> used) {
        String relative = AndroidDocumentLocators.relativePathFor(
                treeLocator, candidate.stableDocumentKey());
        if (relative == null) relative = sourceRelativeDisplayName(candidate.displayFilename());
        return uniqueRelative(relative, used);
    }

    private static String sourceRelativeDisplayName(String displayFilename) {
        String name = displayFilename.replace('\\', '_');
        int slash = name.lastIndexOf('/');
        if (slash >= 0) name = name.substring(slash + 1);
        if (name.isEmpty() || ".".equals(name) || "..".equals(name)) name = "rom";
        return name;
    }

    private static String uniqueRelative(String relative, HashSet<String> used) {
        String candidate = relative;
        int serial = 1;
        while (!used.add(candidate)) {
            candidate = serial++ + "-" + relative;
        }
        return candidate;
    }

    private static String fileName(String relativePath) {
        int slash = relativePath.lastIndexOf('/');
        return slash < 0 ? relativePath : relativePath.substring(slash + 1);
    }

    private static final class MemoryStore implements CatalogStateStore {
        byte[] bytes;

        @Override public byte[] read() {
            return bytes == null ? null : bytes.clone();
        }

        @Override public void writeAtomically(byte[] encoded) {
            bytes = encoded.clone();
        }
    }
}
