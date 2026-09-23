package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.EOFException;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.charset.CharacterCodingException;
import java.nio.charset.CodingErrorAction;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.AbstractList;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.RandomAccess;
import java.util.concurrent.atomic.AtomicReferenceArray;

/** Deterministic, bounded codec for the rebuildable Game Center cache. */
public final class GameCenterSnapshotCodec {
    private static final int MAGIC = 0x464E4743; // FNGC
    private static final int STARTUP_MAGIC = 0x464E4753; // FNGS
    private static final int CHECKSUM_BYTES = 32;
    private static final int MAX_COUNT = 100_000;
    private static final int MAX_STRING_BYTES = 64 * 1024;
    public static final int MAX_BYTES = 24 * 1024 * 1024;

    private GameCenterSnapshotCodec() {
    }

    public static byte[] encode(GameCenterSnapshot snapshot) throws CodecException {
        if (snapshot == null) throw error(ErrorCode.INVALID_FIELD);
        try {
            ByteArrayOutputStream bodyBytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bodyBytes);
            out.writeInt(MAGIC);
            out.writeInt(snapshot.schemaVersion());
            out.writeLong(snapshot.nativeGeneration());
            out.writeLong(snapshot.sourceEpoch());
            out.write(hexToBytes(snapshot.builtinManifestSha256()));
            writeCount(out, snapshot.rows().size());
            for (GameCenterSnapshot.Row row : snapshot.rows()) writeRow(out, row);
            writeCount(out, snapshot.sources().size());
            for (GameCenterSnapshot.SourceRow source : snapshot.sources()) {
                writeSource(out, source);
            }
            byte[] catalog = snapshot.catalogStateBytes();
            writeLength(out, catalog.length, MAX_BYTES - CHECKSUM_BYTES);
            out.write(catalog);
            out.flush();
            byte[] body = bodyBytes.toByteArray();
            if (body.length > MAX_BYTES - CHECKSUM_BYTES) throw error(ErrorCode.BOUNDS);
            byte[] encoded = Arrays.copyOf(body, body.length + CHECKSUM_BYTES);
            System.arraycopy(sha256(body), 0, encoded, body.length, CHECKSUM_BYTES);
            return encoded;
        } catch (CodecException failure) {
            throw failure;
        } catch (IOException impossible) {
            throw new CodecException(ErrorCode.IO, impossible);
        } catch (IllegalArgumentException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
    }

    public static byte[] encodeStartup(GameCenterSnapshot snapshot, int visibleRowLimit)
            throws CodecException {
        if (snapshot == null || visibleRowLimit < 0) throw error(ErrorCode.INVALID_FIELD);
        try {
            ByteArrayOutputStream bodyBytes = new ByteArrayOutputStream();
            DataOutputStream out = new DataOutputStream(bodyBytes);
            out.writeInt(STARTUP_MAGIC);
            out.writeInt(GameCenterSnapshot.CURRENT_SCHEMA);
            writeCount(out, snapshot.rows().size());
            int visibleCount = Math.min(snapshot.rows().size(), visibleRowLimit);
            writeCount(out, visibleCount);
            for (int index = 0; index < visibleCount; index++) {
                writeRow(out, snapshot.rows().get(index));
            }
            out.flush();
            byte[] body = bodyBytes.toByteArray();
            if (body.length > MAX_BYTES - CHECKSUM_BYTES) throw error(ErrorCode.BOUNDS);
            byte[] encoded = Arrays.copyOf(body, body.length + CHECKSUM_BYTES);
            System.arraycopy(sha256(body), 0, encoded, body.length, CHECKSUM_BYTES);
            return encoded;
        } catch (CodecException failure) {
            throw failure;
        } catch (IOException impossible) {
            throw new CodecException(ErrorCode.IO, impossible);
        }
    }

