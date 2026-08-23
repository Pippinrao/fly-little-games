package com.flynes.emu.launch;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertThrows;

import com.flynes.emu.RomScanner;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.data.RomIdentity;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipOutputStream;

public final class ExactRomLoaderTest {
    @Test
    public void launchRequestMapCodecRoundTripsExactLocation() {
        LaunchRequest request = zipRequest(
                "content://games/collection.zip", "nested/SECOND.nes", bytes("second"));

        Map<String, String> encoded = request.toMap();

        Map<String, String> compatibilityFixture = Map.of(
                "canonicalGameId", "game",
                "variantId", "variant",
                "sourceId", "source",
                "sourceUri", "content://games/collection.zip",
                "entryPath", "nested/SECOND.nes",
                "packageFormat", "ZIP",
                "romFormat", "INES",
                "compatibility", "PLAYABLE",
                "romSha1", identity(bytes("second")).sha1());
        assertEquals(compatibilityFixture, encoded);
        assertEquals(request, LaunchRequest.fromMap(compatibilityFixture));
        assertThrows(UnsupportedOperationException.class, () -> encoded.put("variantId", "other"));
    }

    @Test
    public void launchRequestRejectsBlankLocationAndNonPlayableCompatibility() {
        assertThrows(IllegalArgumentException.class, () -> new LaunchRequest(
                "game", "variant", "source", " ", null,
                PackageFormat.RAW_NES, RomFormat.INES, CompatibilityState.PLAYABLE,
                identity(bytes("rom"))));
        assertThrows(IllegalArgumentException.class, () -> new LaunchRequest(
                "game", "variant", "source", "content://game.nes", null,
                PackageFormat.RAW_NES, RomFormat.INES, CompatibilityState.UNKNOWN,
                identity(bytes("rom"))));
        assertThrows(IllegalArgumentException.class, () -> new LaunchRequest(
                "game", "variant", "source", "content://games.zip", " ",
                PackageFormat.ZIP, RomFormat.INES, CompatibilityState.PLAYABLE,
                identity(bytes("rom"))));
    }

    @Test
    public void zipLoadsOnlyTheExactNamedEntry() throws Exception {
        byte[] first = bytes("first-rom");
        byte[] second = bytes("second-rom");
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("first.nes", first);
        entries.put("nested/SECOND.nes", second);
        byte[] archive = zip(entries);
        ExactRomLoader loader = loaderFor(archive);

        byte[] loaded = loader.load(zipRequest(
                "content://games/collection.zip", "nested/SECOND.nes", second));

        assertArrayEquals(second, loaded);
    }

    @Test
    public void loaderPropagatesExactSourceIdAndUriToStreamOpener() throws Exception {
        byte[] rom = bytes("rom");
        String[] openedLocation = new String[2];
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> {
            openedLocation[0] = sourceId;
            openedLocation[1] = sourceUri;
            return new ByteArrayInputStream(rom);
        });
        LaunchRequest request = new LaunchRequest(
                "game", "variant", "source-exact", "content://provider/document/42", null,
                PackageFormat.RAW_NES, RomFormat.INES, CompatibilityState.PLAYABLE,
                identity(rom));

