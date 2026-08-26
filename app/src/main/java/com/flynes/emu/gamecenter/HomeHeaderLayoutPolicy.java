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
}
