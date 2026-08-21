package com.flynes.emu.save;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertThrows;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.data.RomIdentity;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.IOException;

@RunWith(AndroidJUnit4.class)
public final class SaveRepositoryTest {
    private static final RomIdentity ID_A =
            new RomIdentity("1111111111111111111111111111111111111111");
    private static final RomIdentity ID_B =
            new RomIdentity("2222222222222222222222222222222222222222");

    @Test
    public void savesAreIsolatedBySha1() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File tempDir = new File(context.getCacheDir(), "save-isolation-" + System.nanoTime());
        SaveRepository repo = SaveRepository.forTest(tempDir);
        repo.writeAutosave(ID_A, new byte[]{1, 2, 3}, 100L);
        repo.writeAutosave(ID_B, new byte[]{9, 8}, 200L);
        assertArrayEquals(new byte[]{1, 2, 3}, repo.readAutosave(ID_A).orElseThrow().state());
        assertArrayEquals(new byte[]{9, 8}, repo.readAutosave(ID_B).orElseThrow().state());
    }

    @Test
    public void failedWritePreservesPreviousState() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File tempDir = new File(context.getCacheDir(), "save-rollback-" + System.nanoTime());
        SaveRepository repo = SaveRepository.forTest(tempDir);
        repo.writeAutosave(ID_A, new byte[]{1, 2, 3}, 100L);
        repo.setFaultInjector(bytes -> { throw new IOException("disk full"); });
        assertThrows(IOException.class,
                () -> repo.writeAutosave(ID_A, new byte[]{4}, 200L));
        assertArrayEquals(new byte[]{1, 2, 3}, repo.readAutosave(ID_A).orElseThrow().state());
    }
}
