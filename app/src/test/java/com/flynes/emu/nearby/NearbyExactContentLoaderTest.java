package com.flynes.emu.nearby;

import static org.junit.Assert.*;

import com.flynes.emu.catalog.*;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchRequest;
import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class NearbyExactContentLoaderTest {
    private static final byte[] ROM = "exact-test-payload".getBytes(StandardCharsets.UTF_8);

    @Test public void exactSuccessPreservesUserStateAndOwnsBytes() throws Exception {
        Fixture f = new Fixture(false);
        f.catalog.setFavorite("game", true);
        List<GameCatalogEntry> before = f.catalog.canonicalEntries();
        NearbyExactContentLoader.LoadedContent result = f.loader().load("variant");
        assertNotNull("an exact load returns scoped content", result);
        assertArrayEquals(ROM, result.bytes());
        assertEquals("package", result.variant().packageId());
        assertEquals("source", result.variant().sourceId());
        assertEquals(f.uri, result.variant().sourceUri());
        assertEquals(LaunchRequest.forVariant(result.variant()).hashes(), result.variant().hashes());
        assertSame(before, f.catalog.canonicalEntries());
        assertEquals(0, before.get(0).playCount());
        assertTrue(before.get(0).favorite());
        assertTrue(f.catalog.recentEntries().isEmpty());
        byte[] first = result.bytes();
        first[0] ^= 1;
        f.physical[0] ^= 1;
        assertArrayEquals(ROM, result.bytes());
        assertEquals(List.of("source:" + f.uri), f.opened);
        assertEquals(2, f.accessChecks);
    }

    @Test public void missingAndUnknownSelectionNeverOpenAnyAlternative() throws Exception {
        Fixture f = new Fixture(false);
        for (String id : new String[]{null, "", "unknown"}) {
            assertFailure(f, id, NearbyExactContentLoader.FailureCode.VARIANT_NOT_FOUND);
        }
        assertEquals(0, f.opened.size());
    }

    @Test public void revokedCatalogSourceIsNotPlayableBeforeReading() throws Exception {
        Fixture f = new Fixture(false);
        f.permission = RomSource.PermissionState.NEEDS_REAUTHORIZE;
        f.publish();
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.NOT_PLAYABLE);
        assertTrue(f.opened.isEmpty());
    }

    @Test public void permissionRevokedBeforeReadRejectsWithoutOpening() throws Exception {
        Fixture f = new Fixture(false);
        f.readGranted = false;
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.SOURCE_ACCESS_DENIED);
        assertTrue(f.opened.isEmpty());
    }

    @Test public void permissionRevokedDuringReadRejectsLoadedBytes() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> f.readGranted = false;
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.SOURCE_ACCESS_DENIED);
        assertEquals(1, f.opened.size());
        assertEquals(2, f.accessChecks);
    }

    @Test public void removedSelectionDuringReadRejectsWithoutHashFallback() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> {
            f.variantId = "other-variant";
            f.sourceId = "other-source";
            f.publish();
        };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
        assertEquals(1, f.opened.size());
    }

    @Test public void sameHashReusedVariantIdFromDifferentSourceRejects() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> { f.sourceId = "other-source"; f.publish(); };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void changedExactLocatorAfterReadRejects() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> { f.uri = "content://fixture/document/replacement.nes"; f.publish(); };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void staleCatalogSourceAfterReadRejects() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> { f.permission = RomSource.PermissionState.NEEDS_REAUTHORIZE; f.publish(); };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void changedPayloadAfterReadRejects() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> {
            f.payload = "changed".getBytes(StandardCharsets.UTF_8);
            f.physical = f.payload.clone();
            f.publish();
        };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void changedChecksumMetadataCannotBeReturnedAsVerifiedIdentity() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> { f.crcOverride = "00000000"; f.publish(); };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void metadataOnlyCanonicalRenameRebasesIdentity() throws Exception {
        Fixture f = new Fixture(false);
        f.afterRead = () -> { f.canonicalId = "renamed-game"; f.publish(); };
        NearbyExactContentLoader.LoadedContent result = f.loader().load("variant");
        assertNotNull(result);
        assertEquals("renamed-game", result.variant().canonicalGameId());
        assertEquals("variant", result.variant().variantId());
        assertArrayEquals(ROM, result.bytes());
        assertEquals(0, f.catalog.canonicalEntries().get(0).playCount());
    }

    @Test public void exactZipEntryLoadsAndRawIdentityCannotBeMutated() throws Exception {
        Fixture f = new Fixture(true);
        NearbyExactContentLoader.LoadedContent result = f.loader().load("variant");
        assertNotNull(result);
        assertArrayEquals(ROM, result.bytes());
        byte[] rawName = result.variant().zipEntryIdentity().rawNameBytes();
        rawName[0] ^= 1;
        assertArrayEquals("target.nes".getBytes(StandardCharsets.UTF_8),
                result.variant().zipEntryIdentity().rawNameBytes());
    }

    @Test public void replacedZipPhysicalPackageFailsRealDigestValidation() throws Exception {
        Fixture f = new Fixture(true);
        f.physical = zip("other.nes", ROM);
        NearbyExactContentLoader.ContentException failure = assertFailure(
                f, "variant", NearbyExactContentLoader.FailureCode.LOAD_FAILED);
        assertEquals(ExactRomLoader.ErrorCode.HASH_MISMATCH,
                ((ExactRomLoader.LoadException) failure.getCause()).code());
    }

    @Test public void wrongExactZipEntryNeverFallsBackToSamePayload() throws Exception {
        Fixture f = new Fixture(true);
        f.entryName = "other.nes";
        f.publish();
        NearbyExactContentLoader.ContentException failure = assertFailure(
                f, "variant", NearbyExactContentLoader.FailureCode.LOAD_FAILED);
        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_MISSING,
                ((ExactRomLoader.LoadException) failure.getCause()).code());
    }

    @Test public void zipEntryReplacedInCatalogAfterReadRejects() throws Exception {
        Fixture f = new Fixture(true);
        f.afterRead = () -> { f.entryName = "replacement.nes"; f.publish(); };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    @Test public void reentrantValidatorCannotAuthorizeOldCatalogSnapshot() throws Exception {
        Fixture f = new Fixture(false);
        f.afterValidation = () -> {
            if (f.accessChecks == 2) {
                f.sourceId = "replacement-source";
                f.publish();
            }
        };
        assertFailure(f, "variant", NearbyExactContentLoader.FailureCode.CATALOG_CHANGED);
    }

    private static NearbyExactContentLoader.ContentException assertFailure(
            Fixture f, String id, NearbyExactContentLoader.FailureCode code) {
        NearbyExactContentLoader.ContentException failure = assertThrows(
                NearbyExactContentLoader.ContentException.class, () -> f.loader().load(id));
        assertEquals(code, failure.code());
        for (GameCatalogEntry entry : f.catalog.canonicalEntries()) assertEquals(0, entry.playCount());
        return failure;
    }

    private static final class Fixture {
        final GameCatalog catalog = new GameCatalog();
        final boolean zipped;
        byte[] payload = ROM.clone();
        byte[] physical;
        String canonicalId = "game";
        String sourceId = "source";
        String variantId = "variant";
        String uri = "content://fixture/document/package";
        String entryName = "target.nes";
        String crcOverride;
        RomSource.PermissionState permission = RomSource.PermissionState.GRANTED;
        boolean readGranted = true;
        int accessChecks;
        Runnable afterRead = () -> {};
        Runnable afterValidation = () -> {};
        final List<String> opened = new ArrayList<>();

        Fixture(boolean zipped) throws Exception {
            this.zipped = zipped;
            physical = zipped ? zip(entryName, payload) : payload.clone();
            publish();
        }

        void publish() {
            RomHashes hashes = hashes(payload, physical);
            if (crcOverride != null) hashes = new RomHashes(hashes.payloadSha1(),
                    hashes.payloadSha256(), hashes.physicalPackageSha256(), crcOverride);
            RomSource source = new RomSource(sourceId, RomSource.Type.SAF_TREE,
                    "content://fixture/tree/source", permission);
            RomVariant variant = new RomVariant(variantId,
                    new CanonicalGame(canonicalId, "Fixture", null, List.of()),
                    zipped ? entryName : null, RomFormat.INES,
                    CompatibilityDecision.playableNes(), hashes, RomAnalysis.basic(0),
                    zipped ? ZipEntryIdentity.fromRawName(entryName.getBytes(StandardCharsets.UTF_8), 0) : null,
                    zipped ? ZipNameEncoding.UTF8_EFS : null);
            PhysicalPackage pkg = new PhysicalPackage("package", source, uri,
                    zipped ? "fixture.zip" : "fixture.nes", zipped ? PackageFormat.ZIP : PackageFormat.RAW,
                    hashes.physicalPackageSha256(), List.of(variant));
            catalog.applyScanResult(ScanResult.success(List.of(pkg), List.of()));
        }

        NearbyExactContentLoader loader() {
            return new NearbyExactContentLoader(catalog, new ExactRomLoader((source, locator) -> {
                opened.add(source + ":" + locator);
                return new ByteArrayInputStream(physical) {
                    @Override public void close() { afterRead.run(); }
                };
            }), (source, locator) -> {
                accessChecks++;
                if (!readGranted) throw new SecurityException("read revoked");
                afterValidation.run();
            });
        }
    }

    private static RomHashes hashes(byte[] payload, byte[] physical) {
        CRC32 crc = new CRC32();
        crc.update(payload);
        return new RomHashes(digest("SHA-1", payload), digest("SHA-256", payload),
                digest("SHA-256", physical), String.format(Locale.ROOT, "%08X", crc.getValue()));
    }

    private static String digest(String algorithm, byte[] bytes) {
        try {
            StringBuilder hex = new StringBuilder();
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes))
                hex.append(String.format(Locale.ROOT, "%02X", value & 255));
            return hex.toString();
        } catch (Exception failure) { throw new AssertionError(failure); }
    }

    private static byte[] zip(String entryName, byte[] payload) throws Exception {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry(entryName));
            zip.write(payload);
            zip.closeEntry();
        }
        return bytes.toByteArray();
    }
}
