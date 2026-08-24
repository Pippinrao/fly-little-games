package com.flynes.emu.gamecenter;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumMap;
import java.util.List;
import java.util.Locale;

/** Pure, deterministic navigation state for the landscape Game Center. */
public final class GameCenterState {
    public enum Category { RECENT, FAVORITES, ALL, BUILTIN }

    private Category category = Category.ALL;
    private String query = "";
    private final EnumMap<Category, String> selections = new EnumMap<>(Category.class);
    private final EnumMap<Category, Integer> pages = new EnumMap<>(Category.class);

    public GameCenterState() {
        for (Category value : Category.values()) pages.put(value, 0);
    }

    public static GameCenterState restore(
            String categoryName, String query, String selectedCanonicalId, int[] restoredPages) {
        GameCenterState state = new GameCenterState();
        try {
            state.category = Category.valueOf(categoryName);
        } catch (RuntimeException ignored) {
            state.category = Category.ALL;
        }
        state.query = query == null ? "" : query;
        if (selectedCanonicalId != null && !selectedCanonicalId.trim().isEmpty()) {
            state.selections.put(state.category, selectedCanonicalId);
        }
        if (restoredPages != null) {
            Category[] values = Category.values();
            for (int index = 0; index < values.length && index < restoredPages.length; index++) {
                state.pages.put(values[index], Math.max(0, restoredPages[index]));
            }
        }
        return state;
    }

    public Category category() { return category; }
    public String query() { return query; }
    public String selectedCanonicalId() { return selections.get(category); }
    public int page() { return pages.get(category); }

    public void setCategory(Category value) {
        category = value == null ? Category.ALL : value;
    }

    public void setQuery(String value) {
        query = value == null ? "" : value;
        pages.put(category, 0);
    }

    public void select(String canonicalId) {
        if (canonicalId == null || canonicalId.trim().isEmpty()) selections.remove(category);
        else selections.put(category, canonicalId);
    }

    public void setPage(int value) { pages.put(category, Math.max(0, value)); }

    public List<GameCenterItem> itemsFor(Category value, List<GameCenterItem> all) {
        ArrayList<GameCenterItem> result = new ArrayList<>();
        for (GameCenterItem item : all) {
            boolean include = switch (value) {
                case RECENT -> item.lastPlayedSequence() > 0;
                case FAVORITES -> item.favorite();
                case ALL -> true;
                case BUILTIN -> item.builtin();
            };
            if (include) result.add(item);
        }
        if (value == Category.RECENT) {
            result.sort((left, right) -> Long.compare(
                    right.lastPlayedSequence(), left.lastPlayedSequence()));
        }
        return Collections.unmodifiableList(result);
    }

    public List<GameCenterItem> filtered(List<GameCenterItem> all) {
        List<GameCenterItem> categoryItems = itemsFor(category, all);
        String needle = normalize(query);
        if (needle.isEmpty()) return categoryItems;
        ArrayList<GameCenterItem> result = new ArrayList<>();
        for (GameCenterItem item : categoryItems) {
            if (contains(item.titleEn(), needle) || contains(item.titleZhHans(), needle)
                    || contains(item.originalFilename(), needle)) result.add(item);
        }
        return Collections.unmodifiableList(result);
    }

    public List<GameCenterItem> pageItems(List<GameCenterItem> filtered, int pageSize) {
        if (pageSize <= 0) throw new IllegalArgumentException("page size must be positive");
        int start = Math.min(page() * pageSize, filtered.size());
        int end = Math.min(start + pageSize, filtered.size());
        return Collections.unmodifiableList(new ArrayList<>(filtered.subList(start, end)));
    }

    public void reconcile(List<GameCenterItem> visible, int pageSize) {
        int maxPage = visible.isEmpty() ? 0 : (visible.size() - 1) / pageSize;
        setPage(Math.min(page(), maxPage));
        String selected = selectedCanonicalId();
        boolean exists = false;
        for (GameCenterItem item : visible) exists |= item.canonicalId().equals(selected);
        if (!exists) select(visible.isEmpty() ? null : visible.get(0).canonicalId());
    }

    public void movePage(int delta, int itemCount, int pageSize) {
        int max = itemCount == 0 ? 0 : (itemCount - 1) / pageSize;
        setPage(Math.min(max, Math.max(0, page() + delta)));
    }

    public boolean canMovePrevious() { return page() > 0; }
    public boolean canMoveNext(int itemCount, int pageSize) {
        return (page() + 1) * pageSize < itemCount;
    }

    public int[] pagesSnapshot() {
        Category[] values = Category.values();
        int[] result = new int[values.length];
        for (int index = 0; index < values.length; index++) result[index] = pages.get(values[index]);
        return result;
    }

    private static boolean contains(String value, String needle) {
        return normalize(value).contains(needle);
    }

    private static String normalize(String value) {
        return Normalizer.normalize(value == null ? "" : value, Normalizer.Form.NFKC)
                .toLowerCase(Locale.ROOT).trim();
    }
}
