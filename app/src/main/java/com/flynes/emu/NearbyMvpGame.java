package com.flynes.emu;

import android.content.Context;

import com.flynes.emu.catalog.BuiltinGames;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/** Resolves the first manifest-declared two-player game without naming content in product code. */
final class NearbyMvpGame {
    static final class Selection {
        final BuiltinGames.Entry entry;
        final byte[] rom;
        Selection(BuiltinGames.Entry entry, byte[] rom) { this.entry = entry; this.rom = rom; }
    }

    static Selection load(Context context) throws IOException {
        BuiltinGames games = BuiltinGames.fromAssets(context);
        for (BuiltinGames.Entry entry : games.all()) {
            if (entry.multiplayerEligibility != BuiltinGames.MultiplayerEligibility.SUPPORTED ||
                    entry.multiplayerMaxPlayers != 2) continue;
            try (InputStream input = context.getAssets().open(entry.assetPath());
                 ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                byte[] buffer = new byte[8192];
                int count;
                while ((count = input.read(buffer)) != -1) {
                    if (count > 0) output.write(buffer, 0, count);
                    if (output.size() > 4 * 1024 * 1024) throw new IOException("MVP ROM is too large");
                }
                return new Selection(entry, output.toByteArray());
            }
        }
        throw new IOException("No two-player game is declared by the content manifest");
    }

    private NearbyMvpGame() {}
}
