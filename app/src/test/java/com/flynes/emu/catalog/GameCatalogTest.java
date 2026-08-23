package com.flynes.emu.catalog;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.data.RomIdentity;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

public final class GameCatalogTest {
    private static final RomIdentity IDENTITY_A =
            new RomIdentity("1111111111111111111111111111111111111111");
    private static final RomIdentity IDENTITY_B =
            new RomIdentity("2222222222222222222222222222222222222222");

    @Test
    public void constructorsRejectBlankIdsAndUris() {
        assertThrows(IllegalArgumentException.class, () -> new RomSource(
                " ", RomSource.Type.BUILTIN, "asset:///roms", RomSource.PermissionState.NOT_REQUIRED));
        assertThrows(IllegalArgumentException.class, () -> new RomSource(
                "builtin", RomSource.Type.BUILTIN, " ", RomSource.PermissionState.NOT_REQUIRED));
        assertThrows(IllegalArgumentException.class, () -> new CanonicalGame(
                " ", IDENTITY_A, "Contra", "魂斗罗", List.of()));
        assertThrows(IllegalArgumentException.class, () -> new RomVariant(
                " ", game("game-a", IDENTITY_A), null, RomFormat.INES,
                CompatibilityState.PLAYABLE));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "package-a", builtin(), " ", "contra.nes", PackageFormat.RAW_NES, List.of()));
    }

    @Test
    public void packageFormatEnforcesExactEntryPathRules() {
        RomVariant noEntry = variant("variant-a", game("game-a", IDENTITY_A), null);
        RomVariant withEntry = variant("variant-b", game("game-b", IDENTITY_B), "folder/game.nes");

        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "zip-a", builtin(), "asset:///games.zip", "games.zip", PackageFormat.ZIP,
                List.of(noEntry)));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "zip-without-locator",
                builtin(),
                "asset:///games.zip",
                "games.zip",
                PackageFormat.ZIP,
                List.of(withEntry)));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "raw-a", builtin(), "asset:///game.nes", "game.nes", PackageFormat.RAW_NES,
                List.of(withEntry)));

        PhysicalPackage empty = new PhysicalPackage(
                "zip-empty", builtin(), "asset:///empty.zip", "empty.zip", PackageFormat.ZIP,
                List.of());
        PhysicalPackage many = new PhysicalPackage(
                "zip-many", builtin(), "asset:///many.zip", "many.zip", PackageFormat.ZIP,
                List.of(
                        zipVariant("variant-a", game("game-a", IDENTITY_A), "a.nes", 0),
                        zipVariant("variant-b", game("game-b", IDENTITY_B), "b.nes", 64)));

        assertTrue(empty.variants().isEmpty());
        assertEquals(2, many.variants().size());
    }

    @Test
    public void zipEntryIdentityIsValidatedAndPropagatedWithSourcePermission() {
        ZipEntryIdentity locator = ZipEntryIdentity.fromRawName(
                new byte[]{(byte) 0xBB, (byte) 0xEA}, 42);
        RomVariant zipVariant = new RomVariant(
                "variant-zip",
                game("game-a", IDENTITY_A),
                "魂.nes",
                RomFormat.INES,
                CompatibilityState.PLAYABLE,
                locator,
                ZipNameEncoding.GB18030);
        RomSource source = new RomSource(
                "tree",
                RomSource.Type.SAF_TREE,
                "content://tree/roms",
                RomSource.PermissionState.GRANTED);
        PhysicalPackage zip = new PhysicalPackage(
                "package-zip",
                source,
                "content://tree/roms/archive.zip",
                "archive.zip",
                PackageFormat.ZIP,
                List.of(zipVariant));
        GameCatalog catalog = new GameCatalog();

        assertTrue(catalog.applyScanResult(ScanResult.success(List.of(zip), List.of())));

        GameVariant projected = catalog.resolveVariant("variant-zip").orElseThrow();
        assertEquals(locator, projected.zipEntryIdentity());
        assertEquals(RomSource.PermissionState.GRANTED, projected.sourcePermissionState());
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "package-raw",
                source,
                "content://tree/roms/game.nes",
                "game.nes",
                PackageFormat.RAW_NES,
                List.of(new RomVariant(
                        "variant-raw",
                        game("game-a", IDENTITY_A),
                        null,
                        RomFormat.INES,
                        CompatibilityState.PLAYABLE,
                        locator,
                        ZipNameEncoding.GB18030))));
        assertThrows(IllegalArgumentException.class,
                () -> new ZipEntryIdentity("0", 0));
        assertThrows(IllegalArgumentException.class,
                () -> new ZipEntryIdentity("GG", 0));
        assertThrows(IllegalArgumentException.class,
                () -> new ZipEntryIdentity("00", -1));
    }

    @Test
    public void domainCollectionsAreDefensivelyCopied() {
        ArrayList<String> aliases = new ArrayList<>(List.of("Probotector"));
        CanonicalGame canonical = new CanonicalGame(
                "game-a", IDENTITY_A, "Contra", "魂斗罗", aliases);
        aliases.add("Gryzor");

        ArrayList<RomVariant> variants = new ArrayList<>();
        variants.add(variant("variant-a", canonical, null));
        PhysicalPackage physicalPackage = new PhysicalPackage(
                "package-a", builtin(), "asset:///contra.nes", "Contra.nes",
                PackageFormat.RAW_NES, variants);
        variants.clear();

        assertEquals(List.of("Probotector"), canonical.aliases());
        assertThrows(UnsupportedOperationException.class,
                () -> canonical.aliases().add("Gryzor"));
        assertEquals(1, physicalPackage.variants().size());
        assertThrows(UnsupportedOperationException.class,
                () -> physicalPackage.variants().clear());
    }

    @Test
    public void fatalScanDoesNotReplaceLastGoodCatalog() {
        GameCatalog catalog = new GameCatalog();
        PhysicalPackage first = rawPackage(
                "package-a", "variant-a", game("game-a", IDENTITY_A), "Contra (USA).nes");
        assertTrue(catalog.applyScanResult(ScanResult.success(List.of(first), List.of())));
        List<GameCatalogEntry> before = catalog.canonicalEntries();

        PhysicalPackage replacement = rawPackage(
                "package-b", "variant-b", game("game-b", IDENTITY_B), "Other.nes");
        ScanResult fatal = new ScanResult(List.of(replacement), List.of(new ScanIssue(
                ScanIssue.Code.PERMISSION_REVOKED,
                ScanIssue.Severity.FATAL,
                "Built-in source became unavailable")));

        assertFalse(catalog.applyScanResult(fatal));
        assertEquals(before, catalog.canonicalEntries());
        assertEquals("game-a", catalog.canonicalEntries().get(0).canonicalGame().id());
    }

    @Test
    public void sameIdentityAcrossPackagesGroupsIntoOneCanonicalEntry() {
        CanonicalGame canonical = game("game-a", IDENTITY_A);
        PhysicalPackage raw = rawPackage(
                "package-raw", "variant-raw", canonical, "Contra.nes");
        PhysicalPackage zip = new PhysicalPackage(
                "package-zip", builtin(), "asset:///collection.zip", "Collection.zip",
                PackageFormat.ZIP,
                List.of(zipVariant(
                        "variant-zip", canonical, "roms/contra.nes", 0)));
        GameCatalog catalog = new GameCatalog();

        assertTrue(catalog.applyScanResult(ScanResult.success(List.of(raw, zip), List.of())));

        assertEquals(1, catalog.canonicalEntries().size());
        assertEquals(2, catalog.canonicalEntries().get(0).variants().size());
        assertTrue(catalog.resolveVariant("variant-raw").isPresent());
        assertTrue(catalog.resolveVariant("variant-zip").isPresent());
    }

    @Test
    public void identityGroupingRetainsSearchMetadataFromEveryVariant() {
        CanonicalGame englishMetadata = new CanonicalGame(
                "game-a", IDENTITY_A, "Contra", "", List.of("Probotector"));
        CanonicalGame chineseMetadata = new CanonicalGame(
                "game-b", IDENTITY_A, "", "魂斗罗", List.of("Gryzor"));
        PhysicalPackage first = rawPackage(
                "package-a", "variant-a", englishMetadata, "Contra.nes");
        PhysicalPackage second = rawPackage(
                "package-b", "variant-b", chineseMetadata, "魂斗罗.nes");
        GameCatalog catalog = new GameCatalog();

        catalog.applyScanResult(ScanResult.success(List.of(first, second), List.of()));

        assertEquals(1, catalog.canonicalEntries().size());
        assertEquals("Contra", catalog.canonicalEntries().get(0).canonicalGame().englishTitle());
        assertEquals("魂斗罗", catalog.canonicalEntries().get(0).canonicalGame().zhHansTitle());
        assertEquals(1, catalog.search("probotector").size());
        assertEquals(1, catalog.search("gryzor").size());
    }

    @Test
    public void searchMatchesChineseEnglishAliasesAndOriginalFilenameIgnoringCase() {
        CanonicalGame canonical = new CanonicalGame(
                "game-a", IDENTITY_A, "Contra", "魂斗罗", List.of("Probotector", "Gryzor"));
        PhysicalPackage physicalPackage = rawPackage(
                "package-a", "variant-a", canonical, "Contra (USA) (Rev A).nes");
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(physicalPackage), List.of()));

        assertEquals(1, catalog.search("CONTRA").size());
        assertEquals(1, catalog.search("魂斗罗").size());
        assertEquals(1, catalog.search("PROBOTECTOR").size());
        assertEquals(1, catalog.search("rev a").size());
        assertTrue(catalog.search("metroid").isEmpty());
    }

    @Test
    public void successfulReplacementPreservesUserStateByRomIdentity() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(rawPackage(
                "package-a", "variant-a", game("game-a", IDENTITY_A), "Contra.nes")), List.of()));
        assertTrue(catalog.setFavorite("game-a", true));
        assertTrue(catalog.recordSuccessfulLaunch("game-a"));
        GameCatalogEntry before = catalog.canonicalEntries().get(0);

        catalog.applyScanResult(ScanResult.success(List.of(rawPackage(
                "package-b", "variant-b", game("game-renamed", IDENTITY_A), "魂斗罗.nes")), List.of()));
        GameCatalogEntry after = catalog.canonicalEntries().get(0);

        assertNotSame(before, after);
        assertTrue(after.favorite());
        assertEquals(1, after.playCount());
        assertTrue(after.isRecent());
    }

    @Test
    public void favoriteAndRecentViewsAreFilteredOrderedAndImmutable() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(
                rawPackage("package-a", "variant-a", game("game-a", IDENTITY_A), "A.nes"),
                rawPackage("package-b", "variant-b", game("game-b", IDENTITY_B), "B.nes")),
                List.of()));
        catalog.setFavorite("game-a", true);
        catalog.recordSuccessfulLaunch("game-a");
        catalog.recordSuccessfulLaunch("game-b");

        List<GameCatalogEntry> favorites = catalog.favoriteEntries();
        List<GameCatalogEntry> recent = catalog.recentEntries();

        assertEquals(List.of("game-a"), favorites.stream()
                .map(entry -> entry.canonicalGame().id()).collect(java.util.stream.Collectors.toList()));
        assertEquals(List.of("game-b", "game-a"), recent.stream()
                .map(entry -> entry.canonicalGame().id()).collect(java.util.stream.Collectors.toList()));
        assertThrows(UnsupportedOperationException.class, favorites::clear);
        assertThrows(UnsupportedOperationException.class, recent::clear);
    }

    private static RomSource builtin() {
        return new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms",
                RomSource.PermissionState.NOT_REQUIRED);
    }

    private static CanonicalGame game(String id, RomIdentity identity) {
        return new CanonicalGame(id, identity, "Contra", "魂斗罗", List.of("Probotector"));
    }

    private static RomVariant variant(String id, CanonicalGame game, String entryPath) {
        return new RomVariant(id, game, entryPath, RomFormat.INES, CompatibilityState.PLAYABLE);
    }

    private static RomVariant zipVariant(
            String id,
            CanonicalGame game,
            String entryPath,
            int localHeaderOffset) {
        return new RomVariant(
                id,
                game,
                entryPath,
                RomFormat.INES,
                CompatibilityState.PLAYABLE,
                ZipEntryIdentity.fromRawName(
                        entryPath.getBytes(java.nio.charset.StandardCharsets.UTF_8),
                        localHeaderOffset),
                ZipNameEncoding.UTF8_EFS);
    }

    private static PhysicalPackage rawPackage(
            String packageId,
            String variantId,
            CanonicalGame canonical,
            String originalFilename) {
        return new PhysicalPackage(
                packageId,
                builtin(),
                "asset:///" + originalFilename,
                originalFilename,
                PackageFormat.RAW_NES,
                List.of(variant(variantId, canonical, null)));
    }
}
