package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public final class FrameBufferPoolTest {
    @Test public void leasesBoundDirectStorageAndReturnItExactlyOnce() {
        FrameBufferPool pool = new FrameBufferPool(1, 64);
        FrameLease first = pool.acquire();
        assertNotNull(first);
        assertEquals(64, first.buffer().capacity());
        assertNull(pool.acquire());
        first.close();
        first.close();
        assertNotNull(pool.acquire());
    }
}
