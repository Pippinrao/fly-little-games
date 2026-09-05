package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.app.NativeCatalogEntry;
import com.flynes.emu.app.NativeSourceStatus;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CanonicalUserState;

import org.junit.Test;

import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class NativeCatalogProjectorTest {
    @Test
    public void projectsNativeRowsOntoJavaCatalogStateWithoutEmbeddingSafInCanonicalIds() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] builtin = map.builtinUuid();
        byte[] tree = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        map.put(tree, "content://tree/roms");
        AndroidPackageLocatorMap locators = new AndroidPackageLocatorMap();
        locators.put(tree, "game.nes", "content://tree/roms/document/game.nes");

        NativeCatalogEntry builtinEntry = entry(builtin, FlyCatalogCommands.SOURCE_SCOPE_BUILTIN,
                "builtin:from-below", "from_below.nes", 1);
        NativeCatalogEntry userEntry = entry(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                "canonical-a", "game.nes", 2);
        CatalogState state = NativeCatalogProjector.project(
                List.of(builtinEntry, userEntry),
                List.of(new NativeSourceStatus(builtin, FlyCatalogCommands.SOURCE_SCOPE_BUILTIN,
                                FlyCatalogCommands.SCAN_FULL, 1),
                        new NativeSourceStatus(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                                FlyCatalogCommands.SCAN_FULL, 1)),
                Map.of("canonical-a", new CanonicalUserState(true, 1, 1, 1)),
                1, map, locators);

        assertEquals("builtin", state.builtinSourceId());
        assertEquals(RomSource.Type.BUILTIN, state.sources().get("builtin").source().type());
        assertEquals("content://tree/roms",
                state.sources().values().stream()
                        .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                        .findFirst().orElseThrow().source().uri());
        CatalogPackage treePkg = state.sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().values().iterator().next();
        assertEquals("content://tree/roms/document/game.nes", treePkg.physicalPackage().sourceUri());
        assertEquals(PackageFormat.RAW, treePkg.physicalPackage().packageFormat());
        assertEquals(RomFormat.INES, treePkg.physicalPackage().variants().get(0).romFormat());
        assertTrue(state.userStates().get("canonical-a").favorite());
        assertEquals(1, state.lastPlayedSequence());
    }

    private static NativeCatalogEntry entry(
            byte[] uuid, int scope, String canonicalId, String path, int hashDigit) {
        byte[] sha1 = fill(20, hashDigit);
        byte[] sha256 = fill(32, hashDigit);
        return new NativeCatalogEntry(
                uuid, 16400, 16400, 16400, 16384, 0, 0, 0, 0, sha1, sha256, sha256,
                new byte[]{0x12, 0x34, (byte) 0xAB, (byte) 0xCD},
                scope, 1, 1, 1, 1, 1, 0, canonicalId, "variant-" + canonicalId, path, path);
    }

    private static byte[] fill(int length, int digit) {
        byte[] bytes = new byte[length];
        Arrays.fill(bytes, (byte) (digit * 17));
        return bytes;
    }
}
