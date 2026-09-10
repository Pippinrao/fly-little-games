package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.StableIds;

import org.junit.Test;

import java.io.IOException;
import java.net.URL;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.stream.Collectors;

/** Freezes Java catalog content identity against the shared version-one corpus. */
public final class ContentIdentityFixtureParityTest {
    private static final String FIXTURE_ROOT = "/catalog/identity/v1/";
    private static final String HASH_HEADER = String.join("\t",
            "schema_version", "case_id", "blob", "blob_size", "blob_sha256",
            "expected_sha1", "expected_sha256", "expected_crc32");
    private static final String STABLE_HEADER = String.join("\t",
            "schema_version", "case_id", "operation", "arg0_utf8_hex",
            "arg1_utf8_hex", "arg2_utf8_hex", "outcome", "expected_value",
            "expected_message");
    private static final Set<String> HASH_CASES = Set.of(
            "empty", "abc", "binary_00_ff", "pad_55", "pad_56", "pad_63",
            "pad_64", "pad_65", "million_a", "digits_123456789",
            "ownership_payload", "ownership_physical");
    private static final Set<String> STABLE_CASES = Set.of(
            "package_exact_golden", "source_exact_golden", "variant_exact_golden",
            "game_lowercase_hash", "entry_exact_golden", "framing_ab_c", "framing_a_bc",
            "chinese", "emoji", "nfc_distinct", "nfd_distinct", "embedded_nul",
            "edge_spaces_preserved", "nonblank_mongolian_vowel_separator",
            "nonblank_next_line", "nonblank_zero_width_space", "nonblank_bom",
            "variant_lowercase_hash", "blank_empty", "blank_ascii_controls",
            "blank_file_separators", "blank_no_break_space", "blank_ogham",
            "blank_en_quad_through_hair", "blank_line_paragraph",
            "blank_narrow_no_break", "blank_medium_math", "blank_ideographic",
            "fullwidth_hex_rejected", "arabic_hex_rejected", "emoji_hex_rejected",
            "short_hash_rejected",
            "variant_bad_hash_precedes_blank", "invalid_utf8_overlong",
            "invalid_utf8_truncated", "invalid_utf8_surrogate", "invalid_utf8_too_large",
            "invalid_utf8_continuation");

    @Test
    public void productionHasherMatchesVersionOneSharedFixtures() throws Exception {
        Path root = fixtureRoot();
        List<HashFixture> fixtures = loadHashManifest(root);
        assertEquals(HASH_CASES, fixtures.stream()
                .map(HashFixture::caseId).collect(Collectors.toSet()));
        assertExactCorpus(root, fixtures);

        Map<String, HashFixture> byId = new HashMap<>();
        for (HashFixture fixture : fixtures) {
            byId.put(fixture.caseId, fixture);
            byte[] blob = Files.readAllBytes(root.resolve(fixture.blob));
            byte[] before = blob.clone();
            assertEquals(fixture.caseId + " size", fixture.blobSize, blob.length);
            assertEquals(fixture.caseId + " independent blob SHA-256",
                    fixture.blobSha256, digest("SHA-256", blob).toLowerCase());

            RomHashes actual = RomContentHasher.hashes(
                    blob, RomContentHasher.sha256(blob));
            assertEquals(fixture.caseId + " SHA-1", fixture.expectedSha1,
                    actual.payloadSha1());
            assertEquals(fixture.caseId + " SHA-256", fixture.expectedSha256,
                    actual.payloadSha256());
            assertEquals(fixture.caseId + " physical SHA-256", fixture.expectedSha256,
                    actual.physicalPackageSha256());
            assertEquals(fixture.caseId + " CRC32", fixture.expectedCrc32,
                    actual.crc32());
            assertArrayEquals(fixture.caseId + " input must remain borrowed/read-only",
                    before, blob);
        }

        HashFixture payloadFixture = byId.get("ownership_payload");
        HashFixture physicalFixture = byId.get("ownership_physical");
        byte[] payload = Files.readAllBytes(root.resolve(payloadFixture.blob));
        byte[] physical = Files.readAllBytes(root.resolve(physicalFixture.blob));
        RomHashes separated = RomContentHasher.hashes(
                payload, RomContentHasher.sha256(physical));
        payload[0] ^= 0x7F;
        physical[0] ^= 0x7F;
        assertEquals(payloadFixture.expectedSha1, separated.payloadSha1());
        assertEquals(payloadFixture.expectedSha256, separated.payloadSha256());
        assertEquals(payloadFixture.expectedCrc32, separated.crc32());
        assertEquals(physicalFixture.expectedSha256, separated.physicalPackageSha256());
        assertNotEquals(separated.payloadSha256(), separated.physicalPackageSha256());
    }

