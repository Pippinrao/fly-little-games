package com.flynes.emu.settings;

public enum SettingsSection {
    DISPLAY("section.display"),
    CONTROLS("section.controls"),
    AUDIO("section.audio"),
    GAME_LANGUAGE("section.game_language"),
    ABOUT("section.about");

    private final String rootKey;
    SettingsSection(String rootKey) { this.rootKey = rootKey; }
    public String rootKey() { return rootKey; }

    public static SettingsSection fromRootKey(String key) {
        for (SettingsSection section : values()) if (section.rootKey.equals(key)) return section;
        return DISPLAY;
    }
}
