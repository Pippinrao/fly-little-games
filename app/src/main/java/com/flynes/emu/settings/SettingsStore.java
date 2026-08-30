package com.flynes.emu.settings;

import java.util.Map;

/** Atomic persistence boundary that keeps settings validation JVM-testable. */
public interface SettingsStore {
    Map<String, ?> snapshot();
    boolean commit(SettingsBatch batch);
}