    @Test
    public void stableIdsMatchVersionOneSharedFixtures() throws Exception {
        Path root = fixtureRoot();
        List<StableFixture> fixtures = loadStableManifest(root);
        assertEquals(STABLE_CASES, fixtures.stream()
                .map(StableFixture::caseId).collect(Collectors.toSet()));

        int skippedMalformedUtf8 = 0;
        for (StableFixture fixture : fixtures) {
            String[] arguments = new String[fixture.arguments.length];
            boolean malformed = false;
            for (int index = 0; index < arguments.length; index++) {
                try {
                    arguments[index] = decodeUtf8(fixture.arguments[index]);
                } catch (CharacterCodingException expected) {
                    malformed = true;
                    break;
                }
            }
            if (malformed) {
                assertFalse(fixture.caseId + " malformed UTF-8 must be an error",
                        fixture.succeeds);
                assertEquals(fixture.caseId, "ID input must be valid Unicode",
                        fixture.expectedMessage);
                skippedMalformedUtf8++;
                continue;
            }

            try {
                String actual = apply(fixture.operation, arguments);
                if (!fixture.succeeds) {
                    fail(fixture.caseId + " unexpectedly succeeded with " + actual);
                }
                assertEquals(fixture.caseId, fixture.expectedValue, actual);
            } catch (IllegalArgumentException error) {
                if (fixture.succeeds) {
                    throw new AssertionError(fixture.caseId + " unexpectedly failed", error);
                }
                assertEquals(fixture.caseId, fixture.expectedMessage, error.getMessage());
            }
        }
        assertEquals("C++-only malformed UTF-8 fixture count", 5, skippedMalformedUtf8);
    }

    private static String apply(String operation, String[] arguments) {
        return switch (operation) {
            case "package_id" -> StableIds.packageId(arguments[0], arguments[1]);
            case "saf_source_id" -> StableIds.safSourceId(arguments[0]);
            case "variant_id" -> StableIds.variantId(arguments[0], arguments[1], arguments[2]);
            case "provisional_game_id" -> StableIds.provisionalGameId(arguments[0]);
            case "entry_outcome_id" -> StableIds.entryOutcomeId(arguments[0], arguments[1]);
            default -> throw new AssertionError("unsupported operation " + operation);
        };
    }

    private static Path fixtureRoot() throws Exception {
        URL resource = ContentIdentityFixtureParityTest.class.getResource(FIXTURE_ROOT);
        assertNotNull("missing shared content identity fixture directory", resource);
        assertEquals("shared identity fixtures must be ordinary test resources",
                "file", resource.getProtocol());
        return Paths.get(resource.toURI());
    }

