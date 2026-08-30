package com.flynes.emu.gamecenter;

import java.util.Locale;

/** Keeps all category controls reachable when text expansion consumes the landscape header. */
public final class HomeHeaderLayoutPolicy {
    private HomeHeaderLayoutPolicy() { }

    public static boolean compact(float fontScale, Locale locale) {
        boolean pseudoExpanded = locale != null && "en".equals(locale.getLanguage())
                && "XA".equals(locale.getCountry());
        return fontScale >= 1.8f || pseudoExpanded;
    }

    /** Keeps the selected game's actual title readable at accessibility font sizes. */
    public static int detailTitleMaxLines(float fontScale) {
        return fontScale >= 1.8f ? 2 : 1;
    }

    public static int launchButtonHeightDp(float fontScale) {
        return fontScale >= 1.8f ? 88 : 56;
    }
}
