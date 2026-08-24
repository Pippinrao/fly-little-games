package com.flynes.emu.catalog.persistence;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;

import org.junit.Test;

import java.io.IOException;
import java.util.Collections;
import java.util.List;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

public final class CatalogRepositoryTest {
    @Test
    public void fullScanReplacesOnlyTargetFatalPreservesAndPackageErrorBecomesStale()
            throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource a = source("a", RomSource.Type.SAF_TREE);
        RomSource b = source("b", RomSource.Type.SAF_TREE);
        CatalogState state = CatalogState.empty(builtin).withSource(a).withSource(b);
        state = CatalogReconciler.reconcile(state, full(state, a, 1, pkg(a, "a1", "ga", 'A')));
        state = CatalogReconciler.reconcile(state, full(state, b, 1, pkg(b, "b1", "gb", 'B')));

        CatalogState replaced = CatalogReconciler.reconcile(
                state, full(state, a, 2, pkg(a, "a2", "ga2", 'C')));
        assertFalse(replaced.sources().get("a").packages().containsKey("a1"));
        assertTrue(replaced.sources().get("a").packages().containsKey("a2"));
        assertTrue(replaced.sources().get("b").packages().containsKey("b1"));

        SourceScanResult fatal = new SourceScanResult(
                "a", replaced.revision(), 3, SourceScanResult.Completeness.FATAL,
                a,
                Collections.emptyList(), Collections.emptyList(), Collections.emptyList(),
                List.of(new ScanIssue(ScanIssue.Code.PERMISSION_REVOKED,
                        ScanIssue.Severity.FATAL, "a", null)), 0);
        CatalogState preserved = CatalogReconciler.reconcile(replaced, fatal);
        assertTrue(preserved.sources().get("a").packages().containsKey("a2"));
        assertTrue(preserved.sources().get("b").packages().containsKey("b1"));
        assertEquals(RomSource.PermissionState.NEEDS_REAUTHORIZE,
                preserved.sources().get("a").source().permissionState());

        CatalogRepository reauthorizer = new CatalogRepository(
                preserved, new MemoryStore(), new GameCatalog());
        reauthorizer.reauthorizeSource(a);
        preserved = reauthorizer.state();

