package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.SharedPreferences;

import com.flynes.emu.catalog.migration.LegacyLibraryMigrator;

public final class AndroidLegacyMigrationMarker implements LegacyLibraryMigrator.MigrationMarker {
    private static final String PREFERENCES = "catalog_migration";
    private static final String COMPLETE = "legacy_game_library_v1_complete";
    private final SharedPreferences preferences;

    public AndroidLegacyMigrationMarker(Context context) {
        preferences = context.getApplicationContext().getSharedPreferences(
                PREFERENCES, Context.MODE_PRIVATE);
    }

    @Override public boolean isComplete() { return preferences.getBoolean(COMPLETE, false); }

    @Override public void markComplete() {
        if (!preferences.edit().putBoolean(COMPLETE, true).commit()) {
            throw new IllegalStateException("migration marker commit failed");
        }
    }
}
