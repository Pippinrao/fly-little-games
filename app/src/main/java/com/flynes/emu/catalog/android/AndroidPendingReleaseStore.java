package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.SharedPreferences;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.source.PendingRelease;
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

    @Override public Map<String, PendingRelease> readAll() throws IOException {
        TreeMap<String, PendingRelease> result = new TreeMap<>();
        TreeMap<String, ?> stored = new TreeMap<>(preferences.getAll());
        for (Map.Entry<String, ?> item : stored.entrySet()) {
            if (!(item.getValue() instanceof String)) {
                throw new IOException("invalid pending release record");
            }
            try {
                String storedKey = DomainValidation.requireNonBlank(
                        item.getKey(), "pending action id");
                String locator = DomainValidation.requireNonBlank(
                        (String) item.getValue(), "pending locator");
                PendingRelease pending;
                if (storedKey.startsWith("orphan:")) {
                    pending = PendingRelease.orphanGrant(
                            storedKey.substring("orphan:".length()), locator);
                } else if (storedKey.startsWith("remove:")) {
                    pending = PendingRelease.removeSource(
                            storedKey.substring("remove:".length()), locator);
                } else {
                    // Compatibility with removal tombstones written by the first store version.
                    pending = PendingRelease.removeSource(storedKey, locator);
                }
                PendingRelease previous = result.put(pending.actionId(), pending);
                if (previous != null && !previous.equals(pending)) {
                    throw new IOException("conflicting pending release record");
                }
            } catch (RuntimeException invalid) {
                throw new IOException("invalid pending release record", invalid);
            }
        }
        return result;
    }

    @Override public void put(PendingRelease pending) throws IOException {
        PendingRelease checked = DomainValidation.requireNonNull(
                pending, "pending release");
        SharedPreferences.Editor edit = preferences.edit()
                .putString(checked.actionId(), checked.locator());
        if (checked.intent() == PendingRelease.Intent.REMOVE_SOURCE) {
            edit.remove(checked.sourceId());
        }
        if (!edit.commit()) {
            throw new IOException("pending release write failed");
        }
    }

    @Override public void remove(String actionId) throws IOException {
        String checkedAction = DomainValidation.requireNonBlank(
                actionId, "pending action id");
        SharedPreferences.Editor edit = preferences.edit().remove(checkedAction);
        if (checkedAction.startsWith("remove:")) {
            edit.remove(checkedAction.substring("remove:".length()));
        }
        if (!edit.commit()) {
            throw new IOException("pending release clear failed");
        }
    }
}
