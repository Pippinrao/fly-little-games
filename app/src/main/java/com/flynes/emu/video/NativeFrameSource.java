package com.flynes.emu.video;

import java.nio.ByteBuffer;

/** Copies native snapshots into alternating direct buffers for synchronous consumers. */
public final class NativeFrameSource implements FramePublisher.Source {
    public interface Bridge {
        long copyVideoFrame(ByteBuffer destination, int[] metadata);
    }

    private final Bridge bridge;
    private final ByteBuffer[] buffers;
    private final int[] metadata = new int[5];
    private int nextBuffer;

    public NativeFrameSource(Bridge bridge, int bufferBytes) {
        if (bridge == null || bufferBytes <= 0) {
            throw new IllegalArgumentException("A bridge and positive buffer size are required");
        }
        this.bridge = bridge;
        this.buffers = new ByteBuffer[]{
                ByteBuffer.allocateDirect(bufferBytes),
                ByteBuffer.allocateDirect(bufferBytes)
        };
    }

    @Override public PublishedFrame copyLatest() {
        ByteBuffer destination = buffers[nextBuffer];
        destination.clear();
        long sequence = bridge.copyVideoFrame(destination, metadata);
        if (sequence < 0) return null;
        PublishedFrame frame = PublishedFrame.fromNative(sequence, metadata, destination);
        nextBuffer = (nextBuffer + 1) % buffers.length;
        return frame;
    }
}