    public static GameCenterSnapshot decodeStartup(byte[] encoded) throws CodecException {
        if (encoded == null || encoded.length < 16 + CHECKSUM_BYTES) {
            throw error(ErrorCode.TRUNCATED);
        }
        if (encoded.length > MAX_BYTES) throw error(ErrorCode.BOUNDS);
        int bodyLength = encoded.length - CHECKSUM_BYTES;
        byte[] expected = Arrays.copyOfRange(encoded, bodyLength, encoded.length);
        if (!MessageDigest.isEqual(expected, sha256(encoded, 0, bodyLength))) {
            throw error(ErrorCode.CHECKSUM_MISMATCH);
        }
        try {
            DataInputStream in = new DataInputStream(
                    new ByteArrayInputStream(encoded, 0, bodyLength));
            if (in.readInt() != STARTUP_MAGIC) throw error(ErrorCode.INVALID_MAGIC);
            if (in.readInt() != GameCenterSnapshot.CURRENT_SCHEMA) {
                throw error(ErrorCode.UNKNOWN_VERSION);
            }
            int totalCount = readCount(in);
            int visibleCount = readCount(in);
            if (visibleCount > totalCount) throw error(ErrorCode.INVALID_FIELD);
            ArrayList<GameCenterSnapshot.Row> visible = new ArrayList<>(visibleCount);
            for (int index = 0; index < visibleCount; index++) visible.add(readRow(in));
            if (in.available() != 0) throw error(ErrorCode.INVALID_FIELD);
            return new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, 0L,
                    "0".repeat(64), 0L, new StartupRowList(totalCount, visible),
                    List.of(), new byte[0]);
        } catch (EOFException truncated) {
            throw new CodecException(ErrorCode.TRUNCATED, truncated);
        } catch (CodecException failure) {
            throw failure;
        } catch (IOException failure) {
            throw new CodecException(ErrorCode.IO, failure);
        } catch (RuntimeException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
    }

    public static GameCenterSnapshot decode(byte[] encoded) throws CodecException {
        return decode(encoded, true);
    }

    /** Decodes the UI rows now and leaves the larger native hydration payload for background work. */
    public static GameCenterSnapshot decodeProjection(byte[] encoded) throws CodecException {
        return decode(encoded, false);
    }

