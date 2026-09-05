package com.flynes.emu.settings;

import android.content.Context;

import com.flynes.emu.FlyNesApplication;

/** Production settings come from FLYSET01 when the application owns a native app. */
public final class SettingsAccess {
    private SettingsAccess() {
    }

    public static SettingsRepository repository(Context context) {
        Context application = context.getApplicationContext();
        if (application instanceof FlyNesApplication flynes
                && flynes.settingsRepository() != null) {
            return flynes.settingsRepository();
        }
        return new SettingsRepository(new SharedPreferencesSettingsStore(context));
    }
}
