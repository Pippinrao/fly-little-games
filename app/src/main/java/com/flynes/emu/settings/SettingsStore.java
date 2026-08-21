package com.flynes.emu.settings;

/** Minimal persistence boundary that keeps settings validation JVM-testable. */
public interface SettingsStore {
    String getString(String key, String fallback);
    int getInt(String key, int fallback);
    boolean getBoolean(String key, boolean fallback);
    void putString(String key, String value);
    void putInt(String key, int value);
    void putBoolean(String key, boolean value);
}
