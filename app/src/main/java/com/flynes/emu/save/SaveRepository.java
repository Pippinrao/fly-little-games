package com.flynes.emu.save;

import android.content.Context;
import android.util.AtomicFile;

import com.flynes.emu.data.RomIdentity;

import java.io.ByteArrayOutputStream;
import java.io.DataInputStream;
import java.io.DataOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.Arrays;
import java.util.Objects;
import java.util.Optional;

public final class SaveRepository implements SaveStore {
    @FunctionalInterface public interface FaultInjector {
        void beforeWrite(byte[] bytes) throws IOException;
    }

    private static final byte[] EMPTY = new byte[0];
    private final File root;
    private FaultInjector faultInjector = bytes -> { };

    public SaveRepository(Context context) {
        this(new File(context.getFilesDir(), "saves"));
    }

    private SaveRepository(File root) {
        this.root = Objects.requireNonNull(root, "root");
    }

    public static SaveRepository forTest(File root) {
        return new SaveRepository(root);
    }

    public void setFaultInjector(FaultInjector injector) {
        faultInjector = Objects.requireNonNull(injector, "injector");
    }

    @Override public void writeAutosave(RomIdentity id, byte[] state, long savedAt)
            throws IOException {
        File directory = directory(id);
        ensureDirectory(directory);
        writeAtomic(new File(directory, "autosave.nst"), state);
        writeTimestamp(new File(directory, "metadata.bin"), savedAt);
    }

    @Override public Optional<SaveRecord> readAutosave(RomIdentity id) throws IOException {
        File directory = directory(id);
        File state = new File(directory, "autosave.nst");
        if (!state.exists()) return Optional.empty();
        long savedAt = readTimestamp(new File(directory, "metadata.bin"));
        return Optional.of(new SaveRecord(readAtomic(state), savedAt));
    }

    @Override public void writeBattery(RomIdentity id, byte[] battery) throws IOException {
        File directory = directory(id);
        ensureDirectory(directory);
        writeAtomic(new File(directory, "battery.sav"), battery);
    }

    @Override public byte[] readBattery(RomIdentity id) throws IOException {
        File battery = new File(directory(id), "battery.sav");
        return battery.exists() ? readAtomic(battery) : EMPTY;
    }

    private File directory(RomIdentity id) {
        return new File(root, Objects.requireNonNull(id, "id").directoryName());
    }

    private static void ensureDirectory(File directory) throws IOException {
        if (!directory.isDirectory() && !directory.mkdirs() && !directory.isDirectory()) {
            throw new IOException("Could not create save directory: " + directory);
        }
    }

    private void writeAtomic(File target, byte[] bytes) throws IOException {
        byte[] owned = Arrays.copyOf(Objects.requireNonNull(bytes, "bytes"), bytes.length);
        AtomicFile atomic = new AtomicFile(target);
        FileOutputStream output = null;
        try {
            output = atomic.startWrite();
            faultInjector.beforeWrite(owned);
            output.write(owned);
            output.getFD().sync();
            atomic.finishWrite(output);
        } catch (IOException | RuntimeException error) {
            if (output != null) atomic.failWrite(output);
            throw error;
        }
    }

    private static byte[] readAtomic(File target) throws IOException {
        AtomicFile atomic = new AtomicFile(target);
        try (InputStream input = atomic.openRead();
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            byte[] buffer = new byte[8192];
            int read;
            while ((read = input.read(buffer)) != -1) output.write(buffer, 0, read);
            return output.toByteArray();
        }
    }

    private static void writeTimestamp(File target, long savedAt) throws IOException {
        AtomicFile atomic = new AtomicFile(target);
        FileOutputStream raw = null;
        try {
            raw = atomic.startWrite();
            DataOutputStream output = new DataOutputStream(raw);
            output.writeLong(savedAt);
            output.flush();
            raw.getFD().sync();
            atomic.finishWrite(raw);
        } catch (IOException error) {
            if (raw != null) atomic.failWrite(raw);
            throw error;
        }
    }

    private static long readTimestamp(File target) {
        if (!target.exists()) return 0L;
        try (DataInputStream input = new DataInputStream(new FileInputStream(target))) {
            return input.readLong();
        } catch (IOException ignored) {
            return 0L;
        }
    }
}