    private static GameCenterSnapshot decode(byte[] encoded, boolean includeCatalog)
            throws CodecException {
        if (encoded == null || encoded.length < 4 + 4 + 8 + 8 + 32 + 4 + 4 + 4
                + CHECKSUM_BYTES) {
            throw error(ErrorCode.TRUNCATED);
        }
        if (encoded.length > MAX_BYTES) throw error(ErrorCode.BOUNDS);
        int bodyLength = encoded.length - CHECKSUM_BYTES;
        byte[] expected = Arrays.copyOfRange(encoded, bodyLength, encoded.length);
        if (!MessageDigest.isEqual(expected, sha256(encoded, 0, bodyLength))) {
            throw error(ErrorCode.CHECKSUM_MISMATCH);
        }
        try {
            DataInputStream in = new DataInputStream(
                    new ByteArrayInputStream(encoded, 0, bodyLength));
            if (in.readInt() != MAGIC) throw error(ErrorCode.INVALID_MAGIC);
            int schema = in.readInt();
            if (schema != GameCenterSnapshot.CURRENT_SCHEMA) {
                throw error(ErrorCode.UNKNOWN_VERSION);
            }
            long generation = in.readLong();
            long sourceEpoch = in.readLong();
            String fingerprint = bytesToHex(readExact(in, 32));
            int rowCount = readCount(in);
            List<GameCenterSnapshot.Row> rows;
            if (includeCatalog) {
                ArrayList<GameCenterSnapshot.Row> decoded = new ArrayList<>(rowCount);
                for (int index = 0; index < rowCount; index++) decoded.add(readRow(in));
                rows = decoded;
            } else {
                int[] offsets = new int[rowCount];
                for (int index = 0; index < rowCount; index++) {
                    offsets[index] = bodyLength - in.available();
                    skipRow(in);
                }
                rows = new LazyRowList(encoded, offsets);
            }
            int sourceCount = readCount(in);
            ArrayList<GameCenterSnapshot.SourceRow> sources = new ArrayList<>(sourceCount);
            for (int index = 0; index < sourceCount; index++) sources.add(readSource(in));
            int catalogLength = readLength(in, MAX_BYTES - CHECKSUM_BYTES);
            byte[] catalog;
            if (includeCatalog) {
                catalog = readExact(in, catalogLength);
            } else {
                skipExact(in, catalogLength);
                catalog = new byte[0];
            }
            if (in.available() != 0) throw error(ErrorCode.INVALID_FIELD);
            return new GameCenterSnapshot(schema, generation, fingerprint, sourceEpoch,
                    rows, sources, catalog);
        } catch (EOFException truncated) {
            throw new CodecException(ErrorCode.TRUNCATED, truncated);
        } catch (CodecException failure) {
            throw failure;
        } catch (IOException failure) {
            throw new CodecException(ErrorCode.IO, failure);
        } catch (RuntimeException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
    }

    private static void writeRow(DataOutputStream out, GameCenterSnapshot.Row row)
            throws IOException, CodecException {
        writeString(out, row.canonicalId());
        writeString(out, row.titleEn());
        writeString(out, row.titleZhHans());
        writeString(out, row.fallbackTitle());
        writeString(out, row.originalFilename());
        writeString(out, row.searchText());
        out.writeBoolean(row.builtin());
        out.writeBoolean(row.favorite());
        out.writeLong(row.lastPlayedSequence());
        out.writeInt(row.playCount());
        out.writeInt(row.variantCount());
        out.writeBoolean(row.launchable());
        out.writeInt(row.popularityScore());
    }

    private static GameCenterSnapshot.Row readRow(DataInputStream in)
            throws IOException, CodecException {
        return new GameCenterSnapshot.Row(readString(in), readString(in), readString(in),
                readString(in), readString(in), readString(in), in.readBoolean(),
                in.readBoolean(), in.readLong(), in.readInt(), in.readInt(),
                in.readBoolean(), in.readInt());
    }

    private static void skipRow(DataInputStream in) throws IOException, CodecException {
        for (int index = 0; index < 6; index++) {
            skipExact(in, readCount(in, MAX_STRING_BYTES));
        }
        in.readBoolean();
        in.readBoolean();
        in.readLong();
        in.readInt();
        in.readInt();
        in.readBoolean();
        in.readInt();
    }

    private static void writeSource(DataOutputStream out, GameCenterSnapshot.SourceRow source)
            throws IOException, CodecException {
        writeString(out, source.id());
        writeEnum(out, source.type());
        writeEnum(out, source.permissionState());
        writeEnum(out, source.availability());
        writeEnum(out, source.completeness());
        out.writeLong(source.scanToken());
        out.writeInt(source.packageCount());
    }

    private static GameCenterSnapshot.SourceRow readSource(DataInputStream in)
            throws IOException, CodecException {
        return new GameCenterSnapshot.SourceRow(readString(in),
                readEnum(in, RomSource.Type.values()),
                readEnum(in, RomSource.PermissionState.values()),
                readEnum(in, RomSource.Availability.values()),
                readEnum(in, SourceScanResult.Completeness.values()),
                in.readLong(), in.readInt());
    }

    private static void writeString(DataOutputStream out, String value)
            throws IOException, CodecException {
        if (value == null) throw error(ErrorCode.INVALID_FIELD);
        byte[] utf8 = value.getBytes(StandardCharsets.UTF_8);
        if (utf8.length > MAX_STRING_BYTES) throw error(ErrorCode.BOUNDS);
        out.writeInt(utf8.length);
        out.write(utf8);
    }

    private static String readString(DataInputStream in) throws IOException, CodecException {
        int length = readCount(in, MAX_STRING_BYTES);
        byte[] utf8 = readExact(in, length);
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(utf8)).toString();
        } catch (CharacterCodingException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
    }

    private static void writeCount(DataOutputStream out, int value)
            throws IOException, CodecException {
        writeLength(out, value, MAX_COUNT);
    }

    private static int readCount(DataInputStream in) throws IOException, CodecException {
        return readCount(in, MAX_COUNT);
    }

    private static int readCount(DataInputStream in, int maximum)
            throws IOException, CodecException {
        return readLength(in, maximum);
    }

    private static void writeLength(DataOutputStream out, int value, int maximum)
            throws IOException, CodecException {
        if (value < 0 || value > maximum) throw error(ErrorCode.BOUNDS);
        out.writeInt(value);
    }

    private static int readLength(DataInputStream in, int maximum)
            throws IOException, CodecException {
        int value = in.readInt();
        if (value < 0 || value > maximum) throw error(ErrorCode.BOUNDS);
        return value;
    }

    private static <T extends Enum<T>> void writeEnum(DataOutputStream out, T value)
            throws IOException {
        out.writeInt(value.ordinal());
    }

