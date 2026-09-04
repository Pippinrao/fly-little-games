package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import org.junit.Test;

import java.io.IOException;
import java.io.InputStream;
import java.net.URISyntaxException;
import java.net.URL;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

/** Freezes ZIP opening and exposed metadata independently of payload inflation. */
public final class BoundedZipOpenFixtureParityTest {
    private static final String FIXTURE_ROOT = "/zip/open/v1/";
    private static final String MANIFEST_HEADER = String.join("\t",
            "schema_version",
            "case_id",
            "blob",
            "sha256",
            "max_package_bytes",
            "max_payload_bytes",
            "max_zip_entries",
            "max_cumulative_inflated_bytes",
            "max_name_bytes",
            "max_compression_ratio",
            "ratio_guard_threshold_bytes",
            "outcome",
            "error_code",
            "error_message",
            "entries");

    @Test
    public void currentJavaZipOpenMatchesVersionOneSharedFixtures() throws Exception {
        URL rootResource = BoundedZipOpenFixtureParityTest.class.getResource(FIXTURE_ROOT);
        assertNotNull("missing shared ZIP fixture directory", rootResource);
        assertEquals("shared ZIP fixtures must be ordinary test resources",
                "file", rootResource.getProtocol());
        Path root = Paths.get(rootResource.toURI());

        List<Fixture> fixtures = loadManifest(root);
        assertEquals("manifest fixture count", 71, fixtures.size());
        assertExactCorpus(root, fixtures);

        Set<String> caseIds = new HashSet<>();
        for (Fixture fixture : fixtures) {
            assertTrue("duplicate case ID " + fixture.caseId, caseIds.add(fixture.caseId));
            byte[] archive = Files.readAllBytes(root.resolve(fixture.blob));
            assertEquals(fixture.caseId + " SHA-256", fixture.sha256, sha256(archive));

            ScanLimits limits = new ScanLimits(
                    fixture.maxPackageBytes,
                    fixture.maxPayloadBytes,
                    fixture.maxZipEntries,
                    fixture.maxCumulativeInflatedBytes,
                    fixture.maxNameBytes,
                    fixture.maxCompressionRatio,
                    fixture.ratioGuardThresholdBytes);
            if (fixture.outcome.equals("ERROR")) {
                BoundedZipArchive.ArchiveException error = assertThrows(
                        fixture.caseId,
                        BoundedZipArchive.ArchiveException.class,
                        () -> BoundedZipArchive.fromBytes(archive, limits));
                assertEquals(fixture.caseId + " error code",
                        fixture.errorCode, error.code().name());
                assertEquals(fixture.caseId + " error message",
                        fixture.errorMessage, error.getMessage());
                continue;
            }

            BoundedZipArchive.Archive opened;
            try {
                opened = BoundedZipArchive.fromBytes(archive, limits);
            } catch (BoundedZipArchive.ArchiveException error) {
                throw new AssertionError(fixture.caseId + " unexpectedly failed with "
                        + error.code() + ": " + error.getMessage(), error);
            }
            assertEquals(fixture.caseId + " entry count",
                    fixture.entries.size(), opened.entries().size());
            for (int index = 0; index < fixture.entries.size(); index++) {
                ExpectedEntry expected = fixture.entries.get(index);
                BoundedZipArchive.Entry actual = opened.entries().get(index);
                String label = fixture.caseId + " entry " + index;
                assertArrayEquals(label + " raw name", expected.rawName, actual.rawName());
                assertArrayEquals(label + " identity raw name",
                        expected.rawName, actual.identity().rawNameBytes());
                assertArrayEquals(label + " central extra",
                        expected.centralExtra, actual.centralExtra());
                assertEquals(label + " local-header offset",
                        expected.localHeaderOffset, actual.identity().localHeaderOffset());
                assertEquals(label + " flags", expected.flags, actual.flags());
                assertEquals(label + " method", expected.method, actual.method());
                assertEquals(label + " CRC32", expected.crc32, actual.crc32());
                assertEquals(label + " compressed size",
                        expected.compressedSize, actual.compressedSize());
                assertEquals(label + " uncompressed size",
                        expected.uncompressedSize, actual.uncompressedSize());
                assertEquals(label + " directory", expected.directory, actual.isDirectory());
            }
        }
        assertRequiredCases(caseIds);
    }

    @Test
    public void fixtureManifestNumbersRequireCanonicalUnsignedDecimal() {
        for (String value : List.of("+1", "01", "-0")) {
            AssertionError error = assertThrows(
                    AssertionError.class,
                    () -> Fixture.parse(replaceColumn(validErrorRow(), 4, value), 2));
            assertTrue(error.getMessage(),
                    error.getMessage().contains("max_package_bytes")
                            && error.getMessage().contains("canonical unsigned decimal"));
        }
    }

