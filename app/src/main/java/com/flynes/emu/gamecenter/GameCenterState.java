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
    public GameCenterState() { }

    public static GameCenterState restore(
            String categoryName, String query, String selectedCanonicalId) {
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
        return state;
    }

    public Category category() { return category; }
    public String query() { return query; }
    public String selectedCanonicalId() { return selections.get(category); }

    public void setCategory(Category value) {
        category = value == null ? Category.ALL : value;
    }

    public void setQuery(String value) {
        query = value == null ? "" : value;
    }

    public void select(String canonicalId) {
        if (canonicalId == null || canonicalId.trim().isEmpty()) selections.remove(category);
        else selections.put(category, canonicalId);
    }

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

    public void reconcile(List<GameCenterItem> visible) {
        String selected = selectedCanonicalId();
        boolean exists = false;
        for (GameCenterItem item : visible) exists |= item.canonicalId().equals(selected);
        if (!exists) select(visible.isEmpty() ? null : visible.get(0).canonicalId());
    }

    private static boolean contains(String value, String needle) {
        return normalize(value).contains(needle);
    }

    private static String normalize(String value) {
        return Normalizer.normalize(value == null ? "" : value, Normalizer.Form.NFKC)
                .toLowerCase(Locale.ROOT).trim();
    }
}
