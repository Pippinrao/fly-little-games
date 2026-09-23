package com.flynes.emu;

import com.flynes.emu.launch.LaunchResult;

import org.junit.Test;

import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

public final class AndroidGameLaunchServiceTest {
    @Test public void cachedSelectionWaitsForNativeAndResolvesTheLiveVariant() throws Exception {
        CompletableFuture<Void> nativeReady = new CompletableFuture<>();
        AtomicInteger launches = new AtomicInteger();
        AtomicReference<String> launchedVariant = new AtomicReference<>();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> pending = worker.submit(() ->
                    AndroidGameLaunchService.launchAfterNativeReady(
                            nativeReady, "cached-game",
                            canonicalId -> "live-variant",
                            variantId -> {
                                launches.incrementAndGet();
                                launchedVariant.set(variantId);
                                return LaunchResult.unexpectedFailure("fixture");
                            }));

            Thread.sleep(50);
            assertEquals("no ROM open may happen before native readiness", 0, launches.get());
            assertFalse(pending.isDone());

            nativeReady.complete(null);
            pending.get(5, TimeUnit.SECONDS);
            assertEquals(1, launches.get());
            assertEquals("live-variant", launchedVariant.get());
        } finally {
            worker.shutdownNow();
        }
    }

    @Test public void missingLiveCanonicalEntryFailsWithoutOpening() throws Exception {
        AtomicInteger launches = new AtomicInteger();

        LaunchResult result = AndroidGameLaunchService.launchAfterNativeReady(
                CompletableFuture.completedFuture(null), "deleted-game",
                canonicalId -> null,
                variantId -> {
                    launches.incrementAndGet();
                    return LaunchResult.unexpectedFailure("must not run");
                });

        assertEquals(LaunchResult.Code.CATALOG_CHANGED, result.code());
        assertEquals(0, launches.get());
    }

    @Test public void resolverRunsAfterReadinessAndSeesReplacementIdentity() throws Exception {
        CompletableFuture<Void> nativeReady = new CompletableFuture<>();
        AtomicReference<String> liveVariant = new AtomicReference<>("stale-variant");
        AtomicReference<String> launchedVariant = new AtomicReference<>();
        ExecutorService worker = Executors.newSingleThreadExecutor();
        try {
            Future<LaunchResult> pending = worker.submit(() ->
                    AndroidGameLaunchService.launchAfterNativeReady(
                            nativeReady, "same-canonical-id", canonicalId -> liveVariant.get(),
                            variantId -> {
                                launchedVariant.set(variantId);
                                return LaunchResult.unexpectedFailure("fixture");
                            }));

            liveVariant.set("replacement-variant");
            nativeReady.complete(null);
            pending.get(5, TimeUnit.SECONDS);

            assertEquals("replacement-variant", launchedVariant.get());
        } finally {
            worker.shutdownNow();
        }
    }
}
