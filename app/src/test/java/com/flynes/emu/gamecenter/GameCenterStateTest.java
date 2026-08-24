package com.flynes.emu.gamecenter;

import org.junit.Test;

import java.util.Arrays;
import java.util.Collections;
import java.util.List;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

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

    @Test public void remembersSelectionAndPageIndependentlyPerCategory() {
        GameCenterState state = new GameCenterState();
        state.select("mario");
        state.setPage(3);
        state.setCategory(GameCenterState.Category.BUILTIN);
        state.select("builtin");
        state.setPage(1);

        state.setCategory(GameCenterState.Category.ALL);
        assertEquals("mario", state.selectedCanonicalId());
        assertEquals(3, state.page());
        state.setCategory(GameCenterState.Category.BUILTIN);
        assertEquals("builtin", state.selectedCanonicalId());
        assertEquals(1, state.page());
    }

    @Test public void restoreClampsPageAndRepairsMissingSelection() {
        GameCenterState state = GameCenterState.restore(
                "ALL", "zelda", "missing", new int[]{4, 3, 8, 2});
        List<GameCenterItem> visible = Arrays.asList(BUILTIN, FAVORITE, OTHER);

        state.reconcile(visible, 2);

        assertEquals(1, state.page());
        assertEquals("builtin", state.selectedCanonicalId());
        assertEquals("zelda", state.query());
    }

    @Test public void paginatesWithoutTurningLibraryIntoVerticalList() {
        GameCenterState state = new GameCenterState();
        state.setCategory(GameCenterState.Category.ALL);
        List<GameCenterItem> items = Arrays.asList(BUILTIN, FAVORITE, OTHER);

        assertEquals(Arrays.asList(BUILTIN, FAVORITE), state.pageItems(items, 2));
        assertTrue(state.canMoveNext(items.size(), 2));
        assertFalse(state.canMovePrevious());
        state.movePage(1, items.size(), 2);
        assertEquals(Collections.singletonList(OTHER), state.pageItems(items, 2));
        assertTrue(state.canMovePrevious());
        assertFalse(state.canMoveNext(items.size(), 2));
    }
}
