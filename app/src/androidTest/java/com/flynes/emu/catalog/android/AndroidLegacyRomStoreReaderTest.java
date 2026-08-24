package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.migration.LegacyLibraryMigrator;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidLegacyRomStoreReaderTest {
    @Test
    public void readsWithoutClearingLegacyKeysAndMarkerIsSeparate() {
        Context context = ApplicationProvider.getApplicationContext();
        SharedPreferences legacy = context.getSharedPreferences(
                AndroidLegacyRomStoreReader.PREFERENCES, Context.MODE_PRIVATE);
        legacy.edit().clear().commit();
        legacy.edit()
                .putString(AndroidLegacyRomStoreReader.TREE_URI, "content://tree/root")
                .putString(AndroidLegacyRomStoreReader.GAMES,
                        "[{\"name\":\"Old\",\"uri\":\"content://doc/raw\","
                                + "\"source\":\"saf\",\"zipped\":false}]")
                .commit();
        LegacyLibraryMigrator.LegacySnapshot snapshot =
                new AndroidLegacyRomStoreReader(context).read();
        assertFalse(snapshot.malformed());
        assertEquals(1, snapshot.rows().size());

        AndroidLegacyMigrationMarker marker = new AndroidLegacyMigrationMarker(context);
        marker.markComplete();
        assertTrue(marker.isComplete());
        assertTrue(legacy.contains(AndroidLegacyRomStoreReader.TREE_URI));
        assertTrue(legacy.contains(AndroidLegacyRomStoreReader.GAMES));
        legacy.edit().clear().commit();
    }
}
