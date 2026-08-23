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
import com.flynes.emu.data.RomIdentity;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;

public final class LaunchCoordinatorTest {
    @Test
    public void successfulGatewayCommitRecordsHistoryAndCatalogExactlyOnce() {
        byte[] rom = bytes("valid-rom");
        GameCatalog catalog = catalogFor(identity(rom), CompatibilityState.PLAYABLE);
        RecordingHistory history = new RecordingHistory();
        RecordingGateway gateway = new RecordingGateway(false);
        LaunchCoordinator coordinator = coordinator(catalog, rom, gateway, history);

        LaunchResult result = coordinator.launch("variant-a");

        assertTrue(result.isSuccess());
        assertEquals(LaunchResult.Code.SUCCESS, result.code());
        assertEquals(1, history.requests.size());
        assertEquals("variant-a", history.requests.get(0).variantId());
        assertNotNull(gateway.request);
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
        RomSource source = new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms",
                RomSource.PermissionState.NOT_REQUIRED);
        CanonicalGame canonical = new CanonicalGame(
                "game-a", identity, "Game", "游戏", List.of("Alias"));
        RomVariant variant = new RomVariant(
                "variant-a", canonical, null, RomFormat.INES, compatibility);
        PhysicalPackage physicalPackage = new PhysicalPackage(
                "package-a", source, "asset:///roms/game.nes", "game.nes",
                PackageFormat.RAW_NES, List.of(variant));
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(physicalPackage), List.of()));
        return catalog;
    }

    private static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
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
        final List<LaunchRequest> requests = new ArrayList<>();

        @Override
        public void recordSuccessfulLaunch(LaunchRequest request) {
            requests.add(request);
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
