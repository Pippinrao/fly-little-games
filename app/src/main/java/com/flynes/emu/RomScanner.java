package com.flynes.emu;

import android.content.ContentResolver;
import android.content.Context;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;
import android.util.Log;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/**
 * Scans a SAF (Storage Access Framework) tree for .nes / .zip ROM files,
 * parses iNES headers for plain .nes files, and produces GameEntry objects.
 *
 * Implemented with DocumentsContract + ContentResolver only (zero extra
 * dependencies): DocumentFile lives in androidx.documentfile, which this app
 * does not ship. Depth is capped and oversized files are skipped so a large
 * tree cannot stall the scan; missing/denied subtrees are skipped, never
 * thrown.
 */
public class RomScanner {

    private static final String TAG = "FlyNES";

    /** ROMs are tiny; anything over this is not a game ROM and is skipped. */
    public static final long MAX_ROM_BYTES = 4L * 1024 * 1024; // 4 MiB

    private static final byte[] NES_MAGIC = {'N', 'E', 'S', 0x1A};

    private RomScanner() {
    }

    /**
     * Recursively scans up to {@code maxDepth} directory levels below the tree
     * root (root children are depth 1). Returns matching entries, or an empty
     * list on any failure — never throws.
     */
    public static List<GameEntry> scanTree(Context ctx, Uri treeUri, int maxDepth) {
        List<GameEntry> out = new ArrayList<>();
        if (ctx == null || treeUri == null) return out;
        try {
            String treeDocId = DocumentsContract.getTreeDocumentId(treeUri);
            Uri childrenUri = DocumentsContract.buildChildDocumentsUriUsingTree(treeUri, treeDocId);
            collect(ctx.getContentResolver(), childrenUri, out, 1, maxDepth);
        } catch (IllegalArgumentException e) {
            Log.w(TAG, "scanTree: not a tree URI: " + treeUri, e);
        }
        return out;
    }

    private static void collect(ContentResolver cr, Uri childrenUri, List<GameEntry> out,
                                int depth, int maxDepth) {
        if (childrenUri == null || depth > maxDepth) return;
        Cursor c = null;
        try {
            c = cr.query(childrenUri, new String[]{
                    DocumentsContract.Document.COLUMN_DOCUMENT_ID,
                    DocumentsContract.Document.COLUMN_DISPLAY_NAME,
                    DocumentsContract.Document.COLUMN_MIME_TYPE,
                    DocumentsContract.Document.COLUMN_SIZE,
            }, null, null, null);
            if (c == null) return;
            while (c.moveToNext()) {
                String docId = c.getString(0);
                String name = c.getString(1);
                String mime = c.getString(2);
                long size = c.isNull(3) ? -1 : c.getLong(3);
                if (docId == null || name == null) continue;

                if (DocumentsContract.Document.MIME_TYPE_DIR.equals(mime)) {
                    if (depth < maxDepth) {
                        Uri sub = DocumentsContract.buildChildDocumentsUriUsingTree(childrenUri, docId);
                        collect(cr, sub, out, depth + 1, maxDepth);
                    }
                    continue;
                }

                Uri docUri = DocumentsContract.buildDocumentUriUsingTree(childrenUri, docId);
                String lower = name.toLowerCase();
                if (lower.endsWith(".nes")) {
                    GameEntry e = parseNesEntry(cr, docUri, name, size);
                    if (e != null) out.add(e);
                } else if (lower.endsWith(".zip")) {
                    if (size > MAX_ROM_BYTES) continue;
                    GameEntry e = new GameEntry();
                    e.name = stripExtension(name);
                    e.uri = docUri.toString();
                    e.source = "saf";
                    e.size = size;
                    e.mapper = -1;
                    e.prgKb = -1;
                    e.chrKb = -1;
                    e.zipped = true;
                    e.popularity = Popularity.score(e.name);
                    out.add(e);
                }
            }
        } catch (SecurityException e) {
            Log.w(TAG, "scan: permission denied, skipping subtree: " + childrenUri, e);
        } catch (Exception e) {
            Log.w(TAG, "scan: skipping subtree " + childrenUri + ": " + e);
        } finally {
            if (c != null) c.close();
        }
    }

    /** Reads the first 16 header bytes of a .nes and builds its entry, or null. */
    private static GameEntry parseNesEntry(ContentResolver cr, Uri docUri, String name, long size) {
        if (size > MAX_ROM_BYTES) return null;
        byte[] header = new byte[16];
        int n = readAtMost(cr, docUri, header, header.length);
        if (n < header.length || !isNesHeader(header)) return null; // not iNES -> skip
        GameEntry e = new GameEntry();
        e.name = stripExtension(name);
        e.uri = docUri.toString();
        e.source = "saf";
        e.size = size;
        // iNES: PRG in 16 KiB units, CHR in 8 KiB units,
        // mapper = upper nibble of byte 6 | upper nibble of byte 7.
        e.prgKb = (header[4] & 0xFF) * 16;
        e.chrKb = (header[5] & 0xFF) * 8;
        e.mapper = ((header[6] >> 4) & 0x0F) | (header[7] & 0xF0);
        e.zipped = false;
        e.popularity = Popularity.score(e.name);
        return e;
    }

    private static boolean isNesHeader(byte[] h) {
        if (h.length < 4) return false;
        for (int i = 0; i < 4; i++) {
            if (h[i] != NES_MAGIC[i]) return false;
        }
        return true;
    }

    /** Reads up to {@code maxLen} bytes; returns count read, or -1 on failure. */
    private static int readAtMost(ContentResolver cr, Uri uri, byte[] buf, int maxLen) {
        try (InputStream in = cr.openInputStream(uri)) {
            if (in == null) return -1;
            int off = 0;
            while (off < maxLen) {
                int n = in.read(buf, off, maxLen - off);
                if (n < 0) break;
                off += n;
            }
            return off;
        } catch (IOException | SecurityException e) {
            Log.w(TAG, "readAtMost failed: " + uri, e);
            return -1;
        }
    }

    /**
     * Reads a whole SAF document into memory, refusing anything larger than
     * {@code maxBytes} (returns null). Returns null on any failure too.
     */
    public static byte[] readFully(ContentResolver cr, Uri uri, long maxBytes) {
        try (InputStream in = cr.openInputStream(uri)) {
            if (in == null) return null;
            return readFully(in, maxBytes);
        } catch (IOException | SecurityException e) {
            Log.w(TAG, "readFully failed: " + uri, e);
            return null;
        }
    }

    /**
     * Reads a stream fully, refusing anything larger than {@code maxBytes}
     * (returns null). Caller owns the stream. Also used by {@link RomLoader}
     * for assets and zip entries.
     */
    public static byte[] readFully(InputStream in, long maxBytes) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        long total = 0;
        int n;
        while ((n = in.read(buf)) > 0) {
            total += n;
            if (maxBytes > 0 && total > maxBytes) return null; // oversize -> refuse
            out.write(buf, 0, n);
        }
        return out.toByteArray();
    }

    /** "foo.nes" -> "foo"; keeps Chinese names as-is. */
    private static String stripExtension(String name) {
        int dot = name.lastIndexOf('.');
        return dot > 0 ? name.substring(0, dot) : name;
    }
}
