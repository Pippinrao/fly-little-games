package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.source.PendingRelease;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidPendingReleaseStoreTest {
    @Test
    public void tombstoneSurvivesNewInstanceUntilExplicitClear() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("catalog_pending_releases", Context.MODE_PRIVATE)
                .edit().clear().commit();
        AndroidPendingReleaseStore first = new AndroidPendingReleaseStore(context);
        PendingRelease pending = PendingRelease.orphanGrant(
                "source", "content://provider/tree/private");
        first.put(pending);

        AndroidPendingReleaseStore restarted = new AndroidPendingReleaseStore(context);
        assertEquals(pending, restarted.readAll().get(pending.actionId()));
        restarted.remove(pending.actionId());
        assertTrue(new AndroidPendingReleaseStore(context).readAll().isEmpty());
    }

    @Test
    public void legacyRemovalTombstoneCanBeClearedByTypedActionId() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences(AndroidPendingReleaseStore.PREFERENCES, Context.MODE_PRIVATE)
                .edit().clear().putString(
                        "legacy-source", "content://provider/tree/legacy").commit();
        AndroidPendingReleaseStore store = new AndroidPendingReleaseStore(context);
        PendingRelease migrated = store.readAll().values().iterator().next();
        assertEquals(PendingRelease.Intent.REMOVE_SOURCE, migrated.intent());

        store.remove(migrated.actionId());

        assertTrue(store.readAll().isEmpty());
    }
}
