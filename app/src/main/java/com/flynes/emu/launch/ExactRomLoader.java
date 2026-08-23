package com.flynes.emu.launch;

import com.flynes.emu.RomScanner;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.data.RomIdentity;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import java.util.Objects;
import java.util.zip.CRC32;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

public final class ExactRomLoader {
    public static final int MAX_ZIP_ENTRIES = 1024;
    public static final long MAX_ZIP_SOURCE_BYTES = RomScanner.MAX_ROM_BYTES * 2L;
    public static final long MAX_ZIP_INFLATED_BYTES = RomScanner.MAX_ROM_BYTES * 4L;

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
                payload = readExactZipEntry(source, request);
            }
        } catch (LoadException failure) {
            throw failure;
        } catch (IOException | SecurityException failure) {
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

    private static byte[] readExactZipEntry(InputStream source, LaunchRequest request)
            throws IOException, LoadException {
        byte[] archive = readZipSource(source);
        ZipStructure structure = validateZipStructure(archive);
        String entryPath = request.entryPath();
        ZipEntryIdentity requestedIdentity = request.zipEntryIdentity();
        byte[] requestedRawName = requestedIdentity == null
                ? entryPath.getBytes(StandardCharsets.UTF_8)
                : requestedIdentity.rawNameBytes();

        int entryCount = 0;
        int matchingEntries = 0;
        boolean matchingEntryIsDirectory = false;
        byte[] payload = null;
        long totalInflatedBytes = 0;
        byte[] buffer = new byte[8192];
        try (ZipInputStream zip = new ZipInputStream(
                new ByteArrayInputStream(archive), StandardCharsets.ISO_8859_1)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                if (entryCount >= structure.entries().size()) {
                    throw invalidZip("ZIP local and central entry counts differ");
                }
                CentralEntryMetadata centralEntry = structure.entries().get(entryCount);
                entryCount++;
                if (entryCount > MAX_ZIP_ENTRIES) {
                    throw zipEntryLimitExceeded();
                }
                validateStreamHeader(entry, centralEntry);

                boolean rawNameMatches = Arrays.equals(
                        requestedRawName, centralEntry.rawName());
                boolean offsetMatches = requestedIdentity == null
                        || requestedIdentity.localHeaderOffset()
                        == centralEntry.localHeaderOffset();
                boolean matches = rawNameMatches && offsetMatches;
                ByteArrayOutputStream selectedPayload = null;
                long selectedPayloadBytes = 0;
                long entryInflatedBytes = 0;
                CRC32 entryCrc = new CRC32();
                if (matches) {
                    matchingEntries++;
                    if (matchingEntries > 1) {
                        throw new LoadException(
                                ErrorCode.ZIP_ENTRY_DUPLICATE,
                                "ZIP contains duplicate exact entry path: " + entryPath);
                    }
                    matchingEntryIsDirectory = entry.isDirectory();
                    if (!matchingEntryIsDirectory) {
                        selectedPayload = new ByteArrayOutputStream();
                    }
                }

                while (true) {
                    int count = zip.read(buffer);
                    if (count < 0) {
                        break;
                    }
                    if (count == 0) {
                        int singleByte = zip.read();
                        if (singleByte < 0) {
                            break;
                        }
                        totalInflatedBytes = checkedInflatedTotal(totalInflatedBytes, 1);
                        entryInflatedBytes++;
                        entryCrc.update(singleByte);
                        if (selectedPayload != null) {
                            selectedPayloadBytes = checkedPayloadTotal(selectedPayloadBytes, 1);
                            selectedPayload.write(singleByte);
                        }
                        continue;
                    }
                    totalInflatedBytes = checkedInflatedTotal(totalInflatedBytes, count);
                    entryInflatedBytes += count;
                    entryCrc.update(buffer, 0, count);
                    if (selectedPayload != null) {
                        selectedPayloadBytes = checkedPayloadTotal(selectedPayloadBytes, count);
                        selectedPayload.write(buffer, 0, count);
                    }
                }
                if (selectedPayload != null) {
                    payload = selectedPayload.toByteArray();
                }
                zip.closeEntry();
                validateStreamedEntry(entry, centralEntry, entryCrc.getValue(), entryInflatedBytes);
            }
        } catch (LoadException failure) {
            throw failure;
        } catch (IOException | IllegalArgumentException failure) {
            throw new LoadException(ErrorCode.INVALID_ZIP, "invalid ZIP package", failure);
        }
        if (entryCount != structure.entries().size()) {
            throw invalidZip("ZIP local and central entry counts differ");
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

    private static byte[] readZipSource(InputStream input) throws IOException, LoadException {
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
                if (total > MAX_ZIP_SOURCE_BYTES) {
                    throw zipSourceTooLarge();
                }
                output.write(singleByte);
                continue;
            }
            total += count;
            if (total > MAX_ZIP_SOURCE_BYTES) {
                throw zipSourceTooLarge();
            }
            output.write(buffer, 0, count);
        }
        return output.toByteArray();
    }

    private static ZipStructure validateZipStructure(byte[] archive) throws LoadException {
        int endRecord = findEndRecord(archive);
        int diskNumber = unsignedShort(archive, endRecord + 4);
        int centralDirectoryDisk = unsignedShort(archive, endRecord + 6);
        int entriesOnDisk = unsignedShort(archive, endRecord + 8);
        int totalEntries = unsignedShort(archive, endRecord + 10);
        long centralDirectorySize = unsignedInt(archive, endRecord + 12);
        long centralDirectoryOffset = unsignedInt(archive, endRecord + 16);
        if (totalEntries > MAX_ZIP_ENTRIES) {
            throw zipEntryLimitExceeded();
        }
        if (diskNumber != 0 || centralDirectoryDisk != 0 || entriesOnDisk != totalEntries) {
            throw invalidZip("split ZIP archives are unsupported");
        }
        if (totalEntries == 0xFFFF
                || centralDirectorySize == 0xFFFFFFFFL
                || centralDirectoryOffset == 0xFFFFFFFFL) {
            throw invalidZip("ZIP64 archives are unsupported");
        }
        long centralDirectoryEnd = centralDirectoryOffset + centralDirectorySize;
        if (centralDirectoryEnd != endRecord) {
            throw invalidZip("ZIP central directory bounds are invalid");
        }
        if (centralDirectoryOffset > Integer.MAX_VALUE) {
            throw invalidZip("ZIP central directory offset is invalid");
        }

        int cursor = (int) centralDirectoryOffset;
        ArrayList<CentralEntryMetadata> entries = new ArrayList<>(totalEntries);
        for (int i = 0; i < totalEntries; i++) {
            requireRange(archive, cursor, 46, "ZIP central directory is truncated");
            if (unsignedInt(archive, cursor) != 0x02014B50L) {
                throw invalidZip("ZIP central-directory entry signature is invalid");
            }
            int nameLength = unsignedShort(archive, cursor + 28);
            int extraLength = unsignedShort(archive, cursor + 30);
            int commentLength = unsignedShort(archive, cursor + 32);
            if (unsignedShort(archive, cursor + 34) != 0) {
                throw invalidZip("split ZIP entries are unsupported");
            }
            long localHeaderOffset = unsignedInt(archive, cursor + 42);
            if (localHeaderOffset == 0xFFFFFFFFL || localHeaderOffset > Integer.MAX_VALUE) {
                throw invalidZip("ZIP64 local headers are unsupported");
            }
            long centralEntryEnd = (long) cursor + 46L
                    + nameLength + extraLength + commentLength;
            if (centralEntryEnd > centralDirectoryEnd) {
                throw invalidZip("ZIP central-directory entry is truncated");
            }
            byte[] rawName = Arrays.copyOfRange(
                    archive, cursor + 46, cursor + 46 + nameLength);
            int flags = unsignedShort(archive, cursor + 8);
            if ((flags & 0x0800) != 0) {
                decodeStrict(rawName, StandardCharsets.UTF_8);
            }
            CentralEntryMetadata entry = new CentralEntryMetadata(
                    rawName,
                    (int) localHeaderOffset,
                    flags,
                    unsignedShort(archive, cursor + 10),
                    unsignedInt(archive, cursor + 16),
                    unsignedInt(archive, cursor + 20),
                    unsignedInt(archive, cursor + 24));
            validateLocalHeader(
                    archive,
                    entry,
                    cursor + 46,
                    nameLength,
                    centralDirectoryOffset);
            entries.add(entry);
            cursor = (int) centralEntryEnd;
        }
        if (cursor != centralDirectoryEnd) {
            throw invalidZip("ZIP central-directory entry count is invalid");
        }
        entries.sort(Comparator.comparingInt(CentralEntryMetadata::localHeaderOffset));
        for (int i = 1; i < entries.size(); i++) {
            if (entries.get(i - 1).localHeaderOffset() == entries.get(i).localHeaderOffset()) {
                throw invalidZip("ZIP entries share a local-header offset");
            }
        }
        return new ZipStructure(List.copyOf(entries));
    }

    private static void validateLocalHeader(
            byte[] archive,
            CentralEntryMetadata centralEntry,
            int centralNameOffset,
            int centralNameLength,
            long centralDirectoryOffset) throws LoadException {
        int localHeaderOffset = centralEntry.localHeaderOffset();
        requireRange(archive, localHeaderOffset, 30, "ZIP local header is truncated");
        if (unsignedInt(archive, localHeaderOffset) != 0x04034B50L) {
            throw invalidZip("ZIP local-entry signature is invalid");
        }
        int localFlags = unsignedShort(archive, localHeaderOffset + 6);
        int localMethod = unsignedShort(archive, localHeaderOffset + 8);
        if (localFlags != centralEntry.flags()) {
            throw invalidZip("ZIP local and central entry flags differ");
        }
        if (localMethod != centralEntry.method()) {
            throw invalidZip("ZIP local and central compression methods differ");
        }
        int localNameLength = unsignedShort(archive, localHeaderOffset + 26);
        int localExtraLength = unsignedShort(archive, localHeaderOffset + 28);
        if (localNameLength != centralNameLength) {
            throw invalidZip("ZIP local and central entry names differ");
        }
        requireRange(
                archive,
                localHeaderOffset + 30,
                localNameLength + localExtraLength,
                "ZIP local entry metadata is truncated");
        for (int i = 0; i < localNameLength; i++) {
            if (archive[localHeaderOffset + 30 + i] != archive[centralNameOffset + i]) {
                throw invalidZip("ZIP local and central entry names differ");
            }
        }
        long dataOffset = (long) localHeaderOffset + 30L
                + localNameLength + localExtraLength;
        if (dataOffset + centralEntry.compressedSize() > centralDirectoryOffset) {
            throw invalidZip("ZIP entry payload is truncated");
        }
        long localCrc = unsignedInt(archive, localHeaderOffset + 14);
        long localCompressedSize = unsignedInt(archive, localHeaderOffset + 18);
        long localUncompressedSize = unsignedInt(archive, localHeaderOffset + 22);
        boolean usesDataDescriptor = (localFlags & 0x0008) != 0;
        if (usesDataDescriptor) {
            requireZeroOrEqual(localCrc, centralEntry.crc32(), "CRC");
            requireZeroOrEqual(
                    localCompressedSize, centralEntry.compressedSize(), "compressed size");
            requireZeroOrEqual(
                    localUncompressedSize, centralEntry.uncompressedSize(), "size");
        } else if (localCrc != centralEntry.crc32()
                || localCompressedSize != centralEntry.compressedSize()
                || localUncompressedSize != centralEntry.uncompressedSize()) {
            throw invalidZip("ZIP local and central CRC or sizes differ");
        }
    }

    private static void requireZeroOrEqual(long local, long central, String field)
            throws LoadException {
        if (local != 0 && local != central) {
            throw invalidZip("ZIP local and central " + field + " differ");
        }
    }

    private static void validateStreamHeader(
            ZipEntry streamedEntry, CentralEntryMetadata centralEntry) throws LoadException {
        if (streamedEntry.getMethod() != centralEntry.method()) {
            throw invalidZip("ZIP streamed and central compression methods differ");
        }
    }

    private static String decodeStrict(byte[] bytes, java.nio.charset.Charset charset)
            throws LoadException {
        try {
            return charset.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(bytes))
                    .toString();
        } catch (CharacterCodingException failure) {
            throw new LoadException(
                    ErrorCode.INVALID_ZIP, "ZIP entry name has invalid encoding", failure);
        }
    }

    private static void validateStreamedEntry(
            ZipEntry streamedEntry,
            CentralEntryMetadata centralEntry,
            long actualCrc,
            long actualUncompressedSize) throws LoadException {
        if (actualCrc != centralEntry.crc32()
                || streamedEntry.getCrc() != centralEntry.crc32()) {
            throw invalidZip("ZIP central CRC does not match inflated entry");
        }
        if (actualUncompressedSize != centralEntry.uncompressedSize()
                || streamedEntry.getSize() != centralEntry.uncompressedSize()) {
            throw invalidZip("ZIP central size does not match inflated entry");
        }
        if (streamedEntry.getCompressedSize() != centralEntry.compressedSize()) {
            throw invalidZip("ZIP central compressed size does not match streamed entry");
        }
    }

    private static int findEndRecord(byte[] archive) throws LoadException {
        if (archive.length < 22) {
            throw invalidZip("ZIP end record is missing");
        }
        int earliest = Math.max(0, archive.length - 22 - 0xFFFF);
        for (int offset = archive.length - 22; offset >= earliest; offset--) {
            if (unsignedInt(archive, offset) != 0x06054B50L) {
                continue;
            }
            int commentLength = unsignedShort(archive, offset + 20);
            if ((long) offset + 22L + commentLength == archive.length) {
                return offset;
            }
        }
        throw invalidZip("ZIP end record is missing or truncated");
    }

    private static int unsignedShort(byte[] bytes, int offset) throws LoadException {
        requireRange(bytes, offset, 2, "ZIP structure is truncated");
        return (bytes[offset] & 0xFF) | ((bytes[offset + 1] & 0xFF) << 8);
    }

    private static long unsignedInt(byte[] bytes, int offset) throws LoadException {
        requireRange(bytes, offset, 4, "ZIP structure is truncated");
        return (bytes[offset] & 0xFFL)
                | ((bytes[offset + 1] & 0xFFL) << 8)
                | ((bytes[offset + 2] & 0xFFL) << 16)
                | ((bytes[offset + 3] & 0xFFL) << 24);
    }

    private static void requireRange(byte[] bytes, int offset, int length, String message)
            throws LoadException {
        if (offset < 0 || length < 0 || (long) offset + length > bytes.length) {
            throw invalidZip(message);
        }
    }

    private static long checkedInflatedTotal(long current, int added) throws LoadException {
        long total = current + added;
        if (total > MAX_ZIP_INFLATED_BYTES) {
            throw new LoadException(
                    ErrorCode.ZIP_INFLATED_LIMIT_EXCEEDED,
                    "ZIP inflated data exceeds " + MAX_ZIP_INFLATED_BYTES + " bytes");
        }
        return total;
    }

    private static long checkedPayloadTotal(long current, int added) throws LoadException {
        long total = current + added;
        if (total > RomScanner.MAX_ROM_BYTES) {
            throw payloadTooLarge();
        }
        return total;
    }

    private static LoadException zipSourceTooLarge() {
        return new LoadException(
                ErrorCode.ZIP_SOURCE_LIMIT_EXCEEDED,
                "ZIP source exceeds " + MAX_ZIP_SOURCE_BYTES + " bytes");
    }

    private static LoadException zipEntryLimitExceeded() {
        return new LoadException(
                ErrorCode.ZIP_ENTRY_LIMIT_EXCEEDED,
                "ZIP contains more than " + MAX_ZIP_ENTRIES + " entries");
    }

    private static LoadException invalidZip(String message) {
        return new LoadException(ErrorCode.INVALID_ZIP, message);
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
        ZipEntryIdentity exactZipEntry = request.zipEntryIdentity();
        if (exactZipEntry != null
                && endsWithAsciiIgnoreCase(exactZipEntry.rawNameBytes(), ".exe")) {
            throw new LoadException(
                    ErrorCode.EXECUTABLE_REJECTED,
                    "executable ZIP entries cannot be loaded as ROMs");
        }
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

    private static boolean endsWithAsciiIgnoreCase(byte[] value, String suffix) {
        if (value.length < suffix.length()) {
            return false;
        }
        int start = value.length - suffix.length();
        for (int i = 0; i < suffix.length(); i++) {
            int actual = value[start + i] & 0xFF;
            int expected = suffix.charAt(i);
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

    private record ZipStructure(List<CentralEntryMetadata> entries) {
    }

    private record CentralEntryMetadata(
            byte[] rawName,
            int localHeaderOffset,
            int flags,
            int method,
            long crc32,
            long compressedSize,
            long uncompressedSize) {
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
