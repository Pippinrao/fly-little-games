package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipEntryNameDecoder;
import com.flynes.emu.catalog.ZipNameEncoding;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.Comparator;
import java.util.List;
import java.util.zip.CRC32;
import java.util.zip.DataFormatException;
import java.util.zip.Inflater;

/**
 * One API24-safe ZIP implementation shared by catalog scanning and exact launch loading.
 * It validates EOCD, central/local identity, compression metadata, descriptors, and declared
 * bounds before exposing metadata. Payload bytes are inflated and integrity-checked only when the
 * caller selects an entry, so the archive retains one physical byte array and no resident payloads.
 */
public final class BoundedZipArchive {
    private static final long LOCAL_SIGNATURE = 0x04034B50L;
    private static final long CENTRAL_SIGNATURE = 0x02014B50L;
    private static final long END_SIGNATURE = 0x06054B50L;
    private static final int FLAG_ENCRYPTED = 0x0001;
    private static final int FLAG_DATA_DESCRIPTOR = 0x0008;

    private BoundedZipArchive() {
    }

    public static Archive open(InputStream input, ScanLimits limits)
            throws IOException, ArchiveException {
        DomainValidation.requireNonNull(input, "ZIP input");
        return fromBytes(readBounded(input, limits.maxPackageBytes()), limits);
    }

    public static Archive fromBytes(byte[] archive, ScanLimits limits) throws ArchiveException {
        DomainValidation.requireNonNull(archive, "ZIP bytes");
        DomainValidation.requireNonNull(limits, "scan limits");
        if (archive.length > limits.maxPackageBytes()) {
            throw failure(Code.PACKAGE_LIMIT_EXCEEDED, "ZIP package is over the source limit");
        }
        List<Integer> endRecords = findEndRecords(archive);
        Integer structuralEndRecord = null;
        ArchiveException firstStructuralFailure = null;
        // Candidate uniqueness must not change when a caller tightens open-time policy.
        for (int endRecord : endRecords) {
            try {
                fromBytesAtEndRecord(archive, limits, endRecord, false);
            } catch (ArchiveException error) {
                if (firstStructuralFailure == null) {
                    firstStructuralFailure = error;
                }
                continue;
            }
            if (structuralEndRecord != null) {
                throw invalid("ZIP end record is ambiguous");
            }
            structuralEndRecord = endRecord;
        }
        if (structuralEndRecord != null) {
            return fromBytesAtEndRecord(archive, limits, structuralEndRecord, true);
        }

        // Preserve the legacy error precedence for an archive with no supported structural
        // interpretation. In particular, an entry limit can precede the ZIP64 sentinel error.
        fromBytesAtEndRecord(archive, limits, endRecords.get(0), true);
        if (firstStructuralFailure != null) {
            throw firstStructuralFailure;
        }
        throw invalid("ZIP end record is missing or truncated");
    }

