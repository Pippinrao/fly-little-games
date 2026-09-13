package com.flynes.emu.app;

import java.util.List;
import java.util.Locale;

/** Immutable, language-independent metadata; never a catalog or save-state identity. */
public record NativeGameTitle(String indexId, String english, String chinese,
        List<String> aliases, int matchKind) {
    public static final NativeGameTitle UNKNOWN = new NativeGameTitle("", "", "", List.of(), 0);

    public NativeGameTitle {
        aliases = List.copyOf(aliases);
    }

    public String displayName(String fallback, Locale locale) {
        boolean zh = locale != null && "zh".equals(locale.getLanguage());
        String primary = zh ? chinese : english;
        String secondary = zh ? english : chinese;
        return !primary.isEmpty() ? primary : !secondary.isEmpty() ? secondary
                : fallback == null ? "" : fallback;
    }
}
