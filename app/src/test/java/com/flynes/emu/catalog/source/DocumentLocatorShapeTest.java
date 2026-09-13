package com.flynes.emu.catalog.source;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * Regression cover for the 2026-09-13 device crash: a SAF tree locator with an appended
 * relative path was handed to {@code ContentResolver.openInputStream}, and the storage
 * provider answered with {@code IllegalArgumentException: Invalid URI}. Every locator the
 * catalog persists must be recognisable as an openable document locator.
 */
public final class DocumentLocatorShapeTest {
    /** Verbatim locator from the crash report (AndroidRuntime, thread "flynes-launch"). */
    private static final String CRASH_LOCATOR =
            "content://com.android.externalstorage.documents/tree/primary%3AROMs/神风马里奥3.zip";

    @Test
    public void rejectsTheExactLocatorShapeThatCrashedTheDevice() {
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(CRASH_LOCATOR));
    }

    @Test
    public void rejectsATreeLocatorWithAnyAppendedPath() {
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://com.android.externalstorage.documents/tree/primary%3AROMs/sub/game.nes"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://provider/tree/root/"));
    }

    @Test
    public void rejectsATreeLocatorItself() {
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://com.android.externalstorage.documents/tree/primary%3AROMs"));
    }

    @Test
    public void acceptsTreeScopedDocumentLocators() {
        assertTrue(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://com.android.externalstorage.documents/tree/primary%3AROMs/document/"
                        + "primary%3AROMs%2F%E7%A5%9E%E9%A3%8E%E9%A9%AC%E9%87%8C%E5%A5%A53.zip"));
        assertTrue(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://provider/tree/roms/document/game.nes"));
    }

    @Test
    public void acceptsPlainDocumentLocators() {
        assertTrue(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://provider/document/primary%3AROMs%2Fgame.nes"));
    }

    @Test
    public void rejectsEverythingThatIsNotAContentDocument() {
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(null));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(""));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator("   "));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator("asset:///roms/thwaite.nes"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "unresolved://saf-source/game.nes"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator("content://provider"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator("content://provider/tree"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://provider/tree/root/document"));
        assertFalse(DocumentLocatorShape.isOpenableDocumentLocator(
                "content://provider/document/"));
    }
}
