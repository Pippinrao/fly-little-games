package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.CompatibilityReason;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;

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
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.Map;

/** Deterministic bounded binary state codec. It never serializes ROM payload bytes. */
public final class CatalogStateCodec {
    private static final int MAGIC = 0x464E4341; // FNCA
    private static final int CHECKSUM_BYTES = 32;
    private static final int MAX_BYTES = 16 * 1024 * 1024;
    private static final int MAX_COUNT = 100_000;
    private static final int MAX_STRING_BYTES = 64 * 1024;

    private CatalogStateCodec() {
    }

    public static byte[] encode(CatalogState state) throws CodecException {
        try {
            ByteArrayOutputStream bodyBytes = new BoundedByteArrayOutputStream(
                    MAX_BYTES - CHECKSUM_BYTES);
            DataOutputStream out = new DataOutputStream(bodyBytes);
            out.writeInt(MAGIC);
            out.writeInt(state.schemaVersion());
            out.writeLong(state.revision());
            writeString(out, state.builtinSourceId());
            out.writeLong(state.lastPlayedSequence());
            writeCount(out, state.sources().size());
            for (SourceCatalogState source : state.sources().values()) writeSource(out, source);
            writeCount(out, state.userStates().size());
            for (Map.Entry<String, CanonicalUserState> item : state.userStates().entrySet()) {
                writeString(out, item.getKey());
                CanonicalUserState user = item.getValue();
                out.writeBoolean(user.favorite());
                out.writeLong(user.favoriteUpdatedRevision());
                out.writeLong(user.lastPlayedSequence());
                out.writeInt(user.playCount());
            }
            out.flush();
            byte[] body = bodyBytes.toByteArray();
            if (body.length + CHECKSUM_BYTES > MAX_BYTES) throw error(ErrorCode.BOUNDS);
            byte[] encoded = Arrays.copyOf(body, body.length + CHECKSUM_BYTES);
            System.arraycopy(sha256(body), 0, encoded, body.length, CHECKSUM_BYTES);
            return encoded;
        } catch (IOException impossible) {
            throw new CodecException(ErrorCode.IO, impossible);
        } catch (SizeLimitException oversized) {
            throw new CodecException(ErrorCode.BOUNDS, oversized);
        } catch (IllegalArgumentException invalid) {
            throw new CodecException(ErrorCode.INVALID_FIELD, invalid);
        }
    }

