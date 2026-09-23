package com.flynes.emu.gamecenter;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.persistence.CatalogReconciler;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateCodec;
import com.flynes.emu.catalog.persistence.CatalogStateStore;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import org.junit.Test;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Locale;

public final class GameCenterSnapshotProjectorTest {
    private static final String FINGERPRINT = "ab".repeat(32);

    @Test public void projects2224RowsInStablePopularityOrderWithSourceSummary() throws Exception {
        CatalogState state = stateWithGames(2224, false);

        GameCenterSnapshot snapshot = new GameCenterSnapshotProjector()
                .project(91L, FINGERPRINT, 12L, state);

        assertEquals(2224, snapshot.rows().size());
        assertEquals("game-0000", snapshot.rows().get(0).canonicalId());
        assertEquals(100, snapshot.rows().get(0).popularityScore());
        assertEquals(2, snapshot.sources().size());
        GameCenterSnapshot.SourceRow external = snapshot.sources().stream()
                .filter(item -> item.id().equals("external"))
                .findFirst().orElseThrow();
        assertEquals(2224, external.packageCount());
        assertEquals(91L, snapshot.nativeGeneration());
        assertEquals(12L, snapshot.sourceEpoch());
        assertArrayEquals(CatalogStateCodec.encode(state), snapshot.catalogStateBytes());
    }

    @Test public void cachedRowsSearchAliasesWithoutGameCatalogTraversal() throws Exception {
        CatalogState state = stateWithGames(3, true);
        GameCenterSnapshot snapshot = new GameCenterSnapshotProjector()
                .project(9L, FINGERPRINT, 3L, state);
        GameCenterState navigation = new GameCenterState();

        navigation.setQuery("hidden alias");

        assertEquals(List.of("game-0001"), navigation.filtered(snapshot.rows().stream()
                .map(GameCenterSnapshot.Row::item).toList()).stream()
                .map(GameCenterItem::canonicalId).toList());
        assertTrue(snapshot.rows().stream().filter(row -> row.canonicalId().equals("game-0001"))
                .findFirst().orElseThrow().searchText().contains("hidden alias"));
    }

    @Test public void projectsUserStateFallbackTitleAndLaunchability() throws Exception {
        CatalogState scanned = stateWithGames(2, true);
        CatalogRepository repository = new CatalogRepository(
                scanned, new MemoryStore(), new GameCatalog());
        assertTrue(repository.setFavorite("game-0001", true));
        assertTrue(repository.recordSuccessfulLaunch("game-0001"));

        GameCenterSnapshot snapshot = new GameCenterSnapshotProjector()
                .project(3L, FINGERPRINT, 2L, repository.state());
        GameCenterSnapshot.Row row = snapshot.rows().stream()
                .filter(item -> item.canonicalId().equals("game-0001"))
                .findFirst().orElseThrow();

        assertTrue(row.favorite());
        assertEquals(1, row.playCount());
        assertEquals(1, row.lastPlayedSequence());
        assertEquals("Mystery Cartridge", row.fallbackTitle());
        assertEquals(1, row.variantCount());
        assertTrue(row.launchable());
        assertFalse(row.builtin());
    }

    private static CatalogState stateWithGames(int count, boolean specialMetadata) {
        RomSource builtin = new RomSource("builtin", RomSource.Type.BUILTIN,
                "asset://builtin", RomSource.PermissionState.NOT_REQUIRED);
        RomSource external = new RomSource("external", RomSource.Type.SAF_TREE,
                "content://synthetic/tree", RomSource.PermissionState.GRANTED);
        CatalogState state = CatalogState.empty(builtin).withSource(external);
        ArrayList<PhysicalPackage> packages = new ArrayList<>(count);
        ArrayList<PackageOutcome> outcomes = new ArrayList<>(count);
        for (int index = 0; index < count; index++) {
            PhysicalPackage item = packageFor(external, index, specialMetadata && index == 1);
            packages.add(item);
            outcomes.add(new PackageOutcome(item.id(), PackageOutcome.Status.INDEXED,
                    PackageOutcome.Reason.INDEXED));
        }
        return CatalogReconciler.reconcile(state, new SourceScanResult(
                external.id(), state.revision(), 1, SourceScanResult.Completeness.FULL,
                external, packages, outcomes, Collections.emptyList(),
                Collections.emptyList(), count));
    }

    private static PhysicalPackage packageFor(RomSource source, int index, boolean special) {
        String suffix = String.format(Locale.ROOT, "%04d", index);
        String sha1 = String.format(Locale.ROOT, "%040x", index + 1L);
        String sha256 = String.format(Locale.ROOT, "%064x", index + 1L);
        CanonicalGame game;
        if (special) {
            game = new CanonicalGame("game-" + suffix, List.of(new TitleCandidate(
                    "Mystery Cartridge", TitleCandidate.Language.UNKNOWN,
                    TitleCandidate.Origin.OUTER_FILENAME, TitleCandidate.Confidence.LOW,
                    TitleCandidate.ReviewState.NEEDS_REVIEW)), List.of("Hidden Alias"));
        } else {
            game = new CanonicalGame("game-" + suffix, "Game " + suffix,
                    "游戏 " + suffix, List.of("Alias " + suffix));
        }
        String filename = index == 0 ? "Contra.nes" : "Unknown " + suffix + ".nes";
        RomHashes hashes = new RomHashes(sha1, sha256, sha256,
                String.format(Locale.ROOT, "%08x", index + 1));
        RomVariant variant = new RomVariant("variant-" + suffix, game, null,
                RomFormat.INES, CompatibilityDecision.playableNes(), hashes,
                RomAnalysis.basic(24_592), null, null);
        return new PhysicalPackage("package-" + suffix, source,
                "content://synthetic/" + suffix, filename, PackageFormat.RAW,
                sha256, List.of(variant));
    }

    private static final class MemoryStore implements CatalogStateStore {
        private byte[] bytes;

        @Override public byte[] read() {
            return bytes == null ? null : bytes.clone();
        }

        @Override public void writeAtomically(byte[] encoded) throws IOException {
            bytes = encoded.clone();
        }
    }
}
