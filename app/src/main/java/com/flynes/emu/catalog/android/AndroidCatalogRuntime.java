package com.flynes.emu.catalog.android;

import android.content.Context;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.migration.LegacyLibraryMigrator;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;
import com.flynes.emu.catalog.source.SourceEnumerator;
import com.flynes.emu.catalog.source.SourceRegistry;

import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.concurrent.Callable;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;

/** Isolated catalog bootstrap/runtime seam for the later Game Center UI. */
public final class AndroidCatalogRuntime implements AutoCloseable {
    private final Context context;
    private final AndroidAtomicCatalogStateStore store;
    private final GameCatalog catalog;
    private final CatalogRepository repository;
    private final RomPackageScanner scanner;
    private final PersistedReadPermissionGateway permissions;
    private final SourceRegistry sources;
    private final ExecutorService executor;
    private final AndroidBuiltinCatalogAdapter builtin;

    public AndroidCatalogRuntime(Context context) {
        this(context, defaultStateFile(context));
    }

    public AndroidCatalogRuntime(Context context, File stateFile) {
        if (context == null || stateFile == null) throw new NullPointerException();
        this.context = context.getApplicationContext();
        File parent = stateFile.getParentFile();
        if (parent != null && !parent.exists() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IllegalStateException("catalog directory unavailable");
        }
        store = new AndroidAtomicCatalogStateStore(stateFile);
        catalog = new GameCatalog();
        repository = new CatalogRepository(
                CatalogState.empty(AndroidBuiltinCatalogAdapter.SOURCE), store, catalog);
        scanner = new RomPackageScanner(ScanLimits.defaults());
        permissions = new PersistedReadPermissionGateway(this.context.getContentResolver());
        sources = new SourceRegistry(
                repository, permissions, new AndroidPendingReleaseStore(this.context));
        executor = Executors.newSingleThreadExecutor(runnable -> {
            Thread thread = new Thread(runnable, "flynes-catalog");
            thread.setDaemon(true);
            return thread;
        });
        builtin = new AndroidBuiltinCatalogAdapter(this.context);
    }

    public Future<BootstrapResult> bootstrap() {
        return submit(this::bootstrapOnCatalogThread);
    }

    public Future<RomSource> addOrReauthorizeTree(String locator, int resultFlags) {
        return submit(() -> sources.addOrReauthorize(locator, resultFlags));
    }

    public Future<Void> removeSource(String sourceId) {
        return submit(() -> { sources.remove(sourceId); return null; });
    }

    public Future<SourceScanResult> scanSource(String sourceId) {
        return submit(() -> scanSourceOnCatalogThread(sourceId));
    }

    public Future<Boolean> setFavorite(String canonicalId, boolean favorite) {
        return submit(() -> repository.setFavorite(canonicalId, favorite));
    }

    public Future<Boolean> recordSuccessfulLaunch(String canonicalId) {
        return submit(() -> repository.recordSuccessfulLaunch(canonicalId));
    }

    public CatalogState stateSnapshot() { return repository.state(); }
    public GameCatalog gameCatalog() { return catalog; }

    public AndroidCatalogStreamOpener streamOpener() {
        return new AndroidCatalogStreamOpener(
                context, repository, permissions::hasPersistedRead);
    }

    private BootstrapResult bootstrapOnCatalogThread() throws Exception {
        boolean storeExisted = store.baseFile().exists()
                || new File(store.baseFile().getPath() + ".bak").exists();
        CatalogRepository.LoadResult load = repository.load();
        if (load.status() == CatalogRepository.LoadStatus.RECOVERY_NEEDED) {
            return new BootstrapResult(load, null);
        }
        try {
            sources.retryPendingReleases();
        } catch (CatalogRepository.RepositoryException
                | IOException
                | com.flynes.emu.catalog.source.ReadPermissionGateway.PermissionFailure ignored) {
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
                SourceScanResult.Completeness.FULL, scanned, 1));
        return new BootstrapResult(load, migration);
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

    private <T> Future<T> submit(Callable<T> operation) {
        return executor.submit(operation);
    }

    @Override public void close() { executor.shutdownNow(); }

    private static File defaultStateFile(Context context) {
        return new File(new File(context.getApplicationContext().getFilesDir(), "catalog"),
                "catalog-state.bin");
    }

    public record BootstrapResult(
            CatalogRepository.LoadResult loadResult,
            LegacyLibraryMigrator.Result migrationResult) {
    }
}
