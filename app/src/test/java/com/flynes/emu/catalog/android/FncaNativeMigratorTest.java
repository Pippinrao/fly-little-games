package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.persistence.CanonicalUserState;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class FncaNativeMigratorTest {
    @Test
    public void replaysSourcesFavoritesAndPlaysThenStopsRetryingAfterSuccess() {
        Map<String, String> locators = new LinkedHashMap<>();
        Map<String, String> logBacking = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(locators::get, locators::put, locators::remove);
        AndroidRetryableMigrationLog log = new AndroidRetryableMigrationLog(
                logBacking::get, logBacking::put);
        RecordingHost host = new RecordingHost();
        CatalogState state = catalog();

        assertTrue(new FncaNativeMigrator().migrate(state, map, host, this::open, log));
        assertTrue(log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertFalse(log.shouldRetry(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertEquals(FlyCatalogCommands.SOURCE_SCOPE_BUILTIN, host.begins.get(0).scope);
        assertEquals(FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY, host.begins.get(1).scope);
        assertArrayEquals(map.builtinUuid(), host.begins.get(0).uuid);
        assertEquals("content://tree/roms", map.get(host.begins.get(1).uuid));
        assertEquals("from_below.nes", host.files.get(0).relativePath);
        assertEquals("game.nes", host.files.get(1).relativePath);
        assertEquals(List.of("canonical-a"), host.favorites);
        assertEquals(List.of("canonical-a", "canonical-a"), host.plays);
        assertEquals(2, host.commits.size());

        RecordingHost second = new RecordingHost();
        assertTrue(new FncaNativeMigrator().migrate(state, map, second, this::open, log));
        assertTrue(second.begins.isEmpty());
        assertArrayEquals(host.begins.get(1).uuid, map.uuidForLocator("content://tree/roms"));
    }

    @Test
    public void failedCommitKeepsFncaEligibleAndReusesSourceUuid() {
        Map<String, String> locators = new LinkedHashMap<>();
        Map<String, String> logBacking = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(locators::get, locators::put, locators::remove);
        AndroidRetryableMigrationLog log = new AndroidRetryableMigrationLog(
                logBacking::get, logBacking::put);
        RecordingHost host = new RecordingHost();
        host.commitResult = FlyCatalogCommands.CONFLICT;

        assertFalse(new FncaNativeMigrator().migrate(catalog(), map, host, this::open, log));
        assertFalse(log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        byte[] firstUuid = host.begins.get(0).uuid;
        host.commitResult = FlyCatalogCommands.OK;
        assertTrue(new FncaNativeMigrator().migrate(catalog(), map, host, this::open, log));
        assertArrayEquals(firstUuid, host.begins.get(host.begins.size() / 2).uuid);
        assertTrue(log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
    }

    private FncaNativeMigrator.OpenedFile open(PhysicalPackage pkg) {
        return new FncaNativeMigrator.OpenedFile(
                pkg.originalFilename(), pkg.originalFilename(), 7, shaBytes(pkg));
    }

    private static byte[] shaBytes(PhysicalPackage pkg) {
        String hex = pkg.physicalPackageSha256();
        byte[] bytes = new byte[32];
        for (int index = 0; index < 32; index++) {
            bytes[index] = (byte) Integer.parseInt(hex.substring(index * 2, index * 2 + 2), 16);
        }
        return bytes;
    }

    private static CatalogState catalog() {
        RomSource builtin = new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms/from_below.nes",
                RomSource.PermissionState.NOT_REQUIRED);
        RomSource tree = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "content://tree/roms",
                RomSource.PermissionState.GRANTED);
        PhysicalPackage builtinPkg = pkg(builtin, "builtin-pkg", "canonical-b", "from_below.nes", 'B');
        PhysicalPackage treePkg = pkg(tree, "tree-pkg", "canonical-a", "game.nes", 'A');
        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>();
        sources.put("builtin", sourceState(builtin, builtinPkg));
        sources.put("tree", sourceState(tree, treePkg));
        LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>();
        users.put("canonical-a", new CanonicalUserState(true, 2, 2, 2));
        return new CatalogState(CatalogState.CURRENT_SCHEMA, 4, "builtin", sources, users, 2);
    }

    private static SourceCatalogState sourceState(RomSource source, PhysicalPackage pkg) {
        return new SourceCatalogState(
                source, Map.of(pkg.id(), new CatalogPackage(pkg, CatalogPackage.Freshness.FRESH)),
                Collections.emptyList(), Collections.emptyList(), Collections.emptyList(),
                SourceScanResult.Completeness.FULL, 1);
    }

    private static PhysicalPackage pkg(
            RomSource source, String packageId, String gameId, String filename, char digit) {
        String sha1 = String.valueOf(digit).repeat(40);
        String sha256 = String.valueOf(digit).repeat(64);
        RomHashes hashes = new RomHashes(sha1, sha256, sha256, "1234ABCD");
        CanonicalGame game = new CanonicalGame(gameId, "Game", "", List.of());
        RomVariant variant = new RomVariant(
                "v-" + packageId, game, null, RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes, RomAnalysis.basic(16_400), null, null);
        return new PhysicalPackage(
                packageId, source, source.uri() + "/" + filename, filename,
                PackageFormat.RAW, sha256, List.of(variant));
    }

    private static final class RecordingHost implements FlyCatalogCommands {
        final List<Begin> begins = new ArrayList<>();
        final List<FileAdd> files = new ArrayList<>();
        final List<Integer> commits = new ArrayList<>();
        final List<String> favorites = new ArrayList<>();
        final List<String> plays = new ArrayList<>();
        int commitResult = OK;

        @Override public int scanBegin(byte[] sourceUuid, int sourceScope) {
            begins.add(new Begin(sourceUuid.clone(), sourceScope));
            return OK;
        }

        @Override public int scanAddFile(
                String relativePath, String displayName, int borrowedFd, byte[] expectedSha256) {
            files.add(new FileAdd(relativePath, displayName, borrowedFd, expectedSha256.clone()));
            return OK;
        }

        @Override public int scanCommit(int completeness) {
            commits.add(completeness);
            return commitResult;
        }

        @Override public void scanAbort() {
        }

        @Override public int favoriteSet(String canonicalId, boolean favorite) {
            if (favorite) favorites.add(canonicalId);
            return OK;
        }

        @Override public int markPlayed(String canonicalId) {
            plays.add(canonicalId);
            return OK;
        }

        record Begin(byte[] uuid, int scope) {
        }

        record FileAdd(String relativePath, String displayName, int borrowedFd, byte[] sha) {
        }
    }
}
