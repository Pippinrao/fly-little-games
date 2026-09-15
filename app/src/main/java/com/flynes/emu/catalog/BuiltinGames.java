package com.flynes.emu.catalog;

import android.content.Context;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.HashMap;
import java.util.List;
import java.util.Map;

/**
 * The bundled homebrew games, read from the single shared manifest that ships in
 * content/assets/builtin-games.json.
 *
 * <p>Nothing in the Android source may name a bundled game: adding one is a
 * manifest edit plus an asset, and every platform picks it up from here.
 */
public final class BuiltinGames {

    public enum MultiplayerEligibility { SUPPORTED, UNSUPPORTED, UNKNOWN }

    /** Asset name of the manifest, at the root of the merged assets directory. */
    public static final String ASSET_NAME = "builtin-games.json";
    /** Asset directory that holds the bundled ROMs. */
    public static final String ASSET_DIR = "roms/";

    private static final int SUPPORTED_SCHEMA_VERSION = 1;

    /** One bundled game as declared by the manifest. */
    public static final class Entry {
        public final String canonicalId;
        public final String assetFilename;
        public final String titleEn;
        public final String titleZhHans;
        public final String credit;
        public final String licenseFile;
        public final String licenseSpdx;
        public final String licenseSourceUrl;
        public final int mapper;
        public final int sortOrder;
        public final long multiplayerProfileVersion;
        public final MultiplayerEligibility multiplayerEligibility;
        public final int multiplayerMaxPlayers;

        Entry(String canonicalId, String assetFilename, String titleEn, String titleZhHans,
              String credit, String licenseFile, String licenseSpdx, String licenseSourceUrl,
              int mapper, int sortOrder, long multiplayerProfileVersion,
              MultiplayerEligibility multiplayerEligibility, int multiplayerMaxPlayers) {
            this.canonicalId = canonicalId;
            this.assetFilename = assetFilename;
            this.titleEn = titleEn;
            this.titleZhHans = titleZhHans;
            this.credit = credit;
            this.licenseFile = licenseFile;
            this.licenseSpdx = licenseSpdx;
            this.licenseSourceUrl = licenseSourceUrl;
            this.mapper = mapper;
            this.sortOrder = sortOrder;
            this.multiplayerProfileVersion = multiplayerProfileVersion;
            this.multiplayerEligibility = multiplayerEligibility;
            this.multiplayerMaxPlayers = multiplayerMaxPlayers;
        }

        /** Asset path of this game's ROM, relative to the assets root. */
        public String assetPath() {
            return ASSET_DIR + assetFilename;
        }

        /** URI the rest of the app uses for a bundled entry. */
        public String assetUri() {
            return "file:///android_asset/" + assetPath();
        }
    }

    private final List<Entry> entries;
    private final Map<String, Entry> byCanonicalId;
    private final long multiplayerProfileVersion;

    private BuiltinGames(List<Entry> entries, Map<String, Entry> byCanonicalId,
                         long multiplayerProfileVersion) {
        this.entries = Collections.unmodifiableList(entries);
        this.byCanonicalId = Collections.unmodifiableMap(byCanonicalId);
        this.multiplayerProfileVersion = multiplayerProfileVersion;
    }

    /** Every bundled game, ordered by the manifest's sortOrder. */
    public List<Entry> all() {
        return entries;
    }

    public long multiplayerProfileVersion() { return multiplayerProfileVersion; }

    /** No bundled games: used when the manifest cannot be read, never to hide one. */
    public static BuiltinGames empty() {
        return new BuiltinGames(new ArrayList<>(), new HashMap<>(), 0L);
    }

    /** The bundled game with this canonical id, or null (also for null/blank ids). */
    public Entry byCanonicalId(String canonicalId) {
        if (canonicalId == null || canonicalId.isEmpty()) return null;
        return byCanonicalId.get(canonicalId);
    }

    /** True when this canonical id names a bundled game. */
    public boolean isBundled(String canonicalId) {
        return byCanonicalId(canonicalId) != null;
    }

    /** The bundled game whose license text is this asset filename, or null. */
    public Entry byLicenseFile(String licenseFile) {
        if (licenseFile == null || licenseFile.isEmpty()) return null;
        for (Entry entry : entries) {
            if (licenseFile.equals(entry.licenseFile)) return entry;
        }
        return null;
    }

    /**
     * The bundled game with this ROM filename, or null. The native scan names a
     * bundled entry by its asset filename, so this is how a projected row is
     * matched back to the manifest.
     */
    public Entry byAssetFilename(String assetFilename) {
        if (assetFilename == null || assetFilename.isEmpty()) return null;
        String name = assetFilename;
        int slash = Math.max(name.lastIndexOf('/'), name.lastIndexOf('\\'));
        if (slash >= 0) name = name.substring(slash + 1);
        for (Entry entry : entries) {
            if (name.equals(entry.assetFilename)) return entry;
        }
        return null;
    }

    /** Reads the manifest from the merged Android assets. */
    public static BuiltinGames fromAssets(Context context) throws IOException {
        if (context == null) throw new NullPointerException("context");
        try (InputStream in = context.getAssets().open(ASSET_NAME)) {
            return parse(in);
        }
    }

