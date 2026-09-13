package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
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
import com.flynes.emu.catalog.source.DocumentLocatorShape;

import org.junit.Test;

import java.util.Arrays;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

public final class NativeCatalogProjectorTest {
    @Test public void indexTitlesReplaceRenamedPackageMetadataWithoutChangingIdentity() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] tree = fill(16, 1);
        map.put(tree, "content://provider/tree/roms");
        NativeCatalogEntry row = new NativeCatalogEntry(
                tree, 16400, 17000, 16400, 16384, 0, 0, 0, 0,
                fill(20, 1), fill(32, 1), fill(32, 2), new byte[4],
                FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY, 2, 1, 1, 1, 1, 16,
                "canonical-a", "variant-a", "random.nes", "random.zip", new byte[]{1}, 42,
                new com.flynes.emu.app.NativeGameTitle("title-a", "Super Mario Bros. 3",
                        "超级马里奥3", List.of("超级玛莉3"), 1));
        var state = NativeCatalogProjector.project(List.of(row), List.of(),
                Map.of("canonical-a", new CanonicalUserState(true, 1, 2, 3)), 2,
                map, new AndroidPackageLocatorMap(),
                (uri, path) -> "content://provider/tree/roms/document/game");
        var game = safPackage(state).physicalPackage().variants().get(0).canonicalGame();
        assertEquals("Super Mario Bros. 3", game.englishTitle());
        assertEquals("超级马里奥3", game.zhHansTitle());
        assertTrue(game.aliases().contains("超级玛莉3"));
        assertEquals("canonical-a", game.id());
        assertTrue(state.userStates().get(game.id()).favorite());
    }
    @Test
    public void aWhitespaceBasenameDoesNotPoisonTheWholeCatalogProjection() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] tree = fill(16, 1);
        map.put(tree, "content://provider/tree/roms");
        var state = NativeCatalogProjector.project(
                List.of(entry(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                        "whitespace-name", " .nes", 1)), List.of(), Map.of(), 0,
                map, new AndroidPackageLocatorMap(),
                (uri, path) -> "content://provider/tree/roms/document/game");
        assertEquals(" .nes", safPackage(state).physicalPackage().variants().get(0)
                .canonicalGame().titleCandidates().get(0).value());
    }

    @Test
    public void chineseArchiveTitleIsPreservedSeparatelyFromTheEnglishZipEntry() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] tree = fill(16, 1);
        map.put(tree, "content://provider/tree/roms");
        NativeCatalogEntry row = new NativeCatalogEntry(
                tree, 16400, 17000, 16400, 16384, 0, 0, 0, 0,
                fill(20, 1), fill(32, 1), fill(32, 2), new byte[4],
                FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY, 2, 1, 1, 1, 1, 16,
                "canonical-a", "variant-a", "NES/Contra (USA).nes", "动作/魂斗罗.zip",
                "NES/Contra (USA).nes".getBytes(java.nio.charset.StandardCharsets.UTF_8), 42);
        CatalogPackage pkg = safPackage(NativeCatalogProjector.project(
                List.of(row), List.of(), Map.of(), 0, map, new AndroidPackageLocatorMap(),
                (uri, path) -> "content://provider/tree/roms/document/game"));
        var variant = pkg.physicalPackage().variants().get(0);
        assertEquals("魂斗罗", variant.canonicalGame().zhHansTitle());
        assertEquals("Contra (USA)", variant.canonicalGame().englishTitle());
        assertEquals("魂斗罗", com.flynes.emu.gamecenter.GameTitlePresentation.forLocale(
                variant.canonicalGame(), java.util.Locale.SIMPLIFIED_CHINESE).primary());
        assertEquals("NES/Contra (USA).nes", variant.entryPath());
        assertEquals(42, variant.zipEntryIdentity().localHeaderOffset());
    }

    @Test
    public void projectsNativeRowsOntoJavaCatalogStateWithoutEmbeddingSafInCanonicalIds() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] builtin = map.builtinUuid();
        byte[] tree = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        map.put(tree, "content://provider/tree/roms");
        AndroidPackageLocatorMap locators = new AndroidPackageLocatorMap();
        locators.put(tree, "game.nes", "content://provider/tree/roms/document/game.nes");

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
                1, map, locators, NativeCatalogProjector.LocatorResolver.NONE);

        assertEquals("builtin", state.builtinSourceId());
        assertEquals(RomSource.Type.BUILTIN, state.sources().get("builtin").source().type());
        CatalogPackage builtinPkg = state.sources().get("builtin")
                .packages().values().iterator().next();
        assertEquals("From Below", builtinPkg.physicalPackage().variants().get(0)
                .canonicalGame().englishTitle());
        assertEquals("来自下方", builtinPkg.physicalPackage().variants().get(0)
                .canonicalGame().zhHansTitle());
        assertEquals("content://provider/tree/roms",
                state.sources().values().stream()
                        .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                        .findFirst().orElseThrow().source().uri());
        CatalogPackage treePkg = state.sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().values().iterator().next();
        assertEquals("content://provider/tree/roms/document/game.nes",
                treePkg.physicalPackage().sourceUri());
        assertEquals(PackageFormat.RAW, treePkg.physicalPackage().packageFormat());
        assertEquals(RomFormat.INES, treePkg.physicalPackage().variants().get(0).romFormat());
        assertTrue(state.userStates().get("canonical-a").favorite());
        assertEquals(1, state.lastPlayedSequence());
    }

    @Test
    public void favoriteRevisionDefinesProjectedCatalogRevisionBeforeAnyGameIsPlayed() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] builtin = map.builtinUuid();
        NativeCatalogEntry builtinEntry = entry(builtin, FlyCatalogCommands.SOURCE_SCOPE_BUILTIN,
                "builtin:from-below", "from_below.nes", 1);

        CatalogState state = NativeCatalogProjector.project(
                List.of(builtinEntry),
                List.of(new NativeSourceStatus(builtin, FlyCatalogCommands.SOURCE_SCOPE_BUILTIN,
                        FlyCatalogCommands.SCAN_FULL, 1)),
                Map.of("builtin:from-below", new CanonicalUserState(true, 3, 0, 0)),
                0, map, new AndroidPackageLocatorMap(),
                NativeCatalogProjector.LocatorResolver.NONE);

        assertEquals(3, state.revision());
        assertEquals(0, state.lastPlayedSequence());
        assertTrue(state.userStates().get("builtin:from-below").favorite());
    }

    @Test
    public void anUnresolvablePlatformLocatorIsNeverFabricatedIntoATreeUriWithAnAppendedPath() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] tree = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        map.put(tree, "content://com.android.externalstorage.documents/tree/primary%3AROMs");
        NativeCatalogEntry userEntry = entry(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                "canonical-a", "神风马里奥3.zip", 2);

        CatalogState state = NativeCatalogProjector.project(
                List.of(userEntry),
                List.of(new NativeSourceStatus(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                        FlyCatalogCommands.SCAN_FULL, 1)),
                Map.of(), 0, map, new AndroidPackageLocatorMap(),
                NativeCatalogProjector.LocatorResolver.NONE);

        CatalogPackage projected = safPackage(state);
        String locator = projected.physicalPackage().sourceUri();
        assertFalse("fabricated locator must never look openable: " + locator,
                DocumentLocatorShape.isOpenableDocumentLocator(locator));
        assertFalse("a tree URI with an appended path crashes the storage provider: " + locator,
                locator.startsWith("content://"));
        assertEquals(CatalogPackage.Freshness.PRESERVED_STALE, projected.freshness());
        assertEquals(RomSource.Availability.UNAVAILABLE,
                projected.projectedPackage().source().availability());
    }

    @Test
    public void derivesAnOpenableDocumentLocatorWhenNoScannedLocatorSurvivedTheRestart() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidUuidSafMap map = new AndroidUuidSafMap(backing::get, backing::put, backing::remove);
        byte[] tree = new byte[]{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16};
        map.put(tree, "content://com.android.externalstorage.documents/tree/primary%3AROMs");
        NativeCatalogEntry userEntry = entry(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                "canonical-a", "神风马里奥3.zip", 2);

        CatalogState state = NativeCatalogProjector.project(
                List.of(userEntry),
                List.of(new NativeSourceStatus(tree, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY,
                        FlyCatalogCommands.SCAN_FULL, 1)),
                Map.of(), 0, map, new AndroidPackageLocatorMap(),
                (treeLocator, relativePath) -> "content://com.android.externalstorage.documents"
                        + "/tree/primary%3AROMs/document/primary%3AROMs%2F" + relativePath);

        CatalogPackage projected = safPackage(state);
        assertEquals("content://com.android.externalstorage.documents/tree/primary%3AROMs"
                        + "/document/primary%3AROMs%2F神风马里奥3.zip",
                projected.physicalPackage().sourceUri());
        assertEquals(CatalogPackage.Freshness.FRESH, projected.freshness());
        assertEquals(RomSource.Availability.AVAILABLE,
                projected.projectedPackage().source().availability());
    }

    private static CatalogPackage safPackage(CatalogState state) {
        return state.sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().values().iterator().next();
    }

    private static NativeCatalogEntry entry(
            byte[] uuid, int scope, String canonicalId, String path, int hashDigit) {
        byte[] sha1 = fill(20, hashDigit);
        byte[] sha256 = fill(32, hashDigit);
        return new NativeCatalogEntry(
                uuid, 16400, 16400, 16400, 16384, 0, 0, 0, 0, sha1, sha256, sha256,
                new byte[]{0x12, 0x34, (byte) 0xAB, (byte) 0xCD},
                scope, 1, 1, 1, 1, 1, 0, canonicalId, "variant-" + canonicalId, path, path, new byte[0], -1);
    }

    private static byte[] fill(int length, int digit) {
        byte[] bytes = new byte[length];
        Arrays.fill(bytes, (byte) (digit * 17));
        return bytes;
    }
}
