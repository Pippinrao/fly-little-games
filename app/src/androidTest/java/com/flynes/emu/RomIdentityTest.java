package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.InputStream;

@RunWith(AndroidJUnit4.class)
public final class RomIdentityTest {
    @Test
    public void builtinRomUsesCoreSha1() throws Exception {
        NesCore core = new NesCore();
        assertTrue(core.create());
        Context context = ApplicationProvider.getApplicationContext();
        byte[] rom;
        try (InputStream in = context.getAssets().open("roms/from_below.nes")) {
            rom = in.readAllBytes();
        }
        assertTrue(core.loadRom(rom, null) >= 0);
        assertEquals("77C42676DB38D384C1D6B00090ADBC820BF70AB0",
                core.romInfo().identity().sha1());
        core.destroy();
    }
}
