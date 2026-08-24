package com.flynes.emu.gamecenter;

import org.junit.Test;

import java.util.Arrays;
import java.util.List;

import static org.junit.Assert.assertEquals;

public final class GameCenterStateTest {
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

        assertEquals(items, state.filtered(items));
        state.reconcile(items);
        assertEquals("builtin", state.selectedCanonicalId());
    }
}
