package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

import com.flynes.emu.catalog.EntryOutcome;

import org.junit.Assume;
import org.junit.Test;

import java.io.IOException;
import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import java.util.stream.Collectors;

/** Freezes Java unsupported-payload classification against the shared v1 corpus. */
public final class UnsupportedPayloadClassifierFixtureParityTest {
    private static final String FIXTURE_ROOT = "/catalog/unsupported_payload/v1/";
    private static final String HEADER = String.join("\t",
            "schema_version", "case_id", "blob", "blob_size", "blob_sha256",
            "expected_reason");
    private static final Set<String> REASONS = Set.of(
            "NESTED_ARCHIVE", "EXECUTABLE", "GAME_BOY", "SIDECAR", "UNKNOWN_FORMAT");
    private static final String JUNCTION_LINK_ENV = "FLYNES_TEST_JUNCTION_LINK";
    private static final String JUNCTION_TARGET_ENV = "FLYNES_TEST_JUNCTION_TARGET";
    private static final String CREATE_JUNCTION_SCRIPT = String.join("",
            "$ErrorActionPreference='Stop';",
            "$link=[Environment]::GetEnvironmentVariable('", JUNCTION_LINK_ENV,
            "','Process');",
            "$target=[Environment]::GetEnvironmentVariable('", JUNCTION_TARGET_ENV,
            "','Process');",
            "if ([string]::IsNullOrEmpty($link) -or ",
            "[string]::IsNullOrEmpty($target)) { throw 'missing junction path' };",
            "New-Item -ItemType Junction -Path $link -Target $target ",
            "-ErrorAction Stop | Out-Null");
    private static final Set<String> CASES = Set.of(
            "empty",
            "zip_local_exact", "zip_local_trailing",
            "zip_empty_exact", "zip_empty_trailing",
            "zip_spanned_exact", "zip_spanned_trailing",
            "pk_central_nonmatch", "short_p", "short_pk", "short_pk03",
            "mz_exact", "mz_trailing", "lowercase_mz", "mz_game_boy_priority",
            "game_boy_minimum", "game_boy_longer", "game_boy_logo_byte_diff",
            "game_boy_offset_minus_one", "zip_game_boy_priority",
            "plain_ascii", "allowed_controls", "nul_prefix",
            "forbidden_c0_01", "forbidden_c0_0b", "forbidden_c0_1f",
            "del_text", "c1_text", "high_bytes_text", "invalid_utf8_text",
            "text_length_4095", "text_length_4096", "text_length_4097",
            "forbidden_at_4095", "forbidden_at_4096",
            "nul_at_4095", "nul_at_4096", "ordinary_binary");

    @Test
    public void productionClassifierMatchesVersionOneSharedFixtures() throws Exception {
        Path root = fixtureRoot();
        List<Fixture> fixtures = loadFixtures(root);
        for (Fixture fixture : fixtures) {
            byte[] payload = Files.readAllBytes(root.resolve(fixture.blob));
            byte[] before = payload.clone();
            assertEquals(fixture.caseId, fixture.expectedReason,
                    UnsupportedPayloadClassifier.classify(payload));
            assertArrayEquals(fixture.caseId + " input must remain borrowed/read-only",
                    before, payload);
        }
    }