    private static Archive fromBytesAtEndRecord(
            byte[] archive, ScanLimits limits, int endRecord, boolean applyPolicy)
            throws ArchiveException {
        int diskNumber = unsignedShort(archive, endRecord + 4);
        int centralDirectoryDisk = unsignedShort(archive, endRecord + 6);
        int entriesOnDisk = unsignedShort(archive, endRecord + 8);
        int totalEntries = unsignedShort(archive, endRecord + 10);
        long centralSize = unsignedInt(archive, endRecord + 12);
        long centralOffset = unsignedInt(archive, endRecord + 16);
        if (applyPolicy && totalEntries > limits.maxZipEntries()) {
            throw failure(Code.ENTRY_LIMIT_EXCEEDED, "ZIP entry count exceeds the limit");
        }
        // Multi-disk and ZIP64 layouts are parser support boundaries, not caller policy.
        if (diskNumber != 0 || centralDirectoryDisk != 0 || entriesOnDisk != totalEntries) {
            throw invalid("split ZIP archives are unsupported");
        }
        if (totalEntries == 0xFFFF
                || centralSize == 0xFFFFFFFFL
                || centralOffset == 0xFFFFFFFFL) {
            throw invalid("ZIP64 archives are unsupported");
        }
        long centralEnd = checkedAdd(centralOffset, centralSize, "central directory overflows");
        if (centralEnd != endRecord || centralOffset > Integer.MAX_VALUE) {
            throw invalid("ZIP central directory bounds are invalid");
        }

        ArrayList<CentralEntry> centralEntries = new ArrayList<>(totalEntries);
        int cursor = (int) centralOffset;
        for (int index = 0; index < totalEntries; index++) {
            requireRange(archive, cursor, 46, "ZIP central directory is truncated");
            if (unsignedInt(archive, cursor) != CENTRAL_SIGNATURE) {
                throw invalid("ZIP central directory signature is invalid");
            }
            int flags = unsignedShort(archive, cursor + 8);
            int method = unsignedShort(archive, cursor + 10);
            int nameLength = unsignedShort(archive, cursor + 28);
            int extraLength = unsignedShort(archive, cursor + 30);
            int commentLength = unsignedShort(archive, cursor + 32);
            if (nameLength == 0) {
                throw invalid("ZIP entry name is empty");
            }
            if (applyPolicy && nameLength > limits.maxNameBytes()) {
                throw failure(Code.NAME_LIMIT_EXCEEDED, "ZIP entry name exceeds the limit");
            }
            if (unsignedShort(archive, cursor + 34) != 0) {
                throw invalid("split ZIP entries are unsupported");
            }
            long localOffset = unsignedInt(archive, cursor + 42);
            if (localOffset == 0xFFFFFFFFL || localOffset > Integer.MAX_VALUE) {
                throw invalid("ZIP64 local headers are unsupported");
            }
            long centralEntryEnd = checkedAdd(
                    cursor + 46L,
                    (long) nameLength + extraLength + commentLength,
                    "ZIP central entry overflows");
            if (centralEntryEnd > centralEnd) {
                throw invalid("ZIP central entry is truncated");
            }
            byte[] rawName = Arrays.copyOfRange(
                    archive, cursor + 46, cursor + 46 + nameLength);
            byte[] centralExtra = Arrays.copyOfRange(
                    archive,
                    cursor + 46 + nameLength,
                    cursor + 46 + nameLength + extraLength);
            if (applyPolicy && (flags & FLAG_ENCRYPTED) != 0) {
                throw failure(Code.ENCRYPTED, "encrypted ZIP entries are unsupported");
            }
            if (applyPolicy && method != 0 && method != 8) {
                throw failure(Code.UNSUPPORTED_COMPRESSION,
                        "ZIP compression method is unsupported");
            }
            try {
                ZipEntryNameDecoder.decode(
                        rawName, flags, centralExtra, ZipNameEncoding.CP437);
            } catch (IllegalArgumentException malformedEfsName) {
                throw new ArchiveException(
                        Code.INVALID_ZIP, "ZIP entry name encoding is invalid", malformedEfsName);
            }
            CentralEntry entry = new CentralEntry(
                    rawName,
                    centralExtra,
                    (int) localOffset,
                    flags,
                    method,
                    unsignedInt(archive, cursor + 16),
                    unsignedInt(archive, cursor + 20),
                    unsignedInt(archive, cursor + 24));
            if (applyPolicy) {
                validateRatio(entry, limits);
            }
            centralEntries.add(entry);
            cursor = (int) centralEntryEnd;
        }
        if (cursor != centralEnd) {
            throw invalid("ZIP central entry count is invalid");
        }

        centralEntries.sort(Comparator.comparingInt(CentralEntry::localHeaderOffset));
        long declaredInflated = 0;
        ArrayList<LocalEntry> localEntries = new ArrayList<>(centralEntries.size());
        long previousEntryEnd = -1;
        for (int index = 0; index < centralEntries.size(); index++) {
            CentralEntry central = centralEntries.get(index);
            if (previousEntryEnd > central.localHeaderOffset()) {
                throw invalid("ZIP local entries overlap");
            }
            long descriptorBoundary = index + 1 < centralEntries.size()
                    ? Math.min(centralEntries.get(index + 1).localHeaderOffset(), centralOffset)
                    : centralOffset;
            LocalEntry local = validateLocalHeader(
                    archive, central, centralOffset, descriptorBoundary);
            previousEntryEnd = local.entryEnd();
            if (applyPolicy) {
                declaredInflated = checkedInflated(
                        declaredInflated, central.uncompressedSize(), limits);
            }
            localEntries.add(local);
        }
        if ((centralEntries.isEmpty() && centralOffset != 0)
                || (!centralEntries.isEmpty()
                && centralEntries.get(0).localHeaderOffset() != 0)) {
            throw invalid("ZIP prefix bytes are unsupported");
        }

        ArrayList<Entry> entries = new ArrayList<>(localEntries.size());
        for (LocalEntry local : localEntries) {
            entries.add(new Entry(archive, local, limits));
        }
        return new Archive(archive, entries);
    }

