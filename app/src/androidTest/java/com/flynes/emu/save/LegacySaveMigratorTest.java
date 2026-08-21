package com.flynes.emu.save;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;

import android.content.Context;
import android.content.SharedPreferences;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.data.RomIdentity;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.charset.StandardCharsets;

@RunWith(AndroidJUnit4.class)
public final class LegacySaveMigratorTest {
    private static final String SHA1 = "77C42676DB38D384C1D6B00090ADBC820BF70AB0";

    @Test
    public void validLegacyStateIsCopiedOnceAndOriginalIsRetained() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File root = new File(context.getCacheDir(), "migration-" + System.nanoTime());
        File legacy = new File(root, "autosave.nst");
        root.mkdirs();
        byte[] state = validEmptyState();
        try (FileOutputStream output = new FileOutputStream(legacy)) { output.write(state); }
        SaveRepository repository = SaveRepository.forTest(new File(root, "saves"));
        SharedPreferences marker = context.getSharedPreferences(
                "migration-test-" + System.nanoTime(), 0);

        assertEquals(LegacySaveMigrator.Status.MIGRATED,
                LegacySaveMigrator.migrate(legacy, marker, repository));
        assertEquals(LegacySaveMigrator.Status.ALREADY_DONE,
                LegacySaveMigrator.migrate(legacy, marker, repository));
        assertArrayEquals(state, repository.readAutosave(new RomIdentity(SHA1))
                .orElseThrow().state());
        assertEquals(true, legacy.exists());
    }

    private static byte[] validEmptyState() {
        byte[] state = new byte[81];
        byte[] magic = "FLYNST1\0".getBytes(StandardCharsets.UTF_8);
        System.arraycopy(magic, 0, state, 0, 8);
        state[8] = 1;
        byte[] sha = SHA1.getBytes(StandardCharsets.US_ASCII);
        System.arraycopy(sha, 0, state, 28, sha.length);
        return state;
    }
}
