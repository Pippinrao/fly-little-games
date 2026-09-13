package com.flynes.emu;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.util.ArrayList;
import java.util.List;

/**
 * Persists the scanned game list and the authorized SAF tree URI in
 * SharedPreferences ("game_library"), serialized with org.json (ships with
 * the Android SDK — zero extra dependencies).
 */
public class RomStore {

    private static final String PREFS = "game_library";
    private static final String KEY_TREE_URI = "tree_uri";
    private static final String KEY_GAMES = "games";

    private RomStore() {
    }

    public static void saveTreeUri(Context ctx, Uri uri) {
        prefs(ctx).edit()
                .putString(KEY_TREE_URI, uri == null ? null : uri.toString())
                .apply();
    }

    /** The last authorized SAF tree URI, or null if none was chosen yet. */
    public static Uri getTreeUri(Context ctx) {
        String s = prefs(ctx).getString(KEY_TREE_URI, null);
        return s == null ? null : Uri.parse(s);
    }

    public static void saveGames(Context ctx, List<GameEntry> games) {
        JSONArray arr = new JSONArray();
        if (games != null) {
            for (GameEntry g : games) {
                arr.put(toJson(g));
            }
        }
        prefs(ctx).edit().putString(KEY_GAMES, arr.toString()).apply();
    }

    /** The stored entries, or an empty list when none are stored. */
    public static List<GameEntry> loadGames(Context ctx) {
        List<GameEntry> out = new ArrayList<>();
        String raw = prefs(ctx).getString(KEY_GAMES, null);
        if (raw == null) return out;
        try {
            JSONArray arr = new JSONArray(raw);
            for (int i = 0; i < arr.length(); i++) {
                GameEntry g = fromJson(arr.optJSONObject(i));
                if (g != null) out.add(g);
            }
        } catch (JSONException e) {
            // Corrupt/legacy payload: ignore, return what parsed (possibly empty).
        }
        return out;
    }

    public static void clear(Context ctx) {
        prefs(ctx).edit().clear().apply();
    }

    private static SharedPreferences prefs(Context ctx) {
        return ctx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }

    private static JSONObject toJson(GameEntry g) {
        JSONObject o = new JSONObject();
        try {
            o.put("name", g.name == null ? "" : g.name);
            o.put("uri", g.uri == null ? "" : g.uri);
            o.put("source", g.source == null ? "saf" : g.source);
            o.put("size", g.size);
            o.put("mapper", g.mapper);
            o.put("prgKb", g.prgKb);
            o.put("chrKb", g.chrKb);
            o.put("zipped", g.zipped);
            o.put("popularity", g.popularity);
            o.put("payloadSha256", g.payloadSha256);
            o.put("romName", g.romName);
        } catch (JSONException e) {
            // JSONObject.put never throws for these value types; unreachable.
        }
        return o;
    }

    private static GameEntry fromJson(JSONObject o) {
        if (o == null) return null;
        GameEntry g = new GameEntry();
        g.name = o.optString("name", "");
        g.uri = o.optString("uri", "");
        g.source = o.optString("source", "saf");
        g.size = o.optLong("size", -1);
        g.mapper = o.optInt("mapper", -1);
        g.prgKb = o.optInt("prgKb", -1);
        g.chrKb = o.optInt("chrKb", -1);
        g.zipped = o.optBoolean("zipped", false);
        g.popularity = o.optInt("popularity", 0);
        g.payloadSha256 = o.optString("payloadSha256", "");
        g.romName = o.optString("romName", "");
        return g;
    }
}