    private static List<HashFixture> loadHashManifest(Path root) throws Exception {
        String[] lines = readManifest(root.resolve("hashes.tsv"), HASH_HEADER);
        List<HashFixture> fixtures = new ArrayList<>();
        Set<String> caseIds = new HashSet<>();
        Set<String> blobs = new HashSet<>();
        for (int index = 1; index < lines.length - 1; index++) {
            String[] fields = splitColumns(lines[index], index + 1, 8);
            String caseId = identifier(fields[1], "case_id", index + 1);
            assertEquals("schema_version on line " + (index + 1), "1", fields[0]);
            assertTrue("duplicate case_id " + caseId, caseIds.add(caseId));
            assertEquals("canonical blob name on line " + (index + 1),
                    caseId + ".bin", fields[2]);
            assertTrue("duplicate blob " + fields[2], blobs.add(fields[2]));
            long size = canonicalUnsigned(fields[3], "blob_size", index + 1);
            assertTrue("blob_size exceeds Java array range", size <= Integer.MAX_VALUE);
            requirePattern(fields[4], "[0-9a-f]{64}", "blob_sha256", index + 1);
            requirePattern(fields[5], "[0-9A-F]{40}", "expected_sha1", index + 1);
            requirePattern(fields[6], "[0-9A-F]{64}", "expected_sha256", index + 1);
            requirePattern(fields[7], "[0-9A-F]{8}", "expected_crc32", index + 1);
            Path blob = root.resolve(fields[2]);
            assertTrue("blob must be a regular non-symlink file: " + fields[2],
                    Files.isRegularFile(blob, LinkOption.NOFOLLOW_LINKS)
                            && !Files.isSymbolicLink(blob));
            fixtures.add(new HashFixture(caseId, fields[2], (int) size, fields[4],
                    fields[5], fields[6], fields[7]));
        }
        assertEquals("hash fixture count", 12, fixtures.size());
        return fixtures;
    }

    private static List<StableFixture> loadStableManifest(Path root) throws Exception {
        String[] lines = readManifest(root.resolve("stable_ids.tsv"), STABLE_HEADER);
        List<StableFixture> fixtures = new ArrayList<>();
        Set<String> caseIds = new HashSet<>();
        for (int index = 1; index < lines.length - 1; index++) {
            String[] fields = splitColumns(lines[index], index + 1, 9);
            assertEquals("schema_version on line " + (index + 1), "1", fields[0]);
            String caseId = identifier(fields[1], "case_id", index + 1);
            assertTrue("duplicate case_id " + caseId, caseIds.add(caseId));
            int arity = switch (fields[2]) {
                case "package_id", "entry_outcome_id" -> 2;
                case "saf_source_id", "provisional_game_id" -> 1;
                case "variant_id" -> 3;
                default -> throw new AssertionError(
                        "manifest line " + (index + 1) + " invalid operation " + fields[2]);
            };
            byte[][] arguments = new byte[arity][];
            for (int argument = 0; argument < 3; argument++) {
                String encoded = fields[3 + argument];
                if (argument >= arity) {
                    assertEquals("unused argument must be NONE", "NONE", encoded);
                } else {
                    assertNotEquals("required argument must not be NONE", "NONE", encoded);
                    arguments[argument] = decodeLowerHex(encoded, index + 1);
                }
            }
            boolean succeeds;
            if (fields[6].equals("SUCCESS")) {
                succeeds = true;
                assertNotEquals("SUCCESS expected_value", "NONE", fields[7]);
                assertEquals("SUCCESS expected_message", "NONE", fields[8]);
                requirePattern(fields[7], prefixPattern(fields[2]),
                        "expected_value", index + 1);
            } else if (fields[6].equals("ERROR")) {
                succeeds = false;
                assertEquals("ERROR expected_value", "NONE", fields[7]);
                assertFalse("ERROR expected_message", fields[8].isBlank()
                        || fields[8].equals("NONE"));
            } else {
                throw new AssertionError("manifest line " + (index + 1) + " invalid outcome");
            }
            fixtures.add(new StableFixture(caseId, fields[2], arguments, succeeds,
                    fields[7], fields[8]));
        }
        assertEquals("stable fixture count", 38, fixtures.size());
        return fixtures;
    }

    private static String prefixPattern(String operation) {
        String prefix = switch (operation) {
            case "package_id" -> "pkg:";
            case "saf_source_id" -> "source:";
            case "variant_id" -> "variant:";
            case "provisional_game_id" -> "game:";
            case "entry_outcome_id" -> "entry:";
            default -> throw new AssertionError(operation);
        };
        return prefix + "[0-9A-F]{64}";
    }

