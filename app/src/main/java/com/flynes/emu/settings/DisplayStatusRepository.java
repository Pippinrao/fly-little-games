package com.flynes.emu.settings;

import android.content.Context;

public final class DisplayStatusRepository {
    private static final String REQUESTED = "display.status.requested_millihz";
    private static final String ACTUAL = "display.status.actual_millihz";
    private static final String REASON = "display.status.reason";
    private final SettingsStore store;
    public DisplayStatusRepository(SettingsStore store) { this.store = store; }
    public DisplayStatusRepository(Context context) { this(new SharedPreferencesSettingsStore(context)); }
    public void save(DisplayStatus value) {
        store.putInt(REQUESTED, Math.round(value.requestedHz() * 1000f));
        store.putInt(ACTUAL, Math.round(value.actualHz() * 1000f));
        store.putString(REASON, value.fallbackReason());
    }
    public DisplayStatus load() {
        return new DisplayStatus(store.getInt(REQUESTED, 0) / 1000f,
                store.getInt(ACTUAL, 0) / 1000f, store.getString(REASON, ""));
    }
}