    @Test
    public void loaderRejectsMalformedManifestAndCorpus() throws Exception {
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes-unsupported-payload-java-");
        try {
            Path root = copyCorpus(source, scratch.resolve("schema"));
            replaceField(root.resolve("manifest.tsv"), 0, 0, "2");
            expectLoadFailure(root, "schema_version");

            root = copyCorpus(source, scratch.resolve("columns"));
            List<String> lines = manifestLines(root);
            lines.set(1, lines.get(1) + "\textra");
            writeManifest(root, lines);
            expectLoadFailure(root, "column count");

            root = copyCorpus(source, scratch.resolve("duplicate-case"));
            replaceField(root.resolve("manifest.tsv"), 1, 1, "empty");
            replaceField(root.resolve("manifest.tsv"), 1, 2, "empty.bin");
            expectLoadFailure(root, "duplicate case_id");

            root = copyCorpus(source, scratch.resolve("unsafe-blob"));
            replaceField(root.resolve("manifest.tsv"), 0, 2, "../empty.bin");
            expectLoadFailure(root, "safe basename");

            root = copyCorpus(source, scratch.resolve("hash-case"));
            List<String> hashLines = manifestLines(root);
            String[] hashFields = hashLines.get(1).split("\t", -1);
            hashFields[4] = hashFields[4].toUpperCase();
            hashLines.set(1, String.join("\t", hashFields));
            writeManifest(root, hashLines);
            expectLoadFailure(root, "lowercase SHA-256");

            root = copyCorpus(source, scratch.resolve("reason"));
            replaceField(root.resolve("manifest.tsv"), 0, 5, "INDEXED");
            expectLoadFailure(root, "canonical reason");

            root = copyCorpus(source, scratch.resolve("changed-blob"));
            Files.write(root.resolve("plain_ascii.bin"), new byte[]{'x'});
            expectLoadFailure(root, "blob_size");

            root = copyCorpus(source, scratch.resolve("extra-file"));
            Files.write(root.resolve("orphan.bin"), new byte[]{1});
            expectLoadFailure(root, "exact corpus");

            root = copyCorpus(source, scratch.resolve("crlf"));
            byte[] lf = Files.readAllBytes(root.resolve("manifest.tsv"));
            Files.write(root.resolve("manifest.tsv"),
                    new String(lf, StandardCharsets.US_ASCII)
                            .replace("\n", "\r\n").getBytes(StandardCharsets.US_ASCII));
            expectLoadFailure(root, "LF");
        } finally {
            deleteTree(scratch);
        }
    }

    @Test
    public void loaderRejectsHardlinkAliasesWhenSupported() throws Exception {
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes-unsupported-payload-alias-");
        try {
            Path root = copyCorpus(source, scratch.resolve("hardlink"));
            Path alias = root.resolve("plain_ascii.bin");
            Files.delete(alias);
            try {
                Files.createLink(alias, root.resolve("lowercase_mz.bin"));
            } catch (IOException | UnsupportedOperationException unavailable) {
                Assume.assumeNoException("hard links unavailable", unavailable);
            }
            expectLoadFailure(root, "must have one hard link");
        } finally {
            deleteTree(scratch);
        }
    }

    @Test
    public void loaderRejectsHardlinkToFileOutsideCorpusWhenSupported() throws Exception {
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes-unsupported-payload-external-link-");
        try {
            Path root = copyCorpus(source, scratch.resolve("corpus"));
            Path alias = scratch.resolve("outside-hardlink.bin");
            try {
                Files.createLink(alias, root.resolve("plain_ascii.bin"));
            } catch (IOException | UnsupportedOperationException unavailable) {
                Assume.assumeNoException("hard links unavailable", unavailable);
            }
            expectLoadFailure(root, "must have one hard link");
        } finally {
            deleteTree(scratch);
        }
    }

    @Test
    public void loaderRejectsSymlinkAliasesWhenSupported() throws Exception {
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes-unsupported-payload-symlink-");
        try {
            Path root = copyCorpus(source, scratch.resolve("symlink"));
            Path target = scratch.resolve("outside.bin");
            Files.write(target, new byte[]{'x'});
            Path linked = root.resolve("plain_ascii.bin");
            Files.delete(linked);
            try {
                Files.createSymbolicLink(linked, target);
            } catch (IOException | UnsupportedOperationException unavailable) {
                Assume.assumeNoException("symbolic links unavailable", unavailable);
            }
            expectLoadFailure(root, "non-symlink");
        } finally {
            deleteTree(scratch);
        }
    }