        assertArrayEquals(rom, loader.load(request));
        assertEquals("source-exact", openedLocation[0]);
        assertEquals("content://provider/document/42", openedLocation[1]);
    }

    @Test
    public void zipRejectsMissingExactEntry() throws Exception {
        ExactRomLoader loader = loaderFor(zip(Map.of("game.nes", bytes("rom"))));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/collection.zip", "GAME.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_MISSING, failure.code());
    }

    @Test
    public void zipRejectsDuplicateExactEntryNames() throws Exception {
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("one.nes", bytes("first"));
        entries.put("two.nes", bytes("second"));
        byte[] duplicateArchive = replaceAscii(zip(entries), "one.nes", "rom.nes");
        duplicateArchive = replaceAscii(duplicateArchive, "two.nes", "rom.nes");
        ExactRomLoader loader = loaderFor(duplicateArchive);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/collection.zip", "rom.nes", bytes("first"))));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_DUPLICATE, failure.code());
    }

    @Test
    public void zipRejectsDirectoryAsPayload() throws Exception {
        byte[] archive;
        try (ByteArrayOutputStream bytes = new ByteArrayOutputStream();
             ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry("game.nes/"));
            zip.closeEntry();
            zip.finish();
            archive = bytes.toByteArray();
        }
        ExactRomLoader loader = loaderFor(archive);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/collection.zip", "game.nes/", new byte[0])));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_IS_DIRECTORY, failure.code());
    }

    @Test
    public void rawPayloadOverScannerLimitIsRejected() {
        byte[] oversized = new byte[(int) RomScanner.MAX_ROM_BYTES + 1];
        ExactRomLoader loader = loaderFor(oversized);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(rawRequest("content://games/large.nes", identity(oversized))));

        assertEquals(ExactRomLoader.ErrorCode.PAYLOAD_TOO_LARGE, failure.code());
    }

    @Test
    public void rawPayloadAtExactScannerLimitIsAccepted() throws Exception {
        byte[] exactLimit = new byte[(int) RomScanner.MAX_ROM_BYTES];
        exactLimit[0] = 'N';

        byte[] loaded = loaderFor(exactLimit).load(rawRequest(
                "content://games/exact.nes", identity(exactLimit)));

        assertEquals(RomScanner.MAX_ROM_BYTES, loaded.length);
        assertEquals('N', loaded[0]);
    }

    @Test
    public void zipPayloadAtExactScannerLimitIsAccepted() throws Exception {
        byte[] exactLimit = new byte[(int) RomScanner.MAX_ROM_BYTES];
        exactLimit[0] = 'N';
        ExactRomLoader loader = loaderFor(zip(Map.of("game.nes", exactLimit)));

        byte[] loaded = loader.load(zipRequest(
                "content://games/exact.zip", "game.nes", exactLimit));

        assertEquals(RomScanner.MAX_ROM_BYTES, loaded.length);
        assertEquals('N', loaded[0]);
    }

    @Test
    public void compressedZipBombPayloadOverScannerLimitIsRejected() throws Exception {
        byte[] oversized = new byte[(int) RomScanner.MAX_ROM_BYTES + 1];
        ExactRomLoader loader = loaderFor(zip(Map.of("game.nes", oversized)));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/bomb.zip", "game.nes", oversized)));

        assertEquals(ExactRomLoader.ErrorCode.PAYLOAD_TOO_LARGE, failure.code());
    }

    @Test
    public void largeSkippedEntryCountsAgainstTotalInflatedLimit() throws Exception {
        byte[] skipped = new byte[(int) ExactRomLoader.MAX_ZIP_INFLATED_BYTES];
        byte[] target = bytes("rom");
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("skipped.bin", skipped);
        entries.put("game.nes", target);
        ExactRomLoader loader = loaderFor(zip(entries));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/skipped.zip", "game.nes", target)));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_INFLATED_LIMIT_EXCEEDED, failure.code());
    }

    @Test
    public void oversizedSkippedEntryAfterTargetStillCountsAgainstInflatedLimit()
            throws Exception {
        byte[] target = bytes("rom");
        byte[] skipped = new byte[(int) ExactRomLoader.MAX_ZIP_INFLATED_BYTES];
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("game.nes", target);
        entries.put("skipped.bin", skipped);
        ExactRomLoader loader = loaderFor(zip(entries));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/skipped-after.zip", "game.nes", target)));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_INFLATED_LIMIT_EXCEEDED, failure.code());
    }

    @Test
    public void tooManyZipEntriesAreRejectedEvenWhenTargetAppearsFirst() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = zipWithEmptyEntries(
                ExactRomLoader.MAX_ZIP_ENTRIES, "game.nes", target);
        ExactRomLoader loader = loaderFor(archive);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/many.zip", "game.nes", target)));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_LIMIT_EXCEEDED, failure.code());
    }

    @Test
    public void declaredEntryCountLimitShortCircuitsBeforeCentralDirectoryTraversal()
            throws Exception {
        byte[] archive = zip(Map.of());
        archive[8] = (byte) 0xFF;
        archive[9] = (byte) 0xFF;
        archive[10] = (byte) 0xFF;
        archive[11] = (byte) 0xFF;

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loaderFor(archive).load(zipRequest(
                        "content://games/declared-many.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_LIMIT_EXCEEDED, failure.code());
    }

    @Test
    public void zipEntryCountAtExactLimitIsAccepted() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = zipWithEmptyEntries(
                ExactRomLoader.MAX_ZIP_ENTRIES - 1, "game.nes", target);
        ExactRomLoader loader = loaderFor(archive);

        assertArrayEquals(target, loader.load(zipRequest(
                "content://games/entries-limit.zip", "game.nes", target)));
    }

    @Test
    public void cumulativeInflatedBytesAtExactLimitAreAccepted() throws Exception {
        byte[] target = bytes("rom");
        byte[] skipped = new byte[(int) ExactRomLoader.MAX_ZIP_INFLATED_BYTES - target.length];
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("skipped.bin", skipped);
        entries.put("game.nes", target);
        ExactRomLoader loader = loaderFor(zip(entries));

        assertArrayEquals(target, loader.load(zipRequest(
                "content://games/inflated-limit.zip", "game.nes", target)));
    }

    @Test
    public void zipSourceBytesAreBoundedBeforeTraversal() {
        byte[] oversizedSource = new byte[(int) ExactRomLoader.MAX_ZIP_SOURCE_BYTES + 1];
        oversizedSource[0] = 'P';
        oversizedSource[1] = 'K';
        oversizedSource[2] = 3;
        oversizedSource[3] = 4;
        ExactRomLoader loader = loaderFor(oversizedSource);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/huge.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_SOURCE_LIMIT_EXCEEDED, failure.code());
    }

    @Test
    public void zipSourceAtExactByteLimitIsAccepted() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = storedZipAtSize(
                (int) ExactRomLoader.MAX_ZIP_SOURCE_BYTES, target);
        ExactRomLoader loader = loaderFor(archive);

        assertArrayEquals(target, loader.load(zipRequest(
                "content://games/source-limit.zip", "game.nes", target)));
    }

    @Test
    public void zipWithLocalEntryButNoCentralDirectoryIsInvalid() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = zip(Map.of("game.nes", target));
        int centralDirectory = indexOfSignature(archive, 0x50, 0x4B, 0x01, 0x02);
        byte[] truncated = Arrays.copyOf(archive, centralDirectory);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loaderFor(truncated).load(zipRequest(
                        "content://games/truncated.zip", "game.nes", target)));

        assertEquals(ExactRomLoader.ErrorCode.INVALID_ZIP, failure.code());
    }

    @Test
    public void fakeEndRecordAndSplitMarkerAreInvalidZip() {
        byte[][] malformedArchives = {
                {(byte) 0x50, (byte) 0x4B, (byte) 0x05, (byte) 0x06},
                {(byte) 0x50, (byte) 0x4B, (byte) 0x07, (byte) 0x08}
        };

        for (byte[] archive : malformedArchives) {
            ExactRomLoader.LoadException failure = assertThrows(
                    ExactRomLoader.LoadException.class,
                    () -> loaderFor(archive).load(zipRequest(
                            "content://games/fake.zip", "game.nes", bytes("rom"))));
            assertEquals(ExactRomLoader.ErrorCode.INVALID_ZIP, failure.code());
        }
    }

    @Test
    public void validEmptyZipReportsMissingEntry() throws Exception {
        byte[] archive = zip(Map.of());

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loaderFor(archive).load(zipRequest(
                        "content://games/empty.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.ZIP_ENTRY_MISSING, failure.code());
    }

    @Test
    public void centralCrcMustMatchSelectedAndSkippedInflatedPayloads() throws Exception {
        byte[] selected = bytes("selected-rom");
        LinkedHashMap<String, byte[]> entries = new LinkedHashMap<>();
        entries.put("game.nes", selected);
        entries.put("skipped.bin", bytes("skipped-data"));
        byte[] archive = zip(entries);

        assertCentralMutationInvalid(archive, "game.nes", 16, "game.nes", selected);
        assertCentralMutationInvalid(archive, "skipped.bin", 16, "game.nes", selected);
    }

    @Test
    public void centralCompressedAndUncompressedSizesMustMatchStreamedEntry()
            throws Exception {
        byte[] selected = bytes("selected-rom");
        byte[] archive = zip(Map.of("game.nes", selected));

        assertCentralMutationInvalid(archive, "game.nes", 20, "game.nes", selected);
        assertCentralMutationInvalid(archive, "game.nes", 24, "game.nes", selected);
    }

    @Test
    public void centralCompressionMethodMustMatchLocalHeader() throws Exception {
        byte[] selected = bytes("selected-rom");
        byte[] archive = zip(Map.of("game.nes", selected));

        assertCentralMutationInvalid(archive, "game.nes", 10, "game.nes", selected);
    }

    @Test
    public void centralFlagsMustMatchLocalHeader() throws Exception {
        byte[] selected = bytes("selected-rom");
        byte[] archive = zip(Map.of("game.nes", selected));

        assertCentralMutationInvalid(archive, "game.nes", 8, "game.nes", selected);
    }

    @Test
    public void malformedNonZipSourceIsClassifiedAsInvalidZip() {
        ExactRomLoader loader = loaderFor(bytes("not a zip"));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/broken.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.INVALID_ZIP, failure.code());
    }

    @Test
    public void sourceZipExceptionIsClassifiedAsIoErrorNotInvalidArchive() {
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> new InputStream() {
            @Override
            public int read() throws IOException {
                throw new ZipException("provider stream failed");
            }

            @Override
            public int read(byte[] buffer, int offset, int length) throws IOException {
                throw new ZipException("provider stream failed");
            }
        });

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/read-failure.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.IO_ERROR, failure.code());
    }

    @Test
    public void sourceCloseZipExceptionIsClassifiedAsIoError() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = zip(Map.of("game.nes", target));
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> new InputStream() {
            private final ByteArrayInputStream delegate = new ByteArrayInputStream(archive);

            @Override
            public int read() {
                return delegate.read();
            }

            @Override
            public int read(byte[] buffer, int offset, int length) {
                return delegate.read(buffer, offset, length);
            }

            @Override
            public void close() throws IOException {
                throw new ZipException("provider close failed");
            }
        });

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/close-failure.zip", "game.nes", target)));

        assertEquals(ExactRomLoader.ErrorCode.IO_ERROR, failure.code());
    }

    @Test
    public void sourceReadSecurityExceptionIsClassifiedAsIoError() {
        ExactRomLoader loader = new ExactRomLoader((sourceId, sourceUri) -> new InputStream() {
            @Override
            public int read() {
                throw new SecurityException("provider permission changed");
            }

            @Override
            public int read(byte[] buffer, int offset, int length) {
                throw new SecurityException("provider permission changed");
            }
        });

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(zipRequest(
                        "content://games/security-failure.zip", "game.nes", bytes("rom"))));

        assertEquals(ExactRomLoader.ErrorCode.IO_ERROR, failure.code());
    }

    @Test
    public void malformedUtf8EntryNameIsClassifiedAsInvalidZip() throws Exception {
        byte[] target = bytes("rom");
        byte[] archive = replaceAsciiWithInvalidUtf8(zip(Map.of("ab", target)), "ab");

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loaderFor(archive).load(zipRequest(
                        "content://games/bad-name.zip", "ab", target)));

        assertEquals(ExactRomLoader.ErrorCode.INVALID_ZIP, failure.code());
    }

    @Test
    public void sha1MismatchIsRejected() {
        ExactRomLoader loader = loaderFor(bytes("actual"));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(rawRequest(
                        "content://games/game.nes", identity(bytes("expected")))));

        assertEquals(ExactRomLoader.ErrorCode.SHA1_MISMATCH, failure.code());
    }

    @Test
    public void executableEntryIsNeverTreatedAsPlayable() throws Exception {
        byte[] executable = bytes("MZ-not-a-rom");
        ExactRomLoader loader = loaderFor(zip(Map.of("evil.EXE", executable)));

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(new LaunchRequest(
                        "game", "variant", "source", "content://games/archive.zip", "evil.EXE",
                        PackageFormat.ZIP, RomFormat.UNKNOWN, CompatibilityState.PLAYABLE,
                        identity(executable))));

        assertEquals(ExactRomLoader.ErrorCode.EXECUTABLE_REJECTED, failure.code());
    }

    @Test
    public void executablePayloadIsRejectedWhenRawContentUriIsOpaque() {
        byte[] executable = bytes("MZ-not-a-rom");
        ExactRomLoader loader = loaderFor(executable);

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loader.load(rawRequest(
                        "content://provider/document/42", identity(executable))));

        assertEquals(ExactRomLoader.ErrorCode.EXECUTABLE_REJECTED, failure.code());
    }

    private static ExactRomLoader loaderFor(byte[] payload) {
        return new ExactRomLoader((sourceId, sourceUri) -> new ByteArrayInputStream(payload));
    }

    private static LaunchRequest rawRequest(String uri, RomIdentity identity) {
        return new LaunchRequest(
                "game", "variant", "source", uri, null,
                PackageFormat.RAW_NES, RomFormat.INES, CompatibilityState.PLAYABLE, identity);
    }

    private static LaunchRequest zipRequest(String uri, String entry, byte[] bytes) {
        return new LaunchRequest(
                "game", "variant", "source", uri, entry,
                PackageFormat.ZIP, RomFormat.INES, CompatibilityState.PLAYABLE, identity(bytes));
    }

    private static byte[] zip(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> entry : entries.entrySet()) {
                zip.putNextEntry(new ZipEntry(entry.getKey()));
                zip.write(entry.getValue());
                zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static byte[] zipWithEmptyEntries(
            int emptyEntryCount,
            String targetName,
            byte[] target) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            zip.putNextEntry(new ZipEntry(targetName));
            zip.write(target);
            zip.closeEntry();
            for (int i = 0; i < emptyEntryCount; i++) {
                zip.putNextEntry(new ZipEntry("empty-" + i));
                zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static byte[] storedZipAtSize(int exactSize, byte[] target) throws IOException {
        byte[] emptyArchive = storedZip(Map.of(
                "game.nes", target,
                "padding.bin", new byte[0]));
        int paddingSize = exactSize - emptyArchive.length;
        if (paddingSize < 0) {
            throw new IllegalArgumentException("exact ZIP size is too small");
        }
        byte[] result = storedZip(Map.of(
                "game.nes", target,
                "padding.bin", new byte[paddingSize]));
        assertEquals(exactSize, result.length);
        return result;
    }

    private static byte[] storedZip(Map<String, byte[]> entries) throws IOException {
        ByteArrayOutputStream bytes = new ByteArrayOutputStream();
        try (ZipOutputStream zip = new ZipOutputStream(bytes)) {
            for (Map.Entry<String, byte[]> value : entries.entrySet()) {
                CRC32 crc = new CRC32();
                crc.update(value.getValue());
                ZipEntry entry = new ZipEntry(value.getKey());
                entry.setMethod(ZipEntry.STORED);
                entry.setSize(value.getValue().length);
                entry.setCompressedSize(value.getValue().length);
                entry.setCrc(crc.getValue());
                zip.putNextEntry(entry);
                zip.write(value.getValue());
                zip.closeEntry();
            }
        }
        return bytes.toByteArray();
    }

    private static void assertCentralMutationInvalid(
            byte[] archive,
            String mutatedEntry,
            int fieldOffset,
            String requestedEntry,
            byte[] requestedPayload) {
        byte[] tampered = archive.clone();
        int centralHeader = findCentralHeader(tampered, mutatedEntry);
        tampered[centralHeader + fieldOffset] ^= 0x01;

        ExactRomLoader.LoadException failure = assertThrows(
                ExactRomLoader.LoadException.class,
                () -> loaderFor(tampered).load(zipRequest(
                        "content://games/tampered.zip", requestedEntry, requestedPayload)));
        assertEquals(ExactRomLoader.ErrorCode.INVALID_ZIP, failure.code());
    }

    private static int findCentralHeader(byte[] archive, String entryName) {
        byte[] expectedName = entryName.getBytes(StandardCharsets.UTF_8);
        for (int offset = 0; offset <= archive.length - 46; offset++) {
            if ((archive[offset] & 0xFF) != 0x50
                    || (archive[offset + 1] & 0xFF) != 0x4B
                    || (archive[offset + 2] & 0xFF) != 0x01
                    || (archive[offset + 3] & 0xFF) != 0x02) {
                continue;
            }
            int nameLength = (archive[offset + 28] & 0xFF)
                    | ((archive[offset + 29] & 0xFF) << 8);
            if (nameLength != expectedName.length || offset + 46 + nameLength > archive.length) {
                continue;
            }
            boolean matches = true;
            for (int i = 0; i < nameLength; i++) {
                if (archive[offset + 46 + i] != expectedName[i]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return offset;
            }
        }
        throw new AssertionError("central entry was not found: " + entryName);
    }

    private static int indexOfSignature(byte[] source, int... signature) {
        for (int i = 0; i <= source.length - signature.length; i++) {
            boolean matches = true;
            for (int j = 0; j < signature.length; j++) {
                if ((source[i + j] & 0xFF) != signature[j]) {
                    matches = false;
                    break;
                }
            }
            if (matches) {
                return i;
            }
        }
        throw new AssertionError("ZIP signature was not found");
    }

    private static byte[] replaceAscii(byte[] source, String from, String to) {
        if (from.length() != to.length()) {
            throw new IllegalArgumentException("replacement must preserve ZIP field lengths");
        }
        byte[] result = source.clone();
        byte[] needle = from.getBytes(StandardCharsets.US_ASCII);
        byte[] replacement = to.getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i <= result.length - needle.length; i++) {
            boolean match = true;
            for (int j = 0; j < needle.length; j++) {
                if (result[i + j] != needle[j]) {
                    match = false;
                    break;
                }
            }
            if (match) {
                System.arraycopy(replacement, 0, result, i, replacement.length);
                i += replacement.length - 1;
            }
        }
        return result;
    }

    private static byte[] replaceAsciiWithInvalidUtf8(byte[] source, String name) {
        if (name.length() != 2) {
            throw new IllegalArgumentException("fixture name must contain two ASCII bytes");
        }
        byte[] result = source.clone();
        byte[] needle = name.getBytes(StandardCharsets.US_ASCII);
        for (int i = 0; i <= result.length - needle.length; i++) {
            if (result[i] == needle[0] && result[i + 1] == needle[1]) {
                result[i] = (byte) 0xC3;
                result[i + 1] = 0x28;
                i++;
            }
        }
        return result;
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
}
