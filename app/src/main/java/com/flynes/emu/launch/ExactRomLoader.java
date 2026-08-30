package com.flynes.emu.launch;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.scan.BoundedZipArchive;
import com.flynes.emu.catalog.scan.ScanLimits;

import java.io.IOException;
import java.io.InputStream;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.CRC32;

/** Opens an exact immutable launch token and verifies physical and payload fingerprints. */
public final class ExactRomLoader {
    public static final int MAX_ZIP_ENTRIES = ScanLimits.defaults().maxZipEntries();
    public static final long MAX_ZIP_SOURCE_BYTES = ScanLimits.defaults().maxPackageBytes();
    public static final long MAX_ZIP_INFLATED_BYTES =
            ScanLimits.defaults().maxCumulativeInflatedBytes();

    private final StreamOpener streamOpener;
    private final ScanLimits limits;

    public ExactRomLoader(StreamOpener streamOpener) {
        this(streamOpener, ScanLimits.defaults());
    }

    public ExactRomLoader(StreamOpener streamOpener, ScanLimits limits) {
        this.streamOpener = Objects.requireNonNull(streamOpener, "stream opener");
        this.limits = Objects.requireNonNull(limits, "scan limits");
    }

    public byte[] load(LaunchRequest request) throws LoadException {
        Objects.requireNonNull(request, "request");
        rejectExecutableLocator(request);
        InputStream opened;
        try {
            opened = streamOpener.open(request.sourceId(), request.sourceUri());
        } catch (IOException | SecurityException failure) {
            throw new LoadException(
                    ErrorCode.SOURCE_OPEN_FAILED, "could not open ROM source", failure);
        }
        if (opened == null) {
            throw new LoadException(
                    ErrorCode.SOURCE_OPEN_FAILED, "stream opener returned no stream");
        }

        byte[] physicalBytes;
        try (InputStream source = opened) {
            physicalBytes = BoundedZipArchive.readBounded(source, limits.maxPackageBytes());
        } catch (BoundedZipArchive.ArchiveException failure) {
            throw map(failure);
        } catch (IOException | SecurityException failure) {
            throw new LoadException(ErrorCode.IO_ERROR, "could not read ROM package", failure);
        }
        verifyHexDigest(
                "SHA-256",
                physicalBytes,
                request.hashes().physicalPackageSha256(),
                "physical package SHA-256");
        byte[] payload;
        if (request.packageFormat() == PackageFormat.RAW) {
            payload = physicalBytes;
        } else {
            BoundedZipArchive.Archive archive;
            try {
                archive = BoundedZipArchive.fromBytes(physicalBytes, limits);
                BoundedZipArchive.Entry entry = archive.requireExact(request.zipEntryIdentity());
                if (entry.isDirectory()) {
                    throw new LoadException(
                            ErrorCode.ZIP_ENTRY_IS_DIRECTORY,
                            "exact ZIP locator points to a directory");
                }
                payload = entry.readPayload();
            } catch (BoundedZipArchive.ArchiveException failure) {
                throw map(failure);
            }
        }
        if (payload.length > limits.maxPayloadBytes()) {
            throw new LoadException(
                    ErrorCode.PAYLOAD_TOO_LARGE, "ROM payload exceeds the payload limit");
        }
        if (hasDosExecutableSignature(payload)) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable payloads cannot be loaded as ROMs");
        }
        verifyHexDigest(
                "SHA-256", payload, request.hashes().payloadSha256(), "payload SHA-256");
        verifyHexDigest("SHA-1", payload, request.hashes().payloadSha1(), "payload SHA-1");
        CRC32 crc32 = new CRC32();
        crc32.update(payload);
        String actualCrc = String.format(Locale.ROOT, "%08X", crc32.getValue());
        if (!actualCrc.equals(request.hashes().crc32())) {
            throw new LoadException(
                    ErrorCode.HASH_MISMATCH, "payload CRC32 does not match launch request");
        }
        return payload;
    }

    private static void verifyHexDigest(
            String algorithm, byte[] bytes, String expected, String description)
            throws LoadException {
        String actual;
        try {
            StringBuilder hex = new StringBuilder(expected.length());
            for (byte value : MessageDigest.getInstance(algorithm).digest(bytes)) {
                hex.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
            }
            actual = hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(algorithm + " is required", impossible);
        }
        if (!actual.equals(expected)) {
            throw new LoadException(
                    ErrorCode.HASH_MISMATCH, description + " does not match launch request");
        }
    }

    private static void rejectExecutableLocator(LaunchRequest request) throws LoadException {
        if (request.zipEntryIdentity() != null
                && endsWithAsciiIgnoreCase(
                        request.zipEntryIdentity().rawNameBytes(), ".exe")) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable ZIP entries cannot be loaded as ROMs");
        }
        String displayLocator = request.entryPath() == null
                ? request.sourceUri() : request.entryPath();
        int query = displayLocator.indexOf('?');
        if (query >= 0) {
            displayLocator = displayLocator.substring(0, query);
        }
        int fragment = displayLocator.indexOf('#');
        if (fragment >= 0) {
            displayLocator = displayLocator.substring(0, fragment);
        }
        if (displayLocator.toLowerCase(Locale.ROOT).endsWith(".exe")) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable files cannot be loaded as ROMs");
        }
    }

    private static boolean endsWithAsciiIgnoreCase(byte[] value, String suffix) {
        if (value.length < suffix.length()) {
            return false;
        }
        int start = value.length - suffix.length();
        for (int index = 0; index < suffix.length(); index++) {
            int actual = value[start + index] & 0xFF;
            int expected = suffix.charAt(index);
            if (actual >= 'A' && actual <= 'Z') {
                actual += 'a' - 'A';
            }
            if (actual != expected) {
                return false;
            }
        }
        return true;
    }

    private static boolean hasDosExecutableSignature(byte[] payload) {
        return payload.length >= 2 && payload[0] == 'M' && payload[1] == 'Z';
    }

    private static LoadException map(BoundedZipArchive.ArchiveException failure) {
        ErrorCode code = switch (failure.code()) {
            case PACKAGE_LIMIT_EXCEEDED -> ErrorCode.ZIP_SOURCE_LIMIT_EXCEEDED;
            case ENTRY_LIMIT_EXCEEDED -> ErrorCode.ZIP_ENTRY_LIMIT_EXCEEDED;
            case INFLATED_LIMIT_EXCEEDED -> ErrorCode.ZIP_INFLATED_LIMIT_EXCEEDED;
            case PAYLOAD_LIMIT_EXCEEDED -> ErrorCode.PAYLOAD_TOO_LARGE;
            case NAME_LIMIT_EXCEEDED -> ErrorCode.ZIP_NAME_LIMIT_EXCEEDED;
            case RATIO_LIMIT_EXCEEDED -> ErrorCode.ZIP_RATIO_LIMIT_EXCEEDED;
            case ENTRY_MISSING -> ErrorCode.ZIP_ENTRY_MISSING;
            case INVALID_ZIP, ENCRYPTED, UNSUPPORTED_COMPRESSION -> ErrorCode.INVALID_ZIP;
        };
        return new LoadException(code, failure.getMessage(), failure);
    }

    @FunctionalInterface
    public interface StreamOpener {
        InputStream open(String sourceId, String sourceUri) throws IOException, SecurityException;
    }

    public enum ErrorCode {
        SOURCE_OPEN_FAILED,
        IO_ERROR,
        INVALID_ZIP,
        ZIP_SOURCE_LIMIT_EXCEEDED,
        ZIP_ENTRY_LIMIT_EXCEEDED,
        ZIP_INFLATED_LIMIT_EXCEEDED,
        ZIP_NAME_LIMIT_EXCEEDED,
        ZIP_RATIO_LIMIT_EXCEEDED,
        ZIP_ENTRY_MISSING,
        ZIP_ENTRY_DUPLICATE,
        ZIP_ENTRY_IS_DIRECTORY,
        PAYLOAD_TOO_LARGE,
        EXECUTABLE_REJECTED,
        HASH_MISMATCH
    }

    public static final class LoadException extends Exception {
        private final ErrorCode code;

        LoadException(ErrorCode code, String message) {
            super(message);
            this.code = DomainValidation.requireNonNull(code, "load error code");
        }

        LoadException(ErrorCode code, String message, Throwable cause) {
            super(message, cause);
            this.code = DomainValidation.requireNonNull(code, "load error code");
        }

        public ErrorCode code() {
            return code;
        }
    }
}