    @Test
    public void loaderRejectsSymlinkRootWhenSupported() throws Exception {
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes-unsupported-payload-root-link-");
        try {
            Path target = copyCorpus(source, scratch.resolve("target"));
            Path root = scratch.resolve("root-link");
            try {
                Files.createSymbolicLink(root, target);
            } catch (IOException | UnsupportedOperationException unavailable) {
                Assume.assumeNoException("directory symbolic links unavailable", unavailable);
            }
            expectLoadFailure(root, "ordinary non-symlink directory");
        } finally {
            deleteTree(scratch);
        }
    }

    @Test
    public void loaderRejectsWindowsJunctionRootWhenSupported() throws Exception {
        Assume.assumeTrue("Windows-only reparse regression",
                System.getProperty("os.name").startsWith("Windows"));
        Path source = fixtureRoot();
        Path scratch = Files.createTempDirectory("flynes junction & regression ");
        Path junction = scratch.resolve("junction");
        try {
            Path target = copyCorpus(source, scratch.resolve("target"));
            createWindowsJunction(junction, target);
            assertTrue("junction helper did not create a reparse point",
                    isFilesystemAlias(junction));
            expectLoadFailure(junction, "ordinary non-symlink directory");
        } finally {
            Files.deleteIfExists(junction);
            deleteTree(scratch);
        }
    }

    private static void createWindowsJunction(Path junction, Path target) throws Exception {
        ProcessBuilder builder = new ProcessBuilder(
                "powershell.exe", "-NoLogo", "-NoProfile", "-NonInteractive",
                "-Command", CREATE_JUNCTION_SCRIPT)
                .redirectErrorStream(true);
        builder.environment().put(JUNCTION_LINK_ENV, junction.toString());
        builder.environment().put(JUNCTION_TARGET_ENV, target.toString());
        Process created = builder.start();
        String output = new String(created.getInputStream().readAllBytes(),
                StandardCharsets.ISO_8859_1);
        int exitCode = created.waitFor();
        assertEquals("junction creation failed: " + output, 0, exitCode);
    }

    private static Path fixtureRoot() throws Exception {
        URL resource = UnsupportedPayloadClassifierFixtureParityTest.class
                .getResource(FIXTURE_ROOT);
        assertNotNull("missing shared unsupported-payload fixture directory", resource);
        assertEquals("shared fixtures must be ordinary test resources", "file",
                resource.getProtocol());
        return Paths.get(resource.toURI());
    }

    private static List<Fixture> loadFixtures(Path root) throws Exception {
        assertTrue("fixture root must be an ordinary non-symlink directory",
                Files.isDirectory(root, LinkOption.NOFOLLOW_LINKS)
                        && !isFilesystemAlias(root));
        String[] lines = readManifest(root.resolve("manifest.tsv"));
        List<Fixture> fixtures = new ArrayList<>();
        Set<String> caseIds = new HashSet<>();
        Set<String> blobNames = new HashSet<>();
        for (int index = 1; index < lines.length - 1; index++) {
            String[] fields = lines[index].split("\t", -1);
            assertEquals("manifest column count on line " + (index + 1),
                    6, fields.length);
            assertEquals("schema_version on line " + (index + 1), "1", fields[0]);
            requirePattern(fields[1], "[a-z][a-z0-9_]*", "case_id", index + 1);
            assertTrue("duplicate case_id " + fields[1], caseIds.add(fields[1]));
            assertEquals("blob must be safe basename <case_id>.bin",
                    fields[1] + ".bin", fields[2]);
            assertTrue("duplicate blob " + fields[2], blobNames.add(fields[2]));
            long size = canonicalUnsigned(fields[3], "blob_size", index + 1);
            assertTrue("blob_size exceeds Java array range", size <= Integer.MAX_VALUE);
            requirePattern(fields[4], "[0-9a-f]{64}", "lowercase SHA-256", index + 1);
            assertTrue("canonical reason on line " + (index + 1), REASONS.contains(fields[5]));
            fixtures.add(new Fixture(fields[1], fields[2], (int) size, fields[4],
                    EntryOutcome.Reason.valueOf(fields[5])));
        }
        assertEquals("fixture count", 38, fixtures.size());
        assertEquals("exact frozen case IDs", CASES, caseIds);
        assertExactCorpus(root, blobNames);
        for (Fixture fixture : fixtures) {
            Path path = root.resolve(fixture.blob);
            byte[] blob = Files.readAllBytes(path);
            assertEquals(fixture.caseId + " blob_size", fixture.blobSize, blob.length);
            assertEquals(fixture.caseId + " blob_sha256", fixture.blobSha256,
                    sha256(blob));
        }
        return fixtures;
    }

