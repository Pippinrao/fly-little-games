package com.flynes.emu.gamecenter;

import org.junit.Test;

import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

public final class GameCenterStateTest {
    @Test public void zipRankingUsesBothLeafNamesAndExcludesDirectories() {
        assertEquals(100, com.flynes.emu.Popularity.scorePackage("collection.zip", "NES/Contra.nes"));
        assertEquals(100, com.flynes.emu.Popularity.scorePackage("魂斗罗.zip", "game.nes"));
        assertEquals(98, com.flynes.emu.Popularity.scorePackage("Mario collection/Metroid.nes", ""));
        assertEquals(0, com.flynes.emu.Popularity.scorePackage("Mario collection/game.zip", "Contra folder/game.nes"));
    }

    @Test public void explicitPackageScoreTakesPrecedenceOverSearchAliases() {
        GameCenterState state = new GameCenterState();
        GameCenterItem metroid = new GameCenterItem("metroid", "Metroid", "", false, false, 0,
                "Mario collection/Metroid.nes", 98);
        assertEquals(List.of(OTHER, metroid), state.filtered(List.of(metroid, OTHER)));
    }
    private static final GameCenterItem BUILTIN = new GameCenterItem(
            "builtin", "From Below", "", true, false, 0, "from_below.nes");
    private static final GameCenterItem FAVORITE = new GameCenterItem(
            "mario", "Super Mario Bros.", "超级马里奥", false, true, 7, "mario.nes");
    private static final GameCenterItem OTHER = new GameCenterItem(
            "contra", "Contra", "魂斗罗", false, false, 0, "contra.zip");

    @Test public void filtersEveryCategoryAndSearchesAllLocalizedFields() {
        GameCenterState state = new GameCenterState();
        List<GameCenterItem> all = Arrays.asList(BUILTIN, FAVORITE, OTHER);

        assertEquals(1, state.itemsFor(GameCenterState.Category.RECENT, all).size());
        assertEquals(1, state.itemsFor(GameCenterState.Category.FAVORITES, all).size());
        assertEquals(3, state.itemsFor(GameCenterState.Category.ALL, all).size());
        assertEquals(1, state.itemsFor(GameCenterState.Category.BUILTIN, all).size());

        state.setQuery("魂斗");
        assertEquals("contra", state.filtered(all).get(0).canonicalId());
        state.setQuery("MARIO.NES");
        assertEquals("mario", state.filtered(all).get(0).canonicalId());
    }

    @Test public void remembersSelectionIndependentlyPerCategory() {
        GameCenterState state = new GameCenterState();
        state.select("mario");
        state.setCategory(GameCenterState.Category.BUILTIN);
        state.select("builtin");

        state.setCategory(GameCenterState.Category.ALL);
        assertEquals("mario", state.selectedCanonicalId());
        state.setCategory(GameCenterState.Category.BUILTIN);
        assertEquals("builtin", state.selectedCanonicalId());
    }

    @Test public void restoreRepairsMissingSelectionWithoutPageState() {
        GameCenterState state = GameCenterState.restore(
                "ALL", "zelda", "missing");
        List<GameCenterItem> visible = Arrays.asList(BUILTIN, FAVORITE, OTHER);

        state.reconcile(visible);

        assertEquals("builtin", state.selectedCanonicalId());
        assertEquals("zelda", state.query());
    }

    @Test public void returnsTheEntireFilteredLibraryForContinuousScrolling() {
        GameCenterState state = new GameCenterState();
        state.setCategory(GameCenterState.Category.ALL);
        List<GameCenterItem> items = Arrays.asList(BUILTIN, FAVORITE, OTHER);

        assertEquals(Arrays.asList(OTHER, FAVORITE, BUILTIN), state.filtered(items));
        state.reconcile(items);
        assertEquals("builtin", state.selectedCanonicalId());
    }

    @Test public void allSortsByPopularityThenContentIdAndDeduplicatesOnlyIdenticalContent() {
        GameCenterState state = new GameCenterState();
        GameCenterItem translated = new GameCenterItem("contra-zh", "", "魂斗罗",
                false, false, 0, "translated.zip");
        GameCenterItem sequel = new GameCenterItem("contra2", "Contra II", "",
                false, false, 0, "sequel.nes");
        GameCenterItem lower = new GameCenterItem("donkey", "", "",
                false, false, 0, "DONKEY KONG.nes");
        List<GameCenterItem> input = Arrays.asList(BUILTIN, lower, sequel, FAVORITE,
                translated, OTHER, OTHER);
        List<GameCenterItem> expected = Arrays.asList(OTHER, translated, sequel, FAVORITE,
                lower, BUILTIN);
        assertEquals(expected, state.filtered(input));
        java.util.ArrayList<GameCenterItem> reversed = new java.util.ArrayList<>(input);
        java.util.Collections.reverse(reversed);
        assertEquals(expected, state.filtered(reversed));
        state.setQuery("contra");
        assertEquals(Arrays.asList(OTHER, sequel), state.filtered(input));
    }

