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
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.LinkedHashMap;
import java.util.Locale;
import java.util.Map;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

public final class ExactRomLoaderTest {
    @Test
    public void launchRequestMapCodecRoundTripsExactLocation() {
        LaunchRequest request = zipRequest(
                "content://games/collection.zip", "nested/SECOND.nes", bytes("second"));

        Map<String, String> encoded = request.toMap();

        assertEquals(request, LaunchRequest.fromMap(encoded));
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