    private static String[] readManifest(Path path, String header) throws IOException {
        assertTrue("manifest must be a regular non-symlink file: " + path.getFileName(),
                Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                        && !Files.isSymbolicLink(path));
        byte[] bytes = Files.readAllBytes(path);
        for (byte value : bytes) {
            assertTrue("manifest must be ASCII: " + path.getFileName(),
                    (value & 0xFF) <= 0x7F);
        }
        String text = new String(bytes, StandardCharsets.US_ASCII);
        assertFalse("manifest must use LF: " + path.getFileName(), text.contains("\r"));
        assertTrue("manifest must end with LF: " + path.getFileName(), text.endsWith("\n"));
        String[] lines = text.split("\n", -1);
        assertEquals("manifest header: " + path.getFileName(), header, lines[0]);
        assertEquals("nothing may follow final LF", "", lines[lines.length - 1]);
        for (int index = 1; index < lines.length - 1; index++) {
            assertFalse("manifest may not contain empty rows", lines[index].isEmpty());
        }
        return lines;
    }

    private static void assertExactCorpus(Path root, List<HashFixture> fixtures)
            throws IOException {
        Set<String> expected = new HashSet<>(Set.of("hashes.tsv", "stable_ids.tsv"));
        for (HashFixture fixture : fixtures) {
            expected.add(fixture.blob);
        }
        Set<String> actual;
        try (var entries = Files.list(root)) {
            actual = entries.map(path -> path.getFileName().toString())
                    .collect(Collectors.toSet());
        }
        assertEquals("exact identity fixture corpus", expected, actual);
        for (String name : actual) {
            Path path = root.resolve(name);
            assertTrue("corpus entry must be regular and non-symlink: " + name,
                    Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                            && !Files.isSymbolicLink(path));
        }
    }

    private static String[] splitColumns(String row, int lineNumber, int count) {
        String[] fields = row.split("\t", -1);
        assertEquals("manifest column count on line " + lineNumber, count, fields.length);
        return fields;
    }

    private static String identifier(String value, String field, int lineNumber) {
        requirePattern(value, "[a-z][a-z0-9_]*", field, lineNumber);
        return value;
    }

    private static long canonicalUnsigned(String value, String field, int lineNumber) {
        requirePattern(value, "0|[1-9][0-9]*", field, lineNumber);
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException error) {
            throw new AssertionError("manifest line " + lineNumber + " " + field
                    + " exceeds Java signed range", error);
        }
    }

    private static void requirePattern(String value, String pattern, String field, int lineNumber) {
        assertTrue("manifest line " + lineNumber + " invalid " + field + ": " + value,
                value.matches(pattern));
    }

    private static byte[] decodeLowerHex(String value, int lineNumber) {
        assertTrue("manifest line " + lineNumber + " argument must be even lowercase hex",
                value.length() % 2 == 0 && value.matches("[0-9a-f]*"));
        byte[] result = new byte[value.length() / 2];
        for (int index = 0; index < result.length; index++) {
            result[index] = (byte) Integer.parseInt(value.substring(index * 2, index * 2 + 2), 16);
        }
        return result;
    }

    private static String decodeUtf8(byte[] value) throws CharacterCodingException {
        return StandardCharsets.UTF_8.newDecoder()
                .onMalformedInput(CodingErrorAction.REPORT)
                .onUnmappableCharacter(CodingErrorAction.REPORT)
                .decode(ByteBuffer.wrap(value)).toString();
    }

    private static String digest(String algorithm, byte[] value) throws Exception {
        StringBuilder result = new StringBuilder();
        for (byte item : MessageDigest.getInstance(algorithm).digest(value)) {
            result.append(String.format("%02X", item & 0xFF));
        }
        return result.toString();
    }

    private record HashFixture(
            String caseId,
            String blob,
            int blobSize,
            String blobSha256,
            String expectedSha1,
            String expectedSha256,
            String expectedCrc32) {
    }

    private record StableFixture(
            String caseId,
            String operation,
            byte[][] arguments,
            boolean succeeds,
            String expectedValue,
            String expectedMessage) {
    }
}
