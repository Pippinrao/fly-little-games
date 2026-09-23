package com.flynes.emu.catalog.android;

import android.util.AtomicFile;

import com.flynes.emu.gamecenter.GameCenterSnapshotCodec;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;

/** Atomic last-known-good store for the rebuildable Game Center projection. */
public final class AndroidAtomicGameCenterSnapshotStore {
    private final AtomicFile file;

    public AndroidAtomicGameCenterSnapshotStore(File baseFile) {
        if (baseFile == null) throw new NullPointerException("base file");
        file = new AtomicFile(baseFile);
    }

    public byte[] read() throws IOException {
        File backup = new File(file.getBaseFile().getPath() + ".bak");
        File readable = backup.exists() ? backup : file.getBaseFile();
        if (!readable.exists()) return null;
        if (readable.length() > GameCenterSnapshotCodec.MAX_BYTES) {
            throw new IOException("game center snapshot exceeds size bound");
        }
        try (FileInputStream input = new FileInputStream(readable)) {
            ByteArrayOutputStream output = new ByteArrayOutputStream();
            byte[] buffer = new byte[8192];
            int total = 0;
            while (true) {
                int count = input.read(buffer);
                if (count < 0) break;
                if (count == 0) continue;
                total = Math.addExact(total, count);
                if (total > GameCenterSnapshotCodec.MAX_BYTES) {
                    throw new IOException("game center snapshot exceeds size bound");
                }
                output.write(buffer, 0, count);
            }
            return output.toByteArray();
        } catch (ArithmeticException oversized) {
            throw new IOException("game center snapshot exceeds size bound", oversized);
        }
    }

    public void writeAtomically(byte[] encoded) throws IOException {
        if (encoded == null) throw new NullPointerException("encoded snapshot");
        if (encoded.length > GameCenterSnapshotCodec.MAX_BYTES) {
            throw new IOException("game center snapshot exceeds size bound");
        }
        File parent = file.getBaseFile().getParentFile();
        if (parent != null && !parent.isDirectory() && !parent.mkdirs() && !parent.isDirectory()) {
            throw new IOException("game center snapshot directory unavailable");
        }
        FileOutputStream output = null;
        try {
            output = file.startWrite();
            output.write(encoded);
            output.flush();
            output.getFD().sync();
            file.finishWrite(output);
        } catch (IOException | RuntimeException failure) {
            if (output != null) file.failWrite(output);
            if (failure instanceof IOException io) throw io;
            throw new IOException("atomic game center snapshot write failed", failure);
        }
    }

    public File baseFile() {
        return file.getBaseFile();
    }
}