    private static <T> T readEnum(DataInputStream in, T[] values)
            throws IOException, CodecException {
        int ordinal = in.readInt();
        if (ordinal < 0 || ordinal >= values.length) throw error(ErrorCode.INVALID_FIELD);
        return values[ordinal];
    }

    private static byte[] readExact(DataInputStream in, int count) throws IOException {
        byte[] bytes = new byte[count];
        in.readFully(bytes);
        return bytes;
    }

    private static void skipExact(DataInputStream in, int count) throws IOException {
        int remaining = count;
        while (remaining > 0) {
            int skipped = in.skipBytes(remaining);
            if (skipped <= 0) throw new EOFException();
            remaining -= skipped;
        }
    }

    private static byte[] hexToBytes(String hex) throws CodecException {
        if (hex == null || hex.length() != 64) throw error(ErrorCode.INVALID_FIELD);
        byte[] bytes = new byte[32];
        try {
            for (int index = 0; index < bytes.length; index++) {
                bytes[index] = (byte) Integer.parseInt(
                        hex.substring(index * 2, index * 2 + 2), 16);
            }
        } catch (NumberFormatException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
        return bytes;
    }

    private static String bytesToHex(byte[] bytes) {
        StringBuilder output = new StringBuilder(bytes.length * 2);
        for (byte item : bytes) output.append(String.format("%02x", item & 0xff));
        return output.toString();
    }

    private static byte[] sha256(byte[] bytes) {
        return sha256(bytes, 0, bytes.length);
    }

    private static byte[] sha256(byte[] bytes, int offset, int length) {
        try {
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            digest.update(bytes, offset, length);
            return digest.digest();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-256 unavailable", impossible);
        }
    }

    private static final class LazyRowList extends AbstractList<GameCenterSnapshot.Row>
            implements RandomAccess, GameCenterSnapshot.LazyRows {
        private final byte[] encoded;
        private final int[] offsets;
        private final AtomicReferenceArray<GameCenterSnapshot.Row> decoded;

        LazyRowList(byte[] encoded, int[] offsets) {
            this.encoded = encoded;
            this.offsets = offsets;
            decoded = new AtomicReferenceArray<>(offsets.length);
        }

        @Override public GameCenterSnapshot.Row get(int index) {
            GameCenterSnapshot.Row cached = decoded.get(index);
            if (cached != null) return cached;
            try {
                DataInputStream input = new DataInputStream(new ByteArrayInputStream(
                        encoded, offsets[index], encoded.length - offsets[index]));
                GameCenterSnapshot.Row row = readRow(input);
                if (decoded.compareAndSet(index, null, row)) return row;
                return decoded.get(index);
            } catch (IOException | CodecException invalid) {
                throw new IllegalStateException("validated snapshot row could not be decoded", invalid);
            }
        }

        @Override public int size() {
            return offsets.length;
        }
    }

    private static final class StartupRowList extends AbstractList<GameCenterSnapshot.Row>
            implements RandomAccess, GameCenterSnapshot.LazyRows {
        private final int totalCount;
        private final List<GameCenterSnapshot.Row> visible;

        StartupRowList(int totalCount, List<GameCenterSnapshot.Row> visible) {
            this.totalCount = totalCount;
            this.visible = List.copyOf(visible);
        }

        @Override public GameCenterSnapshot.Row get(int index) {
            if (index < 0 || index >= totalCount) throw new IndexOutOfBoundsException(index);
            if (index < visible.size()) return visible.get(index);
            return new GameCenterSnapshot.Row(
                    "deferred-" + index, "", "", "", "", "",
                    false, false, 0L, 0, 0, false, 0);
        }

        @Override public int size() {
            return totalCount;
        }
    }

    private static CodecException error(ErrorCode code) {
        return new CodecException(code, null);
    }

    public enum ErrorCode {
        TRUNCATED,
        BOUNDS,
        CHECKSUM_MISMATCH,
        INVALID_MAGIC,
        UNKNOWN_VERSION,
        INVALID_FIELD,
        IO
    }

    public static final class CodecException extends Exception {
        private final ErrorCode code;

        CodecException(ErrorCode code, Throwable cause) {
            super(code.name(), cause);
            this.code = code;
        }

        public ErrorCode code() {
            return code;
        }
    }
}
