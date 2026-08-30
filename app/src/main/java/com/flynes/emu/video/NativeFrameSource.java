package com.flynes.emu.video;

import java.nio.ByteBuffer;

/** Copies native snapshots into alternating direct buffers for synchronous consumers. */
public final class NativeFrameSource implements FramePublisher.Source {
    public interface Bridge {
        FrameCopyResult copyVideoFrameIfNew(ByteBuffer destination, int[] metadata,
                                            long lastSequence);
    }

    private final Bridge bridge;
    private final FrameBufferPool pool;
    private final int[] metadata = new int[5];
    private long lastSequence = -1L;

    public NativeFrameSource(Bridge bridge, int bufferBytes) {
        if (bridge == null || bufferBytes <= 0) {
            throw new IllegalArgumentException("A bridge and positive buffer size are required");
        }
        this.bridge = bridge;
        this.pool = new FrameBufferPool(2, bufferBytes);
    }

    @Override public PublishedFrame copyLatest() {
        FrameLease lease = pool.acquire();
        if (lease == null) return null;
        ByteBuffer destination = lease.buffer();
        FrameCopyResult result = bridge.copyVideoFrameIfNew(destination, metadata, lastSequence);
        if (result == null || result.kind() != FrameCopyResult.Kind.NEW) {
            lease.close();
            return null;
        }
        PublishedFrame frame;
        try {
            frame = PublishedFrame.fromNative(result.sequence(), metadata, destination, lease);
        } catch (RuntimeException failure) {
            lease.close();
            throw failure;
        }
        lastSequence = result.sequence();
        return frame;
    }
}