    @Test public void cachedOrderRefreshesWhenSameListChangesTitlesOrUserState() {
        GameCenterState state = new GameCenterState();
        java.util.ArrayList<GameCenterItem> rows = new java.util.ArrayList<>(List.of(BUILTIN, OTHER));
        assertEquals(List.of(OTHER, BUILTIN), state.filtered(rows));
        GameCenterItem renamed = new GameCenterItem("builtin", "Super Mario", "",
                true, true, 10, "from_below.nes");
        rows.set(0, renamed);
        assertEquals(List.of(renamed, OTHER), state.filtered(rows));
        rows.clear();
        assertEquals(List.of(), state.filtered(rows));
    }

    // --- P3: independent two-player filter (design U04, cases C02/C03) --------

    private static final GameCenterState.MultiplayerCapabilityRegistry REGISTRY =
            new GameCenterState.MultiplayerCapabilityRegistry(7L);
    static {
        REGISTRY.put("dual-cart", GameCenterState.MultiplayerEligibility.SUPPORTED, 7L);
        REGISTRY.put("solo-builtin", GameCenterState.MultiplayerEligibility.UNSUPPORTED, 7L);
        REGISTRY.put("stale-profile", GameCenterState.MultiplayerEligibility.SUPPORTED, 6L);
    }
    private static final GameCenterItem DUAL_CART = new GameCenterItem(
            "dual-cart", "Battle City", "坦克大战", false, true, 0, "battle_city.nes");
    private static final GameCenterItem SOLO_BUILTIN = new GameCenterItem(
            "solo-builtin", "From Below", "来自下方", true, true, 0, "from_below.nes");
    private static final GameCenterItem STALE_PROFILE = new GameCenterItem(
            "stale-profile", "Old Profile Game", "", false, true, 0, "old.nes");
    private static final GameCenterItem UNKNOWN_GAME = new GameCenterItem(
            "unread", "Unknown Game", "", false, true, 0, "unknown.nes");

    @Test public void registryTriValueProjection() {
        assertEquals(GameCenterState.MultiplayerEligibility.SUPPORTED,
                REGISTRY.eligibilityFor("dual-cart"));
        assertEquals(GameCenterState.MultiplayerEligibility.UNSUPPORTED,
                REGISTRY.eligibilityFor("solo-builtin"));
        // Unread and version-mismatched ids project UNKNOWN, never UNSUPPORTED.
        assertEquals(GameCenterState.MultiplayerEligibility.UNKNOWN,
                REGISTRY.eligibilityFor("unread"));
        assertEquals(GameCenterState.MultiplayerEligibility.UNKNOWN,
                REGISTRY.eligibilityFor("stale-profile"));
    }

    @Test public void filterOnKeepsOnlySupportedInStableOrder() {
        GameCenterState favorites = GameCenterState.restore("FAVORITES", "", "", true);
        List<GameCenterItem> rows = List.of(DUAL_CART, SOLO_BUILTIN, STALE_PROFILE, UNKNOWN_GAME);
        List<GameCenterItem> base = favorites.filtered(rows);
        List<GameCenterItem> filtered = favorites.filtered(rows, REGISTRY);
        assertEquals(List.of("dual-cart"), idsOf(filtered));
        assertEquals(idsOf(base).stream().filter("dual-cart"::equals).toList(), idsOf(filtered));
    }

    @Test public void filterOffIsIdenticalAndUnknownStaysVisible() {
        GameCenterState favorites = GameCenterState.restore("FAVORITES", "", "", false);
        List<GameCenterItem> rows = List.of(DUAL_CART, SOLO_BUILTIN, STALE_PROFILE, UNKNOWN_GAME);
        assertEquals(idsOf(favorites.filtered(rows)), idsOf(favorites.filtered(rows, REGISTRY)));
        assertTrue(idsOf(favorites.filtered(rows, REGISTRY)).contains("unread"));
    }

