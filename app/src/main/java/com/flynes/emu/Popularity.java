package com.flynes.emu;

/**
 * 游戏名元数据（非版权内容），用于受欢迎程度排序。
 *
 * 内置的经典 NES/FC 游戏热度榜：每个条目是一组中英文/别名关键词 + 分数。
 * 条目按分数从高到低排列，{@link #score(String)} 对文件名做小写包含匹配，
 * 第一个命中的条目（即分数最高的命中）决定该文件的热度分；未命中返回 0。
 * 分数设计：最热门的约 20 款游戏 100~90 分，之后逐档递减，最低 10 分。
 */
public class Popularity {

    /** One ranked entry: any keyword contained in the filename scores `score`. */
    static final class Rank {
        final String[] keywords;
        final int score;

        Rank(int score, String... keywords) {
            this.score = score;
            this.keywords = keywords;
        }
    }

    private static final Rank[] RANKS = PopularityRanks.RANKS;

    private Popularity() {
    }

    /**
     * Scores a filename 0..100. Lowercases the filename, then walks the ranked
     * list from highest score down: the first entry containing any of its
     * keywords wins. Returns 0 when nothing matches.
     */
    public static int score(String filename) {
        if (filename == null) return 0;
        String lower = asciiLower(filename);
        for (Rank r : RANKS) {
            for (String kw : r.keywords) {
                if (lower.contains(kw)) return r.score;
            }
        }
        return 0;
    }

    /** Ranking input is file identity, independent of locale, display title or search aliases. */
    public static int scorePackage(String outerFilename, String entryPath) {
        return Math.max(score(leaf(outerFilename)), score(leaf(entryPath)));
    }

    private static String leaf(String value) {
        if (value == null) return "";
        int separator = Math.max(value.lastIndexOf('/'), value.lastIndexOf('\\'));
        return value.substring(separator + 1);
    }

    // Match C++ byte-for-byte for ASCII case folding, independent of device locale.
    private static String asciiLower(String value) {
        StringBuilder result = new StringBuilder(value.length());
        for (int i = 0; i < value.length(); ++i) {
            char ch = value.charAt(i);
            result.append(ch >= 'A' && ch <= 'Z' ? (char) (ch - 'A' + 'a') : ch);
        }
        return result.toString();
    }

    /** Number of curated entries (useful for tests/tools). */
    public static int entryCount() {
        return RANKS.length;
    }
}
