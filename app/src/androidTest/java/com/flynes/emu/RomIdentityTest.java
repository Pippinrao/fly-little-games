package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.BuiltinGames;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.InputStream;
import java.util.Locale;

@RunWith(AndroidJUnit4.class)
public final class RomIdentityTest {
    /**
     * The core identifies a cartridge by the SHA-1 of the payload behind the
     * 16-byte iNES header, so every bundled ROM is checked against its own entry
     * in the shared manifest. Hardcoding one title here is what let a retired
     * cartridge hash survive into the seven-game bundle unnoticed.
     */
    @Test
    public void everyBundledRomMatchesItsManifestPayloadIdentity() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        BuiltinGames games;
        try (InputStream manifest = context.getAssets().open(BuiltinGames.ASSET_NAME)) {
            games = BuiltinGames.parse(manifest);
        }
        assertTrue("the bundle must ship at least one game", games.all().size() > 0);
        for (BuiltinGames.Entry game : games.all()) {
            byte[] rom;
            try (InputStream in = context.getAssets().open(game.assetPath())) {
                rom = in.readAllBytes();
            }
            NesCore core = new NesCore();
            assertTrue("core failed to start for " + game.canonicalId, core.create());
            try {
                assertTrue("core refused " + game.assetFilename, core.loadRom(rom, null) >= 0);
                String identity = core.romInfo().identity().sha1();
                assertEquals("core identity for " + game.assetFilename, payloadSha1(rom), identity);
                assertNotNull(identity);
            } finally {
                core.destroy();
            }
        }
    }

    /** SHA-1 over the ROM payload, which is the file minus its 16-byte header. */
    private static String payloadSha1(byte[] rom) throws Exception {
        byte[] payload = new byte[rom.length - 16];
        System.arraycopy(rom, 16, payload, 0, payload.length);
        byte[] digest = java.security.MessageDigest.getInstance("SHA-1").digest(payload);
        StringBuilder hex = new StringBuilder(digest.length * 2);
        for (byte item : digest) hex.append(String.format(Locale.ROOT, "%02X", item & 0xff));
        return hex.toString();
    }
}
