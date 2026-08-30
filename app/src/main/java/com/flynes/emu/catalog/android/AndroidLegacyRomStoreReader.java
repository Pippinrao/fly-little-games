package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.SharedPreferences;

import com.flynes.emu.catalog.migration.LegacyLibraryMigrator;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;

/** Read-only seam for the legacy game_library preferences. */
public final class AndroidLegacyRomStoreReader {
    public static final String PREFERENCES = "game_library";
    public static final String TREE_URI = "tree_uri";
    public static final String GAMES = "games";

    private final SharedPreferences preferences;

    public AndroidLegacyRomStoreReader(Context context) {
        preferences = context.getApplicationContext().getSharedPreferences(
                PREFERENCES, Context.MODE_PRIVATE);
    }

    public LegacyLibraryMigrator.LegacySnapshot read() {
        String tree = preferences.getString(TREE_URI, null);
        String raw = preferences.getString(GAMES, null);
        ArrayList<LegacyLibraryMigrator.LegacyRow> rows = new ArrayList<>();
        if (raw == null) return new LegacyLibraryMigrator.LegacySnapshot(tree, false, rows);
        try {
            JSONArray array = new JSONArray(raw);
            for (int index = 0; index < array.length(); index++) {
                JSONObject value = array.optJSONObject(index);
                if (value == null) return new LegacyLibraryMigrator.LegacySnapshot(tree, true, rows);
                rows.add(new LegacyLibraryMigrator.LegacyRow(
                        value.optString("name", ""), value.optString("uri", ""),
                        value.optString("source", "saf"),
                        value.optBoolean("zipped", false)));
            }
            return new LegacyLibraryMigrator.LegacySnapshot(tree, false, rows);
        } catch (JSONException malformed) {
            return new LegacyLibraryMigrator.LegacySnapshot(tree, true, rows);
        }
    }
}