    private static String[] readManifest(Path path) throws IOException {
        assertTrue("manifest must be a regular non-symlink file",
                Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                        && !isFilesystemAlias(path));
        byte[] bytes = Files.readAllBytes(path);
        for (byte value : bytes) {
            assertTrue("manifest must be ASCII", (value & 0xFF) <= 0x7F);
        }
        String text = new String(bytes, StandardCharsets.US_ASCII);
        assertFalse("manifest must use LF", text.contains("\r"));
        assertTrue("manifest must end with LF", text.endsWith("\n"));
        String[] lines = text.split("\n", -1);
        assertEquals("manifest header", HEADER, lines[0]);
        assertEquals("nothing may follow final LF", "", lines[lines.length - 1]);
        for (int index = 1; index < lines.length - 1; index++) {
            assertFalse("manifest may not contain empty rows", lines[index].isEmpty());
        }
        return lines;
    }

    private static void assertExactCorpus(Path root, Set<String> blobNames)
            throws Exception {
        Set<String> expected = new HashSet<>(blobNames);
        expected.add("manifest.tsv");
        List<Path> actualPaths;
        try (var entries = Files.list(root)) {
            actualPaths = entries.collect(Collectors.toList());
        }
        Set<String> actual = actualPaths.stream()
                .map(path -> path.getFileName().toString()).collect(Collectors.toSet());
        assertEquals("exact corpus", expected, actual);
        for (Path path : actualPaths) {
            assertTrue(path.getFileName() + " must be regular and non-symlink",
                    Files.isRegularFile(path, LinkOption.NOFOLLOW_LINKS)
                            && !isFilesystemAlias(path));
            assertEquals(path.getFileName() + " must have one hard link",
                    1L, hardLinkCount(path));
        }
        for (int left = 0; left < actualPaths.size(); left++) {
            for (int right = left + 1; right < actualPaths.size(); right++) {
                assertFalse("corpus files must not be hard-link aliases",
                        Files.isSameFile(actualPaths.get(left), actualPaths.get(right)));
            }
        }
    }

    private static long hardLinkCount(Path path) throws IOException {
        try {
            Number links = (Number) Files.getAttribute(
                    path, "unix:nlink", LinkOption.NOFOLLOW_LINKS);
            return links.longValue();
        } catch (UnsupportedOperationException | IllegalArgumentException unavailable) {
            if (!System.getProperty("os.name").startsWith("Windows")) {
                throw new AssertionError("platform cannot verify hard-link count", unavailable);
            }
        }

        Process query = new ProcessBuilder(
                "fsutil.exe", "hardlink", "list", path.toString())
                .redirectErrorStream(true)
                .start();
        byte[] output = query.getInputStream().readAllBytes();
        int exitCode;
        try {
            exitCode = query.waitFor();
        } catch (InterruptedException interrupted) {
            Thread.currentThread().interrupt();
            throw new IOException("interrupted while verifying hard-link count", interrupted);
        }
        if (exitCode != 0) {
            throw new AssertionError("Windows could not verify hard-link count for "
                    + path.getFileName());
        }
        long links = new String(output, StandardCharsets.ISO_8859_1).lines()
                .filter(line -> !line.isBlank()).count();
        if (links == 0) {
            throw new AssertionError("Windows returned no hard-link entries for "
                    + path.getFileName());
        }
        return links;
    }

