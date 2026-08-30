package com.flynes.emu.video;

import java.nio.ByteBuffer;

/** Exclusive, close-once access to one pool buffer. */
public final class FrameLease implements AutoCloseable {
    private ByteBuffer buffer;
    private final FrameBufferPool owner;

    FrameLease(ByteBuffer buffer, FrameBufferPool owner) {
        this.buffer = buffer;
        this.owner = owner;
    }

    public synchronized ByteBuffer buffer() {
        if (buffer == null) throw new IllegalStateException("frame lease is closed");
        return buffer;
    }

    @Override public void close() {
        ByteBuffer released;
        synchronized (this) {
            released = buffer;
            buffer = null;
        }
        if (released != null) owner.release(released);
    }
}
