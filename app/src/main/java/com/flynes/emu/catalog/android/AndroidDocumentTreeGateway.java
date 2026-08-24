package com.flynes.emu.catalog.android;

import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.DocumentsContract;

import com.flynes.emu.catalog.source.DocumentTreeGateway;

import java.io.FileNotFoundException;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayList;
import java.util.List;

/** Read-only DocumentsContract adapter; it never requests provider write access. */
public final class AndroidDocumentTreeGateway implements DocumentTreeGateway {
    private static final String[] PROJECTION = {
            DocumentsContract.Document.COLUMN_DOCUMENT_ID,
            DocumentsContract.Document.COLUMN_DISPLAY_NAME,
            DocumentsContract.Document.COLUMN_MIME_TYPE
    };

    private final ContentResolver resolver;
    private final Uri treeUri;

    public AndroidDocumentTreeGateway(ContentResolver resolver, String treeLocator) {
        if (resolver == null || treeLocator == null) throw new NullPointerException();
        this.resolver = resolver;
        this.treeUri = Uri.parse(treeLocator);
    }

    @Override
    public String rootDocumentId() throws IOException, SecurityException {
        if (!DocumentsContract.isTreeUri(treeUri)) throw new IOException("not a tree URI");
        final String expected;
        try { expected = DocumentsContract.getTreeDocumentId(treeUri); }
        catch (RuntimeException invalid) { throw new IOException("invalid tree URI", invalid); }
        if (expected == null || expected.length() == 0) throw new IOException("missing root ID");
        Uri root = DocumentsContract.buildDocumentUriUsingTree(treeUri, expected);
        try (Cursor cursor = resolver.query(root, PROJECTION, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst()) throw new IOException("missing root");
            String actual = cursor.getString(0);
            if (!expected.equals(actual) || cursor.moveToNext()) {
                throw new IOException("root identity mismatch");
            }
        }
        return expected;
    }

    @Override
    public List<DocumentNode> listChildren(String parentDocumentId)
            throws IOException, SecurityException {
        Uri children = DocumentsContract.buildChildDocumentsUriUsingTree(
                treeUri, parentDocumentId);
        ArrayList<DocumentNode> result = new ArrayList<>();
        try (Cursor cursor = resolver.query(children, PROJECTION, null, null, null)) {
            if (cursor == null) throw new IOException("null children cursor");
            while (cursor.moveToNext()) {
                String id = cursor.getString(0);
                String name = cursor.getString(1);
                String mime = cursor.getString(2);
                if (id == null || id.length() == 0 || name == null || name.length() == 0
                        || mime == null || mime.length() == 0) {
                    throw new IOException("incomplete document row");
                }
                Uri locator = DocumentsContract.buildDocumentUriUsingTree(treeUri, id);
                result.add(new DocumentNode(
                        id, name, DocumentsContract.Document.MIME_TYPE_DIR.equals(mime),
                        locator.toString()));
            }
        } catch (IllegalArgumentException malformed) {
            throw new IOException("invalid document tree", malformed);
        }
        return result;
    }

    @Override
    public InputStream open(String contentLocator) throws IOException, SecurityException {
        InputStream opened = resolver.openInputStream(Uri.parse(contentLocator));
        if (opened == null) throw new FileNotFoundException("provider returned null stream");
        return opened;
    }
}
