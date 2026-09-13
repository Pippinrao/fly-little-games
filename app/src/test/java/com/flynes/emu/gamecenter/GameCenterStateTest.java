package com.flynes.emu.gamecenter;

import org.junit.Test;

import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;

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
            "builtin", "Thwaite", "", true, false, 0, "thwaite.nes");
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
                true, true, 10, "library-entry.nes");
        rows.set(0, renamed);
        assertEquals(List.of(renamed, OTHER), state.filtered(rows));
        rows.clear();
        assertEquals(List.of(), state.filtered(rows));
    }
}
