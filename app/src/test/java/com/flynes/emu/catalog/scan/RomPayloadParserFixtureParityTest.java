package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.CompatibilityReason;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;

import org.junit.Test;

import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

public final class RomPayloadParserFixtureParityTest {
    private static final String FIXTURE_ROOT = "/rom/v1/";
    private static final String MANIFEST_HEADER = String.join("\t",
            "schema_version",
            "case_id",
            "blob",
            "sha256",
            "recognized",
            "format",
            "state",
            "reason",
            "expected_bytes",
            "actual_bytes",
            "prg_bytes",
            "chr_bytes",
            "mapper",
            "submapper",
            "trainer",
            "battery",
            "disk_sides",
            "warnings");

    @Test
    public void currentJavaParserMatchesVersionOneSharedFixtures() throws Exception {
        String manifest = new String(readResource("manifest.tsv"), StandardCharsets.US_ASCII);
        String[] lines = manifest.split("\\R", -1);
        assertTrue("manifest must end with a newline", lines.length > 1);
        assertEquals("manifest header", MANIFEST_HEADER, lines[0]);
        assertEquals("manifest must have exactly 25 cases", 27, lines.length);
        assertEquals("manifest must have no data after its final newline", "", lines[26]);

        Set<String> caseIds = new HashSet<>();
        Set<String> blobNames = new HashSet<>();
        for (int index = 1; index < lines.length - 1; index++) {
            Fixture fixture = Fixture.parse(lines[index], index + 1);
            assertTrue(fixture.caseId, caseIds.add(fixture.caseId));
            assertTrue(fixture.caseId, blobNames.add(fixture.blob));
            assertEquals(fixture.caseId, "1", fixture.schemaVersion);

            byte[] payload = readResource(fixture.blob);
            assertEquals(fixture.caseId + " actual byte count",
                    fixture.actualBytes, payload.length);
            assertEquals(fixture.caseId + " SHA-256",
                    fixture.sha256, sha256(payload));

            RomPayloadParser.Parsed parsed = RomPayloadParser.parse(payload);
            assertEquals(fixture.caseId + " recognition",
                    fixture.recognized, parsed != null);
            if (parsed == null) {
                assertUnrecognizedFixture(fixture);
                continue;
            }

            assertEquals(fixture.caseId + " format", fixture.format, parsed.format().name());
            assertEquals(fixture.caseId + " state",
                    fixture.state, parsed.compatibility().state().name());
            assertEquals(fixture.caseId + " reason",
                    fixture.reason, parsed.compatibility().reason().name());

            RomAnalysis analysis = parsed.analysis();
            assertEquals(fixture.caseId + " expected bytes",
                    fixture.expectedBytes, analysis.expectedBytes());
            assertEquals(fixture.caseId + " actual bytes",
                    fixture.actualBytes, analysis.actualBytes());
            assertEquals(fixture.caseId + " PRG bytes",
                    fixture.prgBytes, analysis.prgBytes());
            assertEquals(fixture.caseId + " CHR bytes",
                    fixture.chrBytes, analysis.chrBytes());
            assertEquals(fixture.caseId + " mapper", fixture.mapper, analysis.mapper());
            assertEquals(fixture.caseId + " submapper",
                    fixture.submapper, analysis.submapper());
            assertEquals(fixture.caseId + " trainer", fixture.trainer, analysis.trainer());
            assertEquals(fixture.caseId + " battery", fixture.battery, analysis.battery());
            assertEquals(fixture.caseId + " disk sides",
                    fixture.diskSides, analysis.diskSides());
            assertEquals(fixture.caseId + " ordered warnings",
                    fixture.warnings,
                    analysis.warnings().stream().map(RomAnalysis.Warning::name).toList());
        }

        assertRequiredCases(caseIds);
    }

