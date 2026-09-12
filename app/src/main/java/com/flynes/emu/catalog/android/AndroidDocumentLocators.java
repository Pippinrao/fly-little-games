package com.flynes.emu.catalog.android;

import android.net.Uri;
import android.provider.DocumentsContract;

import com.flynes.emu.catalog.source.DocumentLocatorShape;
import com.flynes.emu.catalog.source.SourceRelativePath;

/**
 * Derives a real document locator from a SAF tree locator and a path relative to the tree root.
 *
 * <p>The scan-time locator map is process-local, so after a cold start every persisted package
 * needs its document locator rebuilt from the tree. Building it with
 * {@link DocumentsContract#buildDocumentUriUsingTree} keeps the result an openable document URI
 * instead of a tree URI with an appended path.
 */
public final class AndroidDocumentLocators {
    private AndroidDocumentLocators() {
    }

    /**
     * Returns the canonical tree-relative path of {@code documentId}, or {@code null} when the
     * document is not addressable relative to the tree root. Nested libraries need this: the
     * display name alone cannot address {@code ROMs/NES/game.zip}.
     */
    public static String relativePathFor(String treeLocator, String documentId) {
        String rootDocumentId = treeDocumentId(treeLocator);
        if (rootDocumentId == null || documentId == null) return null;
        String prefix = rootDocumentId + "/";
        if (!documentId.startsWith(prefix)) return null;
        String relative = documentId.substring(prefix.length());
        return SourceRelativePath.isCanonical(relative) ? relative : null;
    }

    /** Returns an openable document locator, or {@code null} when none can be derived. */
    public static String documentUriFor(String treeLocator, String relativePath) {
        if (relativePath == null || !SourceRelativePath.isCanonical(relativePath)) return null;
        Uri tree = treeUri(treeLocator);
        String rootDocumentId = treeDocumentId(treeLocator);
        if (tree == null || rootDocumentId == null) return null;
        Uri document;
        try {
            document = DocumentsContract.buildDocumentUriUsingTree(
                    tree, rootDocumentId + "/" + relativePath);
        } catch (RuntimeException invalid) {
            return null;
        }
        String locator = document == null ? null : document.toString();
        return DocumentLocatorShape.isOpenableDocumentLocator(locator) ? locator : null;
    }

    private static Uri treeUri(String treeLocator) {
        if (treeLocator == null) return null;
        Uri tree;
        try {
            tree = Uri.parse(treeLocator);
        } catch (RuntimeException invalid) {
            return null;
        }
        return tree != null && DocumentsContract.isTreeUri(tree) ? tree : null;
    }

    private static String treeDocumentId(String treeLocator) {
        Uri tree = treeUri(treeLocator);
        if (tree == null) return null;
        String rootDocumentId;
        try {
            rootDocumentId = DocumentsContract.getTreeDocumentId(tree);
        } catch (RuntimeException invalid) {
            return null;
        }
        return rootDocumentId == null || rootDocumentId.isEmpty() ? null : rootDocumentId;
    }
}