    public static byte[] readBounded(InputStream input, long limit)
            throws IOException, ArchiveException {
        if (limit <= 0) {
            throw new IllegalArgumentException("read limit must be positive");
        }
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        byte[] buffer = new byte[8192];
        long total = 0;
        while (true) {
            int count = input.read(buffer, 0, buffer.length);
            if (count < 0) {
                break;
            }
            if (count == 0) {
                int single = input.read();
                if (single < 0) {
                    break;
                }
                total++;
                if (total > limit) {
                    throw failure(Code.PACKAGE_LIMIT_EXCEEDED,
                            "package exceeds the source limit");
                }
                output.write(single);
            } else {
                total += count;
                if (total > limit) {
                    throw failure(Code.PACKAGE_LIMIT_EXCEEDED,
                            "package exceeds the source limit");
                }
                output.write(buffer, 0, count);
            }
        }
        return output.toByteArray();
    }

    private static LocalEntry validateLocalHeader(
            byte[] archive,
            CentralEntry central,
            long centralOffset,
            long descriptorBoundary) throws ArchiveException {
        int offset = central.localHeaderOffset();
        requireRange(archive, offset, 30, "ZIP local header is truncated");
        if (unsignedInt(archive, offset) != LOCAL_SIGNATURE) {
            throw invalid("ZIP local header signature is invalid");
        }
        int localFlags = unsignedShort(archive, offset + 6);
        int localMethod = unsignedShort(archive, offset + 8);
        if (localFlags != central.flags() || localMethod != central.method()) {
            throw invalid("ZIP local and central flags or methods differ");
        }
        int nameLength = unsignedShort(archive, offset + 26);
        int extraLength = unsignedShort(archive, offset + 28);
        if (nameLength != central.rawName.length) {
            throw invalid("ZIP local and central entry names differ");
        }
        requireRange(archive, offset + 30, nameLength + extraLength,
                "ZIP local entry metadata is truncated");
        for (int index = 0; index < nameLength; index++) {
            if (archive[offset + 30 + index] != central.rawName[index]) {
                throw invalid("ZIP local and central entry names differ");
            }
        }
        long dataOffsetLong = checkedAdd(
                offset + 30L, (long) nameLength + extraLength,
                "ZIP local data offset overflows");
        long payloadEnd = checkedAdd(
                dataOffsetLong, central.compressedSize(), "ZIP entry size overflows");
        if (payloadEnd > centralOffset || dataOffsetLong > Integer.MAX_VALUE) {
            throw invalid("ZIP compressed payload is truncated");
        }
        long localCrc = unsignedInt(archive, offset + 14);
        long localCompressed = unsignedInt(archive, offset + 18);
        long localUncompressed = unsignedInt(archive, offset + 22);
        if ((localFlags & FLAG_DATA_DESCRIPTOR) != 0) {
            requireZeroOrEqual(localCrc, central.crc32(), "CRC");
            requireZeroOrEqual(localCompressed, central.compressedSize(), "compressed size");
            requireZeroOrEqual(localUncompressed, central.uncompressedSize(), "size");
        } else if (localCrc != central.crc32()
                || localCompressed != central.compressedSize()
                || localUncompressed != central.uncompressedSize()) {
            throw invalid("ZIP local and central CRC or sizes differ");
        }
        long entryEnd = payloadEnd;
        if ((localFlags & FLAG_DATA_DESCRIPTOR) != 0) {
            if (payloadEnd > descriptorBoundary) {
                throw invalid("ZIP local entries overlap");
            }
            long available = descriptorBoundary - payloadEnd;
            if (available < 4) {
                throw invalid(descriptorBoundary < centralOffset
                        ? "ZIP local entries overlap"
                        : "ZIP data descriptor overlaps the central directory");
            }
            long first = unsignedInt(archive, (int) payloadEnd);
            boolean unsignedMatches = available >= 12
                    && descriptorMatches(archive, (int) payloadEnd, central);
            boolean signedMatches = first == 0x08074B50L
                    && available >= 16
                    && descriptorMatches(archive, (int) payloadEnd + 4, central);
            if (unsignedMatches && signedMatches) {
                throw invalid("ZIP data descriptor is ambiguous");
            }
            if (signedMatches) {
                entryEnd = payloadEnd + 16;
            } else if (unsignedMatches) {
                entryEnd = payloadEnd + 12;
            } else if (descriptorBoundary < centralOffset
                    && (available < 12 || (first == 0x08074B50L && available < 16))) {
                throw invalid("ZIP local entries overlap");
            } else if (first != 0x08074B50L && available < 12) {
                throw invalid("ZIP data descriptor overlaps the central directory");
            } else if (first == 0x08074B50L && available >= 12 && available < 16) {
                throw invalid("ZIP data descriptor overlaps the central directory");
            } else {
                throw invalid("ZIP data descriptor differs from central metadata");
            }
        }
        return new LocalEntry(central, (int) dataOffsetLong, payloadEnd, entryEnd);
    }

