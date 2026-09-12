package com.flynes.emu.catalog.source;

import java.util.ArrayList;
import java.util.List;

/**
 * Pure-JVM shape check for storage-access-framework locators.
 *
 * <p>{@code ContentResolver} only opens <em>document</em> locators. A tree locator with an
 * appended relative path, for example
 * {@code content://com.android.externalstorage.documents/tree/primary%3AROMs/神风马里奥3.zip},
 * is not a document locator: the storage provider answers it with
 * {@code IllegalArgumentException: Invalid URI} instead of a stream. That unchecked fault used
 * to escape the launch thread and killed the process, so every locator is checked against this
 * shape before it is persisted or opened.
 */
public final class DocumentLocatorShape {
    private static final String CONTENT_SCHEME = "content://";
    private static final String TREE_SEGMENT = "tree";
    private static final String DOCUMENT_SEGMENT = "document";

    private DocumentLocatorShape() {
    }

    /**
     * Returns whether {@code locator} is a content URI that addresses exactly one document:
     * either {@code content://<authority>/document/<documentId>} or
     * {@code content://<authority>/tree/<treeId>/document/<documentId>}.
     */
    public static boolean isOpenableDocumentLocator(String locator) {
        if (locator == null) return false;
        String trimmed = locator.trim();
        if (!trimmed.startsWith(CONTENT_SCHEME)) return false;
        String withoutScheme = trimmed.substring(CONTENT_SCHEME.length());
        int authorityEnd = withoutScheme.indexOf('/');
        if (authorityEnd <= 0) return false;
        String authority = withoutScheme.substring(0, authorityEnd);
        if (authority.indexOf('?') >= 0 || authority.indexOf('#') >= 0) return false;
        String path = withoutScheme.substring(authorityEnd);
        int end = path.length();
        int query = path.indexOf('?');
        if (query >= 0) end = Math.min(end, query);
        int fragment = path.indexOf('#');
        if (fragment >= 0) end = Math.min(end, fragment);
        List<String> segments = segments(path.substring(0, end));
        if (segments.size() == 2) {
            return DOCUMENT_SEGMENT.equals(segments.get(0)) && !segments.get(1).isEmpty();
        }
        if (segments.size() == 4) {
            return TREE_SEGMENT.equals(segments.get(0)) && !segments.get(1).isEmpty()
                    && DOCUMENT_SEGMENT.equals(segments.get(2)) && !segments.get(3).isEmpty();
        }
        return false;
    }

    private static List<String> segments(String path) {
        List<String> segments = new ArrayList<>(4);
        int start = 0;
        for (int index = 0; index <= path.length(); index++) {
            if (index == path.length() || path.charAt(index) == '/') {
                if (index > start) segments.add(path.substring(start, index));
                start = index + 1;
            }
        }
        return segments;
    }
}
