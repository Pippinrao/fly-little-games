package com.flynes.emu.catalog.source;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

/**
 * The native catalog rejects any relative path that is not canonical, so the platform layer must
 * never hand one over. The rules mirror {@code is_safe_relative_path}.
 */
public final class SourceRelativePathTest {
    @Test
    public void acceptsNestedRelativePathsWithForwardSlashes() {
        assertTrue(SourceRelativePath.isCanonical("game.nes"));
        assertTrue(SourceRelativePath.isCanonical("NES/game.nes"));
        assertTrue(SourceRelativePath.isCanonical("ROMs 中文/NES/神风马里奥3.zip"));
        assertTrue(SourceRelativePath.isCanonical("primary:ROMs/NES/game.nes"));
    }

    @Test
    public void rejectsPathsThatAreNotRelativeToTheTreeRoot() {
        assertFalse(SourceRelativePath.isCanonical(null));
        assertFalse(SourceRelativePath.isCanonical(""));
        assertFalse(SourceRelativePath.isCanonical("/NES/game.nes"));
        assertFalse(SourceRelativePath.isCanonical("\\NES\\game.nes"));
        assertFalse(SourceRelativePath.isCanonical("NES/game.nes/"));
        assertFalse(SourceRelativePath.isCanonical("NES//game.nes"));
        assertFalse(SourceRelativePath.isCanonical("./game.nes"));
        assertFalse(SourceRelativePath.isCanonical("../game.nes"));
        assertFalse(SourceRelativePath.isCanonical("NES/../game.nes"));
        assertFalse(SourceRelativePath.isCanonical("C:/ROMs/game.nes"));
    }
}