        PackageOutcome error = new PackageOutcome(
                "a2", PackageOutcome.Status.ERROR, PackageOutcome.Reason.IO_ERROR);
        PhysicalPackage changedSibling = pkg(a, "a3", "ga3", 'D');
        PackageOutcome changedOutcome = new PackageOutcome(
                "a3", PackageOutcome.Status.INDEXED, PackageOutcome.Reason.INDEXED);
        SourceScanResult failedPackage = new SourceScanResult(
                "a", preserved.revision(), 4, SourceScanResult.Completeness.FULL,
                a, List.of(changedSibling), List.of(error, changedOutcome),
                Collections.emptyList(), Collections.emptyList(), 2);
        CatalogState stale = CatalogReconciler.reconcile(preserved, failedPackage);
        assertEquals(CatalogPackage.Freshness.PRESERVED_STALE,
                stale.sources().get("a").packages().get("a2").freshness());
        assertTrue(stale.sources().get("a").packages().containsKey("a3"));
        assertFalse(stale.sources().get("a").packages().get("a2")
                .projectedPackage().source().isUsable());
        GameCatalog projectedCatalog = new GameCatalog();
        new CatalogRepository(stale, new MemoryStore(), projectedCatalog);
        assertFalse(projectedCatalog.resolveVariant("v-a2").orElseThrow().isLaunchable());
    }

    @Test
    public void repositoryDoesNotPublishOnStoreFailureAndPersistsUserState() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState initial = CatalogState.empty(builtin);
        MemoryStore store = new MemoryStore();
        GameCatalog catalog = new GameCatalog();
        CatalogRepository repository = new CatalogRepository(initial, store, catalog);
        repository.commitScan(full(initial, builtin, 1,
                pkg(builtin, "built", "game", 'A')));
        assertTrue(repository.setFavorite("game", true));
        assertTrue(repository.recordSuccessfulLaunch("game"));
        assertTrue(catalog.canonicalEntries().get(0).favorite());
        assertEquals(1, catalog.canonicalEntries().get(0).playCount());

        long revision = repository.state().revision();
        store.fail = true;
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> repository.setFavorite("game", false));
        assertEquals(revision, repository.state().revision());
        assertTrue(catalog.canonicalEntries().get(0).favorite());

        CatalogState decoded = CatalogStateCodec.decode(store.bytes);
        assertTrue(decoded.userStates().get("game").favorite());
    }

    @Test
    public void codecIsDeterministicChecksIntegrityAndHandlesLargeCatalog() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState state = CatalogState.empty(builtin);
        java.util.ArrayList<PhysicalPackage> packages = new java.util.ArrayList<>();
        java.util.ArrayList<PackageOutcome> outcomes = new java.util.ArrayList<>();
        for (int index = 0; index < 2401; index++) {
            PhysicalPackage item = index == 0
                    ? zipPkg(builtin, "p0", "g0", '0')
                    : pkg(builtin, "p" + index, "g" + index,
                    "0123456789ABCDEF".charAt(index % 16));
            packages.add(item);
            outcomes.add(new PackageOutcome(item.id(), PackageOutcome.Status.INDEXED,
                    PackageOutcome.Reason.INDEXED));
        }
        state = CatalogReconciler.reconcile(state, new SourceScanResult(
                builtin.id(), state.revision(), 1, SourceScanResult.Completeness.FULL,
                builtin, packages, outcomes,
                List.of(new EntryOutcome("p0", "entry:p0", EntryOutcome.Status.INDEXED,
                        EntryOutcome.Reason.INDEXED)),
                List.of(new ScanIssue(ScanIssue.Code.CASE_COLLISION,
                        ScanIssue.Severity.WARNING, builtin.id(), "p0")), packages.size()));
        assertEquals(2401, state.sources().get("builtin").packages().size());
        byte[] first = CatalogStateCodec.encode(state);
        byte[] second = CatalogStateCodec.encode(state);
        assertArrayEquals(first, second);
        assertEquals(state, CatalogStateCodec.decode(first));
        byte[] corrupt = first.clone();
        corrupt[corrupt.length / 2] ^= 1;
        CatalogStateCodec.CodecException failure = assertThrows(
                CatalogStateCodec.CodecException.class,
                () -> CatalogStateCodec.decode(corrupt));
        assertEquals(CatalogStateCodec.ErrorCode.CHECKSUM_MISMATCH, failure.code());
        assertEquals(CatalogStateCodec.ErrorCode.TRUNCATED, assertThrows(
                CatalogStateCodec.CodecException.class,
                () -> CatalogStateCodec.decode(java.util.Arrays.copyOf(first, 20))).code());
        byte[] unknownVersion = first.clone();
        putInt(unknownVersion, 4, 99);
        resign(unknownVersion);
        assertEquals(CatalogStateCodec.ErrorCode.UNKNOWN_VERSION, assertThrows(
                CatalogStateCodec.CodecException.class,
                () -> CatalogStateCodec.decode(unknownVersion)).code());
        byte[] negativeCount = first.clone();
        int builtinLength = readInt(negativeCount, 16);
        int sourceCountOffset = 28 + builtinLength;
        putInt(negativeCount, sourceCountOffset, -1);
        resign(negativeCount);
        assertEquals(CatalogStateCodec.ErrorCode.BOUNDS, assertThrows(
                CatalogStateCodec.CodecException.class,
                () -> CatalogStateCodec.decode(negativeCount)).code());
        assertTrue(first.length < 16 * 1024 * 1024);
        assertFalse(contains(first, "SYNTHETIC_ROM_PAYLOAD_BYTES"
                .getBytes(java.nio.charset.StandardCharsets.US_ASCII)));
    }

    @Test
    public void fullDeletionStaleRevisionAndBuiltinRemovalAreExplicit() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource removable = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(removable);
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        SourceScanResult first = full(initial, removable, 1,
                pkg(removable, "tree-pkg", "tree-game", 'A'));
        repository.commitScan(first);
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> repository.commitScan(first));
        CatalogState afterFirst = repository.state();
        SourceScanResult staleToken = full(afterFirst, removable, 1,
                pkg(removable, "tree-pkg", "tree-game", 'A'));
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> repository.commitScan(staleToken));

        CatalogState beforeDelete = repository.state();
        repository.commitScan(new SourceScanResult(
                removable.id(), beforeDelete.revision(), 2,
                SourceScanResult.Completeness.FULL, removable,
                Collections.emptyList(), Collections.emptyList(), Collections.emptyList(),
                Collections.emptyList(), 0));
        assertTrue(repository.state().sources().get("tree").packages().isEmpty());
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> repository.removeSource("builtin"));
        repository.removeSource("tree");
        assertFalse(repository.state().sources().containsKey("tree"));
    }

    @Test
    public void canonicalOverrideMigratesUserStateByPayloadAndRestartRestoresIt()
            throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState initial = CatalogState.empty(builtin);
        MemoryStore store = new MemoryStore();
        GameCatalog firstCatalog = new GameCatalog();
        CatalogRepository first = new CatalogRepository(initial, store, firstCatalog);
        first.commitScan(full(initial, builtin, 1,
                pkg(builtin, "same-package", "old-canonical", 'A')));
        first.setFavorite("old-canonical", true);
        first.recordSuccessfulLaunch("old-canonical");

        CatalogState beforeOverride = first.state();
        first.commitScan(full(beforeOverride, builtin, 2,
                pkg(builtin, "same-package", "new-canonical", 'A')));
        assertTrue(firstCatalog.canonicalEntries().get(0).favorite());
        assertEquals(1, firstCatalog.canonicalEntries().get(0).playCount());
        assertEquals("new-canonical",
                firstCatalog.canonicalEntries().get(0).canonicalGame().id());

        GameCatalog restartedCatalog = new GameCatalog();
        CatalogRepository restarted = new CatalogRepository(initial, store, restartedCatalog);
        assertEquals(CatalogRepository.LoadStatus.LOADED, restarted.load().status());
        assertTrue(restartedCatalog.canonicalEntries().get(0).favorite());
        assertEquals(1, restartedCatalog.canonicalEntries().get(0).playCount());

        byte[] corrupt = store.bytes.clone();
        corrupt[12] ^= 1;
        store.bytes = corrupt;
        long revision = restarted.state().revision();
        assertEquals(CatalogRepository.LoadStatus.RECOVERY_NEEDED, restarted.load().status());
        assertEquals(revision, restarted.state().revision());
        assertArrayEquals(corrupt, store.bytes);
    }

    @Test
    public void concurrentScanAndRemovalAreSerializedWithoutResurrectingSource() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource tree = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(tree);
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        SourceScanResult scan = full(initial, tree, 1,
                pkg(tree, "tree-pkg", "tree-game", 'A'));
        CountDownLatch start = new CountDownLatch(1);
        AtomicReference<Throwable> unexpected = new AtomicReference<>();
        Thread scanner = new Thread(() -> {
            try { start.await(); repository.commitScan(scan); }
            catch (CatalogRepository.RepositoryException expected) { /* removal won */ }
            catch (Throwable failure) { unexpected.set(failure); }
        });
        Thread remover = new Thread(() -> {
            try { start.await(); repository.removeSource("tree"); }
            catch (Throwable failure) { unexpected.set(failure); }
        });
        scanner.start(); remover.start(); start.countDown();
        scanner.join(TimeUnit.SECONDS.toMillis(5));
        remover.join(TimeUnit.SECONDS.toMillis(5));
        assertFalse(scanner.isAlive()); assertFalse(remover.isAlive());
        if (unexpected.get() != null) throw new AssertionError(unexpected.get());
        assertFalse(repository.state().sources().containsKey("tree"));
    }

    @Test
    public void partialScanDowngradesUnmentionedPackagesAndRefreshesOnlyIndexedOnes() {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource tree = source("tree", RomSource.Type.SAF_TREE);
        CatalogState state = CatalogState.empty(builtin).withSource(tree);
        PhysicalPackage first = pkg(tree, "first", "g1", 'A');
        PhysicalPackage second = pkg(tree, "second", "g2", 'B');
        state = CatalogReconciler.reconcile(
                state, full(state, tree, 1, first, second));
        SourceScanResult partial = new SourceScanResult(
                tree.id(), state.revision(), 2, SourceScanResult.Completeness.PARTIAL,
                tree, List.of(first), List.of(new PackageOutcome(
                "first", PackageOutcome.Status.INDEXED, PackageOutcome.Reason.INDEXED)),
                Collections.emptyList(), Collections.emptyList(), 1);

        CatalogState reconciled = CatalogReconciler.reconcile(state, partial);
        assertEquals(CatalogPackage.Freshness.FRESH,
                reconciled.sources().get("tree").packages().get("first").freshness());
        CatalogPackage inherited = reconciled.sources().get("tree").packages().get("second");
        assertEquals(CatalogPackage.Freshness.PRESERVED_STALE, inherited.freshness());
        assertFalse(inherited.projectedPackage().source().isUsable());
    }

    @Test
    public void scanSourceIdentityIsExactAndReauthorizeInvalidatesInFlightScan() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource original = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(original);
        RomSource otherUri = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "source://other",
                RomSource.PermissionState.GRANTED);
        assertThrows(IllegalArgumentException.class, () -> new SourceScanResult(
                "tree", initial.revision(), 1, SourceScanResult.Completeness.FULL,
                original, List.of(pkg(otherUri, "bad", "bad", 'A')),
                List.of(new PackageOutcome("bad", PackageOutcome.Status.INDEXED,
                        PackageOutcome.Reason.INDEXED)),
                Collections.emptyList(), Collections.emptyList(), 1));
        assertThrows(CatalogReconciler.ReconcileException.class,
                () -> CatalogReconciler.reconcile(initial, full(
                        initial, otherUri, 1, pkg(otherUri, "bad", "bad", 'A'))));
        RomSource otherType = new RomSource(
                "tree", RomSource.Type.BUILTIN, "source://tree",
                RomSource.PermissionState.NOT_REQUIRED);
        RomSource otherPermission = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "source://tree",
                RomSource.PermissionState.NEEDS_REAUTHORIZE,
                RomSource.Availability.PERMISSION_REQUIRED);
        for (RomSource mismatch : List.of(otherType, otherPermission)) {
            assertThrows(CatalogReconciler.ReconcileException.class,
                    () -> CatalogReconciler.reconcile(initial, full(
                            initial, mismatch, 1,
                            pkg(mismatch, "bad-identity", "bad", 'B'))));
        }

        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        SourceScanResult inFlight = full(
                initial, original, 1, pkg(original, "old", "old", 'A'));
        repository.reauthorizeSource(otherUri);
        CatalogRepository.RepositoryException stale = assertThrows(
                CatalogRepository.RepositoryException.class,
                () -> repository.commitScan(inFlight));
        assertEquals(CatalogRepository.ErrorCode.STALE_OR_INVALID, stale.code());
        assertEquals(otherUri, repository.state().sources().get("tree").source());
    }

    @Test
    public void reauthorizeRetainsPackagesAsStaleUntilNewIdentityScansThem() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource original = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(original);
        GameCatalog catalog = new GameCatalog();
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), catalog);
        repository.commitScan(full(initial, original, 1,
                pkg(original, "old-package", "game", 'A')));
        assertTrue(catalog.resolveVariant("v-old-package").orElseThrow().isLaunchable());

        RomSource reauthorized = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "source://new-tree",
                RomSource.PermissionState.GRANTED);
        repository.reauthorizeSource(reauthorized);
        CatalogPackage retained = repository.state().sources().get("tree")
                .packages().get("old-package");
        assertEquals(CatalogPackage.Freshness.PRESERVED_STALE, retained.freshness());
        assertEquals(original, retained.physicalPackage().source());
        assertFalse(catalog.resolveVariant("v-old-package").orElseThrow().isLaunchable());

        CatalogState stale = repository.state();
        repository.commitScan(full(stale, reauthorized, 2,
                pkg(reauthorized, "old-package", "game", 'A')));
        assertEquals(CatalogPackage.Freshness.FRESH, repository.state().sources().get("tree")
                .packages().get("old-package").freshness());
        assertTrue(catalog.resolveVariant("v-old-package").orElseThrow().isLaunchable());
    }

    @Test
    public void canonicalLineageRoundTripKeepsLatestStateWithoutResurrectionOrDoubleCount()
            throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState initial = CatalogState.empty(builtin);
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        repository.commitScan(full(initial, builtin, 1,
                pkg(builtin, "same", "canonical-a", 'A')));
        repository.setFavorite("canonical-a", true);
        repository.recordSuccessfulLaunch("canonical-a");

        CatalogState a = repository.state();
        repository.commitScan(full(a, builtin, 2,
                pkg(builtin, "same", "canonical-b", 'A')));
        repository.setFavorite("canonical-b", false);
        repository.recordSuccessfulLaunch("canonical-b");

        CatalogState b = repository.state();
        repository.commitScan(full(b, builtin, 3,
                pkg(builtin, "same", "canonical-a", 'A')));
        CanonicalUserState latest = repository.state().userStates().get("canonical-a");
        assertFalse(latest.favorite());
        assertEquals(2, latest.playCount());
        assertEquals(2, latest.lastPlayedSequence());
        assertFalse(repository.state().userStates().containsKey("canonical-b"));
    }

    @Test
    public void canonicalMergeCombinesDistinctLineagesOnceAndUsesLatestFavorite() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState initial = CatalogState.empty(builtin);
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        repository.commitScan(full(initial, builtin, 1,
                pkg(builtin, "one", "canonical-a", 'A'),
                pkg(builtin, "two", "canonical-b", 'B')));
        repository.setFavorite("canonical-a", true);
        repository.recordSuccessfulLaunch("canonical-a");
        repository.setFavorite("canonical-b", false);
        repository.recordSuccessfulLaunch("canonical-b");
        repository.recordSuccessfulLaunch("canonical-b");

        CatalogState split = repository.state();
        repository.commitScan(full(split, builtin, 2,
                pkg(builtin, "one", "merged", 'A'),
                pkg(builtin, "two", "merged", 'B')));
        CanonicalUserState merged = repository.state().userStates().get("merged");
        assertFalse(merged.favorite());
        assertEquals(3, merged.playCount());
        assertEquals(3, merged.lastPlayedSequence());
        assertEquals(1, repository.state().userStates().size());
    }

    @Test
    public void loadRejectsChecksumValidGlobalDuplicateWithoutChangingLiveState() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource tree = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(tree);
        MemoryStore store = new MemoryStore();
        GameCatalog catalog = new GameCatalog();
        CatalogRepository repository = new CatalogRepository(initial, store, catalog);
        repository.commitScan(full(initial, builtin, 1,
                pkg(builtin, "live", "live-game", 'A')));
        CatalogState live = repository.state();

        PhysicalPackage first = pkg(builtin, "duplicate", "first", 'B');
        PhysicalPackage second = pkg(tree, "duplicate", "second", 'C');
        java.util.LinkedHashMap<String, SourceCatalogState> sources =
                new java.util.LinkedHashMap<>();
        sources.put("builtin", sourceState(builtin, first));
        sources.put("tree", sourceState(tree, second));
        CatalogState invalid = new CatalogState(
                CatalogState.CURRENT_SCHEMA, live.revision() + 1, "builtin", sources,
                Collections.emptyMap(), 0);
        store.bytes = CatalogStateCodec.encode(invalid);
        byte[] preserved = store.bytes.clone();

        CatalogRepository.LoadResult result = repository.load();
        assertEquals(CatalogRepository.LoadStatus.RECOVERY_NEEDED, result.status());
        assertEquals(CatalogStateCodec.ErrorCode.INVALID_FIELD, result.recoveryReason());
        assertEquals(live, repository.state());
        assertEquals("live-game", catalog.canonicalEntries().get(0).canonicalGame().id());
        assertArrayEquals(preserved, store.bytes);
    }

    @Test
    public void sourceScanRejectsDuplicatePackageAndEntryOutcomes() {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        CatalogState initial = CatalogState.empty(builtin);
        PackageOutcome packageOutcome = new PackageOutcome(
                "same", PackageOutcome.Status.SKIPPED, PackageOutcome.Reason.UNKNOWN_FORMAT);
        assertThrows(IllegalArgumentException.class, () -> new SourceScanResult(
                builtin.id(), initial.revision(), 1, SourceScanResult.Completeness.FULL,
                builtin, Collections.emptyList(), List.of(packageOutcome, packageOutcome),
                Collections.emptyList(), Collections.emptyList(), 2));
        EntryOutcome entryOutcome = new EntryOutcome(
                "same", "entry", EntryOutcome.Status.SKIPPED,
                EntryOutcome.Reason.UNKNOWN_FORMAT);
        assertThrows(IllegalArgumentException.class, () -> new SourceScanResult(
                builtin.id(), initial.revision(), 1, SourceScanResult.Completeness.FULL,
                builtin, Collections.emptyList(), List.of(packageOutcome),
                List.of(entryOutcome, entryOutcome), Collections.emptyList(), 1));
    }

    @Test
    public void sourceRemovalDropsOrphanedCanonicalUserState() throws Exception {
        RomSource builtin = source("builtin", RomSource.Type.BUILTIN);
        RomSource tree = source("tree", RomSource.Type.SAF_TREE);
        CatalogState initial = CatalogState.empty(builtin).withSource(tree);
        CatalogRepository repository = new CatalogRepository(
                initial, new MemoryStore(), new GameCatalog());
        repository.commitScan(full(initial, tree, 1,
                pkg(tree, "tree-package", "tree-game", 'A')));
        repository.setFavorite("tree-game", true);

        repository.removeSource("tree");

        assertFalse(repository.state().userStates().containsKey("tree-game"));
    }

    private static SourceCatalogState sourceState(
            RomSource source, PhysicalPackage physicalPackage) {
        return new SourceCatalogState(
                source, java.util.Map.of(physicalPackage.id(), new CatalogPackage(
                physicalPackage, CatalogPackage.Freshness.FRESH)),
                List.of(new PackageOutcome(physicalPackage.id(), PackageOutcome.Status.INDEXED,
                        PackageOutcome.Reason.INDEXED)),
                Collections.emptyList(), Collections.emptyList(),
                SourceScanResult.Completeness.FULL, 1);
    }

    private static SourceScanResult full(
            CatalogState state, RomSource source, long token, PhysicalPackage... packages) {
        return new SourceScanResult(
                source.id(), state.revision(), token, SourceScanResult.Completeness.FULL,
                source, List.of(packages),
                java.util.Arrays.stream(packages).map(item -> new PackageOutcome(
                        item.id(), PackageOutcome.Status.INDEXED, PackageOutcome.Reason.INDEXED))
                        .toList(),
                Collections.emptyList(), Collections.emptyList(), packages.length);
    }

    private static RomSource source(String id, RomSource.Type type) {
        return new RomSource(id, type, "source://" + id,
                type == RomSource.Type.BUILTIN
                        ? RomSource.PermissionState.NOT_REQUIRED
                        : RomSource.PermissionState.GRANTED);
    }

    private static PhysicalPackage pkg(
            RomSource source, String packageId, String gameId, char hashDigit) {
        String sha1 = String.valueOf(hashDigit).repeat(40);
        String sha256 = String.valueOf(hashDigit).repeat(64);
        RomHashes hashes = new RomHashes(sha1, sha256, sha256, "1234ABCD");
        CanonicalGame game = new CanonicalGame(gameId, "Game " + gameId, "", List.of());
        RomVariant variant = new RomVariant(
                "v-" + packageId, game, null, RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes,
                RomAnalysis.basic(16_400), null, null);
        return new PhysicalPackage(
                packageId, source, "memory://" + packageId, packageId + ".nes",
                PackageFormat.RAW, sha256, List.of(variant));
    }

    private static PhysicalPackage zipPkg(
            RomSource source, String packageId, String gameId, char hashDigit) {
        String sha1 = String.valueOf(hashDigit).repeat(40);
        String sha256 = String.valueOf(hashDigit).repeat(64);
        RomHashes hashes = new RomHashes(sha1, sha256, sha256, "1234ABCD");
        CanonicalGame game = new CanonicalGame(gameId, "Zip Game", "压缩游戏", List.of("Alias"));
        ZipEntryIdentity identity = ZipEntryIdentity.fromRawName(
                "目录/game.nes".getBytes(java.nio.charset.StandardCharsets.UTF_8), 42);
        RomVariant variant = new RomVariant(
                "v-" + packageId, game, "目录/game.nes", RomFormat.NES2,
                CompatibilityDecision.playableNes(), hashes,
                new RomAnalysis(16_400, 16_410, 16_384, 0, 4, 1,
                        true, true, 0, List.of(RomAnalysis.Warning.TRAILING_DATA)),
                identity, ZipNameEncoding.GB18030);
        return new PhysicalPackage(
                packageId, source, "memory://zip-token", "游戏.zip",
                PackageFormat.ZIP, sha256, List.of(variant));
    }

    private static final class MemoryStore implements CatalogStateStore {
        byte[] bytes;
        boolean fail;

        @Override
        public byte[] read() {
            return bytes == null ? null : bytes.clone();
        }

        @Override
        public void writeAtomically(byte[] encoded) throws IOException {
            if (fail) throw new IOException("fault");
            bytes = encoded.clone();
        }
    }

    private static int readInt(byte[] value, int offset) {
        return (value[offset] & 0xff) << 24 | (value[offset + 1] & 0xff) << 16
                | (value[offset + 2] & 0xff) << 8 | value[offset + 3] & 0xff;
    }

    private static void putInt(byte[] value, int offset, int item) {
        value[offset] = (byte) (item >>> 24); value[offset + 1] = (byte) (item >>> 16);
        value[offset + 2] = (byte) (item >>> 8); value[offset + 3] = (byte) item;
    }

    private static void resign(byte[] encoded) throws Exception {
        int bodyLength = encoded.length - 32;
        byte[] digest = java.security.MessageDigest.getInstance("SHA-256")
                .digest(java.util.Arrays.copyOf(encoded, bodyLength));
        System.arraycopy(digest, 0, encoded, bodyLength, digest.length);
    }

    private static boolean contains(byte[] haystack, byte[] needle) {
        for (int start = 0; start <= haystack.length - needle.length; start++) {
            int index = 0;
            while (index < needle.length && haystack[start + index] == needle[index]) index++;
            if (index == needle.length) return true;
        }
        return false;
    }
}