    private static void assertUnrecognizedFixture(Fixture fixture) {
        assertFalse(fixture.caseId + " must be marked unrecognized", fixture.recognized);
        assertEquals(fixture.caseId, RomFormat.UNKNOWN.name(), fixture.format);
        assertEquals(fixture.caseId, CompatibilityState.UNKNOWN.name(), fixture.state);
        assertEquals(fixture.caseId, CompatibilityReason.UNKNOWN_FORMAT.name(), fixture.reason);
        assertEquals(fixture.caseId, fixture.actualBytes, fixture.expectedBytes);
        assertEquals(fixture.caseId, 0, fixture.prgBytes);
        assertEquals(fixture.caseId, 0, fixture.chrBytes);
        assertEquals(fixture.caseId, -1, fixture.mapper);
        assertEquals(fixture.caseId, -1, fixture.submapper);
        assertFalse(fixture.caseId, fixture.trainer);
        assertFalse(fixture.caseId, fixture.battery);
        assertEquals(fixture.caseId, 0, fixture.diskSides);
        assertTrue(fixture.caseId, fixture.warnings.isEmpty());
    }

    private static void assertRequiredCases(Set<String> caseIds) {
        Set<String> required = Set.of(
                "playable_ines_metadata",
                "dirty_header_ines",
                "nes_header_too_short",
                "zero_prg_ines",
                "truncated_ines",
                "nes2_extended_mapper_metadata",
                "nes2_exponential_64_96",
                "nes2_exponential_multiplier_overflow",
                "valid_headered_fds",
                "valid_headerless_fds",
                "zero_side_fds",
                "truncated_headered_fds",
                "invalid_headered_fds_signature",
                "valid_unif_positive_prg",
                "unif_missing_prg",
                "unif_truncated_chunk_data",
                "unknown_bytes");
        assertTrue("manifest is missing a required parity case: " + caseIds,
                caseIds.containsAll(required));
    }

    private static byte[] readResource(String name) throws IOException {
        try (InputStream input = RomPayloadParserFixtureParityTest.class
                .getResourceAsStream(FIXTURE_ROOT + name)) {
            assertNotNull("missing shared fixture resource " + name, input);
            return input.readAllBytes();
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

    private record Fixture(
            String schemaVersion,
            String caseId,
            String blob,
            String sha256,
            boolean recognized,
            String format,
            String state,
            String reason,
            long expectedBytes,
            long actualBytes,
            long prgBytes,
            long chrBytes,
            int mapper,
            int submapper,
            boolean trainer,
            boolean battery,
            int diskSides,
            List<String> warnings) {

        static Fixture parse(String line, int lineNumber) {
            String[] values = line.split("\t", -1);
            assertEquals("manifest column count on line " + lineNumber, 18, values.length);
            assertTrue("unsafe blob name on line " + lineNumber,
                    values[2].matches("[a-z0-9_]+\\.bin"));
            assertTrue("invalid SHA-256 on line " + lineNumber,
                    values[3].matches("[0-9a-f]{64}"));
            List<String> warnings = values[17].equals("NONE")
                    ? List.of()
                    : Arrays.asList(values[17].split(",", -1));
            return new Fixture(
                    values[0],
                    values[1],
                    values[2],
                    values[3],
                    parseBoolean(values[4], lineNumber),
                    values[5],
                    values[6],
                    values[7],
                    Long.parseLong(values[8]),
                    Long.parseLong(values[9]),
                    Long.parseLong(values[10]),
                    Long.parseLong(values[11]),
                    Integer.parseInt(values[12]),
                    Integer.parseInt(values[13]),
                    parseBoolean(values[14], lineNumber),
                    parseBoolean(values[15], lineNumber),
                    Integer.parseInt(values[16]),
                    warnings);
        }

        private static boolean parseBoolean(String value, int lineNumber) {
            assertTrue("invalid boolean on line " + lineNumber,
                    value.equals("true") || value.equals("false"));
            return Boolean.parseBoolean(value);
        }
    }
}
