package com.flynes.emu.catalog;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotSame;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.CRC32;

public final class GameCatalogTest {
    @Test
    public void constructorsRejectBlankIdsUrisAndMismatchedPhysicalHashes() {
        byte[] payload = bytes("payload");
        RomHashes hashes = hashes(payload, payload);
        CanonicalGame game = game("game-a");
        RomVariant variant = variant("variant-a", game, hashes, null);

        assertThrows(IllegalArgumentException.class, () -> new RomSource(
                " ", RomSource.Type.BUILTIN, "asset:///roms",
                RomSource.PermissionState.NOT_REQUIRED));
        assertThrows(IllegalArgumentException.class, () -> new CanonicalGame(
                " ", "Contra", "魂斗罗", List.of()));
        assertThrows(IllegalArgumentException.class, () -> new RomVariant(
                " ", game, null, RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "package-a", builtin(), " ", "contra.nes", PackageFormat.RAW,
                hashes.physicalPackageSha256(), List.of(variant)));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "package-a", builtin(), "asset:///contra.nes", "contra.nes",
                PackageFormat.RAW, sha256(bytes("different")), List.of(variant)));
    }

    @Test
    public void packageFormatEnforcesExactEntryLocatorRules() {
        byte[] physical = bytes("physical");
        RomHashes hashes = hashes(bytes("rom"), physical);
        RomVariant raw = variant("raw", game("game-a"), hashes, null);
        RomVariant zip = zipVariant("zip", game("game-a"), hashes, "folder/game.nes", 42);

        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "zip", builtin(), "asset:///games.zip", "games.zip", PackageFormat.ZIP,
                hashes.physicalPackageSha256(), List.of(raw)));
        assertThrows(IllegalArgumentException.class, () -> new PhysicalPackage(
                "raw", builtin(), "asset:///game.nes", "game.nes", PackageFormat.RAW,
                hashes.physicalPackageSha256(), List.of(zip)));
        assertEquals(1, new PhysicalPackage(
                "zip", builtin(), "asset:///games.zip", "games.zip", PackageFormat.ZIP,
                hashes.physicalPackageSha256(), List.of(zip)).variants().size());
    }

    @Test
    public void zipEntryIdentityAndSourceStateAreProjected() {
        byte[] physical = bytes("physical");
        RomHashes hashes = hashes(bytes("rom"), physical);
        ZipEntryIdentity locator = ZipEntryIdentity.fromRawName(
                new byte[]{(byte) 0xBB, (byte) 0xEA}, 42);
        RomVariant zipVariant = new RomVariant(
                "variant-zip", game("game-a"), "魂.nes", RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes, RomAnalysis.basic(3),
                locator, ZipNameEncoding.GB18030);
        RomSource source = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "content://tree/roms",
                RomSource.PermissionState.GRANTED,
                RomSource.Availability.AVAILABLE);
        PhysicalPackage zip = new PhysicalPackage(
                "package-zip", source, "content://tree/archive", "archive.zip",
                PackageFormat.ZIP, hashes.physicalPackageSha256(), List.of(zipVariant));
        GameCatalog catalog = new GameCatalog();

        assertTrue(catalog.applyScanResult(ScanResult.success(List.of(zip), List.of())));
        GameVariant projected = catalog.resolveVariant("variant-zip").orElseThrow();
        assertEquals(locator, projected.zipEntryIdentity());
        assertEquals(RomSource.PermissionState.GRANTED, projected.sourcePermissionState());
        assertEquals(RomSource.Availability.AVAILABLE, projected.sourceAvailability());
    }

    @Test
    public void domainCollectionsAreDefensivelyCopied() {
        ArrayList<String> aliases = new ArrayList<>(List.of("Probotector"));
        CanonicalGame canonical = new CanonicalGame(
                "game-a", "Contra", "魂斗罗", aliases);
        aliases.add("Gryzor");
        byte[] payload = bytes("payload");
        RomHashes hashes = hashes(payload, payload);
        ArrayList<RomVariant> variants = new ArrayList<>();
        variants.add(variant("variant-a", canonical, hashes, null));
        PhysicalPackage physicalPackage = new PhysicalPackage(
                "package-a", builtin(), "asset:///contra.nes", "Contra.nes",
                PackageFormat.RAW, hashes.physicalPackageSha256(), variants);
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
                "package-a", "variant-a", game("game-a"), bytes("first"));
        assertTrue(catalog.applyScanResult(ScanResult.success(List.of(first), List.of())));
        List<GameCatalogEntry> before = catalog.canonicalEntries();

        ScanResult fatal = new ScanResult(List.of(rawPackage(
                "package-b", "variant-b", game("game-b"), bytes("second"))),
                List.of(new ScanIssue(
                        ScanIssue.Code.PERMISSION_REVOKED,
                        ScanIssue.Severity.FATAL,
                        "tree",
                        null)));

        assertFalse(catalog.applyScanResult(fatal));
        assertEquals(before, catalog.canonicalEntries());
    }

    @Test
    public void sameCanonicalIdGroupsDifferentPayloadHashesAndMergesSearchMetadata() {
        CanonicalGame english = new CanonicalGame(
                "release:contra", "Contra", "", List.of("Probotector"));
        CanonicalGame chinese = new CanonicalGame(
                "release:contra", "", "魂斗罗", List.of("Gryzor"));
        PhysicalPackage first = rawPackage(
                "package-a", "variant-a", english, bytes("usa"));
        PhysicalPackage second = rawPackage(
                "package-b", "variant-b", chinese, bytes("japan"));
        GameCatalog catalog = new GameCatalog();

        catalog.applyScanResult(ScanResult.success(List.of(second, first), List.of()));

        assertEquals(1, catalog.canonicalEntries().size());
        assertEquals(2, catalog.canonicalEntries().get(0).variants().size());
        assertEquals("Contra", catalog.canonicalEntries().get(0).canonicalGame().englishTitle());
        assertEquals("魂斗罗", catalog.canonicalEntries().get(0).canonicalGame().zhHansTitle());
        assertEquals(1, catalog.search("probotector").size());
        assertEquals(1, catalog.search("gryzor").size());
    }

    @Test
    public void identicalPayloadDoesNotGroupDistinctCanonicalIds() {
        byte[] payload = bytes("same");
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(
                rawPackage("package-a", "variant-a", game("release:a"), payload),
                rawPackage("package-b", "variant-b", game("release:b"), payload)), List.of()));

        assertEquals(2, catalog.canonicalEntries().size());
    }

    @Test
    public void searchMatchesTitlesAliasesOriginalFilenameAndUnknownCandidate() {
        TitleCandidate filename = new TitleCandidate(
                "Mysterious Local Name", TitleCandidate.Language.UNKNOWN,
                TitleCandidate.Origin.OUTER_FILENAME, TitleCandidate.Confidence.LOW,
                TitleCandidate.ReviewState.NEEDS_REVIEW);
        CanonicalGame canonical = new CanonicalGame(
                "game-a", List.of(filename), List.of("Probotector"));
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(rawPackage(
                "package-a", "variant-a", canonical, bytes("payload"))), List.of()));

        assertEquals(1, catalog.search("MYSTERIOUS").size());
        assertEquals(1, catalog.search("probotector").size());
        assertEquals(1, catalog.search("package-a").size());
        assertTrue(catalog.search("metroid").isEmpty());
    }

    @Test
    public void replacementPreservesUserStateOnlyByStableCanonicalId() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(rawPackage(
                "package-a", "variant-a", game("release:contra"), bytes("v1"))), List.of()));
        catalog.setFavorite("release:contra", true);
        catalog.recordSuccessfulLaunch("release:contra");
        GameCatalogEntry before = catalog.canonicalEntries().get(0);

        catalog.applyScanResult(ScanResult.success(List.of(rawPackage(
                "package-b", "variant-b", game("release:contra"), bytes("v2"))), List.of()));
        GameCatalogEntry after = catalog.canonicalEntries().get(0);

        assertNotSame(before, after);
        assertTrue(after.favorite());
        assertEquals(1, after.playCount());
    }

    @Test
    public void favoriteAndRecentViewsAreFilteredOrderedAndImmutable() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(
                rawPackage("package-a", "variant-a", game("game-a"), bytes("a")),
                rawPackage("package-b", "variant-b", game("game-b"), bytes("b"))), List.of()));
        catalog.setFavorite("game-a", true);
        catalog.recordSuccessfulLaunch("game-a");
        catalog.recordSuccessfulLaunch("game-b");

        assertEquals(List.of("game-a"), catalog.favoriteEntries().stream()
                .map(item -> item.canonicalGame().id()).toList());
        assertEquals(List.of("game-b", "game-a"), catalog.recentEntries().stream()
                .map(item -> item.canonicalGame().id()).toList());
        assertThrows(UnsupportedOperationException.class, catalog.favoriteEntries()::clear);
        assertThrows(UnsupportedOperationException.class, catalog.recentEntries()::clear);
    }

    private static RomSource builtin() {
        return new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms",
                RomSource.PermissionState.NOT_REQUIRED);
    }

    private static CanonicalGame game(String id) {
        return new CanonicalGame(id, "Contra", "魂斗罗", List.of("Probotector"));
    }

    private static RomVariant variant(
            String id, CanonicalGame game, RomHashes hashes, String entryPath) {
        return new RomVariant(
                id, game, entryPath, RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes);
    }

    private static RomVariant zipVariant(
            String id,
            CanonicalGame game,
            RomHashes hashes,
            String entryPath,
            int offset) {
        return new RomVariant(
                id, game, entryPath, RomFormat.INES,
                CompatibilityDecision.playableNes(), hashes, RomAnalysis.basic(3),
                ZipEntryIdentity.fromRawName(
                        entryPath.getBytes(StandardCharsets.UTF_8), offset),
                ZipNameEncoding.UTF8_EFS);
    }

    private static PhysicalPackage rawPackage(
            String packageId, String variantId, CanonicalGame game, byte[] payload) {
        RomHashes hashes = hashes(payload, payload);
        return new PhysicalPackage(
                packageId, builtin(), "asset:///" + packageId,
                packageId + ".nes", PackageFormat.RAW,
                hashes.physicalPackageSha256(),
                List.of(variant(variantId, game, hashes, null)));
    }

    private static RomHashes hashes(byte[] payload, byte[] physical) {
        CRC32 crc = new CRC32();
        crc.update(payload);
        return new RomHashes(
                digest("SHA-1", payload), digest("SHA-256", payload),
                digest("SHA-256", physical),
                String.format(Locale.ROOT, "%08X", crc.getValue()));
    }

    private static String sha256(byte[] bytes) {
        return digest("SHA-256", bytes);
    }

    private static String digest(String algorithm, byte[] bytes) {
        try {
            StringBuilder result = new StringBuilder();
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes)) {
                result.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return result.toString();
        } catch (Exception impossible) {
            throw new AssertionError(impossible);
        }
    }

    private static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }
}
