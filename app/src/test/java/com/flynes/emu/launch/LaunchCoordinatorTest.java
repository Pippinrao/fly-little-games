package com.flynes.emu.launch;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;
import com.flynes.emu.data.RomIdentity;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class LaunchCoordinatorTest {
    @Test
    public void successfulGatewayCommitRecordsHistoryAndCatalogExactlyOnce() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        List<String> openedLocation = new ArrayList<>();
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            openedLocation.add(sourceId);
            openedLocation.add(sourceUri);
            return new ByteArrayInputStream(rom);
        });
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);

        LaunchResult result = coordinator.launch("variant-a");

        assertTrue(result.isSuccess());
        assertEquals(LaunchResult.Code.SUCCESS, result.code());
        assertEquals(1, history.requests.size());
        assertEquals("variant-a", history.requests.get(0).variantId());
        assertEquals(List.of("builtin", "asset:///roms/game.nes"), openedLocation);
        assertNotNull(gateway.request);
        assertEquals("builtin", gateway.request.sourceId());
        assertEquals("asset:///roms/game.nes", gateway.request.sourceUri());
        assertEquals(gateway.request, history.requests.get(0));
        assertArrayEquals(rom, gateway.bytes);
        GameCatalogEntry entry = catalog.canonicalEntries().get(0);
        assertEquals(1, entry.playCount());
        assertTrue(entry.isRecent());
        assertFalse(entry.favorite());
    }

    @Test
    public void hashFailurePreservesHistoryCatalogAndSession() {
        byte[] actual = bytes("actual-rom");
        GameCatalog catalog = catalogFor(identity(bytes("different-rom")), CompatibilityState.PLAYABLE);
        catalog.setFavorite("game-a", true);
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = coordinator(catalog, actual, gateway, history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.HASH_MISMATCH, result.code());
        assertTrue(history.requests.isEmpty());
        assertEquals(0, gateway.calls);
        GameCatalogEntry entry = catalog.canonicalEntries().get(0);
        assertEquals(0, entry.playCount());
        assertFalse(entry.isRecent());
        assertTrue(entry.favorite());
    }

    @Test
    public void sessionFailurePreservesHistoryAndCatalog() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(true);
        LaunchCoordinator coordinator = coordinator(catalog, rom, gateway, history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.SESSION_FAILED, result.code());
        assertTrue(history.requests.isEmpty());
        assertEquals(0, catalog.canonicalEntries().get(0).playCount());
        assertFalse(catalog.canonicalEntries().get(0).isRecent());
    }

    @Test
    public void uncheckedGatewayFailureReturnsTypedSessionFailure() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingHistory history = new RecordingHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                (request, bytes) -> {
                    throw new IllegalStateException("unexpected gateway failure");
                },
                history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.SESSION_FAILED, result.code());
        assertFalse(result.sessionCommitted());
        assertTrue(history.requests.isEmpty());
        assertEquals(0, catalog.canonicalEntries().get(0).playCount());
    }

    @Test
    public void unresolvedAndNonPlayableVariantsReturnTypedFailuresWithoutOpening() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.UNSUPPORTED);
        int[] opens = {0};
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            opens[0]++;
            return new ByteArrayInputStream(rom);
        });
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);

        assertEquals(LaunchResult.Code.VARIANT_NOT_FOUND, coordinator.launch("missing").code());
        assertEquals(LaunchResult.Code.NOT_PLAYABLE, coordinator.launch("variant-a").code());
        assertEquals(0, opens[0]);
        assertTrue(history.requests.isEmpty());
        assertEquals(0, gateway.calls);
    }

    @Test
    public void scanReusingIdsForDifferentIdentityRejectsStaleLaunchBeforeSessionCommit()
            throws Exception {
        byte[] oldRom = bytes("old-rom");
        byte[] replacementRom = bytes("replacement-rom");
        GameCatalog catalog = catalogFor(identity(oldRom), CompatibilityState.PLAYABLE);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(oldRom);
        });
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            replaceCatalog(
                    catalog,
                    "game-a",
                    "variant-a",
                    identity(replacementRom),
                    "asset:///roms/replacement.nes");
            continueLoad.countDown();

            LaunchResult result = launch.get(5, TimeUnit.SECONDS);

            assertEquals(LaunchResult.Code.CATALOG_CHANGED, result.code());
            assertEquals(0, gateway.calls);
            assertTrue(history.requests.isEmpty());
            GameCatalogEntry replacement = catalog.canonicalEntries().get(0);
            assertEquals(identity(replacementRom), replacement.canonicalGame().identity());
            assertEquals(0, replacement.playCount());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void scanRenamingSameIdentityCreditsCurrentCanonicalEntry() throws Exception {
        byte[] rom = bytes("same-rom");
        RomIdentity identity = identity(rom);
        GameCatalog catalog = catalogFor(identity, CompatibilityState.PLAYABLE);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(rom);
        });
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            replaceCatalog(
                    catalog,
                    "game-renamed",
                    "variant-a",
                    identity,
                    "asset:///roms/game.nes");
            continueLoad.countDown();

            LaunchResult result = launch.get(5, TimeUnit.SECONDS);

            assertTrue(result.isSuccess());
            assertEquals(1, gateway.calls);
            assertEquals("variant-a", gateway.request.variantId());
            assertEquals("game-renamed", gateway.request.canonicalGameId());
            assertEquals("asset:///roms/game.nes", gateway.request.sourceUri());
            assertEquals(1, history.requests.size());
            assertEquals(gateway.request, history.requests.get(0));
            assertEquals(gateway.request, result.request().orElseThrow());
            GameCatalogEntry renamed = catalog.canonicalEntries().get(0);
            assertEquals("game-renamed", renamed.canonicalGame().id());
            assertEquals(1, renamed.playCount());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void movedSourceUriRejectsStaleLaunchBeforeSessionCommit() throws Exception {
        byte[] rom = bytes("same-rom");
        RomIdentity identity = identity(rom);
        GameCatalog catalog = catalogFor(identity, CompatibilityState.PLAYABLE);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(rom);
        });
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            replaceCatalog(
                    catalog,
                    "game-a",
                    "variant-a",
                    identity,
                    "asset:///roms/moved.nes");
            continueLoad.countDown();

            LaunchResult result = launch.get(5, TimeUnit.SECONDS);

            assertEquals(LaunchResult.Code.CATALOG_CHANGED, result.code());
            assertEquals(0, gateway.calls);
            assertTrue(history.requests.isEmpty());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void removedVariantRejectsStaleLaunchBeforeSessionCommit() throws Exception {
        byte[] rom = bytes("same-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(rom);
        });
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            catalog.applyScanResult(ScanResult.success(List.of(), List.of()));
            continueLoad.countDown();

            LaunchResult result = launch.get(5, TimeUnit.SECONDS);

            assertEquals(LaunchResult.Code.CATALOG_CHANGED, result.code());
            assertEquals(0, gateway.calls);
            assertTrue(history.requests.isEmpty());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void revokedPermissionAndIncompatibleReplacementRejectStaleLaunch()
            throws Exception {
        assertStaleMetadataRejected(
                CompatibilityState.PLAYABLE,
                RomSource.PermissionState.NEEDS_REAUTHORIZE);
        assertStaleMetadataRejected(
                CompatibilityState.UNSUPPORTED,
                RomSource.PermissionState.GRANTED);
    }

    @Test
    public void initiallyUnavailableSourceDoesNotOpenOrLaunch() {
        byte[] rom = bytes("same-rom");
        GameCatalog catalog = catalogForSaf(
                identity(rom),
                CompatibilityState.PLAYABLE,
                RomSource.PermissionState.NEEDS_REAUTHORIZE);
        AtomicInteger opens = new AtomicInteger();
        RecordingGateway gateway = new RecordingGateway(false);
        RecordingHistory history = new RecordingHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader((sourceId, sourceUri) -> {
                    opens.incrementAndGet();
                    return new ByteArrayInputStream(rom);
                }),
                gateway,
                history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.NOT_PLAYABLE, result.code());
        assertEquals(0, opens.get());
        assertEquals(0, gateway.calls);
        assertTrue(history.requests.isEmpty());
    }

    @Test
    public void changedZipOffsetRejectsLoadedVariantAsStale() throws Exception {
        byte[] rom = bytes("zip-rom");
        byte[] archive = zip("game.nes", rom);
        ZipEntryIdentity locator = ZipEntryIdentity.fromRawName(bytes("game.nes"), 0);
        GameCatalog catalog = zipCatalog(identity(rom), locator);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(archive);
        });
        RecordingGateway gateway = new RecordingGateway(false);
        RecordingHistory history = new RecordingHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            replaceZipCatalog(
                    catalog,
                    "game-a",
                    ZipEntryIdentity.fromRawName(bytes("game.nes"), 1),
                    identity(rom));
            continueLoad.countDown();

            assertEquals(
                    LaunchResult.Code.CATALOG_CHANGED,
                    launch.get(5, TimeUnit.SECONDS).code());
            assertEquals(0, gateway.calls);
            assertTrue(history.requests.isEmpty());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void coordinatorPropagatesExactZipLocatorAndEncoding() throws Exception {
        byte[] rom = bytes("zip-rom");
        byte[] archive = zip("game.nes", rom);
        ZipEntryIdentity locator = ZipEntryIdentity.fromRawName(bytes("game.nes"), 0);
        GameCatalog catalog = zipCatalog(identity(rom), locator);
        RecordingGateway gateway = new RecordingGateway(false);
        RecordingHistory history = new RecordingHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(archive)),
                gateway,
                history);

        LaunchResult result = coordinator.launch("variant-a");

        assertTrue(result.isSuccess());
        LaunchRequest request = result.request().orElseThrow();
        assertEquals(locator, request.zipEntryIdentity());
        assertEquals(ZipNameEncoding.UTF8_EFS, request.zipNameEncoding());
        assertEquals(request, gateway.request);
        assertEquals(List.of(request), history.requests);
    }

    @Test
    public void concurrentLaunchesLoadTogetherButCommitSessionCatalogAndHistorySerially()
            throws Exception {
        byte[] romA = bytes("rom-a");
        byte[] romB = bytes("rom-b");
        GameCatalog catalog = catalogWithTwoGames(romA, romB);
        CountDownLatch bothLoaded = new CountDownLatch(2);
        CountDownLatch releaseLoads = new CountDownLatch(1);
        Map<String, byte[]> payloads = Map.of(
                "asset:///roms/a.nes", romA,
                "asset:///roms/b.nes", romB);
        Map<String, Thread> loadThreads = Collections.synchronizedMap(new HashMap<>());
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loadThreads.put(sourceUri, Thread.currentThread());
            bothLoaded.countDown();
            awaitForTest(releaseLoads);
            return new ByteArrayInputStream(payloads.get(sourceUri));
        });
        RecordingHistory history = new RecordingHistory();
        BlockingOrderGateway gateway = new BlockingOrderGateway();
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newFixedThreadPool(2);
        try {
            Future<LaunchResult> first = executor.submit(() -> coordinator.launch("variant-a"));
            Future<LaunchResult> second = executor.submit(() -> coordinator.launch("variant-b"));
            assertTrue(bothLoaded.await(5, TimeUnit.SECONDS));
            releaseLoads.countDown();
            assertTrue(gateway.firstEntered.await(5, TimeUnit.SECONDS));

            assertEquals(0, totalPlayCount(catalog));
            assertTrue(history.requests.isEmpty());
            String firstVariant = gateway.variantOrder.get(0);
            String waitingSource = firstVariant.equals("variant-a")
                    ? "asset:///roms/b.nes"
                    : "asset:///roms/a.nes";
            assertTrue(awaitBlockedBefore(
                    loadThreads.get(waitingSource), gateway.secondEntered));
            gateway.releaseFirst.countDown();

            assertTrue(first.get(5, TimeUnit.SECONDS).isSuccess());
            assertTrue(second.get(5, TimeUnit.SECONDS).isSuccess());
            assertEquals(1, gateway.maxConcurrentCalls.get());
            assertEquals(2, totalPlayCount(catalog));
            assertEquals(gateway.variantOrder, history.requests.stream()
                    .map(LaunchRequest::variantId)
                    .collect(java.util.stream.Collectors.toList()));
            assertEquals(
                    gateway.variantOrder.get(gateway.variantOrder.size() - 1),
                    gateway.activeVariantId);
        } finally {
            releaseLoads.countDown();
            gateway.releaseFirst.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void coordinatorsSharingCatalogSerializeHistoryBeforeNextSessionCommit()
            throws Exception {
        byte[] romA = bytes("rom-a");
        byte[] romB = bytes("rom-b");
        GameCatalog catalog = catalogWithTwoGames(romA, romB);
        BlockingFirstHistory history = new BlockingFirstHistory();
        RecordingGateway firstGateway = new RecordingGateway(false);
        CountDownLatch secondGatewayEntered = new CountDownLatch(1);
        CountDownLatch secondLoadEntered = new CountDownLatch(1);
        Thread[] secondLaunchThread = new Thread[1];
        RomSessionGateway secondGateway = (request, bytes) -> secondGatewayEntered.countDown();
        LaunchCoordinator firstCoordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(romA)),
                firstGateway,
                history);
        LaunchCoordinator secondCoordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader((sourceId, sourceUri) -> {
                    secondLaunchThread[0] = Thread.currentThread();
                    secondLoadEntered.countDown();
                    return new ByteArrayInputStream(romB);
                }),
                secondGateway,
                history);
        ExecutorService executor = Executors.newFixedThreadPool(2);
        try {
            Future<LaunchResult> first = executor.submit(
                    () -> firstCoordinator.launch("variant-a"));
            assertTrue(history.firstEntered.await(5, TimeUnit.SECONDS));

            Future<LaunchResult> second = executor.submit(
                    () -> secondCoordinator.launch("variant-b"));

            assertTrue(secondLoadEntered.await(5, TimeUnit.SECONDS));
            assertTrue(awaitBlockedBefore(secondLaunchThread[0], secondGatewayEntered));
            assertEquals(1, totalPlayCount(catalog));
            history.releaseFirst.countDown();

            assertTrue(first.get(5, TimeUnit.SECONDS).isSuccess());
            assertTrue(second.get(5, TimeUnit.SECONDS).isSuccess());
            assertTrue(secondGatewayEntered.await(5, TimeUnit.SECONDS));
            assertEquals(List.of("variant-a", "variant-b"), history.variantOrder);
            assertEquals(2, totalPlayCount(catalog));
        } finally {
            history.releaseFirst.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void gatewayRunsWithoutHoldingPublicCatalogMonitor() throws Exception {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        CountDownLatch catalogWriteCompleted = new CountDownLatch(1);
        boolean[] favoriteRecorded = {false};
        RomSessionGateway gateway = (request, bytes) -> {
            Thread catalogWriter = new Thread(() -> {
                favoriteRecorded[0] = catalog.setFavorite("game-a", true);
                catalogWriteCompleted.countDown();
            });
            catalogWriter.start();
            try {
                if (!catalogWriteCompleted.await(1, TimeUnit.SECONDS)) {
                    throw new RomSessionGateway.SessionException(
                            "gateway observed the public catalog monitor held");
                }
                catalogWriter.join(1000);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new RomSessionGateway.SessionException("interrupted", interrupted);
            }
        };
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                gateway,
                new RecordingHistory());

        LaunchResult result = coordinator.launch("variant-a");

        assertTrue(result.isSuccess());
        assertTrue(favoriteRecorded[0]);
        assertTrue(catalog.canonicalEntries().get(0).favorite());
    }

    @Test
    public void scanPublicationWaitsForGatewayAndCatalogCommit() throws Exception {
        byte[] rom = bytes("valid-rom");
        RomIdentity identity = identity(rom);
        GameCatalog catalog = catalogFor(identity, CompatibilityState.PLAYABLE);
        BlockingOrderGateway gateway = new BlockingOrderGateway();
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                gateway,
                new RecordingHistory());
        CountDownLatch scanStarted = new CountDownLatch(1);
        CountDownLatch scanFinished = new CountDownLatch(1);
        ExecutorService executor = Executors.newFixedThreadPool(2);
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(gateway.firstEntered.await(5, TimeUnit.SECONDS));
            Future<?> scan = executor.submit(() -> {
                scanStarted.countDown();
                replaceCatalog(
                        catalog,
                        "game-after",
                        "variant-a",
                        identity,
                        "asset:///roms/game.nes");
                scanFinished.countDown();
            });
            assertTrue(scanStarted.await(5, TimeUnit.SECONDS));
            assertFalse(scanFinished.await(200, TimeUnit.MILLISECONDS));

            gateway.releaseFirst.countDown();

            assertTrue(launch.get(5, TimeUnit.SECONDS).isSuccess());
            scan.get(5, TimeUnit.SECONDS);
            assertEquals("game-after",
                    catalog.canonicalEntries().get(0).canonicalGame().id());
            assertEquals(1, catalog.canonicalEntries().get(0).playCount());
        } finally {
            gateway.releaseFirst.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void slowHistoryDoesNotBlockCatalogReadFavoriteOrScan() throws Exception {
        byte[] rom = bytes("valid-rom");
        RomIdentity identity = identity(rom);
        GameCatalog catalog = catalogFor(identity, CompatibilityState.PLAYABLE);
        BlockingFirstHistory history = new BlockingFirstHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader(
                        (sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                new RecordingGateway(false),
                history);
        ExecutorService executor = Executors.newFixedThreadPool(4);
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(history.firstEntered.await(5, TimeUnit.SECONDS));

            Future<Boolean> favorite = executor.submit(
                    () -> catalog.setFavorite("game-a", true));
            Future<Integer> search = executor.submit(() -> catalog.search("Game").size());
            Future<Boolean> scan = executor.submit(() -> {
                replaceCatalog(
                        catalog,
                        "game-a",
                        "variant-a",
                        identity,
                        "asset:///roms/game.nes");
                return true;
            });

            assertTrue(favorite.get(1, TimeUnit.SECONDS));
            assertEquals(Integer.valueOf(1), search.get(1, TimeUnit.SECONDS));
            assertTrue(scan.get(1, TimeUnit.SECONDS));
            history.releaseFirst.countDown();
            assertTrue(launch.get(5, TimeUnit.SECONDS).isSuccess());
            assertEquals(1, catalog.canonicalEntries().get(0).playCount());
        } finally {
            history.releaseFirst.countDown();
            executor.shutdownNow();
        }
    }

    @Test
    public void checkedHistoryFailureReturnsTypedPostCommitResult() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchHistory history = request -> {
            throw new LaunchHistory.HistoryException("history store unavailable");
        };
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader((sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                gateway,
                history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.HISTORY_FAILED, result.code());
        assertTrue(result.sessionCommitted());
        assertEquals(1, gateway.calls);
        assertEquals(1, catalog.canonicalEntries().get(0).playCount());
    }

    @Test
    public void uncheckedHistoryFailureDoesNotEscapeCoordinator() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchHistory history = request -> {
            throw new IllegalStateException("unexpected history failure");
        };
        LaunchCoordinator coordinator = new LaunchCoordinator(
                catalog,
                new ExactRomLoader((sourceId, sourceUri) -> new ByteArrayInputStream(rom)),
                gateway,
                history);

        LaunchResult result = coordinator.launch("variant-a");

        assertEquals(LaunchResult.Code.HISTORY_FAILED, result.code());
        assertTrue(result.sessionCommitted());
        assertEquals(1, catalog.canonicalEntries().get(0).playCount());
    }

    private static LaunchCoordinator coordinator(
            GameCatalog catalog,
            byte[] rom,
            RecordingGateway gateway,
            RecordingHistory history) {
        ExactRomLoader loader = new ExactRomLoader(
                (sourceId, sourceUri) -> new ByteArrayInputStream(rom));
        return new LaunchCoordinator(catalog, loader, gateway, history);
    }

    private static GameCatalog catalogFor(
            RomIdentity identity,
            CompatibilityState compatibility) {
        GameCatalog catalog = new GameCatalog();
        replaceCatalog(
                catalog,
                "game-a",
                "variant-a",
                identity,
                "asset:///roms/game.nes",
                compatibility);
        return catalog;
    }

    private static GameCatalog catalogForSaf(
            RomIdentity identity,
            CompatibilityState compatibility,
            RomSource.PermissionState permissionState) {
        GameCatalog catalog = new GameCatalog();
        replaceCatalog(
                catalog,
                "game-a",
                "variant-a",
                identity,
                "content://tree/roms/game.nes",
                compatibility,
                safSource(permissionState));
        return catalog;
    }

    private static GameCatalog zipCatalog(
            RomIdentity identity, ZipEntryIdentity locator) {
        GameCatalog catalog = new GameCatalog();
        replaceZipCatalog(catalog, "game-a", locator, identity);
        return catalog;
    }

    private static void replaceZipCatalog(
            GameCatalog catalog,
            String canonicalGameId,
            ZipEntryIdentity locator,
            RomIdentity identity) {
        RomSource source = new RomSource(
                "tree",
                RomSource.Type.SAF_TREE,
                "content://tree/roms",
                RomSource.PermissionState.GRANTED);
        CanonicalGame canonical = new CanonicalGame(
                canonicalGameId, identity, "Game", "游戏", List.of());
        RomVariant variant = new RomVariant(
                "variant-a",
                canonical,
                "game.nes",
                RomFormat.INES,
                CompatibilityState.PLAYABLE,
                locator,
                ZipNameEncoding.UTF8_EFS);
        PhysicalPackage physicalPackage = new PhysicalPackage(
                "package-variant-a",
                source,
                "content://tree/roms/games.zip",
                "games.zip",
                PackageFormat.ZIP,
                List.of(variant));
        catalog.applyScanResult(ScanResult.success(List.of(physicalPackage), List.of()));
    }

    private static void assertStaleMetadataRejected(
            CompatibilityState replacementCompatibility,
            RomSource.PermissionState replacementPermission) throws Exception {
        byte[] rom = bytes("same-rom");
        RomIdentity identity = identity(rom);
        GameCatalog catalog = catalogForSaf(
                identity,
                CompatibilityState.PLAYABLE,
                RomSource.PermissionState.GRANTED);
        CountDownLatch loaderEntered = new CountDownLatch(1);
        CountDownLatch continueLoad = new CountDownLatch(1);
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            loaderEntered.countDown();
            awaitForTest(continueLoad);
            return new ByteArrayInputStream(rom);
        });
        RecordingGateway gateway = new RecordingGateway(false);
        RecordingHistory history = new RecordingHistory();
        LaunchCoordinator coordinator = new LaunchCoordinator(catalog, loader, gateway, history);
        ExecutorService executor = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> launch = executor.submit(() -> coordinator.launch("variant-a"));
            assertTrue(loaderEntered.await(5, TimeUnit.SECONDS));
            replaceCatalog(
                    catalog,
                    "game-a",
                    "variant-a",
                    identity,
                    "content://tree/roms/game.nes",
                    replacementCompatibility,
                    safSource(replacementPermission));
            continueLoad.countDown();

            LaunchResult result = launch.get(5, TimeUnit.SECONDS);

            assertEquals(LaunchResult.Code.CATALOG_CHANGED, result.code());
            assertEquals(0, gateway.calls);
            assertTrue(history.requests.isEmpty());
        } finally {
            continueLoad.countDown();
            executor.shutdownNow();
        }
    }

    private static void replaceCatalog(
            GameCatalog catalog,
            String canonicalGameId,
            String variantId,
            RomIdentity identity,
            String sourceUri) {
        replaceCatalog(
                catalog,
                canonicalGameId,
                variantId,
                identity,
                sourceUri,
                CompatibilityState.PLAYABLE);
    }

    private static void replaceCatalog(
            GameCatalog catalog,
            String canonicalGameId,
            String variantId,
            RomIdentity identity,
            String sourceUri,
            CompatibilityState compatibility) {
        replaceCatalog(
                catalog,
                canonicalGameId,
                variantId,
                identity,
                sourceUri,
                compatibility,
                new RomSource(
                        "builtin",
                        RomSource.Type.BUILTIN,
                        "asset:///roms",
                        RomSource.PermissionState.NOT_REQUIRED));
    }

    private static void replaceCatalog(
            GameCatalog catalog,
            String canonicalGameId,
            String variantId,
            RomIdentity identity,
            String sourceUri,
            CompatibilityState compatibility,
            RomSource source) {
        CanonicalGame canonical = new CanonicalGame(
                canonicalGameId, identity, "Game", "游戏", List.of("Alias"));
        RomVariant variant = new RomVariant(
                variantId, canonical, null, RomFormat.INES, compatibility);
        PhysicalPackage physicalPackage = new PhysicalPackage(
                "package-" + variantId, source, sourceUri, variantId + ".nes",
                PackageFormat.RAW_NES, List.of(variant));
        catalog.applyScanResult(ScanResult.success(List.of(physicalPackage), List.of()));
    }

    private static RomSource safSource(RomSource.PermissionState permissionState) {
        return new RomSource(
                "tree",
                RomSource.Type.SAF_TREE,
                "content://tree/roms",
                permissionState);
    }

    private static GameCatalog catalogWithTwoGames(byte[] romA, byte[] romB) {
        RomSource source = new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms",
                RomSource.PermissionState.NOT_REQUIRED);
        CanonicalGame gameA = new CanonicalGame(
                "game-a", identity(romA), "A", "甲", List.of());
        CanonicalGame gameB = new CanonicalGame(
                "game-b", identity(romB), "B", "乙", List.of());
        PhysicalPackage packageA = new PhysicalPackage(
                "package-a", source, "asset:///roms/a.nes", "a.nes",
                PackageFormat.RAW_NES,
                List.of(new RomVariant(
                        "variant-a", gameA, null, RomFormat.INES,
                        CompatibilityState.PLAYABLE)));
        PhysicalPackage packageB = new PhysicalPackage(
                "package-b", source, "asset:///roms/b.nes", "b.nes",
                PackageFormat.RAW_NES,
                List.of(new RomVariant(
                        "variant-b", gameB, null, RomFormat.INES,
                        CompatibilityState.PLAYABLE)));
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(packageA, packageB), List.of()));
        return catalog;
    }

    private static int totalPlayCount(GameCatalog catalog) {
        int total = 0;
        for (GameCatalogEntry entry : catalog.canonicalEntries()) {
            total += entry.playCount();
        }
        return total;
    }

    private static void awaitForTest(CountDownLatch latch) throws java.io.IOException {
        try {
            if (!latch.await(5, TimeUnit.SECONDS)) {
                throw new java.io.IOException("timed out waiting for test latch");
            }
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new java.io.IOException("interrupted waiting for test latch", interrupted);
        }
    }

    private static boolean awaitBlockedBefore(Thread thread, CountDownLatch competingAction)
            throws InterruptedException {
        long deadlineNanos = System.nanoTime() + TimeUnit.SECONDS.toNanos(5);
        while (System.nanoTime() < deadlineNanos) {
            if (competingAction.getCount() == 0) {
                return false;
            }
            if (thread.getState() == Thread.State.BLOCKED) {
                return true;
            }
            Thread.sleep(1);
        }
        return false;
    }

    private static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }

    private static byte[] zip(String entryName, byte[] payload) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry(entryName));
            zip.write(payload);
            zip.closeEntry();
        }
        return bytes.toByteArray();
    }

    private static RomIdentity identity(byte[] bytes) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-1").digest(bytes);
            StringBuilder sha1 = new StringBuilder(40);
            for (byte value : digest) {
                sha1.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return new RomIdentity(sha1.toString());
        } catch (NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
    }

    private static final class RecordingHistory implements LaunchHistory {
        final List<LaunchRequest> requests = Collections.synchronizedList(new ArrayList<>());

        @Override
        public void recordSuccessfulLaunch(LaunchRequest request) {
            requests.add(request);
        }
    }

    private static final class BlockingFirstHistory implements LaunchHistory {
        final CountDownLatch firstEntered = new CountDownLatch(1);
        final CountDownLatch releaseFirst = new CountDownLatch(1);
        final List<String> variantOrder = Collections.synchronizedList(new ArrayList<>());
        private final AtomicInteger calls = new AtomicInteger();

        @Override
        public void recordSuccessfulLaunch(LaunchRequest request) throws HistoryException {
            int call = calls.incrementAndGet();
            variantOrder.add(request.variantId());
            if (call == 1) {
                firstEntered.countDown();
                try {
                    if (!releaseFirst.await(5, TimeUnit.SECONDS)) {
                        throw new HistoryException("timed out waiting for history latch");
                    }
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new HistoryException(
                            "interrupted waiting for history latch", interrupted);
                }
            }
        }
    }

    private static final class BlockingOrderGateway implements RomSessionGateway {
        final CountDownLatch firstEntered = new CountDownLatch(1);
        final CountDownLatch secondEntered = new CountDownLatch(1);
        final CountDownLatch releaseFirst = new CountDownLatch(1);
        final AtomicInteger calls = new AtomicInteger();
        final AtomicInteger concurrentCalls = new AtomicInteger();
        final AtomicInteger maxConcurrentCalls = new AtomicInteger();
        final List<String> variantOrder = Collections.synchronizedList(new ArrayList<>());
        volatile String activeVariantId;

        @Override
        public void stageAndReplace(LaunchRequest request, byte[] bytes) throws SessionException {
            int concurrent = concurrentCalls.incrementAndGet();
            maxConcurrentCalls.accumulateAndGet(concurrent, Math::max);
            int call = calls.incrementAndGet();
            variantOrder.add(request.variantId());
            if (call == 1) {
                firstEntered.countDown();
                awaitGateway(releaseFirst);
            } else {
                secondEntered.countDown();
            }
            activeVariantId = request.variantId();
            concurrentCalls.decrementAndGet();
        }

        private static void awaitGateway(CountDownLatch latch) throws SessionException {
            try {
                if (!latch.await(5, TimeUnit.SECONDS)) {
                    throw new SessionException("timed out waiting for gateway latch");
                }
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                throw new SessionException("interrupted waiting for gateway latch", interrupted);
            }
        }
    }

    private static final class RecordingGateway implements RomSessionGateway {
        private final boolean fail;
        int calls;
        LaunchRequest request;
        byte[] bytes;

        RecordingGateway(boolean fail) {
            this.fail = fail;
        }

        @Override
        public void stageAndReplace(LaunchRequest request, byte[] bytes) throws SessionException {
            calls++;
            if (fail) {
                throw new SessionException("core rejected ROM");
            }
            this.request = request;
            this.bytes = bytes.clone();
        }
    }
}
