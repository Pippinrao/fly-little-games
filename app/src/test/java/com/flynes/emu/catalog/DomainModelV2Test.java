package com.flynes.emu.catalog;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.List;
import java.util.Locale;
import java.util.zip.CRC32;

public final class DomainModelV2Test {
    @Test
    public void hashesAreTypedValidatedAndDefensivelyNormalized() {
        RomHashes hashes = hashes(bytes("rom"), bytes("package"));

        assertEquals(40, hashes.payloadSha1().length());
        assertEquals(64, hashes.payloadSha256().length());
        assertEquals(64, hashes.physicalPackageSha256().length());
        assertEquals(8, hashes.crc32().length());
        assertEquals(hashes.payloadSha1(), hashes.romIdentity().sha1());
        assertThrows(IllegalArgumentException.class,
                () -> new RomHashes("0", sha256(bytes("rom")), sha256(bytes("package")),
                        "00000000"));
        assertThrows(IllegalArgumentException.class,
                () -> new RomHashes(sha1(bytes("rom")), "not-a-hash",
                        sha256(bytes("package")), "00000000"));
    }

    @Test
    public void stableIdsUseLocationAndRawLocatorWithoutLeakingInputs() {
        String first = StableIds.packageId("source-a", "content://private/tree/secret.nes");
        String framedDifferently = StableIds.packageId("source", "-a\0content://private/tree/secret.nes");
        String rawVariant = StableIds.variantId(
                first, StableIds.RAW_LOCATOR, sha256(bytes("payload")));
        String zipVariant = StableIds.variantId(
                first, "41422E4E4553@42", sha256(bytes("payload")));

        assertTrue(first.matches("pkg:[0-9A-F]{64}"));
        assertFalse(first.contains("private"));
        assertNotEquals(first, framedDifferently);
        assertNotEquals(rawVariant, zipVariant);
        assertEquals("game:" + sha256(bytes("payload")),
                StableIds.provisionalGameId(sha256(bytes("payload"))));
    }

    @Test
    public void titleCandidatesKeepLanguageOriginConfidenceAndReviewStateSeparate() {
        TitleCandidate outer = new TitleCandidate(
                "魂斗罗 (USA)",
                TitleCandidate.Language.UNKNOWN,
                TitleCandidate.Origin.OUTER_FILENAME,
                TitleCandidate.Confidence.LOW,
                TitleCandidate.ReviewState.NEEDS_REVIEW);
        TitleCandidate verifiedEnglish = new TitleCandidate(
                "Contra",
                TitleCandidate.Language.EN,
                TitleCandidate.Origin.BUILTIN_MANIFEST,
                TitleCandidate.Confidence.VERIFIED,
                TitleCandidate.ReviewState.VERIFIED);
        TitleCandidate unreviewedEnglish = new TitleCandidate(
                "Contra filename guess",
                TitleCandidate.Language.EN,
                TitleCandidate.Origin.OUTER_FILENAME,
                TitleCandidate.Confidence.LOW,
                TitleCandidate.ReviewState.NEEDS_REVIEW);
        CanonicalGame game = new CanonicalGame(
                "release:contra", List.of(outer, unreviewedEnglish, verifiedEnglish),
                List.of("Probotector"));

        assertEquals("Contra", game.englishTitle());
        assertEquals("", game.zhHansTitle());
        assertEquals(3, game.titleCandidates().size());
        assertThrows(UnsupportedOperationException.class,
                () -> game.titleCandidates().add(outer));
        assertThrows(IllegalArgumentException.class, () -> new TitleCandidate(
                " ", TitleCandidate.Language.EN, TitleCandidate.Origin.MANUAL_OVERRIDE,
                TitleCandidate.Confidence.HIGH, TitleCandidate.ReviewState.NEEDS_REVIEW));
    }

    @Test
    public void catalogGroupsDifferentPayloadsOnlyWhenCanonicalIdMatches() {
        byte[] usa = bytes("usa-revision");
        byte[] japan = bytes("japan-revision");
        CanonicalGame release = new CanonicalGame(
                "release:contra", "Contra", "魂斗罗", List.of());
        PhysicalPackage packageA = rawPackage("pkg-a", "variant-a", release, usa);
        PhysicalPackage packageB = rawPackage("pkg-b", "variant-b", release, japan);
        GameCatalog catalog = new GameCatalog();

        assertTrue(catalog.applyScanResult(ScanResult.success(
                List.of(packageB, packageA), List.of())));

        assertEquals(1, catalog.canonicalEntries().size());
        assertEquals(2, catalog.canonicalEntries().get(0).variants().size());
        assertNotEquals(
                catalog.resolveVariant("variant-a").orElseThrow().hashes().payloadSha256(),
                catalog.resolveVariant("variant-b").orElseThrow().hashes().payloadSha256());

        CanonicalGame otherRelease = new CanonicalGame(
                "release:other", "Other", "其他", List.of());
        catalog.applyScanResult(ScanResult.success(List.of(
                rawPackage("pkg-c", "variant-c", release, usa),
                rawPackage("pkg-d", "variant-d", otherRelease, usa)), List.of()));
        assertEquals(2, catalog.canonicalEntries().size());
    }

    @Test
    public void catalogRejectsConflictingVerifiedMetadataForOneCanonicalId() {
        CanonicalGame first = new CanonicalGame(
                "release:stable", "First verified title", "", List.of());
        CanonicalGame conflicting = new CanonicalGame(
                "release:stable", "Different verified title", "", List.of());

        assertThrows(IllegalArgumentException.class, () -> new GameCatalog().applyScanResult(
                ScanResult.success(List.of(
                        rawPackage("pkg-a", "variant-a", first, bytes("payload-a")),
                        rawPackage("pkg-b", "variant-b", conflicting, bytes("payload-b"))),
                        List.of())));
    }

    private static PhysicalPackage rawPackage(
            String packageId,
            String variantId,
            CanonicalGame game,
            byte[] payload) {
        RomHashes hashes = hashes(payload, payload);
        RomVariant variant = new RomVariant(
                variantId,
                game,
                null,
                RomFormat.INES,
                CompatibilityDecision.playableNes(),
                hashes);
        return new PhysicalPackage(
                packageId,
                new RomSource(
                        "builtin", RomSource.Type.BUILTIN, "asset:///roms",
                        RomSource.PermissionState.NOT_REQUIRED),
                "asset:///" + packageId,
                packageId + ".nes",
                PackageFormat.RAW,
                hashes.physicalPackageSha256(),
                List.of(variant));
    }

    static RomHashes hashes(byte[] payload, byte[] physicalPackage) {
        CRC32 crc32 = new CRC32();
        crc32.update(payload);
        return new RomHashes(
                sha1(payload),
                sha256(payload),
                sha256(physicalPackage),
                String.format(Locale.ROOT, "%08X", crc32.getValue()));
    }

    static String sha1(byte[] value) {
        return digest("SHA-1", value);
    }

    static String sha256(byte[] value) {
        return digest("SHA-256", value);
    }

    private static String digest(String algorithm, byte[] value) {
        try {
            StringBuilder encoded = new StringBuilder();
            for (byte item : MessageDigest.getInstance(algorithm).digest(value)) {
                encoded.append(String.format(Locale.ROOT, "%02X", item & 0xFF));
            }
            return encoded.toString();
        } catch (Exception impossible) {
            throw new AssertionError(impossible);
        }
    }

    static byte[] bytes(String value) {
        return value.getBytes(StandardCharsets.UTF_8);
    }
}
