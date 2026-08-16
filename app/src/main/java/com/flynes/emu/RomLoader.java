package com.flynes.emu;

import android.content.Context;
import android.net.Uri;
import android.util.Log;

import java.io.IOException;
import java.io.InputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Loads the raw bytes of a {@link GameEntry}: the bundled asset, a plain SAF
 * document, or the first .nes inside a .zip. The zip is re-opened on every
 * load instead of caching an extracted copy — ROMs are a few hundred KiB at
 * most, which keeps this class (and the app) simple.
 */
public class RomLoader {

    private static final String TAG = "FlyNES";
    private static final String ASSET_ROM_PATH = "roms/from_below.nes";

    private RomLoader() {
    }

    /** Returns the ROM bytes, or null on any failure. Never throws. */
    public static byte[] load(Context ctx, GameEntry g) {
        if (ctx == null || g == null || g.uri == null) return null;
        try {
            if ("assets".equals(g.source)) {
                try (InputStream in = ctx.getAssets().open(ASSET_ROM_PATH)) {
                    return RomScanner.readFully(in, RomScanner.MAX_ROM_BYTES);
                }
            }
            Uri uri = Uri.parse(g.uri);
            if (g.zipped) {
                return readFirstNesFromZip(ctx, uri);
            }
            return RomScanner.readFully(ctx.getContentResolver(), uri, RomScanner.MAX_ROM_BYTES);
        } catch (Exception e) {
            Log.e(TAG, "RomLoader.load failed: " + g.uri, e);
            return null;
        }
    }

    /** Unzips and returns the first entry whose name ends with ".nes". */
    private static byte[] readFirstNesFromZip(Context ctx, Uri uri) throws IOException {
        InputStream raw = ctx.getContentResolver().openInputStream(uri);
        if (raw == null) return null;
        try (ZipInputStream zip = new ZipInputStream(raw)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String entryName = entry.getName();
                if (entryName != null && entryName.toLowerCase().endsWith(".nes")) {
                    return RomScanner.readFully(zip, RomScanner.MAX_ROM_BYTES);
                }
            }
        }
        return null;
    }
}
