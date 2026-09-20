package com.flynes.emu;

import org.junit.Test;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import static org.junit.Assert.*;

public final class NearbyOwnerHandleTest {
    @Test public void admissionDoesNotHoldMonitorAndCloseWaitsOnlyForLease() throws Exception {
        NearbyOwnerHandle handle = new NearbyOwnerHandle(123);
        NearbyOwnerHandle.Lease admitted = handle.acquire();
        assertEquals(123, admitted.value());
        CountDownLatch sealed = new CountDownLatch(1);
        CountDownLatch destroyed = new CountDownLatch(1);
        AtomicInteger destroys = new AtomicInteger();
        Thread close = new Thread(() -> handle.close(sealed::countDown, value -> {
            assertEquals(123, value); destroys.incrementAndGet(); destroyed.countDown();
        }));
        close.start();
        assertTrue(sealed.await(2, TimeUnit.SECONDS));
        try (NearbyOwnerHandle.Lease late = handle.acquire()) { assertEquals(0, late.value()); }
        assertEquals("destroy cannot overlap an admitted JNI call", 1, destroyed.getCount());
        admitted.close(); admitted.close();
        assertTrue(destroyed.await(2, TimeUnit.SECONDS));
        close.join(2000); assertFalse(close.isAlive());
        handle.close(() -> fail("close twice must not recancel"), value -> destroys.incrementAndGet());
        assertEquals(1, destroys.get());
    }

    @Test public void completionAfterReleaseMaySynchronouslyClose() {
        NearbyOwnerHandle handle = new NearbyOwnerHandle(456);
        CompletableFuture<Integer> future = new CompletableFuture<>();
        AtomicInteger destroyed = new AtomicInteger();
        future.thenRun(() -> handle.close(() -> {}, value -> destroyed.incrementAndGet()));
        try (NearbyOwnerHandle.Lease lease = handle.acquire()) { assertEquals(456, lease.value()); }
        future.complete(0);
        assertEquals(1, destroyed.get());
    }

    @Test public void closeDoesNotWaitForUnadmittedRomIoAndMayReenterDuringCancel() {
        NearbyOwnerHandle handle = new NearbyOwnerHandle(789);
        AtomicInteger destroyed = new AtomicInteger();
        handle.close(() -> handle.close(() -> fail("reentrant cancellation"), value -> fail("double destroy")),
                value -> destroyed.incrementAndGet());
        assertEquals(1, destroyed.get());
        try (NearbyOwnerHandle.Lease late = handle.acquire()) { assertEquals(0, late.value()); }
    }
}
