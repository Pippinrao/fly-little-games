package com.flynes.emu.cover;

import com.flynes.emu.video.PublishedFrame;

import java.nio.ByteBuffer;

/** Stable, compact pixel copy that remains valid after native frame buffers are reused. */
public final class CoverFrame {
    private final String canonicalId;
    private final int width;
    private final int height;
    private final PublishedFrame.Format format;
    private final byte[] pixels;

    private CoverFrame(String canonicalId, int width, int height,
            PublishedFrame.Format format, byte[] pixels) {
        this.canonicalId = canonicalId;
        this.width = width;
        this.height = height;
        this.format = format;
        this.pixels = pixels;
    }

    public static CoverFrame copyOf(String canonicalId, PublishedFrame frame) {
        if (canonicalId == null || canonicalId.isBlank() || frame == null) {
            throw new IllegalArgumentException("cover frame requires a game and frame");
        }
        int bytesPerPixel = switch (frame.format()) {
            case RGB565 -> 2;
            case RGB888 -> 3;
            case RGBA8888 -> 4;
        };
        int rowBytes = Math.multiplyExact(frame.width(), bytesPerPixel);
        if (frame.pitch() < rowBytes) throw new IllegalArgumentException("frame pitch is too small");
        byte[] compact = new byte[Math.multiplyExact(rowBytes, frame.height())];
        ByteBuffer source = frame.pixels();
        for (int row = 0; row < frame.height(); row++) {
            source.position(Math.multiplyExact(row, frame.pitch()));
            source.get(compact, Math.multiplyExact(row, rowBytes), rowBytes);
        }
        return new CoverFrame(canonicalId, frame.width(), frame.height(),
                frame.format(), compact);
    }

    public String canonicalId() { return canonicalId; }
    public int width() { return width; }
    public int height() { return height; }
    public PublishedFrame.Format format() { return format; }
    public byte[] pixels() { return pixels.clone(); }
    byte[] pixelsUnsafe() { return pixels; }
}