    private static boolean descriptorMatches(
            byte[] archive, int valuesOffset, CentralEntry central) throws ArchiveException {
        return unsignedInt(archive, valuesOffset) == central.crc32()
                && unsignedInt(archive, valuesOffset + 4) == central.compressedSize()
                && unsignedInt(archive, valuesOffset + 8) == central.uncompressedSize();
    }

    private static byte[] inflate(
            byte[] archive,
            LocalEntry local,
            ScanLimits limits,
            long alreadyInflated) throws ArchiveException {
        CentralEntry central = local.central();
        if (central.uncompressedSize() > Integer.MAX_VALUE) {
            throw failure(Code.INFLATED_LIMIT_EXCEEDED,
                    "ZIP entry cannot fit in memory");
        }
        int compressedSize = (int) central.compressedSize();
        if ((long) compressedSize != central.compressedSize()) {
            throw invalid("ZIP compressed size is invalid");
        }
        if (central.method() == 0) {
            if (central.compressedSize() != central.uncompressedSize()) {
                throw invalid("stored ZIP entry sizes differ");
            }
            return Arrays.copyOfRange(
                    archive, local.dataOffset(), local.dataOffset() + compressedSize);
        }

        Inflater inflater = new Inflater(true);
        try {
            inflater.setInput(archive, local.dataOffset(), compressedSize);
            ByteArrayOutputStream output = new ByteArrayOutputStream(
                    (int) Math.min(central.uncompressedSize(), 64L * 1024L));
            byte[] buffer = new byte[8192];
            long entryTotal = 0;
            while (!inflater.finished()) {
                int count;
                try {
                    count = inflater.inflate(buffer);
                } catch (DataFormatException failure) {
                    throw new ArchiveException(
                            Code.INVALID_ZIP, "ZIP deflate stream is invalid", failure);
                }
                if (count > 0) {
                    entryTotal += count;
                    checkedInflated(alreadyInflated, entryTotal, limits);
                    if (entryTotal > central.uncompressedSize()) {
                        throw invalid("ZIP entry inflated past its declared size");
                    }
                    output.write(buffer, 0, count);
                } else if (inflater.finished()) {
                    break;
                } else if (inflater.needsDictionary() || inflater.needsInput()) {
                    throw invalid("ZIP deflate stream ended before completion");
                } else {
                    throw invalid("ZIP deflate stream made no progress");
                }
            }
            if (inflater.getRemaining() != 0) {
                throw invalid("ZIP compressed size includes trailing bytes");
            }
            return output.toByteArray();
        } finally {
            inflater.end();
        }
    }

