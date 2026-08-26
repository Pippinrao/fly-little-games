package com.flynes.emu.video;

import java.nio.ByteBuffer;
import java.util.ArrayDeque;

/** Bounded direct storage; exhaustion is backpressure, never an unbounded allocation. */
public final class FrameBufferPool {
    private final ArrayDeque<ByteBuffer> available = new ArrayDeque<>();

    public FrameBufferPool(int count, int bufferBytes) {
        if (count <= 0 || bufferBytes <= 0) throw new IllegalArgumentException("invalid pool");
        for (int index = 0; index < count; index++)
            available.add(ByteBuffer.allocateDirect(bufferBytes));
    }

    public synchronized FrameLease acquire() {
        ByteBuffer buffer = available.pollFirst();
        if (buffer == null) return null;
        buffer.clear();
        return new FrameLease(buffer, this);
    }

    synchronized void release(ByteBuffer buffer) {
        buffer.clear();
        available.addLast(buffer);
    }
}
