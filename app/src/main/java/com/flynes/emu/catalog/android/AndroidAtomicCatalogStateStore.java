package com.flynes.emu.catalog.android;

import android.util.AtomicFile;

import com.flynes.emu.catalog.persistence.CatalogStateStore;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/** Bounded AtomicFile-backed state store. Reads never repair or overwrite catalog contents. */
public final class AndroidAtomicCatalogStateStore implements CatalogStateStore {
    public static final int MAX_BYTES = 16 * 1024 * 1024;

    private final AtomicFile file;

    public AndroidAtomicCatalogStateStore(File baseFile) {
        if (baseFile == null) throw new NullPointerException("baseFile");
        file = new AtomicFile(baseFile);
    }

    @Override
    public byte[] read() throws IOException {
        File backup = backupFile();
        File readable = backup.exists() ? backup : file.getBaseFile();
        if (!readable.exists()) return null;
        if (readable.length() > MAX_BYTES) throw new IOException("catalog state exceeds size bound");
        // Read the backup directly so a recovery inspection never mutates or repairs storage.
        try (FileInputStream input = new FileInputStream(readable)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int total = 0;
            while (true) {
                int count = input.read(buffer);
                if (count < 0) break;
                if (count == 0) continue;
                total = Math.addExact(total, count);
                if (total > MAX_BYTES) throw new IOException("catalog state exceeds size bound");
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        } catch (ArithmeticException oversized) {
            throw new IOException("catalog state exceeds size bound", oversized);
        }
    }

    @Override
    public void writeAtomically(byte[] encoded) throws IOException {
        if (encoded == null) throw new NullPointerException("encoded");
        if (encoded.length > MAX_BYTES) throw new IOException("catalog state exceeds size bound");
        FileOutputStream output = null;
        try {
            output = file.startWrite();
            output.write(encoded);
            output.flush();
            output.getFD().sync();
            file.finishWrite(output);
        } catch (IOException | RuntimeException failure) {
            if (output != null) file.failWrite(output);
            if (failure instanceof IOException) throw (IOException) failure;
            throw new IOException("atomic catalog write failed", failure);
        }
    }

    public File baseFile() {
        return file.getBaseFile();
    }

    private File backupFile() {
        return new File(file.getBaseFile().getPath() + ".bak");
    }
}
