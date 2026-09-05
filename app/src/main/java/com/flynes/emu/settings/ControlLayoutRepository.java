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

    /** Native or in-memory owner of the UTF-8 layout string. */
    public interface Backend {
        String controlLayoutGet();
        boolean controlLayoutApply(String utf8);
    }

    private final Backend backend;

    public ControlLayoutRepository(Backend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    public ControlLayoutRepository(SettingsStore store) {
        this(new SettingsStoreBackend(store));
    }

    public ControlLayoutRepository(Context context) {
        this(backendFor(context));
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
