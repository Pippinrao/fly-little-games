package com.flynes.emu.launch;

import com.flynes.emu.RomScanner;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.data.RomIdentity;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.ZipEntry;
import java.util.zip.ZipException;
import java.util.zip.ZipInputStream;

public final class ExactRomLoader {
    private final StreamOpener streamOpener;

    public ExactRomLoader(StreamOpener streamOpener) {
        this.streamOpener = Objects.requireNonNull(streamOpener, "stream opener");
    }

    public byte[] load(LaunchRequest request) throws LoadException {
        Objects.requireNonNull(request, "request");
        rejectExecutable(request);

        InputStream opened;
        try {
            opened = streamOpener.open(request.sourceId(), request.sourceUri());
        } catch (IOException | SecurityException failure) {
            throw new LoadException(
                    ErrorCode.SOURCE_OPEN_FAILED,
                    "could not open ROM source",
                    failure);
        }
        if (opened == null) {
            throw new LoadException(
                    ErrorCode.SOURCE_OPEN_FAILED, "stream opener returned no stream");
        }

        byte[] payload;
        try (InputStream source = opened) {
            if (request.packageFormat() == PackageFormat.RAW_NES) {
                payload = readPayload(source);
            } else {
                payload = readExactZipEntry(source, request.entryPath());
            }
        } catch (LoadException failure) {
            throw failure;
        } catch (ZipException failure) {
            throw new LoadException(
                    ErrorCode.INVALID_ZIP, "invalid ZIP package", failure);
        } catch (IOException failure) {
            throw new LoadException(ErrorCode.IO_ERROR, "could not read ROM payload", failure);
        }

        if (hasDosExecutableSignature(payload)) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable payloads cannot be loaded as ROMs");
        }
        RomIdentity actualIdentity = identityOf(payload);
        if (!actualIdentity.equals(request.identity())) {
            throw new LoadException(
                    ErrorCode.SHA1_MISMATCH,
                    "loaded payload SHA-1 does not match launch request");
        }
        return payload;
    }

    private static byte[] readExactZipEntry(InputStream source, String entryPath)
            throws IOException, LoadException {
        int matchingEntries = 0;
        boolean matchingEntryIsDirectory = false;
        byte[] payload = null;
        try (ZipInputStream zip = new ZipInputStream(source)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entryPath.equals(entry.getName())) {
                    matchingEntries++;
                    if (matchingEntries > 1) {
                        throw new LoadException(
                                ErrorCode.ZIP_ENTRY_DUPLICATE,
                                "ZIP contains duplicate exact entry path: " + entryPath);
                    }
                    matchingEntryIsDirectory = entry.isDirectory();
                    if (!matchingEntryIsDirectory) {
                        payload = readPayload(zip);
                    }
                }
                zip.closeEntry();
            }
        }
        if (matchingEntries == 0) {
            throw new LoadException(
                    ErrorCode.ZIP_ENTRY_MISSING,
                    "ZIP entry is missing: " + entryPath);
        }
        if (matchingEntryIsDirectory) {
            throw new LoadException(
                    ErrorCode.ZIP_ENTRY_IS_DIRECTORY,
                    "ZIP entry is a directory: " + entryPath);
        }
        return payload;
    }

    private static byte[] readPayload(InputStream input) throws IOException, LoadException {
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        long total = 0;
        while (true) {
            int count = input.read(buffer);
            if (count < 0) {
                break;
            }
            if (count == 0) {
                int singleByte = input.read();
                if (singleByte < 0) {
                    break;
                }
                total++;
                if (total > RomScanner.MAX_ROM_BYTES) {
                    throw payloadTooLarge();
                }
                output.write(singleByte);
                continue;
            }
            total += count;
            if (total > RomScanner.MAX_ROM_BYTES) {
                throw payloadTooLarge();
            }
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    private static LoadException payloadTooLarge() {
        return new LoadException(
                ErrorCode.PAYLOAD_TOO_LARGE,
                "ROM payload exceeds " + RomScanner.MAX_ROM_BYTES + " bytes");
    }

    private static void rejectExecutable(LaunchRequest request) throws LoadException {
        String exactName = request.packageFormat() == PackageFormat.ZIP
                ? request.entryPath()
                : request.sourceUri();
        int queryStart = exactName.indexOf('?');
        if (queryStart >= 0) {
            exactName = exactName.substring(0, queryStart);
        }
        int fragmentStart = exactName.indexOf('#');
        if (fragmentStart >= 0) {
            exactName = exactName.substring(0, fragmentStart);
        }
        if (exactName.toLowerCase(Locale.ROOT).endsWith(".exe")) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable files cannot be loaded as ROMs");
        }
    }

    private static boolean hasDosExecutableSignature(byte[] payload) {
        return payload.length >= 2 && payload[0] == 'M' && payload[1] == 'Z';
    }

    private static RomIdentity identityOf(byte[] payload) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-1").digest(payload);
            StringBuilder sha1 = new StringBuilder(40);
            for (byte value : digest) {
                sha1.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            return new RomIdentity(sha1.toString());
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-1 is required by the Java platform", impossible);
        }
    }

    @FunctionalInterface
    public interface StreamOpener {
        InputStream open(String sourceId, String sourceUri) throws IOException, SecurityException;
    }

    public enum ErrorCode {
        SOURCE_OPEN_FAILED,
        IO_ERROR,
        INVALID_ZIP,
        ZIP_ENTRY_MISSING,
        ZIP_ENTRY_DUPLICATE,
        ZIP_ENTRY_IS_DIRECTORY,
        PAYLOAD_TOO_LARGE,
        EXECUTABLE_REJECTED,
        SHA1_MISMATCH
    }

    public static final class LoadException extends Exception {
        private final ErrorCode code;

        LoadException(ErrorCode code, String message) {
            super(message);
            this.code = Objects.requireNonNull(code, "code");
        }

        LoadException(ErrorCode code, String message, Throwable cause) {
            super(message, cause);
            this.code = Objects.requireNonNull(code, "code");
        }

        public ErrorCode code() {
            return code;
        }
    }
}
