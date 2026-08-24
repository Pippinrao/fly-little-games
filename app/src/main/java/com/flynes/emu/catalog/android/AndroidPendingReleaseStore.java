package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.SharedPreferences;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.source.PendingReleaseStore;

import java.io.IOException;
import java.util.Map;
import java.util.TreeMap;

/** Synchronous durable SAF grant-release tombstones. */
public final class AndroidPendingReleaseStore implements PendingReleaseStore {
    static final String PREFERENCES = "catalog_pending_releases";
    private final SharedPreferences preferences;

    public AndroidPendingReleaseStore(Context context) {
        if (context == null) throw new NullPointerException("context");
        preferences = context.getApplicationContext().getSharedPreferences(
                PREFERENCES, Context.MODE_PRIVATE);
    }

    @Override public Map<String, String> readAll() throws IOException {
        TreeMap<String, String> result = new TreeMap<>();
        for (Map.Entry<String, ?> item : preferences.getAll().entrySet()) {
            if (!(item.getValue() instanceof String)) {
                throw new IOException("invalid pending release record");
            }
            try {
                result.put(
                        DomainValidation.requireNonBlank(item.getKey(), "pending source id"),
                        DomainValidation.requireNonBlank(
                                (String) item.getValue(), "pending locator"));
            } catch (RuntimeException invalid) {
                throw new IOException("invalid pending release record", invalid);
            }
        }
        return result;
    }

    @Override public void put(String sourceId, String locator) throws IOException {
        String checkedSource = DomainValidation.requireNonBlank(sourceId, "pending source id");
        String checkedLocator = DomainValidation.requireNonBlank(locator, "pending locator");
        if (!preferences.edit().putString(checkedSource, checkedLocator).commit()) {
            throw new IOException("pending release write failed");
        }
    }

    @Override public void remove(String sourceId) throws IOException {
        String checkedSource = DomainValidation.requireNonBlank(sourceId, "pending source id");
        if (!preferences.edit().remove(checkedSource).commit()) {
            throw new IOException("pending release clear failed");
        }
    }
}
