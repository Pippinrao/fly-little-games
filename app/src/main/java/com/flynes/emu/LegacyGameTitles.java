package com.flynes.emu;

import android.content.ContentResolver;
import android.net.Uri;
import com.flynes.emu.app.FlyNesApp;
import com.flynes.emu.app.NativeGameTitle;
import com.flynes.emu.catalog.HexEncoding;
import java.io.InputStream;
import java.security.MessageDigest;
import java.util.List;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/** Background migration for the legacy library, whose old records have no content hash. */
final class LegacyGameTitles {
    private LegacyGameTitles() { }

    static void hydrate(ContentResolver resolver, GameEntry game) {
        if ("assets".equals(game.source)) {
            // A bundled game's bilingual titles come from the shared manifest, so
            // this migration never invents one; its metadata stays unknown here.
            return;
        }
        byte[] hash = parseHash(game.payloadSha256);
        if (hash == null) {
            try (InputStream raw = resolver.openInputStream(Uri.parse(game.uri))) {
                if (raw != null) {
                    byte[] bytes = null;
                    if (game.zipped) {
                        try (ZipInputStream zip = new ZipInputStream(raw)) {
                            ZipEntry entry;
                            while ((entry = zip.getNextEntry()) != null) {
                                if (!entry.isDirectory() && entry.getName().toLowerCase(Locale.ROOT)
                                        .endsWith(".nes")) {
                                    game.romName = entry.getName();
                                    bytes = RomScanner.readFully(zip, RomScanner.MAX_ROM_BYTES);
                                    break;
                                }
                            }
                        }
                    } else {
                        game.romName = game.name;
                        bytes = RomScanner.readFully(raw, RomScanner.MAX_ROM_BYTES);
                    }
                    if (bytes != null) {
                        hash = MessageDigest.getInstance("SHA-256").digest(bytes);
                        game.payloadSha256 = HexEncoding.lower(hash);
                    }
                }
            } catch (Exception unavailable) {
                // Permission loss/corrupt archive keeps its original display and launch behavior.
            }
        }
        game.titleMetadata = FlyNesApp.resolveGameTitle(hash,
                game.zipped ? game.romName : game.name);
    }

    static byte[] parseHash(String value) {
        if (value == null || value.length() != 64) return null;
        byte[] result = new byte[32];
        for (int i = 0; i < result.length; ++i) {
            int high = Character.digit(value.charAt(i * 2), 16);
            int low = Character.digit(value.charAt(i * 2 + 1), 16);
            if (high < 0 || low < 0) return null;
            result[i] = (byte) ((high << 4) | low);
        }
        return result;
    }

    static NativeGameTitle forLoadedRom(GameEntry game, byte[] bytes) {
        if (bytes == null || "assets".equals(game.source)) return game.titleMetadata;
        try {
            var identified = FlyNesApp.resolveGameTitle(
                    MessageDigest.getInstance("SHA-256").digest(bytes),
                    game.zipped ? game.romName : game.name);
            return identified;
        } catch (java.security.NoSuchAlgorithmException impossible) {
            throw new AssertionError(impossible);
        }
    }
}