    private static void validateRatio(CentralEntry entry, ScanLimits limits)
            throws ArchiveException {
        if (entry.uncompressedSize() <= limits.ratioGuardThresholdBytes()) {
            return;
        }
        long compressed = entry.compressedSize();
        if (compressed == 0
                || entry.uncompressedSize() > compressed * (long) limits.maxCompressionRatio()) {
            throw failure(Code.RATIO_LIMIT_EXCEEDED,
                    "ZIP compression ratio exceeds the limit");
        }
    }

    private static long checkedInflated(long current, long added, ScanLimits limits)
            throws ArchiveException {
        long total = checkedAdd(current, added, "ZIP inflated total overflows");
        if (total > limits.maxCumulativeInflatedBytes()) {
            throw failure(Code.INFLATED_LIMIT_EXCEEDED,
                    "ZIP inflated total exceeds the limit");
        }
        return total;
    }

    private static List<Integer> findEndRecords(byte[] archive) throws ArchiveException {
        if (archive.length < 22) {
            throw invalid("ZIP end record is missing");
        }
        ArrayList<Integer> candidates = new ArrayList<>();
        int earliest = Math.max(0, archive.length - 22 - 0xFFFF);
        for (int offset = archive.length - 22; offset >= earliest; offset--) {
            if (unsignedInt(archive, offset) == END_SIGNATURE) {
                int commentLength = unsignedShort(archive, offset + 20);
                if ((long) offset + 22L + commentLength == archive.length) {
                    candidates.add(offset);
                }
            }
        }
        if (candidates.isEmpty()) {
            throw invalid("ZIP end record is missing or truncated");
        }
        return candidates;
    }

    private static void requireZeroOrEqual(long local, long central, String field)
            throws ArchiveException {
        if (local != 0 && local != central) {
            throw invalid("ZIP local and central " + field + " differ");
        }
    }

    private static int unsignedShort(byte[] bytes, int offset) throws ArchiveException {
        requireRange(bytes, offset, 2, "ZIP structure is truncated");
        return (bytes[offset] & 0xFF) | ((bytes[offset + 1] & 0xFF) << 8);
    }

    private static long unsignedInt(byte[] bytes, int offset) throws ArchiveException {
        requireRange(bytes, offset, 4, "ZIP structure is truncated");
        return (bytes[offset] & 0xFFL)
                | ((bytes[offset + 1] & 0xFFL) << 8)
                | ((bytes[offset + 2] & 0xFFL) << 16)
                | ((bytes[offset + 3] & 0xFFL) << 24);
    }

    private static void requireRange(byte[] bytes, int offset, int length, String message)
            throws ArchiveException {
        if (offset < 0 || length < 0 || (long) offset + length > bytes.length) {
            throw invalid(message);
        }
    }

    private static long checkedAdd(long first, long second, String message)
            throws ArchiveException {
        if (first < 0 || second < 0 || first > Long.MAX_VALUE - second) {
            throw invalid(message);
        }
        return first + second;
    }

    private static ArchiveException invalid(String message) {
        return failure(Code.INVALID_ZIP, message);
    }

    private static ArchiveException failure(Code code, String message) {
        return new ArchiveException(code, message);
    }

    public static final class Archive {
        private final byte[] physicalBytes;
        private final List<Entry> entries;

