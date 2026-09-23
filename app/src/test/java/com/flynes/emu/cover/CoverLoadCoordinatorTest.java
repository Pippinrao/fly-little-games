package com.flynes.emu.cover;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executor;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

public final class CoverLoadCoordinatorTest {
    @Test public void duplicateLoadsShareDecodeAndCancellationDoesNotCancelOtherWaiter()
            throws Exception {
        CountDownLatch entered = new CountDownLatch(1);
        CountDownLatch release = new CountDownLatch(1);
        AtomicInteger calls = new AtomicInteger();
        Object decoded = new Object();
        try (CoverLoadCoordinator<Object> coordinator = new CoverLoadCoordinator<>(
                ignored -> null,
                ignored -> {
                    calls.incrementAndGet();
                    entered.countDown();
                    try { release.await(5, TimeUnit.SECONDS); }
                    catch (InterruptedException failure) { Thread.currentThread().interrupt(); }
                    return decoded;
                })) {
            CompletableFuture<Object> first = coordinator.load("same-id").toCompletableFuture();
            CompletableFuture<Object> second = coordinator.load("same-id").toCompletableFuture();
            assertTrue(entered.await(2, TimeUnit.SECONDS));
            assertEquals(1, calls.get());
            first.cancel(true);
            release.countDown();
            assertSame(decoded, second.get(2, TimeUnit.SECONDS));
            assertTrue(first.isCancelled());
        }
    }

    @Test public void memoryHitSchedulesNoExecutorWork() throws Exception {
        Object cached = new Object();
        AtomicInteger scheduled = new AtomicInteger();
        Executor executor = command -> scheduled.incrementAndGet();
        try (CoverLoadCoordinator<Object> coordinator = new CoverLoadCoordinator<>(
                Map.of("cached", cached)::get, ignored -> new Object(), executor, () -> { })) {
            assertSame(cached, coordinator.load("cached").toCompletableFuture().get());
            assertEquals(0, scheduled.get());
        }
    }
}