    @Test
    public void fixtureManifestUsesCanonicalCaseAndBlobNames() {
        AssertionError badCase = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validErrorRow(), 1, "0case"), 2));
        assertTrue(badCase.getMessage(), badCase.getMessage().contains("case_id"));

        AssertionError badBlob = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validErrorRow(), 2, "other.zip"), 2));
        assertTrue(badBlob.getMessage(), badBlob.getMessage().contains("blob"));
    }

    @Test
    public void fixtureManifestRejectsEmptySuccessNamesAndUnstableErrorMessages() {
        String success = String.join("\t",
                "1", "case_id", "case_id.zip", "0".repeat(64),
                "1", "1", "1", "1", "1", "1", "0",
                "SUCCESS", "NONE", "NONE", "-|-|0|0|0|0|0|0|false");
        AssertionError emptyName = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(success, 2));
        assertTrue(emptyName.getMessage(),
                emptyName.getMessage().contains("raw_name")
                        && emptyName.getMessage().contains("nonempty"));

        for (String message : List.of("   ", "NONE")) {
            AssertionError unstableMessage = assertThrows(
                    AssertionError.class,
                    () -> Fixture.parse(replaceColumn(validErrorRow(), 13, message), 2));
            assertTrue(unstableMessage.getMessage(),
                    unstableMessage.getMessage().contains("error message"));
        }
    }

    private static List<Fixture> loadManifest(Path root) throws IOException {
        Path manifestPath = root.resolve("manifest.tsv");
        assertTrue("fixture manifest must be a regular non-symlink file",
                Files.isRegularFile(manifestPath, LinkOption.NOFOLLOW_LINKS)
                        && !Files.isSymbolicLink(manifestPath));
        String manifest = decodeAscii(Files.readAllBytes(manifestPath));
        assertFalse("manifest must use LF rather than CRLF", manifest.contains("\r"));
        assertTrue("manifest must end with a newline", manifest.endsWith("\n"));
        String[] lines = manifest.split("\n", -1);
        assertEquals("manifest must have no data after its final newline",
                "", lines[lines.length - 1]);
        assertEquals("manifest header", MANIFEST_HEADER, lines[0]);

        List<Fixture> fixtures = new ArrayList<>();
        Set<String> blobs = new HashSet<>();
        for (int index = 1; index < lines.length - 1; index++) {
            Fixture fixture = Fixture.parse(lines[index], index + 1);
            assertTrue("manifest line " + (index + 1) + " field 'blob': duplicate '"
                    + fixture.blob + "'", blobs.add(fixture.blob));
            fixtures.add(fixture);
        }
        assertFalse("manifest must contain at least one fixture", fixtures.isEmpty());
        return fixtures;
    }

    private static void assertExactCorpus(Path root, List<Fixture> fixtures) throws IOException {
        Set<String> expected = fixtures.stream()
                .map(fixture -> fixture.blob)
                .collect(Collectors.toCollection(HashSet::new));
        expected.add("manifest.tsv");
        Set<String> actual;
        try (var paths = Files.list(root)) {
            actual = paths.map(path -> path.getFileName().toString()).collect(Collectors.toSet());
        }
        assertEquals("fixture directory must contain exactly the manifest corpus", expected, actual);
        for (String name : expected) {
            Path path = root.resolve(name);
            assertTrue("fixture corpus entry must be a regular non-symlink file: " + name,
                    Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                            && !Files.isSymbolicLink(path));
        }
    }

    private static String decodeAscii(byte[] bytes) {
        try {
            return StandardCharsets.US_ASCII.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(java.nio.ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException error) {
            throw new AssertionError("fixture manifest must be strict ASCII", error);
        }
    }

    private static void assertRequiredCases(Set<String> caseIds) {
        Set<String> required = Set.of(
                "valid_stored_metadata",
                "valid_deflate_metadata",
                "valid_max_comment_central_reversed",
                "package_limit_exact",
                "package_limit_over",
                "entry_count_exact",
                "entry_count_over",
                "entry_count_sentinel_default_precedence",
                "entry_count_sentinel_zip64",
                "cumulative_exact_multiple",
                "cumulative_over_multiple",
                "name_limit_exact",
                "name_limit_over",
                "empty_name",
                "ratio_guard_exact_bypass",
                "ratio_guard_plus_one_checked",
                "ratio_exact_pass",
                "ratio_plus_one_fail",
                "ratio_zero_compressed_fail",
                "eocd_short",
                "eocd_missing",
                "eocd_misaligned",
                "valid_comment_fake_eocd",
                "prefix_junk",
                "ambiguous_dual_view_wide",
                "ambiguous_dual_view_entry_limit",
                "ambiguous_dual_view_name_limit",
                "ambiguous_dual_view_inflated_limit",
                "ambiguous_dual_view_ratio_limit",
                "ambiguous_dual_view_encrypted",
                "ambiguous_dual_view_unsupported_method",
                "split_archive",
                "zip64_central_size",
                "zip64_central_offset",
                "central_bounds",
                "central_signature",
                "central_truncated",
                "central_entry_truncated",
                "central_count",
                "encrypted",
                "unsupported_method",
                "invalid_efs_utf8",
                "split_central_entry",
                "zip64_local_offset",
                "local_header_truncated",
                "local_signature",
                "local_metadata_truncated",
                "local_flags_mismatch",
                "local_method_mismatch",
                "local_name_length_mismatch",
                "local_name_bytes_mismatch",
                "local_crc_mismatch",
                "local_compressed_size_mismatch",
                "local_uncompressed_size_mismatch",
                "compressed_payload_truncated",
                "descriptor_stored_unsigned",
                "descriptor_stored_signed",
                "descriptor_deflate_unsigned",
                "descriptor_deflate_signed",
                "descriptor_crc_signature_unsigned",
                "descriptor_crc_signature_signed",
                "descriptor_local_equal_metadata",
                "descriptor_local_crc_mismatch",
                "descriptor_local_compressed_mismatch",
                "descriptor_local_size_mismatch",
                "descriptor_truncated",
                "descriptor_mismatched",
                "descriptor_overlaps_central",
                "local_entries_overlap",
                "duplicate_raw_names",
                "directory_suffixes");
        assertEquals("manifest must contain the complete required ZIP-open corpus",
                required, caseIds);
    }

    private static String validErrorRow() {
        return String.join("\t",
                "1", "case_id", "case_id.zip", "0".repeat(64),
                "1", "1", "1", "1", "1", "1", "0",
                "ERROR", "INVALID_ZIP", "stable message", "NONE");
    }

    private static String replaceColumn(String row, int column, String replacement) {
        String[] fields = row.split("\t", -1);
        fields[column] = replacement;
        return String.join("\t", fields);
    }

    private static String sha256(byte[] payload) throws NoSuchAlgorithmException {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(payload);
        StringBuilder result = new StringBuilder(digest.length * 2);
        for (byte value : digest) {
            int unsigned = value & 0xFF;
            result.append(Character.forDigit(unsigned >>> 4, 16));
            result.append(Character.forDigit(unsigned & 0x0F, 16));
        }
        return result.toString();
    }

    private static byte[] parseHex(String value, int lineNumber, String field) {
        if (value.equals("-")) {
            return new byte[0];
        }
        if (!value.matches("[0-9a-f]+") || (value.length() & 1) != 0) {
            fail("manifest line " + lineNumber + " field '" + field
                    + "': expected lowercase whole-byte hex, got '" + value + "'");
        }
        byte[] result = new byte[value.length() / 2];
        for (int index = 0; index < result.length; index++) {
            result[index] = (byte) Integer.parseInt(value.substring(index * 2, index * 2 + 2), 16);
        }
        return result;
    }

    private record ExpectedEntry(
            byte[] rawName,
            byte[] centralExtra,
            int localHeaderOffset,
            int flags,
            int method,
            long crc32,
            long compressedSize,
            long uncompressedSize,
            boolean directory) {

        static ExpectedEntry parse(String value, int lineNumber, int entryIndex) {
            String[] fields = value.split("\\|", -1);
            assertEquals("manifest line " + lineNumber + " entry " + entryIndex
                    + " field count", 9, fields.length);
            String prefix = "entry_" + entryIndex + ".";
            byte[] rawName = parseHex(fields[0], lineNumber, prefix + "raw_name");
            assertTrue("manifest line " + lineNumber + " field '" + prefix
                    + "raw_name': successful entry name must be nonempty", rawName.length > 0);
            return new ExpectedEntry(
                    rawName,
                    parseHex(fields[1], lineNumber, prefix + "central_extra"),
                    parseInt(fields[2], lineNumber, prefix + "local_header_offset", 0, Integer.MAX_VALUE),
                    parseInt(fields[3], lineNumber, prefix + "flags", 0, 0xFFFF),
                    parseInt(fields[4], lineNumber, prefix + "method", 0, 0xFFFF),
                    parseLong(fields[5], lineNumber, prefix + "crc32", 0, 0xFFFFFFFFL),
                    parseLong(fields[6], lineNumber, prefix + "compressed_size", 0, 0xFFFFFFFFL),
                    parseLong(fields[7], lineNumber, prefix + "uncompressed_size", 0, 0xFFFFFFFFL),
                    parseBoolean(fields[8], lineNumber, prefix + "directory"));
        }
    }

    private record Fixture(
            String schemaVersion,
            String caseId,
            String blob,
            String sha256,
            long maxPackageBytes,
            long maxPayloadBytes,
            int maxZipEntries,
            long maxCumulativeInflatedBytes,
            int maxNameBytes,
            int maxCompressionRatio,
            long ratioGuardThresholdBytes,
            String outcome,
            String errorCode,
            String errorMessage,
            List<ExpectedEntry> entries) {

        static Fixture parse(String line, int lineNumber) {
            String[] values = line.split("\t", -1);
            assertEquals("manifest column count on line " + lineNumber, 15, values.length);
            assertEquals("manifest line " + lineNumber + " field 'schema_version'",
                    "1", values[0]);
            assertTrue("manifest line " + lineNumber + " field 'case_id': invalid '"
                    + values[1] + "'", values[1].matches("[a-z][a-z0-9_]*"));
            assertTrue("manifest line " + lineNumber + " field 'blob': unsafe '"
                    + values[2] + "'", values[2].equals(values[1] + ".zip")
                    && Path.of(values[2]).getNameCount() == 1
                    && Path.of(values[2]).getFileName().toString().equals(values[2]));
            assertTrue("manifest line " + lineNumber + " field 'sha256': invalid '"
                    + values[3] + "'", values[3].matches("[0-9a-f]{64}"));

            String outcome = values[11];
            assertTrue("manifest line " + lineNumber + " field 'outcome': invalid '"
                    + outcome + "'", outcome.equals("SUCCESS") || outcome.equals("ERROR"));
            List<ExpectedEntry> entries = new ArrayList<>();
            if (outcome.equals("SUCCESS")) {
                assertEquals("manifest line " + lineNumber + " success error code",
                        "NONE", values[12]);
                assertEquals("manifest line " + lineNumber + " success error message",
                        "NONE", values[13]);
                assertFalse("manifest line " + lineNumber + " success entries must be explicit",
                        values[14].equals("NONE"));
                String[] encodedEntries = values[14].split(";", -1);
                for (int index = 0; index < encodedEntries.length; index++) {
                    entries.add(ExpectedEntry.parse(encodedEntries[index], lineNumber, index));
                }
            } else {
                assertTrue("manifest line " + lineNumber + " field 'error_code': invalid '"
                        + values[12] + "'", Arrays.stream(BoundedZipArchive.Code.values())
                        .anyMatch(code -> code.name().equals(values[12])));
                assertFalse("manifest line " + lineNumber + " error message must be nonblank",
                        values[13].isBlank() || values[13].equals("NONE"));
                assertEquals("manifest line " + lineNumber + " errors expose no entries",
                        "NONE", values[14]);
            }
            return new Fixture(
                    values[0],
                    values[1],
                    values[2],
                    values[3],
                    parseLong(values[4], lineNumber, "max_package_bytes", 1, Long.MAX_VALUE),
                    parseLong(values[5], lineNumber, "max_payload_bytes", 1, Long.MAX_VALUE),
                    parseInt(values[6], lineNumber, "max_zip_entries", 1, Integer.MAX_VALUE),
                    parseLong(values[7], lineNumber,
                            "max_cumulative_inflated_bytes", 1, Long.MAX_VALUE),
                    parseInt(values[8], lineNumber, "max_name_bytes", 1, 0xFFFF),
                    parseInt(values[9], lineNumber,
                            "max_compression_ratio", 1, Integer.MAX_VALUE),
                    parseLong(values[10], lineNumber,
                            "ratio_guard_threshold_bytes", 0, Long.MAX_VALUE),
                    outcome,
                    values[12],
                    values[13],
                    List.copyOf(entries));
        }
    }

    private static int parseInt(
            String value, int lineNumber, String field, int minimum, int maximum) {
        long parsed = parseLong(value, lineNumber, field, minimum, maximum);
        return (int) parsed;
    }

    private static long parseLong(
            String value, int lineNumber, String field, long minimum, long maximum) {
        assertTrue("manifest line " + lineNumber + " field '" + field
                        + "': expected canonical unsigned decimal, got '" + value + "'",
                value.matches("0|[1-9][0-9]*"));
        final long parsed;
        try {
            parsed = Long.parseLong(value);
        } catch (NumberFormatException error) {
            throw new AssertionError("manifest line " + lineNumber + " field '" + field
                    + "': invalid integer '" + value + "'", error);
        }
        assertTrue("manifest line " + lineNumber + " field '" + field
                + "': out of range '" + value + "'", parsed >= minimum && parsed <= maximum);
        return parsed;
    }

    private static boolean parseBoolean(String value, int lineNumber, String field) {
        assertTrue("manifest line " + lineNumber + " field '" + field
                + "': expected true or false, got '" + value + "'",
                value.equals("true") || value.equals("false"));
        return Boolean.parseBoolean(value);
    }
}
