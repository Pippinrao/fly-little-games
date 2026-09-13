package com.flynes.emu.catalog;

import org.junit.Test;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

/**
 * The Android side must read the bundled-game list from the shared manifest
 * instead of carrying its own hardcoded game. These tests run against the real
 * content/assets/builtin-games.json so they fail when the manifest and the code
 * disagree.
 */
public class BuiltinGamesTest {

    /** Walks up from the module directory until the shared manifest is found. */
    private static Path manifestPath() {
        Path candidate = Paths.get("").toAbsolutePath();
        for (int depth = 0; depth < 6 && candidate != null; depth++) {
            Path manifest = candidate.resolve("content/assets/builtin-games.json");
            if (Files.isRegularFile(manifest)) return manifest;
            candidate = candidate.getParent();
        }
        throw new AssertionError("content/assets/builtin-games.json was not found above " + Paths.get("").toAbsolutePath());
    }

    private static InputStream manifestStream() throws IOException {
        return Files.newInputStream(manifestPath());
    }

    @Test public void readsEveryBundledGameFromTheSharedManifest() throws IOException {
        List<BuiltinGames.Entry> games = BuiltinGames.parse(manifestStream()).all();

        assertEquals("every bundled game must appear once", 7, games.size());

        Set<String> ids = new HashSet<>();
        Set<String> assets = new HashSet<>();
        for (BuiltinGames.Entry game : games) {
            assertTrue("canonical id must be namespaced: " + game.canonicalId,
                    game.canonicalId.startsWith("builtin:"));
            assertTrue("asset must be a bare .nes filename: " + game.assetFilename,
                    game.assetFilename.endsWith(".nes") && !game.assetFilename.contains("/"));
            assertTrue("titles must be present", !game.titleEn.isEmpty() && !game.titleZhHans.isEmpty());
            assertTrue("ids must be unique", ids.add(game.canonicalId));
            assertTrue("asset names must be unique", assets.add(game.assetFilename));
        }

        // An id that is not bundled must never resolve to a game.
        assertNull("an unknown id must not resolve",
                BuiltinGames.parse(manifestStream()).byCanonicalId("builtin:not-bundled"));
    }

    @Test public void exposesTitlesAndAssetsForTheThreePlatforms() throws IOException {
        BuiltinGames games = BuiltinGames.parse(manifestStream());

        BuiltinGames.Entry thwaite = games.byCanonicalId("builtin:thwaite");
        assertNotNull(thwaite);
        assertEquals("thwaite.nes", thwaite.assetFilename);
        assertEquals("Thwaite", thwaite.titleEn);
        assertEquals("护村记", thwaite.titleZhHans);
        assertEquals(0, thwaite.mapper);

        BuiltinGames.Entry stb = games.byCanonicalId("builtin:super-tilt-bro");
        assertNotNull(stb);
        assertEquals("super_tilt_bro.nes", stb.assetFilename);
        assertEquals("only the UNROM variant is emulator-usable", 2, stb.mapper);

        assertNull("unknown ids must not resolve", games.byCanonicalId("builtin:not-bundled"));
        assertNull("empty ids must not resolve", games.byCanonicalId(""));
    }

    @Test public void ordersGamesByTheirDeclaredSortOrder() throws IOException {
        List<BuiltinGames.Entry> games = BuiltinGames.parse(manifestStream()).all();
        for (int index = 1; index < games.size(); index++) {
            assertTrue("sortOrder must be ascending",
                    games.get(index - 1).sortOrder < games.get(index).sortOrder);
        }
    }

    @Test public void rejectsAManifestWithNoGames() {
        String empty = "{\"schemaVersion\":1,\"games\":[]}";
        try {
            BuiltinGames.parse(stream(empty));
            fail("a manifest without games must be rejected");
        } catch (IOException expected) {
            assertTrue("the failure must name the manifest: " + expected.getMessage(),
                    expected.getMessage().contains("builtin-games.json"));
        }
    }

    @Test public void rejectsAnUnsupportedSchemaVersion() {
        String wrong = "{\"schemaVersion\":99,\"games\":[{\"canonicalId\":\"builtin:x\","
                + "\"assetFilename\":\"x.nes\",\"titleEn\":\"X\",\"titleZhHans\":\"X\","
                + "\"mapper\":0,\"sortOrder\":1}]}";
        try {
            BuiltinGames.parse(stream(wrong));
            fail("an unknown schemaVersion must be rejected");
        } catch (IOException expected) {
            assertTrue("the failure must mention the schema version: " + expected.getMessage(),
                    expected.getMessage().contains("schemaVersion"));
        }
    }

    @Test public void reportsMalformedJsonAsAnIoFailure() {
        try {
            BuiltinGames.parse(stream("{not json"));
            fail("malformed JSON must be reported as an IOException");
        } catch (IOException expected) {
            assertTrue("the failure must name the manifest: " + expected.getMessage(),
                    expected.getMessage().contains("builtin-games.json"));
        }
    }

    private static InputStream stream(String text) {
        return new ByteArrayInputStream(text.getBytes(StandardCharsets.UTF_8));
    }
}
