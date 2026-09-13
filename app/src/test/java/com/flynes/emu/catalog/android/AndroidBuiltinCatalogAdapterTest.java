package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.BuiltinGames;
import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.TitleCandidate;

import org.junit.Test;

import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

/**
 * The built-in catalog must be produced from the shared manifest: seven games,
 * each pointing at its own asset, with the manifest's verified bilingual titles.
 * Runs on the JVM by feeding the adapter the real content/assets tree instead of
 * the Android asset manager, so a platform mistake fails a fast unit test.
 */
public final class AndroidBuiltinCatalogAdapterTest {

    private static Path repoRoot() {
        Path candidate = Paths.get("").toAbsolutePath();
        for (int depth = 0; depth < 6 && candidate != null; depth++) {
            if (Files.isRegularFile(candidate.resolve("content/assets/builtin-games.json"))) return candidate;
            candidate = candidate.getParent();
        }
        throw new AssertionError("content/assets/builtin-games.json not found above " + Paths.get(""));
    }

    private static AndroidBuiltinCatalogAdapter adapter() throws IOException {
        Path root = repoRoot();
        BuiltinGames games;
        try (InputStream in = Files.newInputStream(root.resolve("content/assets/builtin-games.json"))) {
            games = BuiltinGames.parse(in);
        }
        return new AndroidBuiltinCatalogAdapter(games, assetPath ->
                Files.newInputStream(root.resolve("content/assets").resolve(assetPath)));
    }

    @Test
    public void scansEveryBundledGamePointingAtItsOwnAsset() throws IOException {
        ScanResult scanned = adapter().scan();

        assertEquals("one physical package per bundled game", 7, scanned.packages().size());

        Set<String> canonicalIds = new HashSet<>();
        Set<String> locators = new HashSet<>();
        for (PhysicalPackage physical : scanned.packages()) {
            assertEquals("each bundled package holds exactly one variant", 1, physical.variants().size());
            CanonicalGame game = physical.variants().get(0).canonicalGame();
            assertNotNull(game);
            assertTrue("bundled ids are namespaced: " + game.id(), game.id().startsWith("builtin:"));
            assertTrue("ids must be unique", canonicalIds.add(game.id()));
            assertTrue("each game needs its own asset locator: " + physical.sourceUri(),
                    physical.sourceUri().startsWith(AndroidBuiltinCatalogAdapter.ASSET_ROOT));
            assertTrue("locators must be unique", locators.add(physical.sourceUri()));
            assertTrue("every bundled ROM must be playable",
                    physical.variants().get(0).compatibilityDecision().isPlayable());
        }

        assertEquals("the retired single builtin must be gone", 7, canonicalIds.size());
        assertTrue(canonicalIds.contains("builtin:super-tilt-bro"));
        assertTrue(canonicalIds.contains("builtin:thwaite"));
        assertTrue(canonicalIds.contains("builtin:dabg"));
    }

    @Test
    public void publishesVerifiedBilingualTitlesFromTheManifest() throws IOException {
        BuiltinGames games;
        try (InputStream in = Files.newInputStream(
                repoRoot().resolve("content/assets/builtin-games.json"))) {
            games = BuiltinGames.parse(in);
        }
        BuiltinGames.Entry rhde = games.byCanonicalId("builtin:rhde");
        assertNotNull(rhde);

        PhysicalPackage pkg = adapter().scan().packages().stream()
                .filter(item -> item.sourceUri().equals(
                        AndroidBuiltinCatalogAdapter.assetLocator(rhde.assetFilename)))
                .findFirst().orElseThrow();

        CanonicalGame game = pkg.variants().get(0).canonicalGame();
        assertEquals("RHDE: Furniture Fight", game.englishTitle());
        assertEquals("抢家具大作战", game.zhHansTitle());
        assertTrue("titles must be marked as coming from the builtin manifest",
                game.titleCandidates().stream().allMatch(candidate ->
                        candidate.origin() == TitleCandidate.Origin.BUILTIN_MANIFEST
                                && candidate.reviewState() == TitleCandidate.ReviewState.VERIFIED));
    }

    @Test
    public void resolvesAssetLocatorsForEveryBundledFilename() throws IOException {
        BuiltinGames games;
        try (InputStream in = Files.newInputStream(
                repoRoot().resolve("content/assets/builtin-games.json"))) {
            games = BuiltinGames.parse(in);
        }
        List<BuiltinGames.Entry> all = games.all();
        assertEquals(7, all.size());
        for (BuiltinGames.Entry game : all) {
            assertEquals("asset:///roms/" + game.assetFilename,
                    AndroidBuiltinCatalogAdapter.assetLocator(game.assetFilename));
        }
        assertNull("unknown ids must not be treated as bundled",
                games.byCanonicalId("builtin:not-bundled"));
    }
}