    public static CatalogState decode(byte[] encoded) throws CodecException {
        if (encoded == null || encoded.length < 8 + CHECKSUM_BYTES) {
            throw error(ErrorCode.TRUNCATED);
        }
        if (encoded.length > MAX_BYTES) throw error(ErrorCode.BOUNDS);
        int bodyLength = encoded.length - CHECKSUM_BYTES;
        byte[] body = Arrays.copyOf(encoded, bodyLength);
        byte[] expected = Arrays.copyOfRange(encoded, bodyLength, encoded.length);
        if (!MessageDigest.isEqual(expected, sha256(body))) {
            throw error(ErrorCode.CHECKSUM_MISMATCH);
        }
        try {
            DataInputStream in = new DataInputStream(new ByteArrayInputStream(body));
            if (in.readInt() != MAGIC) throw error(ErrorCode.INVALID_MAGIC);
            int schema = in.readInt();
            if (schema != CatalogState.CURRENT_SCHEMA) throw error(ErrorCode.UNKNOWN_VERSION);
            long revision = in.readLong();
            String builtin = readString(in);
            long sequence = in.readLong();
            LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>();
            int sourceCount = readCount(in);
            for (int index = 0; index < sourceCount; index++) {
                SourceCatalogState source = readSource(in);
                if (sources.put(source.source().id(), source) != null) {
                    throw error(ErrorCode.INVALID_FIELD);
                }
            }
            LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>();
            int userCount = readCount(in);
            for (int index = 0; index < userCount; index++) {
                String id = readString(in);
                CanonicalUserState user = new CanonicalUserState(
                        in.readBoolean(), in.readLong(), in.readLong(), in.readInt());
                if (users.put(id, user) != null) throw error(ErrorCode.INVALID_FIELD);
            }
            if (in.available() != 0) throw error(ErrorCode.INVALID_FIELD);
            return new CatalogState(schema, revision, builtin, sources, users, sequence);
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

    private static void writeSource(DataOutputStream out, SourceCatalogState state)
            throws IOException, CodecException {
        writeRomSource(out, state.source());
        writeEnum(out, state.lastScanCompleteness());
        out.writeLong(state.lastScanToken());
        writeCount(out, state.packages().size());
        for (CatalogPackage item : state.packages().values()) {
            writeEnum(out, item.freshness());
            writePackage(out, item.physicalPackage());
        }
        writeCount(out, state.packageOutcomes().size());
        for (PackageOutcome item : state.packageOutcomes()) {
            writeString(out, item.packageId()); writeEnum(out, item.status()); writeEnum(out, item.reason());
        }
        writeCount(out, state.entryOutcomes().size());
        for (EntryOutcome item : state.entryOutcomes()) {
            writeString(out, item.packageId()); writeString(out, item.entryId());
            writeEnum(out, item.status()); writeEnum(out, item.reason());
        }
        writeCount(out, state.issues().size());
        for (ScanIssue issue : state.issues()) {
            writeEnum(out, issue.code()); writeEnum(out, issue.severity());
            writeNullable(out, issue.sourceId()); writeNullable(out, issue.packageId());
        }
    }

    private static SourceCatalogState readSource(DataInputStream in)
            throws IOException, CodecException {
        RomSource source = readRomSource(in);
        SourceScanResult.Completeness completeness = readEnum(
                in, SourceScanResult.Completeness.values());
        long token = in.readLong();
        LinkedHashMap<String, CatalogPackage> packages = new LinkedHashMap<>();
        int packageCount = readCount(in);
        for (int index = 0; index < packageCount; index++) {
            CatalogPackage.Freshness freshness = readEnum(in, CatalogPackage.Freshness.values());
            PhysicalPackage item = readPackage(in);
            if (packages.put(item.id(), new CatalogPackage(item, freshness)) != null) {
                throw error(ErrorCode.INVALID_FIELD);
            }
        }
        ArrayList<PackageOutcome> packageOutcomes = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) {
            packageOutcomes.add(new PackageOutcome(readString(in),
                    readEnum(in, PackageOutcome.Status.values()),
                    readEnum(in, PackageOutcome.Reason.values())));
        }
        ArrayList<EntryOutcome> entryOutcomes = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) {
            entryOutcomes.add(new EntryOutcome(readString(in), readString(in),
                    readEnum(in, EntryOutcome.Status.values()),
                    readEnum(in, EntryOutcome.Reason.values())));
        }
        ArrayList<ScanIssue> issues = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) {
            issues.add(new ScanIssue(readEnum(in, ScanIssue.Code.values()),
                    readEnum(in, ScanIssue.Severity.values()),
                    readNullable(in), readNullable(in)));
        }
        return new SourceCatalogState(source, packages, packageOutcomes, entryOutcomes,
                issues, completeness, token);
    }

    private static void writePackage(DataOutputStream out, PhysicalPackage item)
            throws IOException, CodecException {
        writeString(out, item.id()); writeRomSource(out, item.source());
        writeString(out, item.sourceUri()); writeString(out, item.originalFilename());
        writeEnum(out, item.packageFormat()); writeString(out, item.physicalPackageSha256());
        writeCount(out, item.variants().size());
        for (RomVariant variant : item.variants()) writeVariant(out, variant);
    }

    private static PhysicalPackage readPackage(DataInputStream in)
            throws IOException, CodecException {
        String id = readString(in); RomSource source = readRomSource(in);
        String uri = readString(in); String filename = readString(in);
        PackageFormat format = readEnum(in, PackageFormat.values());
        String physical = readString(in);
        ArrayList<RomVariant> variants = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) variants.add(readVariant(in));
        return new PhysicalPackage(id, source, uri, filename, format, physical, variants);
    }

    private static void writeVariant(DataOutputStream out, RomVariant item)
            throws IOException, CodecException {
        writeString(out, item.id()); writeCanonical(out, item.canonicalGame());
        writeNullable(out, item.entryPath()); writeEnum(out, item.romFormat());
        writeEnum(out, item.compatibilityDecision().state());
        writeEnum(out, item.compatibilityDecision().reason());
        RomHashes hashes = item.hashes();
        writeString(out, hashes.payloadSha1()); writeString(out, hashes.payloadSha256());
        writeString(out, hashes.physicalPackageSha256()); writeString(out, hashes.crc32());
        writeAnalysis(out, item.analysis());
        out.writeBoolean(item.zipEntryIdentity() != null);
        if (item.zipEntryIdentity() != null) {
            writeString(out, item.zipEntryIdentity().rawNameHex());
            out.writeInt(item.zipEntryIdentity().localHeaderOffset());
            writeEnum(out, item.zipNameEncoding());
        }
    }

    private static RomVariant readVariant(DataInputStream in)
            throws IOException, CodecException {
        String id = readString(in); CanonicalGame game = readCanonical(in);
        String path = readNullable(in); RomFormat format = readEnum(in, RomFormat.values());
        CompatibilityDecision compatibility = new CompatibilityDecision(
                readEnum(in, CompatibilityState.values()),
                readEnum(in, CompatibilityReason.values()));
        RomHashes hashes = new RomHashes(
                readString(in), readString(in), readString(in), readString(in));
        RomAnalysis analysis = readAnalysis(in);
        ZipEntryIdentity identity = null; ZipNameEncoding encoding = null;
        if (in.readBoolean()) {
            identity = new ZipEntryIdentity(readString(in), in.readInt());
            encoding = readEnum(in, ZipNameEncoding.values());
        }
        return new RomVariant(id, game, path, format, compatibility, hashes,
                analysis, identity, encoding);
    }

    private static void writeCanonical(DataOutputStream out, CanonicalGame game)
            throws IOException, CodecException {
        writeString(out, game.id()); writeCount(out, game.titleCandidates().size());
        for (TitleCandidate title : game.titleCandidates()) {
            writeString(out, title.value()); writeEnum(out, title.language());
            writeEnum(out, title.origin()); writeEnum(out, title.confidence());
            writeEnum(out, title.reviewState());
        }
        writeCount(out, game.aliases().size());
        for (String alias : game.aliases()) writeString(out, alias);
    }

    private static CanonicalGame readCanonical(DataInputStream in)
            throws IOException, CodecException {
        String id = readString(in); ArrayList<TitleCandidate> titles = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) {
            titles.add(new TitleCandidate(readString(in),
                    readEnum(in, TitleCandidate.Language.values()),
                    readEnum(in, TitleCandidate.Origin.values()),
                    readEnum(in, TitleCandidate.Confidence.values()),
                    readEnum(in, TitleCandidate.ReviewState.values())));
        }
        ArrayList<String> aliases = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--) aliases.add(readString(in));
        return new CanonicalGame(id, titles, aliases);
    }

    private static void writeAnalysis(DataOutputStream out, RomAnalysis item)
            throws IOException, CodecException {
        out.writeLong(item.expectedBytes()); out.writeLong(item.actualBytes());
        out.writeLong(item.prgBytes()); out.writeLong(item.chrBytes());
        out.writeInt(item.mapper()); out.writeInt(item.submapper());
        out.writeBoolean(item.trainer()); out.writeBoolean(item.battery());
        out.writeInt(item.diskSides()); writeCount(out, item.warnings().size());
        for (RomAnalysis.Warning warning : item.warnings()) writeEnum(out, warning);
    }

    private static RomAnalysis readAnalysis(DataInputStream in)
            throws IOException, CodecException {
        long expected = in.readLong(), actual = in.readLong(), prg = in.readLong(), chr = in.readLong();
        int mapper = in.readInt(), submapper = in.readInt();
        boolean trainer = in.readBoolean(), battery = in.readBoolean();
        int sides = in.readInt(); ArrayList<RomAnalysis.Warning> warnings = new ArrayList<>();
        for (int count = readCount(in); count > 0; count--)
            warnings.add(readEnum(in, RomAnalysis.Warning.values()));
        return new RomAnalysis(expected, actual, prg, chr, mapper, submapper,
                trainer, battery, sides, warnings);
    }

    private static void writeRomSource(DataOutputStream out, RomSource source)
            throws IOException, CodecException {
        writeString(out, source.id()); writeEnum(out, source.type()); writeString(out, source.uri());
        writeEnum(out, source.permissionState()); writeEnum(out, source.availability());
    }

    private static RomSource readRomSource(DataInputStream in)
            throws IOException, CodecException {
        return new RomSource(readString(in), readEnum(in, RomSource.Type.values()), readString(in),
                readEnum(in, RomSource.PermissionState.values()),
                readEnum(in, RomSource.Availability.values()));
    }

    private static void writeNullable(DataOutputStream out, String value)
            throws IOException, CodecException {
        out.writeBoolean(value != null); if (value != null) writeString(out, value);
    }

    private static String readNullable(DataInputStream in) throws IOException, CodecException {
        return in.readBoolean() ? readString(in) : null;
    }

    private static void writeString(DataOutputStream out, String value)
            throws IOException, CodecException {
        byte[] utf8;
        try {
            ByteBuffer encoded = StandardCharsets.UTF_8.newEncoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .encode(java.nio.CharBuffer.wrap(value));
            utf8 = new byte[encoded.remaining()];
            encoded.get(utf8);
        } catch (CharacterCodingException malformed) {
            throw new CodecException(ErrorCode.INVALID_FIELD, malformed);
        }
        if (utf8.length > MAX_STRING_BYTES) throw error(ErrorCode.BOUNDS);
        out.writeInt(utf8.length); out.write(utf8);
    }

    private static String readString(DataInputStream in) throws IOException, CodecException {
        int length = in.readInt();
        if (length < 0 || length > MAX_STRING_BYTES) {
            throw error(ErrorCode.BOUNDS);
        }
        if (length > in.available()) throw error(ErrorCode.TRUNCATED);
        byte[] utf8 = new byte[length]; in.readFully(utf8);
        try {
            return StandardCharsets.UTF_8.newDecoder()
                    .onMalformedInput(CodingErrorAction.REPORT)
                    .onUnmappableCharacter(CodingErrorAction.REPORT)
                    .decode(ByteBuffer.wrap(utf8)).toString();
        } catch (CharacterCodingException malformed) {
            throw new CodecException(ErrorCode.INVALID_FIELD, malformed);
        }
    }

    private static void writeCount(DataOutputStream out, int count)
            throws IOException, CodecException {
        if (count < 0 || count > MAX_COUNT) throw error(ErrorCode.BOUNDS);
        out.writeInt(count);
    }

    private static int readCount(DataInputStream in) throws IOException, CodecException {
        int count = in.readInt();
        if (count < 0 || count > MAX_COUNT) throw error(ErrorCode.BOUNDS);
        return count;
    }

    private static <E extends Enum<E>> void writeEnum(DataOutputStream out, E value)
            throws IOException { out.writeInt(value.ordinal()); }

    private static <E extends Enum<E>> E readEnum(DataInputStream in, E[] values)
            throws IOException, CodecException {
        int ordinal = in.readInt();
        if (ordinal < 0 || ordinal >= values.length) throw error(ErrorCode.INVALID_FIELD);
        return values[ordinal];
    }

    private static byte[] sha256(byte[] value) {
        try { return MessageDigest.getInstance("SHA-256").digest(value); }
        catch (NoSuchAlgorithmException impossible) { throw new IllegalStateException(impossible); }
    }

    private static CodecException error(ErrorCode code) { return new CodecException(code, null); }

    private static final class BoundedByteArrayOutputStream extends ByteArrayOutputStream {
        private final int limit;
        BoundedByteArrayOutputStream(int limit) { this.limit = limit; }

        @Override
        public synchronized void write(int value) {
            ensureCapacityWithinLimit(1);
            super.write(value);
        }

        @Override
        public synchronized void write(byte[] value, int offset, int length) {
            ensureCapacityWithinLimit(length);
            super.write(value, offset, length);
        }

        private void ensureCapacityWithinLimit(int added) {
            if (added < 0 || count > limit - added) throw new SizeLimitException();
        }
    }

    private static final class SizeLimitException extends IllegalArgumentException {
    }

    public enum ErrorCode {
        INVALID_MAGIC, UNKNOWN_VERSION, TRUNCATED, CHECKSUM_MISMATCH, BOUNDS, INVALID_FIELD, IO
    }

    public static final class CodecException extends Exception {
        private final ErrorCode code;
        CodecException(ErrorCode code, Throwable cause) { super(code.name(), cause); this.code = code; }
        public ErrorCode code() { return code; }
    }
}
