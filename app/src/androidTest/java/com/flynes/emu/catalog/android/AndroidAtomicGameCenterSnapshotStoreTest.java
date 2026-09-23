package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertThrows;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.gamecenter.GameCenterSnapshotCodec;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;

@RunWith(AndroidJUnit4.class)
public final class AndroidAtomicGameCenterSnapshotStoreTest {
    @Test public void roundTripsAndFailedWritePreservesLastGood() throws Exception {
        File base = isolated("round-trip");
        deleteFamily(base);
        AndroidAtomicGameCenterSnapshotStore store =
                new AndroidAtomicGameCenterSnapshotStore(base);
        assertNull(store.read());
        byte[] good = new byte[]{1, 2, 3, 4};
        store.writeAtomically(good);
        assertArrayEquals(good, store.read());

        assertThrows(IOException.class, () -> store.writeAtomically(
                new byte[GameCenterSnapshotCodec.MAX_BYTES + 1]));

        assertArrayEquals(good, store.read());
        deleteFamily(base);
    }

    @Test public void interruptedNewFileDoesNotReplaceBase() throws Exception {
        File base = isolated("interrupted");
        deleteFamily(base);
        AndroidAtomicGameCenterSnapshotStore store =
                new AndroidAtomicGameCenterSnapshotStore(base);
        byte[] good = new byte[]{7, 8, 9};
        store.writeAtomically(good);
        try (FileOutputStream output = new FileOutputStream(new File(base.getPath() + ".new"))) {
            output.write(new byte[]{99});
        }

        assertArrayEquals(good, store.read());
        deleteFamily(base);
    }

    @Test public void backupIsReadWithoutBeingRepairedOrOverwritten() throws Exception {
        File base = isolated("backup");
        deleteFamily(base);
        File backup = new File(base.getPath() + ".bak");
        byte[] good = new byte[]{4, 5, 6};
        try (FileOutputStream output = new FileOutputStream(backup)) {
            output.write(good);
        }
        AndroidAtomicGameCenterSnapshotStore store =
                new AndroidAtomicGameCenterSnapshotStore(base);

        assertArrayEquals(good, store.read());
        assertArrayEquals(good, java.nio.file.Files.readAllBytes(backup.toPath()));
        deleteFamily(base);
    }

    private static File isolated(String name) {
        Context context = ApplicationProvider.getApplicationContext();
        return new File(context.getCacheDir(), "game-center-snapshot-" + name + ".bin");
    }

    private static void deleteFamily(File base) {
        delete(base);
        delete(new File(base.getPath() + ".bak"));
        delete(new File(base.getPath() + ".new"));
    }

    private static void delete(File file) {
        if (file.exists() && !file.delete()) throw new AssertionError("test cleanup failed");
    }
}
