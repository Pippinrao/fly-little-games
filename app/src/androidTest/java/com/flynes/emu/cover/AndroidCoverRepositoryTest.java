package com.flynes.emu.cover;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.graphics.Bitmap;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.PublishedFrame;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.nio.ByteBuffer;

@RunWith(AndroidJUnit4.class)
public final class AndroidCoverRepositoryTest {
    @Test public void storesAtomicHashedFourByThreePngAndLoadsIt() {
        Context context = ApplicationProvider.getApplicationContext();
        AndroidCoverRepository repository = new AndroidCoverRepository(context);
        String id = "game/with private locator characters";
        repository.removeForTest(id);

        repository.store(CoverFrame.copyOf(id, colorfulFrame()));
        Bitmap loaded = repository.load(id);

        assertNotNull(loaded);
        assertEquals(320, loaded.getWidth());
        assertEquals(240, loaded.getHeight());
        assertTrue(repository.fileForTest(id).isFile());
        assertFalse(repository.fileForTest(id).getName().contains("game"));
    }

    private static PublishedFrame colorfulFrame() {
        int width = 16, height = 16;
        ByteBuffer pixels = ByteBuffer.allocateDirect(width * height * 2);
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int value = ((x + y) & 1) == 0 ? 0xf800 : 0x07e0;
                pixels.put((byte) value).put((byte) (value >>> 8));
            }
        }
        pixels.position(0);
        return new PublishedFrame(1, width, height, width * 2,
                PublishedFrame.Format.RGB565, pixels, true);
    }
}
