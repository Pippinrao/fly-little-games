package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.flynes.emu.catalog.ZipEntryIdentity;

import org.junit.Test;

import java.io.IOException;
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

/** Freezes selected-entry payload inflation separately from ZIP structural opening. */
public final class BoundedZipPayloadFixtureParityTest {
    private static final String FIXTURE_ROOT = "/zip/payload/v1/";
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
            "selector_raw_name_hex",
            "selector_local_header_offset",
            "outcome",
            "error_code",
            "error_message",
            "payload_length",
            "payload_sha256");

    @Test
    public void currentJavaPayloadReadMatchesVersionOneSharedFixtures() throws Exception {
        URL rootResource = BoundedZipPayloadFixtureParityTest.class.getResource(FIXTURE_ROOT);
        assertNotNull("missing shared ZIP payload fixture directory", rootResource);
        assertEquals("shared ZIP payload fixtures must be ordinary test resources",
                "file", rootResource.getProtocol());
        Path root = Paths.get(rootResource.toURI());

        List<Fixture> fixtures = loadManifest(root);
        assertEquals("manifest fixture count", 19, fixtures.size());
        assertExactCorpus(root, fixtures);

        Set<String> caseIds = new HashSet<>();
        for (Fixture fixture : fixtures) {
            assertTrue("duplicate case ID " + fixture.caseId, caseIds.add(fixture.caseId));
            byte[] physicalBytes = Files.readAllBytes(root.resolve(fixture.blob));
            assertEquals(fixture.caseId + " archive SHA-256",
                    fixture.sha256, sha256(physicalBytes));
            ScanLimits limits = new ScanLimits(
                    fixture.maxPackageBytes,
                    fixture.maxPayloadBytes,
                    fixture.maxZipEntries,
                    fixture.maxCumulativeInflatedBytes,
                    fixture.maxNameBytes,
                    fixture.maxCompressionRatio,
                    fixture.ratioGuardThresholdBytes);

            BoundedZipArchive.Archive archive;
            try {
                archive = BoundedZipArchive.fromBytes(physicalBytes, limits);
            } catch (BoundedZipArchive.ArchiveException error) {
                throw new AssertionError(fixture.caseId + " must pass ZIP-open validation before "
                        + "the selected payload is read, but failed with " + error.code()
                        + ": " + error.getMessage(), error);
            }

            ZipEntryIdentity selector = new ZipEntryIdentity(
                    fixture.selectorRawNameHex,
                    fixture.selectorLocalHeaderOffset);
            if (!fixture.succeeds) {
                BoundedZipArchive.ArchiveException error = assertThrows(
                        fixture.caseId,
                        BoundedZipArchive.ArchiveException.class,
                        () -> archive.requireExact(selector).readPayload());
                assertEquals(fixture.caseId + " error code",
                        fixture.errorCode, error.code().name());
                assertEquals(fixture.caseId + " error message",
                        fixture.errorMessage, error.getMessage());
                continue;
            }

            byte[] payload;
            try {
                payload = archive.requireExact(selector).readPayload();
            } catch (BoundedZipArchive.ArchiveException error) {
                throw new AssertionError(fixture.caseId + " unexpectedly failed with "
                        + error.code() + ": " + error.getMessage(), error);
            }
            assertEquals(fixture.caseId + " payload length",
                    fixture.payloadLength, payload.length);
            assertEquals(fixture.caseId + " payload SHA-256",
                    fixture.payloadSha256, sha256(payload));
        }
        assertRequiredCases(caseIds);
    }

    @Test
    public void manifestRequiresCanonicalUnsignedNumbersAndJavaSignedRanges() {
        for (String value : List.of("+1", "01", "-0")) {
            AssertionError error = assertThrows(
                    AssertionError.class,
                    () -> Fixture.parse(replaceColumn(validSuccessRow(), 4, value), 2));
            assertTrue(error.getMessage(),
                    error.getMessage().contains("max_package_bytes")
                            && error.getMessage().contains("canonical unsigned decimal"));
        }

        assertFieldRejected(4, "9223372036854775808", "max_package_bytes");
        assertFieldRejected(5, "9223372036854775808", "max_payload_bytes");
        assertFieldRejected(6, "2147483648", "max_zip_entries");
        assertFieldRejected(7, "9223372036854775808", "max_cumulative_inflated_bytes");
        assertFieldRejected(9, "2147483648", "max_compression_ratio");
        assertFieldRejected(10, "9223372036854775808", "ratio_guard_threshold_bytes");
        assertFieldRejected(12, "2147483648", "selector_local_header_offset");
        assertFieldRejected(16, "2147483648", "payload_length");
    }

    @Test
    public void manifestRejectsUnsafeIdentityAndIncoherentOutcomeFields() {
        AssertionError badCase = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validSuccessRow(), 1, "0case"), 2));
        assertTrue(badCase.getMessage(), badCase.getMessage().contains("case_id"));

        AssertionError badBlob = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validSuccessRow(), 2, "other.zip"), 2));
        assertTrue(badBlob.getMessage(), badBlob.getMessage().contains("blob"));

        for (String rawName : List.of("", "-", "A0", "abc", "zz")) {
            AssertionError badSelector = assertThrows(
                    AssertionError.class,
                    () -> Fixture.parse(replaceColumn(validSuccessRow(), 11, rawName), 2));
            assertTrue(badSelector.getMessage(),
                    badSelector.getMessage().contains("selector_raw_name_hex"));
        }
        AssertionError longSelector = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(
                        replaceColumn(validSuccessRow(), 11, "aa".repeat(0x10000)), 2));
        assertTrue(longSelector.getMessage(), longSelector.getMessage().contains("ZIP name field"));

        AssertionError successWithError = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validSuccessRow(), 14, "INVALID_ZIP"), 2));
        assertTrue(successWithError.getMessage(), successWithError.getMessage().contains("SUCCESS"));

        for (String message : List.of("", "   ", "NONE")) {
            AssertionError badMessage = assertThrows(
                    AssertionError.class,
                    () -> Fixture.parse(replaceColumn(validErrorRow(), 15, message), 2));
            assertTrue(badMessage.getMessage(),
                    badMessage.getMessage().contains("error_message"));
        }

        AssertionError errorWithPayload = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validErrorRow(), 16, "0"), 2));
        assertTrue(errorWithPayload.getMessage(), errorWithPayload.getMessage().contains("ERROR"));
    }

    private static void assertFieldRejected(int column, String value, String field) {
        AssertionError error = assertThrows(
                AssertionError.class,
                () -> Fixture.parse(replaceColumn(validSuccessRow(), column, value), 2));
        assertTrue(error.getMessage(), error.getMessage().contains(field));
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
            assertFalse("manifest must not contain empty rows", lines[index].isEmpty());
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

    private static int parseInt(
            String value, int lineNumber, String field, int minimum, int maximum) {
        return (int) parseLong(value, lineNumber, field, minimum, maximum);
    }

    private static String validSuccessRow() {
        return String.join("\t",
                "1", "case_id", "case_id.zip", "0".repeat(64),
                "1", "1", "1", "1", "1", "1", "0",
                "61", "0", "SUCCESS", "NONE", "NONE", "0", "0".repeat(64));
    }

    private static String validErrorRow() {
        return String.join("\t",
                "1", "case_id", "case_id.zip", "0".repeat(64),
                "1", "1", "1", "1", "1", "1", "0",
                "61", "0", "ERROR", "INVALID_ZIP", "stable message", "NONE", "NONE");
    }

    private static String replaceColumn(String row, int column, String replacement) {
        String[] fields = row.split("\t", -1);
        fields[column] = replacement;
        return String.join("\t", fields);
    }

    private static void assertRequiredCases(Set<String> caseIds) {
        Set<String> required = Set.of(
                "stored_exact_limit_local_extra",
                "deflate_large_signed_descriptor_exact_limits",
                "deflate_empty",
                "directory_payload_not_rejected",
                "selected_only_bad_sibling",
                "payload_limit_precedes_invalid_decode",
                "int32_memory_cap_precedes_decode",
                "stored_sizes_differ",
                "zlib_wrapped_rejected_raw_only",
                "reserved_deflate_block",
                "truncated_deflate_incomplete",
                "trailing_bytes_precede_size_and_crc",
                "actual_over_declared",
                "actual_over_cumulative_precedes_declared",
                "actual_under_declared_precedes_crc",
                "coherent_metadata_crc_mismatch_high_bit",
                "descriptor_unsigned_read_success",
                "descriptor_signed_read_success",
                "exact_selector_missing");
        assertEquals("manifest must contain the complete required ZIP-payload corpus",
                required, caseIds);
    }

    private record Fixture(
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
            String selectorRawNameHex,
            int selectorLocalHeaderOffset,
            boolean succeeds,
            String errorCode,
            String errorMessage,
            int payloadLength,
            String payloadSha256) {

        static Fixture parse(String line, int lineNumber) {
            String[] values = line.split("\t", -1);
            assertEquals("manifest column count on line " + lineNumber, 18, values.length);
            assertEquals("manifest line " + lineNumber + " field 'schema_version'",
                    "1", values[0]);
            assertTrue("manifest line " + lineNumber + " field 'case_id': invalid '"
                    + values[1] + "'", values[1].matches("[a-z][a-z0-9_]*"));
            Path blobPath = Path.of(values[2]);
            assertTrue("manifest line " + lineNumber + " field 'blob': unsafe or noncanonical '"
                    + values[2] + "'", values[2].equals(values[1] + ".zip")
                    && blobPath.getNameCount() == 1
                    && blobPath.getFileName().toString().equals(values[2]));
            assertTrue("manifest line " + lineNumber + " field 'sha256': expected 64 lowercase "
                    + "hex digits", values[3].matches("[0-9a-f]{64}"));
            assertTrue("manifest line " + lineNumber + " field 'selector_raw_name_hex': "
                    + "expected nonempty lowercase whole-byte hex",
                    values[11].matches("(?:[0-9a-f]{2})+"));
            assertTrue("manifest line " + lineNumber + " field 'selector_raw_name_hex': "
                    + "exceeds the ZIP name field",
                    values[11].length() <= 0xFFFF * 2);
            int selectorOffset = parseInt(values[12], lineNumber,
                    "selector_local_header_offset", 0, Integer.MAX_VALUE);

            boolean succeeds;
            String errorCode;
            String errorMessage;
            int payloadLength;
            String payloadSha256;
            if (values[13].equals("SUCCESS")) {
                succeeds = true;
                assertTrue("manifest line " + lineNumber + " SUCCESS requires NONE error fields",
                        values[14].equals("NONE") && values[15].equals("NONE"));
                payloadLength = parseInt(values[16], lineNumber,
                        "payload_length", 0, Integer.MAX_VALUE);
                assertTrue("manifest line " + lineNumber + " field 'payload_sha256': expected 64 "
                        + "lowercase hex digits", values[17].matches("[0-9a-f]{64}"));
                payloadSha256 = values[17];
                errorCode = "NONE";
                errorMessage = "NONE";
            } else if (values[13].equals("ERROR")) {
                succeeds = false;
                assertTrue("manifest line " + lineNumber + " field 'error_code': unknown value",
                        Arrays.stream(BoundedZipArchive.Code.values())
                                .anyMatch(code -> code.name().equals(values[14])));
                assertFalse("manifest line " + lineNumber + " field 'error_message': ERROR "
                        + "requires a nonblank stable message",
                        values[15].isBlank() || values[15].equals("NONE"));
                assertTrue("manifest line " + lineNumber + " ERROR requires NONE payload fields",
                        values[16].equals("NONE") && values[17].equals("NONE"));
                errorCode = values[14];
                errorMessage = values[15];
                payloadLength = -1;
                payloadSha256 = "NONE";
            } else {
                fail("manifest line " + lineNumber + " field 'outcome': expected SUCCESS or ERROR");
                throw new AssertionError("unreachable");
            }

            return new Fixture(
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
                    values[11],
                    selectorOffset,
                    succeeds,
                    errorCode,
                    errorMessage,
                    payloadLength,
                    payloadSha256);
        }
    }
}