        private Archive(byte[] physicalBytes, List<Entry> entries) {
            this.physicalBytes = physicalBytes;
            this.entries = Collections.unmodifiableList(new ArrayList<>(entries));
        }

        public List<Entry> entries() {
            return entries;
        }

        public Entry requireExact(ZipEntryIdentity identity) throws ArchiveException {
            DomainValidation.requireNonNull(identity, "ZIP entry identity");
            Entry found = null;
            for (Entry entry : entries) {
                if (entry.identity().equals(identity)) {
                    if (found != null) {
                        throw failure(Code.INVALID_ZIP, "ZIP exact locator is duplicated");
                    }
                    found = entry;
                }
            }
            if (found == null) {
                throw failure(Code.ENTRY_MISSING, "ZIP exact locator is missing");
            }
            return found;
        }
    }

    public static final class Entry {
        private final byte[] physicalBytes;
        private final LocalEntry local;
        private final ScanLimits limits;

        private Entry(byte[] physicalBytes, LocalEntry local, ScanLimits limits) {
            this.physicalBytes = physicalBytes;
            this.local = local;
            this.limits = limits;
        }

        private CentralEntry central() {
            return local.central();
        }

        public ZipEntryIdentity identity() {
            return ZipEntryIdentity.fromRawName(
                    central().rawName, central().localHeaderOffset);
        }

        public byte[] rawName() {
            return central().rawName.clone();
        }

        public byte[] centralExtra() {
            return central().centralExtra.clone();
        }

        public int flags() {
            return central().flags;
        }

        public int method() {
            return central().method;
        }

        public long crc32() {
            return central().crc32;
        }

        public long compressedSize() {
            return central().compressedSize;
        }

        public long uncompressedSize() {
            return central().uncompressedSize;
        }

        public boolean isDirectory() {
            byte[] rawName = central().rawName;
            return rawName.length > 0
                    && (rawName[rawName.length - 1] == '/'
                    || rawName[rawName.length - 1] == '\\');
        }

        /** Allocates exactly one selected payload and verifies its declared size and CRC. */
        public byte[] readPayload() throws ArchiveException {
            if (central().uncompressedSize() > limits.maxPayloadBytes()) {
                throw failure(Code.PAYLOAD_LIMIT_EXCEEDED,
                        "ZIP entry exceeds the payload limit");
            }
            byte[] payload = inflate(physicalBytes, local, limits, 0);
            if (payload.length != central().uncompressedSize()) {
                throw invalid("ZIP inflated size differs from central metadata");
            }
            CRC32 crc = new CRC32();
            crc.update(payload);
            if (crc.getValue() != central().crc32()) {
                throw invalid("ZIP inflated CRC differs from central metadata");
            }
            return payload;
        }
    }

    private record CentralEntry(
            byte[] rawName,
            byte[] centralExtra,
            int localHeaderOffset,
            int flags,
            int method,
            long crc32,
            long compressedSize,
            long uncompressedSize) {
    }

    private record LocalEntry(
            CentralEntry central, int dataOffset, long payloadEnd, long entryEnd) {
    }

    public enum Code {
        INVALID_ZIP,
        PACKAGE_LIMIT_EXCEEDED,
        ENTRY_LIMIT_EXCEEDED,
        INFLATED_LIMIT_EXCEEDED,
        PAYLOAD_LIMIT_EXCEEDED,
        NAME_LIMIT_EXCEEDED,
        RATIO_LIMIT_EXCEEDED,
        ENCRYPTED,
        UNSUPPORTED_COMPRESSION,
        ENTRY_MISSING
    }

    public static final class ArchiveException extends Exception {
        private final Code code;

        ArchiveException(Code code, String message) {
            super(message);
            this.code = DomainValidation.requireNonNull(code, "ZIP error code");
        }

        ArchiveException(Code code, String message, Throwable cause) {
            super(message, cause);
            this.code = DomainValidation.requireNonNull(code, "ZIP error code");
        }

        public Code code() {
            return code;
        }
    }
}
