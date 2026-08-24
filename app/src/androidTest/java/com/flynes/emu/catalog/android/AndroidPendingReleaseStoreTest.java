package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

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
        first.put("source", "content://provider/tree/private");

        AndroidPendingReleaseStore restarted = new AndroidPendingReleaseStore(context);
        assertEquals("content://provider/tree/private", restarted.readAll().get("source"));
        restarted.remove("source");
        assertTrue(new AndroidPendingReleaseStore(context).readAll().isEmpty());
    }
}
