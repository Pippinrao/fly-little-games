package com.flynes.emu.nearby;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.GameVariant;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Metadata only. Index zero captures one catalog publication; ROMs are never opened. */
public final class NearbyContentProvider implements AutoCloseable {
    /** Adapter memory budget, not a protocol/catalog limit; overflow rejects the entire batch. */
    public static final int MAX_METADATA_CHOICES = 4096;
    private final GameCatalog catalog;
    private final NearbyExactContentLoader.ReadAccessValidator access;
    private List<GameCatalogEntry> publication;
    private final List<Row> rows = new ArrayList<>();
    private boolean closed;

    public NearbyContentProvider(GameCatalog catalog, NearbyExactContentLoader.ReadAccessValidator access) {
        this.catalog = Objects.requireNonNull(catalog);
        this.access = Objects.requireNonNull(access);
    }

    /** Exact immutable source choice; neither a hash lookup nor a permission lease. */
    public static final class Selection {
        private final List<GameCatalogEntry> publication;
        private final GameVariant variant;
        private final byte[] ref;
        private final byte[] hash;
        private Selection(List<GameCatalogEntry> publication, GameVariant variant,
                byte[] ref, byte[] hash) {
            this.publication = publication;
            this.variant = variant;
            this.ref = ref.clone();
            this.hash = hash.clone();
        }
        public GameVariant variant() { return variant; }
        public byte[] sourceChoiceRef() { return ref.clone(); }
        public byte[] contentHash() { return hash.clone(); }
    }

    public synchronized Selection resolveSelection(byte[] ref) {
        if (!isCurrent(ref)) return null;
        for (Row row : rows) {
            if (Arrays.equals(ref, row.ref)) {
                return new Selection(publication, row.variant, row.ref,
                        Arrays.copyOfRange(row.record, 20, 52));
            }
        }
        return null;
    }

    public synchronized boolean isCurrent(Selection selection) {
        if (selection == null || selection.publication != publication || !isCurrent(selection.ref))
            return false;
        for (Row row : rows) {
            if (Arrays.equals(selection.ref, row.ref)) return selection.variant.equals(row.variant);
        }
        return false;
    }

    /** Returns the existing canonical v1 record; native appends shared start identity. */
    public synchronized byte[] query(int index) {
        if (closed) throw new IllegalStateException("content provider closed");
        if (index < 0) throw new IllegalArgumentException("negative catalog index");
        if (index == 0) {
            rows.clear();
            publication = catalog.canonicalEntries();
            int count = 0;
            for (GameCatalogEntry entry : publication)
                for (GameVariant variant : entry.variants())
                    if (variant.isLaunchable() && ++count > MAX_METADATA_CHOICES)
                        throw new IllegalStateException("nearby metadata capacity exceeded");
            for (GameCatalogEntry entry : publication) {
                for (GameVariant variant : entry.variants()) {
                    if (!variant.isLaunchable()) continue;
                    byte[] name = boundedName(variant.originalFilename());
                    UUID id = UUID.randomUUID();
                    byte[] ref = ByteBuffer.allocate(16).putLong(id.getMostSignificantBits())
                            .putLong(id.getLeastSignificantBits()).array();
                    byte[] hash = new byte[32];
                    String hex = variant.hashes().payloadSha256();
                    for (int i = 0; i < hash.length; ++i)
                        hash[i] = (byte) Integer.parseInt(hex.substring(i * 2, i * 2 + 2), 16);
                    byte[] record = ByteBuffer.allocate(56 + name.length).putShort((short) 1)
                            .putShort((short) 0).put(ref).put(hash).putInt(name.length).put(name).array();
                    rows.add(new Row(variant, ref, record));
                }
            }
        }
        return index < rows.size() ? rows.get(index).record.clone() : null;
    }

    /** No hash fallback: a ref belongs to one exact source in one publication. */
    public synchronized boolean isCurrent(byte[] ref) {
        if (closed || ref == null || ref.length != 16 || publication != catalog.canonicalEntries()) return false;
        for (Row row : rows) {
            if (!Arrays.equals(ref, row.ref)) continue;
            GameVariant current = catalog.resolveVariant(row.variant.variantId()).orElse(null);
            if (!row.variant.equals(current) || !current.isLaunchable()) return false;
            try { access.validate(current.sourceId(), current.sourceUri()); }
            catch (SecurityException denied) { publication = null; return false; }
            return publication == catalog.canonicalEntries();
        }
        return false;
    }

    @Override public synchronized void close() { closed = true; rows.clear(); publication = null; }

    private static byte[] boundedName(String name) {
        StringBuilder out = new StringBuilder();
        int length = 0;
        for (int offset = 0; offset < name.length();) {
            int point = name.codePointAt(offset);
            offset += Character.charCount(point);
            if (point == 0 || (point >= 0xD800 && point <= 0xDFFF)) point = 0xFFFD;
            String part = new String(Character.toChars(point));
            int bytes = part.getBytes(StandardCharsets.UTF_8).length;
            if (length + bytes > 64) break;
            out.append(part); length += bytes;
        }
        if (out.length() == 0) out.append("?");
        return out.toString().getBytes(StandardCharsets.UTF_8);
    }

    private record Row(GameVariant variant, byte[] ref, byte[] record) {}
}
