package com.flynes.emu.gamecenter;

import com.flynes.emu.Popularity;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.Collections;
import java.util.EnumMap;
import java.util.List;
import java.util.Locale;
import java.util.HashSet;

/** Pure, deterministic navigation state for the landscape Game Center. */
public final class GameCenterState {
    public enum Category { RECENT, FAVORITES, ALL, BUILTIN }

    private Category category = Category.ALL;
    private String query = "";
    private final EnumMap<Category, String> selections = new EnumMap<>(Category.class);
    private List<GameCenterItem> rankedInput = Collections.emptyList();
    private List<GameCenterItem> rankedItems = Collections.emptyList();
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
        // Copy the input when caching: callers may replace rows in a mutable list in place.
        if (value == Category.ALL && rankedInput.equals(all)) return rankedItems;
        ArrayList<GameCenterItem> result = new ArrayList<>();
        HashSet<String> seen = new HashSet<>();
        for (GameCenterItem item : all) {
            boolean include = switch (value) {
                case RECENT -> item.lastPlayedSequence() > 0;
                case FAVORITES -> item.favorite();
                case ALL -> true;
                case BUILTIN -> item.builtin();
            };
            // Canonical IDs derive from ROM payload hashes, never from title similarity.
            if (include && seen.add(item.canonicalId())) result.add(item);
        }
        if (value == Category.RECENT) {
            result.sort((left, right) -> Long.compare(
                    right.lastPlayedSequence(), left.lastPlayedSequence()));
        } else if (value == Category.ALL) {
            java.util.HashMap<String, Integer> scores = new java.util.HashMap<>();
            for (GameCenterItem item : all) {
                int score = item.popularityScore() >= 0 ? item.popularityScore()
                        : Math.max(Popularity.score(item.titleEn()), Math.max(
                        Popularity.score(item.titleZhHans()), Popularity.score(item.originalFilename())));
                scores.merge(item.canonicalId(), score, Math::max);
            }
            result.sort((left, right) -> {
                int order = Integer.compare(scores.get(right.canonicalId()), scores.get(left.canonicalId()));
                // Content IDs are stable ASCII hashes: identical tie order on all platforms.
                return order != 0 ? order : left.canonicalId().compareTo(right.canonicalId());
            });
        }
        List<GameCenterItem> immutable = Collections.unmodifiableList(result);
        if (value == Category.ALL) {
            rankedInput = new ArrayList<>(all);
            rankedItems = immutable;
        }
        return immutable;
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
