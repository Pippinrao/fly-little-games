package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.NativeFrameSource;
import com.flynes.emu.video.PublishedFrame;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;

@RunWith(AndroidJUnit4.class)
public final class NesFrameSnapshotTest {
    @Test
    public void copiesACompleteNativeFrameAfterOneEmulatedFrame() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        NesCore core = new NesCore();
        assertTrue(core.create());
        try {
            assertTrue(core.loadDatabase(readAsset(context, "NstDatabase.xml")) >= 0);
            assertTrue(core.loadRom(readAsset(context, "roms/thwaite.nes")) >= 0);
            assertTrue(core.runOneFrame() > 0);

            NativeFrameSource source = new NativeFrameSource(core, 4 * 1024 * 1024);
            PublishedFrame frame = source.copyLatest();
            assertNotNull(frame);
            assertTrue(frame.sequence() > 0);
            assertEquals(256, frame.width());
            assertEquals(240, frame.height());
            assertEquals(PublishedFrame.Format.RGB565, frame.format());
        } finally {
            core.destroy();
        }
    }

    private static byte[] readAsset(Context context, String path) throws Exception {
        try (InputStream input = context.getAssets().open(path);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) >= 0) output.write(buffer, 0, read);
            return output.toByteArray();
        }
    }
}