    @Test public void zeroResultStaysInCategoryAndClosingClearsOnlyTheSwitch() {
        GameCenterState state = GameCenterState.restore("BUILTIN", "", "", true);
        List<GameCenterItem> rows = List.of(SOLO_BUILTIN, DUAL_CART);
        assertEquals(List.of(), idsOf(state.filtered(rows, REGISTRY)));
        assertEquals(GameCenterState.Category.BUILTIN, state.category());
        state.setMultiplayerOnly(false);
        assertEquals(List.of("solo-builtin"), idsOf(state.filtered(rows, REGISTRY)));
        assertFalse(state.multiplayerOnly());
        assertEquals(GameCenterState.Category.BUILTIN, state.category());
    }

    @Test public void connectionEventsDoNotChangeCategoryQueryOrFilter() {
        GameCenterState state = GameCenterState.restore("FAVORITES", "battle", "", true);
        List<GameCenterItem> rows = List.of(DUAL_CART, SOLO_BUILTIN);
        List<String> before = idsOf(state.filtered(rows, REGISTRY));
        // Simulated connection/disconnection: the projection has no connection
        // input, so re-projecting must be identical and state untouched.
        assertEquals(before, idsOf(state.filtered(rows, REGISTRY)));
        assertEquals(GameCenterState.Category.FAVORITES, state.category());
        assertEquals("battle", state.query());
        assertTrue(state.multiplayerOnly());
    }

    private static List<String> idsOf(List<GameCenterItem> items) {
        return items.stream().map(GameCenterItem::canonicalId).toList();
    }

    // --- P3: replay the canonical multiplayer-filter fixture ------------------

    @Test public void replaysSharedMultiplayerFilterFixture() throws Exception {
        String raw;
        try (java.io.InputStream in = getClass().getResourceAsStream(
                "/flynes_product/multiplayer_filter_cases.json")) {
            assertNotNull("fixture must exist on the test classpath", in);
            raw = new String(in.readAllBytes(), java.nio.charset.StandardCharsets.UTF_8);
        }
        org.json.JSONObject fixture = new org.json.JSONObject(raw);
        long profileVersion = fixture.getLong("profileVersion");
        GameCenterState.MultiplayerCapabilityRegistry registry =
                new GameCenterState.MultiplayerCapabilityRegistry(profileVersion);
        org.json.JSONObject capabilities = fixture.getJSONObject("capabilities");
        java.util.Iterator<String> keys = capabilities.keys();
        while (keys.hasNext()) {
            String id = keys.next();
            registry.put(id, GameCenterState.MultiplayerEligibility.valueOf(
                    capabilities.getString(id)), profileVersion);
        }
        org.json.JSONArray catalog = fixture.getJSONArray("catalog");
        List<GameCenterItem> all = new java.util.ArrayList<>();
        for (int i = 0; i < catalog.length(); i++) {
            org.json.JSONObject row = catalog.getJSONObject(i);
            all.add(new GameCenterItem(row.getString("canonicalId"), row.getString("canonicalId"),
                    "", row.getBoolean("builtin"), row.getBoolean("favorite"),
                    row.getLong("lastPlayedSequence"), row.getString("canonicalId") + ".nes",
                    row.getInt("popularityScore")));
        }

        org.json.JSONArray cases = fixture.getJSONArray("cases");
        for (int i = 0; i < cases.length(); i++) {
            org.json.JSONObject testCase = cases.getJSONObject(i);
            String id = testCase.getString("id");
            GameCenterState state = GameCenterState.restore(testCase.getString("category"),
                    testCase.getString("query"), "",
                    testCase.getBoolean("multiplayerOnly"));
            List<GameCenterItem> rows = new java.util.ArrayList<>(all);
            if (testCase.has("duplicateRows")) {
                org.json.JSONArray dupes = testCase.getJSONArray("duplicateRows");
                for (int d = 0; d < dupes.length(); d++) {
                    String dupeId = dupes.getString(d);
                    for (GameCenterItem item : all) {
                        if (item.canonicalId().equals(dupeId)) rows.add(item);
                    }
                }
            }
            List<String> visible = idsOf(state.filtered(rows, registry));
            List<String> expected = new java.util.ArrayList<>();
            org.json.JSONArray expectedIds = testCase.getJSONArray("expectedVisibleIds");
            for (int e = 0; e < expectedIds.length(); e++) {
                expected.add(expectedIds.getString(e));
            }
            assertEquals("fixture case " + id, expected, visible);
            if (testCase.has("categoryStays")) {
                assertEquals("fixture case " + id + " keeps its category",
                        GameCenterState.Category.valueOf(testCase.getString("categoryStays")),
                        state.category());
            }
        }
    }
}
