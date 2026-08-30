package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

@RunWith(AndroidJUnit4.class)
public final class AndroidAtomicCatalogStateStoreTest {
    @Test
    public void roundTripsAndOversizeFailurePreservesLastGood() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File base = new File(context.getCacheDir(), "catalog-atomic-test.bin");
        delete(base); delete(new File(base.getPath() + ".bak"));
        AndroidAtomicCatalogStateStore store = new AndroidAtomicCatalogStateStore(base);
        assertNull(store.read());
        byte[] good = new byte[]{1, 2, 3, 4};
        store.writeAtomically(good);
        assertArrayEquals(good, store.read());

        assertThrows(IOException.class, () -> store.writeAtomically(
                new byte[AndroidAtomicCatalogStateStore.MAX_BYTES + 1]));
        assertArrayEquals(good, store.read());
        delete(base); delete(new File(base.getPath() + ".bak"));
    }

    @Test
    public void recoversAtomicBackupWithoutOverwritingIt() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File base = new File(context.getCacheDir(), "catalog-backup-test.bin");
        File backup = new File(base.getPath() + ".bak");
        delete(base); delete(backup);
        byte[] good = new byte[]{9, 8, 7};
        try (FileOutputStream output = new FileOutputStream(backup)) { output.write(good); }

        AndroidAtomicCatalogStateStore store = new AndroidAtomicCatalogStateStore(base);
        assertArrayEquals(good, store.read());
        assertArrayEquals(good, store.read());
        delete(base); delete(backup);
    }

    private static void delete(File file) {
        if (file.exists() && !file.delete()) throw new AssertionError("test cleanup failed");
    }
}