    /**
     * Parses the manifest. Every failure is reported as an IOException naming the
     * manifest, so callers can surface one localized message instead of crashing.
     */
    public static BuiltinGames parse(InputStream in) throws IOException {
        if (in == null) throw new IOException(ASSET_NAME + " is missing");
        String text;
        try {
            text = readFully(in);
        } catch (IOException failure) {
            throw new IOException(ASSET_NAME + " could not be read: " + failure.getMessage(), failure);
        }

        JSONObject root;
        try {
            root = new JSONObject(text);
        } catch (JSONException failure) {
            throw new IOException(ASSET_NAME + " is not valid JSON: " + failure.getMessage(), failure);
        }

        int schemaVersion = root.optInt("schemaVersion", -1);
        if (schemaVersion != SUPPORTED_SCHEMA_VERSION) {
            throw new IOException(ASSET_NAME + " schemaVersion " + schemaVersion
                    + " is not supported (expected " + SUPPORTED_SCHEMA_VERSION + ")");
        }
        long multiplayerProfileVersion = root.optLong("multiplayerProfileVersion", -1L);
        if (multiplayerProfileVersion <= 0L) {
            throw new IOException(ASSET_NAME + " has no supported multiplayerProfileVersion");
        }

        JSONArray games = root.optJSONArray("games");
        if (games == null || games.length() == 0) {
            throw new IOException(ASSET_NAME + " bundles no games");
        }

        ArrayList<Entry> parsed = new ArrayList<>(games.length());
        HashMap<String, Entry> index = new HashMap<>(games.length());
        for (int position = 0; position < games.length(); position++) {
            JSONObject game = games.optJSONObject(position);
            if (game == null) {
                throw new IOException(ASSET_NAME + " game #" + position + " is not an object");
            }
            String canonicalId = required(game, "canonicalId", position);
            String assetFilename = required(game, "assetFilename", position);
            String titleEn = required(game, "titleEn", position);
            String titleZhHans = required(game, "titleZhHans", position);
            if (!canonicalId.startsWith("builtin:")) {
                throw new IOException(ASSET_NAME + " game " + canonicalId
                        + " must use a builtin: canonical id");
            }
            if (!assetFilename.endsWith(".nes") || assetFilename.contains("/")) {
                throw new IOException(ASSET_NAME + " game " + canonicalId
                        + " must declare a bare .nes assetFilename");
            }
            if (index.containsKey(canonicalId)) {
                throw new IOException(ASSET_NAME + " declares " + canonicalId + " more than once");
            }
            JSONObject license = game.optJSONObject("license");
            JSONObject multiplayer = game.optJSONObject("multiplayerProfile");
            if (multiplayer == null
                    || multiplayer.optLong("version", -1L) != multiplayerProfileVersion) {
                throw new IOException(ASSET_NAME + " game " + canonicalId
                        + " has a missing or version-mismatched multiplayerProfile");
            }
            MultiplayerEligibility eligibility;
            try {
                eligibility = MultiplayerEligibility.valueOf(
                        multiplayer.optString("eligibility", ""));
            } catch (IllegalArgumentException failure) {
                throw new IOException(ASSET_NAME + " game " + canonicalId
                        + " has invalid multiplayer eligibility", failure);
            }
            int maxPlayers = multiplayer.optInt("maxPlayers", 0);
            if (eligibility == MultiplayerEligibility.SUPPORTED && maxPlayers != 2) {
                throw new IOException(ASSET_NAME + " game " + canonicalId
                        + " must declare maxPlayers=2 when multiplayer is supported");
            }
            Entry entry = new Entry(
                    canonicalId,
                    assetFilename,
                    titleEn,
                    titleZhHans,
                    game.optString("credit", ""),
                    license == null ? "" : license.optString("file", ""),
                    license == null ? "" : license.optString("spdx", ""),
                    license == null ? "" : license.optString("sourceUrl", ""),
                    game.optInt("mapper", 0),
                    game.optInt("sortOrder", 0),
                    multiplayerProfileVersion,
                    eligibility,
                    maxPlayers);
            parsed.add(entry);
            index.put(canonicalId, entry);
        }

        parsed.sort(Comparator.comparingInt((Entry entry) -> entry.sortOrder));
        return new BuiltinGames(parsed, index, multiplayerProfileVersion);
    }

    private static String required(JSONObject game, String field, int position) throws IOException {
        String value = game.optString(field, "");
        if (value.isEmpty()) {
            throw new IOException(ASSET_NAME + " game #" + position + " is missing a non-empty " + field);
        }
        return value;
    }

    private static String readFully(InputStream in) throws IOException {
        ByteArrayOutputStream out = new ByteArrayOutputStream(8192);
        byte[] buffer = new byte[8192];
        int count;
        while ((count = in.read(buffer)) != -1) {
            if (count > 0) out.write(buffer, 0, count);
        }
        return new String(out.toByteArray(), StandardCharsets.UTF_8);
    }
}
