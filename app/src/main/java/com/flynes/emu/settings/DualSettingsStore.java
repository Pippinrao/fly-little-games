package com.flynes.emu.settings;

import java.util.Map;
import java.util.Objects;

/** Canonical native snapshot with a best-effort SharedPreferences mirror for preference XML. */
public final class DualSettingsStore implements SettingsStore {
    private final NativeSettingsStore nativeStore;
    private final SettingsStore mirror;

    public DualSettingsStore(NativeSettingsStore nativeStore, SettingsStore mirror) {
        this.nativeStore = Objects.requireNonNull(nativeStore, "native store");
        this.mirror = Objects.requireNonNull(mirror, "mirror");
    }

    @Override public Map<String, ?> snapshot() {
        return nativeStore.snapshot();
    }

    @Override public boolean commit(SettingsBatch batch) {
        if (!nativeStore.commit(batch)) return false;
        mirror.commit(batch);
        return true;
    }
}
