package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.CompatibilityReason;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchRequest;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class RomPackageScannerTest {
    @Test
    public void rawInesIsSniffedWithoutExtensionAndCarriesHashesMetadataAndLowTitle() {
        byte[] payload = ines(1, 0, 0x12, 0x40, false, false, 0);
        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("doc-raw", "没有扩展名", payload)));

        assertEquals(1, result.packages().size());
        assertEquals(PackageOutcome.Status.INDEXED, result.packageOutcomes().get(0).status());
        PhysicalPackage physicalPackage = result.packages().get(0);
        assertEquals(PackageFormat.RAW, physicalPackage.packageFormat());
        assertEquals(64, physicalPackage.physicalPackageSha256().length());
        RomVariant variant = physicalPackage.variants().get(0);
        assertEquals(RomFormat.INES, variant.romFormat());
        assertEquals(CompatibilityState.PLAYABLE,
                variant.compatibilityDecision().state());
        assertEquals(CompatibilityReason.PLAYABLE_NES,
                variant.compatibilityDecision().reason());
        assertEquals(0x41, variant.analysis().mapper());
        assertEquals(0, variant.analysis().submapper());
        assertTrue(variant.analysis().battery());
        assertFalse(variant.analysis().trainer());
        assertEquals(TitleCandidate.Language.UNKNOWN,
                variant.canonicalGame().titleCandidates().get(0).language());
        assertEquals(TitleCandidate.Origin.OUTER_FILENAME,
                variant.canonicalGame().titleCandidates().get(0).origin());
        assertEquals(TitleCandidate.Confidence.LOW,
                variant.canonicalGame().titleCandidates().get(0).confidence());
        assertEquals("game:" + variant.hashes().payloadSha256(),
                variant.canonicalGame().id());
        assertFalse(physicalPackage.id().contains("doc-raw"));
    }

    @Test
    public void nes2ParsesExtendedAndExponentialSizesWithTrainerAndTrailingWarning() {
        byte[] standard = nes2(1, 1, 1, 0, 0x123, 0xA, true, 3);
        byte[] exponential = nes2Exponential(6, 0, 5, 1, false, 0);

        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("standard", "standard.bin", standard),
                PackageCandidate.bytes("exponential", "exponential.bin", exponential)));

        RomVariant standardVariant = variantByFilename(result, "standard.bin");
        assertEquals(RomFormat.NES2, standardVariant.romFormat());
        assertEquals(0x123, standardVariant.analysis().mapper());
        assertEquals(0xA, standardVariant.analysis().submapper());
        assertTrue(standardVariant.analysis().trainer());
        assertTrue(standardVariant.analysis().warnings().contains(
                RomAnalysis.Warning.TRAILING_DATA));

        RomVariant exponentialVariant = variantByFilename(result, "exponential.bin");
        assertEquals((1L << 6) * 1L, exponentialVariant.analysis().prgBytes());
        assertEquals((1L << 5) * 3L, exponentialVariant.analysis().chrBytes());
        assertEquals(CompatibilityState.PLAYABLE,
                exponentialVariant.compatibilityDecision().state());
    }

    @Test
    public void truncatedZeroPrgAndOverflowNes2AreIndexedInvalidWithStableReasons() {
        byte[] truncated = Arrays.copyOf(ines(1, 0, 0, 0, false, false, 0), 32);
        byte[] zeroPrg = ines(0, 0, 0, 0, false, false, 0);
        byte[] overflow = nes2Exponential(63, 3, 0, 0, false, 0);

        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("truncated", "truncated.nes", truncated),
                PackageCandidate.bytes("zero", "zero.nes", zeroPrg),
                PackageCandidate.bytes("overflow", "overflow.nes", overflow)));

        assertEquals(3, result.packages().size());
        assertEquals(CompatibilityReason.NES_TRUNCATED,
                variantByFilename(result, "truncated.nes").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.NES_ZERO_PRG,
                variantByFilename(result, "zero.nes").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.NES_SIZE_OVERFLOW,
                variantByFilename(result, "overflow.nes").compatibilityDecision().reason());
    }

    @Test
    public void validFdsAndUnifAreIndexedButExplicitlyUnsupported() {
        byte[] fds = headeredFds(1);
        byte[] unif = unif(true);

        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("fds", "disk.img", fds),
                PackageCandidate.bytes("unif", "board.data", unif)));

        RomVariant fdsVariant = variantByFilename(result, "disk.img");
        assertEquals(RomFormat.FDS, fdsVariant.romFormat());
        assertEquals(CompatibilityState.UNSUPPORTED,
                fdsVariant.compatibilityDecision().state());
        assertEquals(CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED,
                fdsVariant.compatibilityDecision().reason());
        assertEquals(1, fdsVariant.analysis().diskSides());

        RomVariant unifVariant = variantByFilename(result, "board.data");
        assertEquals(RomFormat.UNIF, unifVariant.romFormat());
        assertEquals(CompatibilityReason.UNIF_PRODUCT_DISABLED,
                unifVariant.compatibilityDecision().reason());
    }

    @Test
    public void zipIndexesEveryRomByExactRawLocatorAndLoaderUsesPayloadSha256() throws Exception {
        byte[] first = ines(1, 0, 0, 0, false, false, 0);
        first[first.length - 1] = 1;
        byte[] second = ines(1, 0, 0, 0, false, false, 0);
        second[second.length - 1] = 2;
        byte[] archive = zip(
                StandardCharsets.UTF_8,
                List.of(
                        entry("first.nes", first),
                        entry("nested/SECOND.nes", second)));
        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("multi", "collection.any", archive)));

        assertEquals(1, result.packages().size());
        PhysicalPackage physicalPackage = result.packages().get(0);
        assertEquals(PackageFormat.ZIP, physicalPackage.packageFormat());
        assertEquals(2, physicalPackage.variants().size());
        assertEquals(2, result.entryOutcomes().stream()
                .filter(outcome -> outcome.status() == EntryOutcome.Status.INDEXED)
                .count());
        assertNotEquals(
                physicalPackage.variants().get(0).zipEntryIdentity(),
                physicalPackage.variants().get(1).zipEntryIdentity());

        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(result);
        GameVariant secondVariant = catalog.canonicalEntries().stream()
                .flatMap(item -> item.variants().stream())
                .filter(item -> item.entryPath().endsWith("SECOND.nes"))
                .findFirst().orElseThrow();
        LaunchRequest request = LaunchRequest.forVariant(secondVariant);
        ExactRomLoader loader = new ExactRomLoader(
                (sourceId, locator) -> new ByteArrayInputStream(archive));

        assertArrayEquals(second, loader.load(request));
    }

    @Test
    public void zipAccountsDirectoriesUnsafePathsNestedArchivesExecutablesGbAndSidecars()
            throws Exception {
        byte[] nestedZip = zip(StandardCharsets.UTF_8,
                List.of(entry("inside.txt", "x".getBytes(StandardCharsets.UTF_8))));
        byte[] archive = zip(StandardCharsets.UTF_8, List.of(
                directory("folder.nes/"),
                entry("../escape.nes", ines(1, 0, 0, 0, false, false, 0)),
                entry("/absolute.nes", ines(1, 0, 0, 0, false, false, 0)),
                entry("C:/drive.nes", ines(1, 0, 0, 0, false, false, 0)),
                entry("nested.zip", nestedZip),
                entry("tool.bin", new byte[]{'M', 'Z', 0, 0}),
                entry("portable.bin", gameBoy()),
                entry("readme.txt", "hello\n".getBytes(StandardCharsets.UTF_8))));
        try {
            BoundedZipArchive.fromBytes(archive, ScanLimits.defaults());
        } catch (BoundedZipArchive.ArchiveException failure) {
            throw new AssertionError(failure.code() + ": " + failure.getMessage(), failure);
        }

        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("mixed", "mixed.zip", archive)));

        assertEquals(1, result.packageOutcomes().size());
        assertEquals(result.packageOutcomes().toString(), PackageOutcome.Status.SKIPPED,
                result.packageOutcomes().get(0).status());
        assertEquals(8, result.entryOutcomes().size());
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.DIRECTORY));
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.INVALID_PATH));
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.NESTED_ARCHIVE));
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.EXECUTABLE));
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.GAME_BOY));
        assertTrue(hasEntryReason(result, EntryOutcome.Reason.SIDECAR));
    }

    @Test
    public void corruptAndOversizePackagesAreErrorsAndEveryCandidateIsAccounted() {
        byte[] valid = ines(1, 0, 0, 0, false, false, 0);
        byte[] corruptZip = new byte[]{'P', 'K', 3, 4, 1, 2, 3};
        ScanLimits limits = new ScanLimits(
                valid.length,
                valid.length,
                8,
                1024 * 1024,
                128,
                200,
                1024);
        RomPackageScanner scanner = new RomPackageScanner(limits);

        ScanResult result = scanner.scan(source(), List.of(
                PackageCandidate.bytes("equal", "equal", valid),
                PackageCandidate.bytes("plus-one", "plus", Arrays.copyOf(valid, valid.length + 1)),
                PackageCandidate.bytes("corrupt", "corrupt", corruptZip),
                PackageCandidate.bytes("unknown", "unknown", new byte[]{1, 2, 3, 4})));

        assertEquals(4, result.packageOutcomes().size());
        assertEquals(PackageOutcome.Status.INDEXED,
                outcome(result, StableIds.packageId(source().id(), "equal")).status());
        assertEquals(PackageOutcome.Reason.PACKAGE_LIMIT_EXCEEDED,
                outcome(result, StableIds.packageId(source().id(), "plus-one")).reason());
        assertEquals(PackageOutcome.Reason.INVALID_ZIP,
                outcome(result, StableIds.packageId(source().id(), "corrupt")).reason());
        assertEquals(PackageOutcome.Status.SKIPPED,
                outcome(result, StableIds.packageId(source().id(), "unknown")).status());
    }

    @Test(timeout = 2000)
    public void zeroLengthBulkReadCannotHangAndProviderOrderCannotChangeResult() {
        byte[] first = ines(1, 0, 0, 0, false, false, 0);
        byte[] second = ines(1, 0, 0, 0, false, false, 1);
        PackageCandidate zeroRead = new PackageCandidate(
                "a", "a.nes", () -> new ZeroThenDataInputStream(first));
        PackageCandidate normal = PackageCandidate.bytes("b", "b.nes", second);

        ScanResult forward = scanner().scan(source(), List.of(zeroRead, normal));
        ScanResult reverse = scanner().scan(source(), List.of(normal, zeroRead));

        assertEquals(forward, reverse);
    }

    @Test
    public void legacyZipNameEncodingsAreDisplayOnlyAndNeverChangeLocator() throws Exception {
        byte[] payload = ines(1, 0, 0, 0, false, false, 0);
        byte[] cp437 = zip(Charset.forName("IBM437"),
                List.of(entry("Grüße.nes", payload)));
        byte[] gb18030 = zip(Charset.forName("GB18030"),
                List.of(entry("魂斗罗.nes", payload)));

        RomVariant cpVariant = scanner().scan(source(), List.of(
                PackageCandidate.bytes("cp", "cp.zip", cp437)))
                .packages().get(0).variants().get(0);
        RomVariant gbVariant = scanner().scan(source(), List.of(
                PackageCandidate.bytes("gb", "gb.zip", gb18030)))
                .packages().get(0).variants().get(0);

        assertEquals("Grüße.nes", cpVariant.entryPath());
        assertEquals("魂斗罗.nes", gbVariant.entryPath());
        assertNotEquals(cpVariant.zipEntryIdentity().rawNameHex(),
                gbVariant.zipEntryIdentity().rawNameHex());
    }

    @Test
    public void locationIdentityPhysicalIdentityAndOverrideGroupingStaySeparate() {
        byte[] first = ines(1, 0, 0, 0, false, false, 0);
        byte[] second = first.clone();
        second[second.length - 1] = 7;

        ScanResult oldAtLocation = scanner().scan(source(), List.of(
                PackageCandidate.bytes("stable-doc", "first.nes", first)));
        ScanResult newAtLocation = scanner().scan(source(), List.of(
                PackageCandidate.bytes("stable-doc", "second.nes", second)));
        PhysicalPackage oldPackage = oldAtLocation.packages().get(0);
        PhysicalPackage newPackage = newAtLocation.packages().get(0);
        assertEquals(oldPackage.id(), newPackage.id());
        assertNotEquals(oldPackage.physicalPackageSha256(),
                newPackage.physicalPackageSha256());
        assertNotEquals(oldPackage.variants().get(0).id(), newPackage.variants().get(0).id());

        ScanResult duplicateBytesAtTwoLocations = scanner().scan(source(), List.of(
                PackageCandidate.bytes("location-a", "a.nes", first),
                PackageCandidate.bytes("location-b", "b.nes", first)));
        assertNotEquals(duplicateBytesAtTwoLocations.packages().get(0).id(),
                duplicateBytesAtTwoLocations.packages().get(1).id());
        assertEquals(duplicateBytesAtTwoLocations.packages().get(0).physicalPackageSha256(),
                duplicateBytesAtTwoLocations.packages().get(1).physicalPackageSha256());

        RomPackageScanner groupedScanner = new RomPackageScanner(
                ScanLimits.defaults(), (hash, format) -> "release:shared");
        ScanResult grouped = groupedScanner.scan(source(), List.of(
                PackageCandidate.bytes("group-a", "region-a.nes", first),
                PackageCandidate.bytes("group-b", "region-b.nes", second)));
        GameCatalog catalog = new GameCatalog();
        assertTrue(catalog.applyScanResult(grouped));
        assertEquals(1, catalog.canonicalEntries().size());
        assertEquals(2, catalog.canonicalEntries().get(0).variants().size());
    }

    @Test
    public void unavailableSourceAndDuplicateDocumentKeysAccountEveryCandidateWithoutOpening() {
        AtomicBoolean opened = new AtomicBoolean();
        RomSource unavailable = new RomSource(
                "unavailable-source",
                RomSource.Type.SAF_TREE,
                "source://redacted",
                RomSource.PermissionState.GRANTED,
                RomSource.Availability.UNAVAILABLE);
        PackageCandidate candidate = new PackageCandidate(
                "private-document-key",
                "private-name.nes",
                () -> {
                    opened.set(true);
                    return new ByteArrayInputStream(ines(1, 0, 0, 0, false, false, 0));
                });

        ScanResult unavailableResult = scanner().scan(unavailable, List.of(candidate));
        assertFalse(opened.get());
        assertEquals(1, unavailableResult.packageOutcomes().size());
        assertEquals(PackageOutcome.Reason.SOURCE_UNAVAILABLE,
                unavailableResult.packageOutcomes().get(0).reason());
        assertTrue(unavailableResult.hasFatalIssue());
        assertFalse(unavailableResult.toString().contains("private-document-key"));
        assertFalse(unavailableResult.toString().contains("private-name.nes"));

        ScanResult duplicates = scanner().scan(source(), List.of(candidate, candidate));
        assertEquals(2, duplicates.packageOutcomes().size());
        assertTrue(duplicates.packageOutcomes().stream().allMatch(item ->
                item.reason() == PackageOutcome.Reason.DUPLICATE_DOCUMENT_KEY));

        ScanResult revoked = scanner().scan(source(), List.of(new PackageCandidate(
                "revoked-document",
                "revoked.nes",
                () -> {
                    throw new SecurityException("content://must-not-be-persisted");
                })));
        assertTrue(revoked.hasFatalIssue());
        assertEquals(PackageOutcome.Reason.SOURCE_UNAVAILABLE,
                revoked.packageOutcomes().get(0).reason());
        assertTrue(revoked.issues().stream().anyMatch(item ->
                item.code() == ScanIssue.Code.PERMISSION_REVOKED
                        && item.severity() == ScanIssue.Severity.FATAL));
        assertFalse(revoked.toString().contains("must-not-be-persisted"));
    }

    @Test
    public void parserCoversDirtyHeaderNes2MultiplierOverflowAndInvalidDiskFormats() {
        byte[] dirty = ines(1, 0, 0x02, 0, true, true, 0);
        byte[] multiplierOverflow = nes2Exponential(62, 3, 0, 0, false, 0);
        byte[] headerlessFds = Arrays.copyOfRange(headeredFds(1), 16, 16 + 65_500);
        byte[] truncatedHeaderlessFds = Arrays.copyOf(headerlessFds, 100);
        byte[] zeroSideFds = headeredFds(0);
        byte[] truncatedFds = Arrays.copyOf(headeredFds(1), 100);
        byte[] invalidUnifChunk = Arrays.copyOf(unif(false), 36);

        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("dirty", "dirty", dirty),
                PackageCandidate.bytes("overflow-7", "overflow-7", multiplierOverflow),
                PackageCandidate.bytes("headerless", "headerless", headerlessFds),
                PackageCandidate.bytes(
                        "truncated-headerless", "truncated-headerless", truncatedHeaderlessFds),
                PackageCandidate.bytes("zero-side", "zero-side", zeroSideFds),
                PackageCandidate.bytes("truncated-fds", "truncated-fds", truncatedFds),
                PackageCandidate.bytes("missing-prg", "missing-prg", unif(false)),
                PackageCandidate.bytes("bad-chunk", "bad-chunk", invalidUnifChunk)));

        RomVariant dirtyVariant = variantByFilename(result, "dirty");
        assertTrue(dirtyVariant.analysis().trainer());
        assertTrue(dirtyVariant.analysis().battery());
        assertTrue(dirtyVariant.analysis().warnings().contains(RomAnalysis.Warning.DIRTY_HEADER));
        assertEquals(CompatibilityReason.NES_SIZE_OVERFLOW,
                variantByFilename(result, "overflow-7").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED,
                variantByFilename(result, "headerless").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.FDS_TRUNCATED,
                variantByFilename(result, "truncated-headerless")
                        .compatibilityDecision().reason());
        assertEquals(CompatibilityReason.FDS_INVALID_SIDE_COUNT,
                variantByFilename(result, "zero-side").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.FDS_TRUNCATED,
                variantByFilename(result, "truncated-fds").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.UNIF_MISSING_PRG,
                variantByFilename(result, "missing-prg").compatibilityDecision().reason());
        assertEquals(CompatibilityReason.UNIF_INVALID_CHUNK,
                variantByFilename(result, "bad-chunk").compatibilityDecision().reason());
    }

    @Test
    public void rawNamesSupportMixedEncodingDuplicatesAndCaseCollisionWithoutBecomingLocators()
            throws Exception {
        byte[] payload = ines(1, 0, 0, 0, false, false, 0);
        List<RawZipEntry> entries = List.of(
                rawEntry("Éclair.nes".getBytes(StandardCharsets.UTF_8), 0x0800, payload),
                rawEntry("Grüße.nes".getBytes(Charset.forName("IBM437")), 0, payload),
                rawEntry("魂斗罗.nes".getBytes(Charset.forName("GB18030")), 0, payload),
                rawEntry("SAME.nes".getBytes(StandardCharsets.US_ASCII), 0, payload),
                rawEntry("same.nes".getBytes(StandardCharsets.US_ASCII), 0, payload),
                rawEntry("same.nes".getBytes(StandardCharsets.US_ASCII), 0, payload));
        ScanResult result = scanner().scan(source(), List.of(
                PackageCandidate.bytes("mixed-encoding", "mixed.zip", rawZip(entries))));

        assertEquals(6, result.packages().get(0).variants().size());
        assertTrue(result.packages().get(0).variants().stream()
                .anyMatch(item -> "Éclair.nes".equals(item.entryPath())));
        assertTrue(result.packages().get(0).variants().stream()
                .anyMatch(item -> "Grüße.nes".equals(item.entryPath())));
        assertTrue(result.packages().get(0).variants().stream()
                .anyMatch(item -> "魂斗罗.nes".equals(item.entryPath())));
        assertTrue(result.issues().stream().anyMatch(item ->
                item.code() == ScanIssue.Code.DUPLICATE_ENTRY_NAME));
        assertTrue(result.issues().stream().anyMatch(item ->
                item.code() == ScanIssue.Code.CASE_COLLISION));
        assertEquals(6, result.packages().get(0).variants().stream()
                .map(item -> item.zipEntryIdentity().localHeaderOffset())
                .distinct().count());
    }

    @Test
    public void unsafeRawPathsAreAccountedAndCentralLocalNameMismatchAndEncryptionAreRejected()
            throws Exception {
        byte[] payload = ines(1, 0, 0, 0, false, false, 0);
        byte[] unsafe = rawZip(List.of(
                rawEntry(new byte[]{'n', 'u', 'l', 0, '.', 'n', 'e', 's'}, 0, payload),
                rawEntry("dot/./game.nes".getBytes(StandardCharsets.US_ASCII), 0, payload),
                rawEntry("back\\..\\game.nes".getBytes(StandardCharsets.US_ASCII), 0, payload),
                rawEntry("\\root.nes".getBytes(StandardCharsets.US_ASCII), 0, payload)));
        ScanResult unsafeResult = scanner().scan(source(), List.of(
                PackageCandidate.bytes("unsafe-raw", "unsafe.zip", unsafe)));
        assertEquals(4, unsafeResult.entryOutcomes().size());
        assertTrue(unsafeResult.entryOutcomes().stream().allMatch(item ->
                item.reason() == EntryOutcome.Reason.INVALID_PATH));

        byte[] hiddenTraversal = zipWithUnicodePath(
                "../hidden.nes", "safe-display.nes", payload);
        ScanResult hiddenTraversalResult = scanner().scan(source(), List.of(
                PackageCandidate.bytes(
                        "hidden-traversal", "hidden-traversal.zip", hiddenTraversal)));
        assertEquals(EntryOutcome.Reason.INVALID_PATH,
                hiddenTraversalResult.entryOutcomes().get(0).reason());
        assertEquals(PackageOutcome.Status.SKIPPED,
                hiddenTraversalResult.packageOutcomes().get(0).status());

        RawZipEntry mismatch = new RawZipEntry(
                "local.nes".getBytes(StandardCharsets.US_ASCII),
                "other.nes".getBytes(StandardCharsets.US_ASCII),
                0,
                payload);
        BoundedZipArchive.ArchiveException mismatchFailure = assertThrows(
                BoundedZipArchive.ArchiveException.class,
                () -> BoundedZipArchive.fromBytes(
                        rawZip(List.of(mismatch)), ScanLimits.defaults()));
        assertEquals(BoundedZipArchive.Code.INVALID_ZIP, mismatchFailure.code());

        byte[] encrypted = rawZip(List.of(rawEntry(
                "secret.nes".getBytes(StandardCharsets.US_ASCII), 1, payload)));
        BoundedZipArchive.ArchiveException encryptedFailure = assertThrows(
                BoundedZipArchive.ArchiveException.class,
                () -> BoundedZipArchive.fromBytes(encrypted, ScanLimits.defaults()));
        assertEquals(BoundedZipArchive.Code.ENCRYPTED, encryptedFailure.code());
        ScanResult encryptedResult = scanner().scan(source(), List.of(
                PackageCandidate.bytes("encrypted", "encrypted.zip", encrypted)));
        assertEquals(PackageOutcome.Reason.INVALID_ZIP,
                encryptedResult.packageOutcomes().get(0).reason());
    }

    @Test
    public void zipAndPayloadLimitsUseStrictGreaterThanBoundaries() throws Exception {
        byte[] payload = ines(1, 0, 0, 0, false, false, 0);
        byte[] twoEntries = rawZip(List.of(
                rawEntry("a.nes".getBytes(StandardCharsets.US_ASCII), 0, payload),
                rawEntry("b.nes".getBytes(StandardCharsets.US_ASCII), 0, payload)));

        ScanLimits twoAllowed = limits(64L * ScanLimits.MIB, 8L * ScanLimits.MIB,
                2, 64L * ScanLimits.MIB, 1024, 200, ScanLimits.MIB);
        assertEquals(2, BoundedZipArchive.fromBytes(twoEntries, twoAllowed).entries().size());
        assertArchiveCode(BoundedZipArchive.Code.ENTRY_LIMIT_EXCEEDED, twoEntries,
                limits(64L * ScanLimits.MIB, 8L * ScanLimits.MIB,
                        1, 64L * ScanLimits.MIB, 1024, 200, ScanLimits.MIB));

        byte[] nameAtLimit = rawZip(List.of(rawEntry(
                "12345678".getBytes(StandardCharsets.US_ASCII), 0, payload)));
        assertEquals(1, BoundedZipArchive.fromBytes(nameAtLimit,
                limits(64L * ScanLimits.MIB, 8L * ScanLimits.MIB,
                        2, 64L * ScanLimits.MIB, 8, 200, ScanLimits.MIB)).entries().size());
        assertArchiveCode(BoundedZipArchive.Code.NAME_LIMIT_EXCEEDED, nameAtLimit,
                limits(64L * ScanLimits.MIB, 8L * ScanLimits.MIB,
                        2, 64L * ScanLimits.MIB, 7, 200, ScanLimits.MIB));

        byte[] smallEntries = rawZip(List.of(
                rawEntry("a".getBytes(StandardCharsets.US_ASCII), 0, new byte[4]),
                rawEntry("b".getBytes(StandardCharsets.US_ASCII), 0, new byte[4])));
        assertEquals(2, BoundedZipArchive.fromBytes(smallEntries,
                limits(1024, 1024, 2, 8, 8, 200, 1024)).entries().size());
        assertArchiveCode(BoundedZipArchive.Code.INFLATED_LIMIT_EXCEEDED, smallEntries,
                limits(1024, 1024, 2, 7, 8, 200, 1024));

        ScanLimits payloadBoundary = limits(
                64L * ScanLimits.MIB, payload.length, 8, 64L * ScanLimits.MIB,
                1024, 200, ScanLimits.MIB);
        ScanResult payloadResult = new RomPackageScanner(payloadBoundary).scan(source(), List.of(
                PackageCandidate.bytes("payload-equal", "equal.nes", payload),
                PackageCandidate.bytes("payload-plus", "plus.nes",
                        Arrays.copyOf(payload, payload.length + 1))));
        assertEquals(PackageOutcome.Status.INDEXED,
                outcome(payloadResult, StableIds.packageId(source().id(), "payload-equal")).status());
        assertEquals(PackageOutcome.Reason.PAYLOAD_LIMIT_EXCEEDED,
                outcome(payloadResult, StableIds.packageId(source().id(), "payload-plus")).reason());
    }

    @Test
    public void compressionRatioGuardStartsOnlyAboveThresholdAndUsesDeclaredRatio()
            throws Exception {
        byte[] atThreshold = zip(StandardCharsets.UTF_8, List.of(
                entry("equal.bin", new byte[1024])));
        byte[] aboveThreshold = zip(StandardCharsets.UTF_8, List.of(
                entry("plus.bin", new byte[1025])));
        ScanLimits ratioOne = limits(
                1024 * 1024, 1024 * 1024, 2, 1024 * 1024,
                128, 1, 1024);

        assertEquals(1, BoundedZipArchive.fromBytes(atThreshold, ratioOne).entries().size());
        assertArchiveCode(BoundedZipArchive.Code.RATIO_LIMIT_EXCEEDED,
                aboveThreshold, ratioOne);

        ScanLimits unrestricted = limits(
                1024 * 1024, 1024 * 1024, 2, 1024 * 1024,
                128, Integer.MAX_VALUE, 0);
        BoundedZipArchive.Entry metadata = BoundedZipArchive
                .fromBytes(aboveThreshold, unrestricted).entries().get(0);
        int minimumAllowedRatio = (int) ((metadata.uncompressedSize()
                + metadata.compressedSize() - 1) / metadata.compressedSize());
        assertEquals(1, BoundedZipArchive.fromBytes(aboveThreshold,
                limits(1024 * 1024, 1024 * 1024, 2, 1024 * 1024,
                        128, minimumAllowedRatio, 0)).entries().size());
        assertArchiveCode(BoundedZipArchive.Code.RATIO_LIMIT_EXCEEDED,
                aboveThreshold,
                limits(1024 * 1024, 1024 * 1024, 2, 1024 * 1024,
                        128, minimumAllowedRatio - 1, 0));
    }

    private static RomPackageScanner scanner() {
        return new RomPackageScanner(ScanLimits.defaults());
    }

    private static RomSource source() {
        return new RomSource(
                "source", RomSource.Type.BUILTIN, "source://builtin",
                RomSource.PermissionState.NOT_REQUIRED);
    }

    private static RomVariant variantByFilename(ScanResult result, String filename) {
        return result.packages().stream()
                .filter(item -> item.originalFilename().equals(filename))
                .findFirst().orElseThrow().variants().get(0);
    }

    private static PackageOutcome outcome(ScanResult result, String packageId) {
        return result.packageOutcomes().stream()
                .filter(item -> item.packageId().equals(packageId))
                .findFirst().orElseThrow();
    }

    private static boolean hasEntryReason(ScanResult result, EntryOutcome.Reason reason) {
        return result.entryOutcomes().stream().anyMatch(item -> item.reason() == reason);
    }

    private static byte[] ines(
            int prgUnits,
            int chrUnits,
            int flags6,
            int flags7,
            boolean trainer,
            boolean dirty,
            int trailing) {
        int actualFlags6 = trainer ? flags6 | 0x04 : flags6 & ~0x04;
        int expected = 16 + (trainer ? 512 : 0) + prgUnits * 16384 + chrUnits * 8192;
        byte[] data = new byte[expected + trailing];
        data[0] = 'N';
        data[1] = 'E';
        data[2] = 'S';
        data[3] = 0x1A;
        data[4] = (byte) prgUnits;
        data[5] = (byte) chrUnits;
        data[6] = (byte) actualFlags6;
        data[7] = (byte) flags7;
        if (dirty) {
            data[12] = 1;
        }
        return data;
    }

    private static byte[] nes2(
            int prgLow,
            int chrLow,
            int prgHigh,
            int chrHigh,
            int mapper,
            int submapper,
            boolean trainer,
            int trailing) {
        long prg = ((long) prgHigh << 8 | prgLow) * 16384L;
        long chr = ((long) chrHigh << 8 | chrLow) * 8192L;
        byte[] data = new byte[Math.toIntExact(16L + (trainer ? 512 : 0) + prg + chr + trailing)];
        data[0] = 'N'; data[1] = 'E'; data[2] = 'S'; data[3] = 0x1A;
        data[4] = (byte) prgLow;
        data[5] = (byte) chrLow;
        data[6] = (byte) (((mapper & 0x0F) << 4) | (trainer ? 0x04 : 0));
        data[7] = (byte) ((mapper & 0xF0) | 0x08);
        data[8] = (byte) ((submapper << 4) | ((mapper >>> 8) & 0x0F));
        data[9] = (byte) ((chrHigh << 4) | prgHigh);
        return data;
    }

    private static byte[] nes2Exponential(
            int prgExponent,
            int prgMultiplierBits,
            int chrExponent,
            int chrMultiplierBits,
            boolean trainer,
            int trailing) {
        long prg = exponentialSize(prgExponent, prgMultiplierBits);
        long chr = exponentialSize(chrExponent, chrMultiplierBits);
        long size = 16L + (trainer ? 512 : 0) + prg + chr + trailing;
        byte[] data = new byte[size > Integer.MAX_VALUE ? 16 : (int) size];
        data[0] = 'N'; data[1] = 'E'; data[2] = 'S'; data[3] = 0x1A;
        data[4] = (byte) ((prgExponent << 2) | prgMultiplierBits);
        data[5] = (byte) ((chrExponent << 2) | chrMultiplierBits);
        data[6] = (byte) (trainer ? 0x04 : 0);
        data[7] = 0x08;
        data[9] = (byte) 0xFF;
        return data;
    }

    private static long exponentialSize(int exponent, int multiplierBits) {
        if (exponent >= 63) {
            return Long.MAX_VALUE;
        }
        return (1L << exponent) * (multiplierBits * 2L + 1L);
    }

    private static byte[] headeredFds(int sides) {
        byte[] data = new byte[16 + 65_500 * sides];
        data[0] = 'F'; data[1] = 'D'; data[2] = 'S'; data[3] = 0x1A;
        data[4] = (byte) sides;
        for (int side = 0; side < sides; side++) {
            byte[] signature = new byte[]{1, '*', 'N', 'I', 'N', 'T', 'E', 'N', 'D', 'O', '-', 'H', 'V', 'C', '*'};
            System.arraycopy(signature, 0, data, 16 + side * 65_500, signature.length);
        }
        return data;
    }

    private static byte[] unif(boolean includePrg) {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        output.writeBytes(new byte[]{'U', 'N', 'I', 'F'});
        output.writeBytes(new byte[28]);
        if (includePrg) {
            output.writeBytes(new byte[]{'P', 'R', 'G', '0', 4, 0, 0, 0, 1, 2, 3, 4});
        }
        return output.toByteArray();
    }

    private static byte[] gameBoy() {
        byte[] data = new byte[0x150];
        byte[] logo = new byte[]{
                (byte) 0xCE, (byte) 0xED, 0x66, 0x66, (byte) 0xCC, 0x0D, 0x00, 0x0B,
                0x03, 0x73, 0x00, (byte) 0x83, 0x00, 0x0C, 0x00, 0x0D,
                0x00, 0x08, 0x11, 0x1F, (byte) 0x88, (byte) 0x89, 0x00, 0x0E,
                (byte) 0xDC, (byte) 0xCC, 0x6E, (byte) 0xE6, (byte) 0xDD, (byte) 0xDD, (byte) 0xD9, (byte) 0x99,
                (byte) 0xBB, (byte) 0xBB, 0x67, 0x63, 0x6E, 0x0E, (byte) 0xEC, (byte) 0xCC,
                (byte) 0xDD, (byte) 0xDC, (byte) 0x99, (byte) 0x9F, (byte) 0xBB, (byte) 0xB9, 0x33, 0x3E};
        System.arraycopy(logo, 0, data, 0x104, logo.length);
        return data;
    }

    private static ZipFixtureEntry entry(String name, byte[] payload) {
        return new ZipFixtureEntry(name, payload, false);
    }

    private static ZipFixtureEntry directory(String name) {
        return new ZipFixtureEntry(name, new byte[0], true);
    }

    private static RawZipEntry rawEntry(byte[] rawName, int flags, byte[] payload) {
        return new RawZipEntry(rawName, rawName, flags, payload);
    }

    /** Minimal deterministic STORED ZIP fixture with independently controlled raw names. */
    private static byte[] rawZip(List<RawZipEntry> entries) {
        ByteArrayOutputStream locals = new ByteArrayOutputStream();
        ArrayList<RawCentralEntry> centralEntries = new ArrayList<>();
        for (RawZipEntry entry : entries) {
            int localOffset = locals.size();
            CRC32 crc = new CRC32();
            crc.update(entry.payload());
            writeU32(locals, 0x04034B50L);
            writeU16(locals, 20);
            writeU16(locals, entry.flags());
            writeU16(locals, 0);
            writeU16(locals, 0);
            writeU16(locals, 0);
            writeU32(locals, crc.getValue());
            writeU32(locals, entry.payload().length);
            writeU32(locals, entry.payload().length);
            writeU16(locals, entry.localRawName().length);
            writeU16(locals, 0);
            locals.write(entry.localRawName(), 0, entry.localRawName().length);
            locals.write(entry.payload(), 0, entry.payload().length);
            centralEntries.add(new RawCentralEntry(entry, localOffset, crc.getValue()));
        }

        ByteArrayOutputStream central = new ByteArrayOutputStream();
        for (RawCentralEntry item : centralEntries) {
            RawZipEntry entry = item.entry();
            writeU32(central, 0x02014B50L);
            writeU16(central, 20);
            writeU16(central, 20);
            writeU16(central, entry.flags());
            writeU16(central, 0);
            writeU16(central, 0);
            writeU16(central, 0);
            writeU32(central, item.crc32());
            writeU32(central, entry.payload().length);
            writeU32(central, entry.payload().length);
            writeU16(central, entry.centralRawName().length);
            writeU16(central, 0);
            writeU16(central, 0);
            writeU16(central, 0);
            writeU16(central, 0);
            writeU32(central, 0);
            writeU32(central, item.localOffset());
            central.write(entry.centralRawName(), 0, entry.centralRawName().length);
        }

        ByteArrayOutputStream archive = new ByteArrayOutputStream();
        byte[] localBytes = locals.toByteArray();
        byte[] centralBytes = central.toByteArray();
        archive.write(localBytes, 0, localBytes.length);
        archive.write(centralBytes, 0, centralBytes.length);
        writeU32(archive, 0x06054B50L);
        writeU16(archive, 0);
        writeU16(archive, 0);
        writeU16(archive, entries.size());
        writeU16(archive, entries.size());
        writeU32(archive, centralBytes.length);
        writeU32(archive, localBytes.length);
        writeU16(archive, 0);
        return archive.toByteArray();
    }

    private static void writeU16(ByteArrayOutputStream output, int value) {
        output.write(value & 0xFF);
        output.write((value >>> 8) & 0xFF);
    }

    private static void writeU32(ByteArrayOutputStream output, long value) {
        output.write((int) value & 0xFF);
        output.write((int) (value >>> 8) & 0xFF);
        output.write((int) (value >>> 16) & 0xFF);
        output.write((int) (value >>> 24) & 0xFF);
    }

    private static ScanLimits limits(
            long maxPackage,
            long maxPayload,
            int maxEntries,
            long maxInflated,
            int maxName,
            int maxRatio,
            long ratioThreshold) {
        return new ScanLimits(
                maxPackage, maxPayload, maxEntries, maxInflated,
                maxName, maxRatio, ratioThreshold);
    }

    private static void assertArchiveCode(
            BoundedZipArchive.Code expected,
            byte[] archive,
            ScanLimits limits) {
        BoundedZipArchive.ArchiveException failure = assertThrows(
                BoundedZipArchive.ArchiveException.class,
                () -> BoundedZipArchive.fromBytes(archive, limits));
        assertEquals(expected, failure.code());
    }

    private static byte[] zip(Charset charset, List<ZipFixtureEntry> entries) throws IOException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(output, charset)) {
            for (ZipFixtureEntry fixture : entries) {
                ZipEntry entry = new ZipEntry(fixture.name());
                if (fixture.directory()) {
                    entry.setSize(0);
                    entry.setCrc(0);
                }
                zip.putNextEntry(entry);
                zip.write(fixture.payload());
                zip.closeEntry();
            }
        }
        return output.toByteArray();
    }

    private static byte[] zipWithUnicodePath(
            String rawPath, String unicodePath, byte[] payload) throws IOException {
        byte[] rawName = rawPath.getBytes(Charset.forName("IBM437"));
        byte[] unicode = unicodePath.getBytes(StandardCharsets.UTF_8);
        CRC32 nameCrc = new CRC32();
        nameCrc.update(rawName);
        byte[] extra = new byte[9 + unicode.length];
        extra[0] = 0x75;
        extra[1] = 0x70;
        extra[2] = (byte) (5 + unicode.length);
        extra[3] = (byte) ((5 + unicode.length) >>> 8);
        extra[4] = 1;
        long crc = nameCrc.getValue();
        for (int index = 0; index < 4; index++) {
            extra[5 + index] = (byte) (crc >>> (index * 8));
        }
        System.arraycopy(unicode, 0, extra, 9, unicode.length);

        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(output, Charset.forName("IBM437"))) {
            ZipEntry entry = new ZipEntry(rawPath);
            entry.setExtra(extra);
            zip.putNextEntry(entry);
            zip.write(payload);
            zip.closeEntry();
        }
        return output.toByteArray();
    }

    private record ZipFixtureEntry(String name, byte[] payload, boolean directory) {
    }

    private record RawZipEntry(
            byte[] localRawName,
            byte[] centralRawName,
            int flags,
            byte[] payload) {
    }

    private record RawCentralEntry(RawZipEntry entry, int localOffset, long crc32) {
    }

    private static final class ZeroThenDataInputStream extends InputStream {
        private final ByteArrayInputStream delegate;
        private boolean returnedZero;

        ZeroThenDataInputStream(byte[] data) {
            delegate = new ByteArrayInputStream(data);
        }

        @Override
        public int read(byte[] bytes, int offset, int length) {
            if (!returnedZero) {
                returnedZero = true;
                return 0;
            }
            return delegate.read(bytes, offset, length);
        }

        @Override
        public int read() {
            return delegate.read();
        }
    }
}
