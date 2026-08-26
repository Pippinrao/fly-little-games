package com.flynes.emu.settings;

import android.content.Context;

import java.util.Map;

public final class DisplayStatusRepository {
    private static final String REQUESTED = "display.status.requested_millihz";
    private static final String ACTUAL = "display.status.actual_millihz";
    private static final String REASON = "display.status.reason";
    private final SettingsStore store;
    public DisplayStatusRepository(SettingsStore store) { this.store = store; }
    public DisplayStatusRepository(Context context) { this(new SharedPreferencesSettingsStore(context)); }
    public void save(DisplayStatus value) {
        store.commit(new SettingsBatch.Builder()
                .putInt(REQUESTED, Math.round(value.requestedHz() * 1000f))
                .putInt(ACTUAL, Math.round(value.actualHz() * 1000f))
                .putString(REASON, value.fallbackReason()).build());
    }
    public DisplayStatus load() {
        Map<String, ?> values = store.snapshot();
        Object requested = values.get(REQUESTED);
        Object actual = values.get(ACTUAL);
        Object reason = values.get(REASON);
        return new DisplayStatus((requested instanceof Integer ? (Integer) requested : 0) / 1000f,
                (actual instanceof Integer ? (Integer) actual : 0) / 1000f,
                reason instanceof String ? (String) reason : "");
    }
}
