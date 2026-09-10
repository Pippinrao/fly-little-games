package com.flynes.emu.settings;

import android.content.Context;

import com.flynes.emu.FlyNesApplication;
import com.flynes.emu.input.ControlLayoutV2;

import java.util.Map;
import java.util.Objects;

/**
 * ControlLayoutV2 persistence. Production reads/writes the shared {@code control_layout.v2}
 * wire string through {@code fly_control_layout_get/apply}; JVM tests use a fake backend.
 */
public final class ControlLayoutRepository {
    public static final String KEY = "controls.layout_v2";
    /** One-shot marker: legacy SharedPreferences layout was seeded into native persist. */
    public static final String MIGRATED_KEY = "controls.layout_v2.native_migrated";

    /** Native or in-memory owner of the UTF-8 layout string. */
    public interface Backend {
        String controlLayoutGet();
        boolean controlLayoutApply(String utf8);
    }

    private final Backend backend;

    public ControlLayoutRepository(Backend backend) {
        this(backend, null);
    }

    /** Production/test path: native backend with optional legacy prefs seed. */
    public ControlLayoutRepository(Backend backend, SettingsStore legacyPrefs) {
        this.backend = Objects.requireNonNull(backend, "backend");
        maybeMigrateLegacyLayout(legacyPrefs);
    }

    public ControlLayoutRepository(SettingsStore store) {
        this(new SettingsStoreBackend(store));
    }

    public ControlLayoutRepository(Context context) {
        this(backendFor(context), legacyPrefsFor(context));
    }

    public ControlLayoutV2 load() {
        String value = backend.controlLayoutGet();
        return ControlLayoutV2.decodeOrRecommended(value == null ? "" : value);
    }

    public void save(ControlLayoutV2 layout) {
        backend.controlLayoutApply(Objects.requireNonNull(layout, "layout").encode());
    }

    public void reset() {
        save(ControlLayoutV2.recommended());
    }

    private static Backend backendFor(Context context) {
        Context application = context.getApplicationContext();
        if (application instanceof FlyNesApplication flynes) {
            Backend nativeBackend = flynes.controlLayoutBackend();
            if (nativeBackend != null) {
                return nativeBackend;
            }
        }
        return new SettingsStoreBackend(new SharedPreferencesSettingsStore(context));
    }

    private static SettingsStore legacyPrefsFor(Context context) {
        Context application = context.getApplicationContext();
        if (application instanceof FlyNesApplication) {
            return new SharedPreferencesSettingsStore(context);
        }
        return null;
    }

    private void maybeMigrateLegacyLayout(SettingsStore legacyPrefs) {
        if (legacyPrefs == null) {
            return;
        }
        Map<String, ?> values = legacyPrefs.snapshot();
        if (Boolean.TRUE.equals(values.get(MIGRATED_KEY))) {
            return;
        }
        Object legacy = values.get(KEY);
        if (legacy instanceof String legacyUtf8 && !legacyUtf8.isEmpty()) {
            if (!backend.controlLayoutApply(legacyUtf8)) {
                return;
            }
        }
        legacyPrefs.commit(new SettingsBatch.Builder().putBoolean(MIGRATED_KEY, true).build());
    }

    private static final class SettingsStoreBackend implements Backend {
        private final SettingsStore store;

        SettingsStoreBackend(SettingsStore store) {
            this.store = Objects.requireNonNull(store, "store");
        }

        @Override public String controlLayoutGet() {
            Map<String, ?> values = store.snapshot();
            Object value = values.get(KEY);
            return value instanceof String ? (String) value : "";
        }

        @Override public boolean controlLayoutApply(String utf8) {
            return store.commit(new SettingsBatch.Builder().putString(KEY, utf8).build());
        }
    }
}