    private static boolean isFilesystemAlias(Path path) throws IOException {
        if (Files.isSymbolicLink(path)) {
            return true;
        }
        try {
            Number attributes = (Number) Files.getAttribute(
                    path, "dos:attributes", LinkOption.NOFOLLOW_LINKS);
            return (attributes.intValue() & 0x400) != 0;
        } catch (UnsupportedOperationException | IllegalArgumentException unavailable) {
            return false;
        }
    }

    private static void requirePattern(
            String value, String pattern, String field, int lineNumber) {
        assertTrue(field + " on line " + lineNumber, value.matches(pattern));
    }

    private static long canonicalUnsigned(String value, String field, int lineNumber) {
        requirePattern(value, "0|[1-9][0-9]*", field, lineNumber);
        try {
            return Long.parseLong(value);
        } catch (NumberFormatException error) {
            throw new AssertionError(field + " out of range on line " + lineNumber, error);
        }
    }

    private static String sha256(byte[] blob) throws Exception {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(blob);
        StringBuilder result = new StringBuilder(64);
        for (byte value : digest) {
            result.append(String.format("%02x", value & 0xFF));
        }
        return result.toString();
    }

    private static Path copyCorpus(Path source, Path destination) throws IOException {
        Files.createDirectories(destination);
        try (var entries = Files.list(source)) {
            for (Path entry : entries.collect(Collectors.toList())) {
                Files.copy(entry, destination.resolve(entry.getFileName()),
                        StandardCopyOption.COPY_ATTRIBUTES);
            }
        }
        return destination;
    }

    private static List<String> manifestLines(Path root) throws IOException {
        return new ArrayList<>(Files.readAllLines(root.resolve("manifest.tsv"),
                StandardCharsets.US_ASCII));
    }

    private static void writeManifest(Path root, List<String> lines) throws IOException {
        Files.write(root.resolve("manifest.tsv"),
                (String.join("\n", lines) + "\n").getBytes(StandardCharsets.US_ASCII));
    }

    private static void replaceField(Path manifest, int row, int column, String value)
            throws IOException {
        List<String> lines = new ArrayList<>(Files.readAllLines(
                manifest, StandardCharsets.US_ASCII));
        String[] fields = lines.get(row + 1).split("\t", -1);
        fields[column] = value;
        lines.set(row + 1, String.join("\t", fields));
        Files.write(manifest,
                (String.join("\n", lines) + "\n").getBytes(StandardCharsets.US_ASCII));
    }

    private static void expectLoadFailure(Path root, String messageFragment) throws Exception {
        try {
            loadFixtures(root);
            fail("loader accepted malformed corpus: " + root.getFileName());
        } catch (AssertionError expected) {
            assertTrue("error must contain '" + messageFragment + "': " + expected.getMessage(),
                    expected.getMessage() != null
                            && expected.getMessage().contains(messageFragment));
        }
    }

    private static void deleteTree(Path root) throws IOException {
        if (!Files.exists(root)) {
            return;
        }
        try (var entries = Files.walk(root)) {
            for (Path path : entries.sorted(Comparator.reverseOrder())
                    .collect(Collectors.toList())) {
                Files.deleteIfExists(path);
            }
        }
    }

    private static final class Fixture {
        private final String caseId;
        private final String blob;
        private final int blobSize;
        private final String blobSha256;
        private final EntryOutcome.Reason expectedReason;

        private Fixture(String caseId, String blob, int blobSize, String blobSha256,
                        EntryOutcome.Reason expectedReason) {
            this.caseId = caseId;
            this.blob = blob;
            this.blobSize = blobSize;
            this.blobSha256 = blobSha256;
            this.expectedReason = expectedReason;
        }
    }
}
