package com.flynes.emu.cover;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.system.ErrnoException;
import android.system.Os;
import android.util.LruCache;

import com.flynes.emu.video.PublishedFrame;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;

/** App-private, atomic cover store. Filenames reveal neither ROM titles nor device locators. */
public final class AndroidCoverRepository implements CoverCaptureCoordinator.Sink {
    private static final int COVER_WIDTH = 320;
    private static final int COVER_HEIGHT = 240;
    private final File directory;
    private final LruCache<String, Bitmap> memory = new LruCache<>(24);

    public AndroidCoverRepository(Context context) {
        if (context == null) throw new IllegalArgumentException("context must not be null");
        directory = new File(context.getNoBackupFilesDir(), "covers/v1");
    }

    @Override public synchronized void store(CoverFrame frame) {
        if (!directory.isDirectory() && !directory.mkdirs() && !directory.isDirectory()) return;
        Bitmap nativeFrame = bitmapFrom(frame);
        Bitmap cover = Bitmap.createScaledBitmap(
                nativeFrame, COVER_WIDTH, COVER_HEIGHT, false);
        if (cover != nativeFrame) nativeFrame.recycle();
        File target = file(frame.canonicalId());
        File temporary;
        try {
            temporary = File.createTempFile("cover-", ".png", directory);
            try (FileOutputStream output = new FileOutputStream(temporary)) {
                if (!cover.compress(Bitmap.CompressFormat.PNG, 100, output)) {
                    temporary.delete();
                    cover.recycle();
                    return;
                }
                output.getFD().sync();
            }
            try {
                Os.rename(temporary.getAbsolutePath(), target.getAbsolutePath());
                memory.put(frame.canonicalId(), cover);
            } catch (ErrnoException failure) {
                temporary.delete();
                cover.recycle();
            }
        } catch (IOException failure) {
            cover.recycle();
        }
    }

    public Bitmap load(String canonicalId) {
        Bitmap cached = memory.get(canonicalId);
        if (cached != null && !cached.isRecycled()) return cached;
        Bitmap decoded = BitmapFactory.decodeFile(file(canonicalId).getAbsolutePath());
        if (decoded != null) memory.put(canonicalId, decoded);
        return decoded;
    }

    public boolean exists(String canonicalId) {
        return file(canonicalId).isFile();
    }

    public void removeForTest(String canonicalId) {
        memory.remove(canonicalId);
        file(canonicalId).delete();
    }

    public File fileForTest(String canonicalId) { return file(canonicalId); }

    private File file(String canonicalId) {
        if (canonicalId == null || canonicalId.isBlank()) {
            throw new IllegalArgumentException("canonical id must not be blank");
        }
        return new File(directory, sha256(canonicalId) + ".png");
    }

    private static Bitmap bitmapFrom(CoverFrame frame) {
        byte[] pixels = frame.pixelsUnsafe();
        if (frame.format() == PublishedFrame.Format.RGB565) {
            Bitmap bitmap = Bitmap.createBitmap(
                    frame.width(), frame.height(), Bitmap.Config.RGB_565);
            bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(pixels).order(ByteOrder.nativeOrder()));
            return bitmap;
        }
        int bytesPerPixel = frame.format() == PublishedFrame.Format.RGB888 ? 3 : 4;
        int[] colors = new int[frame.width() * frame.height()];
        for (int index = 0; index < colors.length; index++) {
            int source = index * bytesPerPixel;
            int red = pixels[source] & 0xff;
            int green = pixels[source + 1] & 0xff;
            int blue = pixels[source + 2] & 0xff;
            int alpha = bytesPerPixel == 4 ? pixels[source + 3] & 0xff : 0xff;
            colors[index] = alpha << 24 | red << 16 | green << 8 | blue;
        }
        return Bitmap.createBitmap(colors, frame.width(), frame.height(), Bitmap.Config.ARGB_8888);
    }

    private static String sha256(String value) {
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256")
                    .digest(value.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder(digest.length * 2);
            for (byte item : digest) hex.append(String.format("%02x", item & 0xff));
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException("SHA-256 unavailable", impossible);
        }
    }
}
