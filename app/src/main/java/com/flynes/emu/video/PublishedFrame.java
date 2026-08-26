package com.flynes.emu.video;

import java.nio.ByteBuffer;

/** Immutable copy of one completely rendered emulator frame. */
public final class PublishedFrame implements AutoCloseable {
    public enum Format { RGB565, RGB888, RGBA8888 }

    private final long sequence;
    private final int width;
    private final int height;
    private final int pitch;
    private final Format format;
    private final ByteBuffer pixels;
    private final boolean complete;
    private final FrameLease lease;

    public PublishedFrame(long sequence, int width, int height, int pitch,
                          Format format, ByteBuffer pixels, boolean complete) {
        this(sequence, width, height, pitch, format, pixels, complete, null);
    }

    private PublishedFrame(long sequence, int width, int height, int pitch,
                           Format format, ByteBuffer pixels, boolean complete,
                           FrameLease lease) {
        this.sequence = sequence;
        this.width = width;
        this.height = height;
        this.pitch = pitch;
        this.format = format;
        this.pixels = pixels.asReadOnlyBuffer();
        this.complete = complete;
        this.lease = lease;
    }

    public static PublishedFrame fromNative(long sequence, int[] metadata,
                                            ByteBuffer storage) {
        return fromNative(sequence, metadata, storage, null);
    }

    static PublishedFrame fromNative(long sequence, int[] metadata,
                                     ByteBuffer storage, FrameLease lease) {
        if (metadata == null || metadata.length < 5 || storage == null || !storage.isDirect()) {
            throw new IllegalArgumentException("Native frame metadata requires a direct buffer");
        }
        int width = metadata[0];
        int height = metadata[1];
        int pitch = metadata[2];
        int formatId = metadata[3];
        int bytes = metadata[4];
        if (sequence < 0 || width <= 0 || height <= 0 || pitch <= 0 || bytes <= 0
                || bytes > storage.capacity() || (long) pitch * height > bytes
                || formatId < 0 || formatId >= Format.values().length) {
            throw new IllegalArgumentException("Invalid native frame metadata");
        }
        ByteBuffer view = storage.duplicate();
        view.clear();
        view.limit(bytes);
        return new PublishedFrame(sequence, width, height, pitch,
                Format.values()[formatId], view.asReadOnlyBuffer(), true, lease);
    }

    public long sequence() { return sequence; }
    public int width() { return width; }
    public int height() { return height; }
    public int pitch() { return pitch; }
    public Format format() { return format; }
    public ByteBuffer pixels() { return pixels.asReadOnlyBuffer(); }
    public boolean complete() { return complete; }
    @Override public void close() { if (lease != null) lease.close(); }
}
