package com.flynes.emu.save;

import android.content.Context;
import android.content.SharedPreferences;

import com.flynes.emu.data.RomIdentity;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;

public final class LegacySaveMigrator {
    public enum Status { NO_FILE, MIGRATED, ALREADY_DONE, NEEDS_MANUAL_RECOVERY, FAILED }

    private static final String PREFS = "save_migration";
    private static final String KEY_STATUS = "legacy_autosave_migration";
    private static final String SUCCESS = "success";

    private LegacySaveMigrator() { }

    public static Status migrate(Context context, SaveRepository repository) {
        return migrate(new File(context.getFilesDir(), "autosave.nst"),
                context.getSharedPreferences(PREFS, Context.MODE_PRIVATE), repository);
    }

    public static Status migrate(File legacy, SharedPreferences marker,
                                 SaveRepository repository) {
        if (SUCCESS.equals(marker.getString(KEY_STATUS, null))) return Status.ALREADY_DONE;
        if (!legacy.isFile()) return Status.NO_FILE;
        try {
            byte[] state = readAll(legacy);
            RomIdentity identity;
            try {
                identity = StateHeaderReader.identity(state);
            } catch (IllegalArgumentException invalid) {
                marker.edit().putString(KEY_STATUS, "needs_manual_recovery").apply();
                return Status.NEEDS_MANUAL_RECOVERY;
            }
            repository.writeAutosave(identity, state,
                    legacy.lastModified() > 0L ? legacy.lastModified() : System.currentTimeMillis());
            marker.edit().putString(KEY_STATUS, SUCCESS).commit();
            return Status.MIGRATED;
        } catch (IOException error) {
            return Status.FAILED;
        }
    }

    private static byte[] readAll(File file) throws IOException {
        try (InputStream input = new FileInputStream(file);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) output.write(buffer, 0, read);
            return output.toByteArray();
        }
    }
}
