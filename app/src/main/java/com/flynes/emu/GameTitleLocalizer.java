package com.flynes.emu;

import com.flynes.emu.app.NativeGameTitle;
import java.util.Locale;

/** Presentation only. Identification is performed once by the shared offline index. */
public final class GameTitleLocalizer {
    private GameTitleLocalizer() { }

    public static String localize(String rawName, Locale locale) {
        return NativeGameTitle.UNKNOWN.displayName(rawName, locale);
    }

    public static String localize(String rawName, NativeGameTitle title, Locale locale) {
        return title.displayName(rawName, locale);
    }
}
