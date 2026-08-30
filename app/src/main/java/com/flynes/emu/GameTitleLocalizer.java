package com.flynes.emu;

import java.util.Locale;

/** Conservative aliases for recognizable NES titles; the original ROM name is always kept. */
public final class GameTitleLocalizer {
    private static final Alias[] COMMON = {
            new Alias("超级马力欧兄弟", "super mario", "supermario"),
            new Alias("魂斗罗", "contra"),
            new Alias("坦克大战", "battle city", "battlecity"),
            new Alias("俄罗斯方块", "tetris"),
            new Alias("冒险岛", "adventure island", "adventureisland"),
            new Alias("忍者神龟", "teenage mutant ninja turtles", "tmnt"),
            new Alias("恶魔城", "castlevania", "akumajou", "akumajo"),
            new Alias("沙罗曼蛇", "life force", "lifeforce", "salamander"),
            new Alias("赤色要塞", "jackal", "top gunner"),
            new Alias("松鼠大作战", "chip 'n dale", "chip'n dale", "rescue rangers"),
            new Alias("双截龙", "double dragon"),
            new Alias("洛克人", "mega man", "megaman", "rockman"),
            new Alias("塞尔达传说", "legend of zelda", "zelda"),
            new Alias("银河战士", "metroid"),
            new Alias("勇者斗恶龙", "dragon quest", "dragon warrior"),
            new Alias("最终幻想", "final fantasy"),
            new Alias("火焰之纹章", "fire emblem"),
            new Alias("泡泡龙", "bubble bobble"),
            new Alias("大金刚", "donkey kong"),
            new Alias("吃豆人", "pac-man", "pacman", "pac man"),
            new Alias("忍者龙剑传", "ninja gaiden"),
            new Alias("敲冰块", "ice climber"),
            new Alias("气球大战", "balloon fight"),
            new Alias("越野机车", "excitebike")
    };

    private GameTitleLocalizer() { }

    public static String localize(String rawName, Locale locale) {
        if (rawName == null || rawName.isEmpty()) return "";
        if (locale == null || !"zh".equals(locale.getLanguage()) || containsCjk(rawName)) {
            return rawName;
        }
        String normalized = rawName.toLowerCase(Locale.ROOT);
        for (Alias alias : COMMON) {
            for (String keyword : alias.keywords) {
                if (normalized.contains(keyword)) return alias.chinese + " · " + rawName;
            }
        }
        return rawName;
    }

    private static boolean containsCjk(String text) {
        for (int i = 0; i < text.length(); i++) {
            Character.UnicodeBlock block = Character.UnicodeBlock.of(text.charAt(i));
            if (block == Character.UnicodeBlock.CJK_UNIFIED_IDEOGRAPHS
                    || block == Character.UnicodeBlock.CJK_COMPATIBILITY_IDEOGRAPHS) {
                return true;
            }
        }
        return false;
    }

    private static final class Alias {
        final String chinese;
        final String[] keywords;

        Alias(String chinese, String... keywords) {
            this.chinese = chinese;
            this.keywords = keywords;
        }
    }
}
