package com.flynes.emu.nearby;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.FlyNesApplication;
import com.flynes.emu.NearbyAvailability;
import com.flynes.emu.NearbySession;
import com.flynes.emu.NearbySessionOwner;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * ENG-03.A: the process holds one real V2 session owner. Pages read link/game
 * state from that snapshot, not from a local connected/confirmed boolean.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbySessionOwnerTest {

    // This is actual owner/JNI lifetime evidence, not a successful connected ROM install.
    private static Object leased(NearbySessionOwner owner, java.util.function.LongFunction<Object> call) throws Exception {
        java.lang.reflect.Method method = NearbySessionOwner.class.getDeclaredMethod("withHandle", java.util.function.LongFunction.class);
        method.setAccessible(true);
        return method.invoke(owner, call);
    }

    @Test public void realOwnerCloseWaitsForEnteredJniLease() throws Exception {
        NearbySessionOwner owner = NearbySessionOwner.create();
        java.util.concurrent.CountDownLatch entered = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.CountDownLatch release = new java.util.concurrent.CountDownLatch(1);
        java.util.concurrent.atomic.AtomicReference<Throwable> failure = new java.util.concurrent.atomic.AtomicReference<>();
        Thread caller = new Thread(() -> {
            try {
                leased(owner, value -> {
                    assertTrue(value != 0);
                    assertEquals(1, owner.v2CreateCount()); // actual JNI inside the admitted boundary
                    entered.countDown();
                    try { assertTrue(release.await(3, java.util.concurrent.TimeUnit.SECONDS)); }
                    catch (InterruptedException e) { throw new AssertionError(e); }
                    return 0;
                });
            } catch (Throwable e) { failure.set(e); }
        });
        Thread closing = new Thread(owner::close);
        try {
            caller.start(); assertTrue(entered.await(3, java.util.concurrent.TimeUnit.SECONDS)); closing.start();
            long deadline = System.nanoTime() + java.util.concurrent.TimeUnit.SECONDS.toNanos(3);
            while (closing.isAlive() && closing.getState() != Thread.State.WAITING && System.nanoTime() < deadline)
                Thread.yield();
            assertEquals("close waits for admitted JNI, without holding its monitor", Thread.State.WAITING, closing.getState());
            assertEquals("new calls cannot reuse closing handle", 0, owner.v2CreateCount());
        } finally {
            release.countDown(); caller.join(3000); closing.join(3000); owner.close();
        }
        org.junit.Assert.assertFalse(caller.isAlive()); org.junit.Assert.assertFalse(closing.isAlive());
        if (failure.get() != null) throw new AssertionError(failure.get());
    }

    @Test public void completionAfterRealOwnerJniLeaseCanCloseSynchronously() throws Exception {
        NearbySessionOwner owner = NearbySessionOwner.create();
        java.util.concurrent.CompletableFuture<Integer> completed = new java.util.concurrent.CompletableFuture<>();
        java.util.concurrent.CompletableFuture<Void> closed = completed.thenRun(owner::close);
        java.util.concurrent.atomic.AtomicReference<Throwable> failure = new java.util.concurrent.atomic.AtomicReference<>();
        Thread work = new Thread(() -> {
            try {
                int result = (Integer) leased(owner, value -> owner.v2CreateCount());
                // Matches production Bridge.install: withHandle returns before coordinator completes.
                completed.complete(result);
            } catch (Throwable e) { failure.set(e); }
        });
        work.start();
        try { closed.get(3, java.util.concurrent.TimeUnit.SECONDS); }
        finally { work.join(3000); owner.close(); }
        org.junit.Assert.assertFalse(work.isAlive());
        if (failure.get() != null) throw new AssertionError(failure.get());
        assertEquals(1, (int) completed.get()); assertEquals(0, owner.v2CreateCount());
    }

    @Test public void diagnosticAsyncPreparationCannotClaimMetadataSelection() throws Exception {
        try (NearbySessionOwner owner = NearbySessionOwner.create()) {
            assertEquals(-18, (int) owner.prepareAndSelectContent(new byte[16]).toCompletableFuture()
                    .get(3, java.util.concurrent.TimeUnit.SECONDS));
            assertEquals(0, owner.snapshot().pendingConfigRevision);
            assertEquals(NearbySessionOwner.GAME_NOT_STARTED, owner.snapshot().gameState);
        }
    }

    @Test public void typedContentSelectionCannotAuthorizeUnknownSources() {
        try (NearbySessionOwner owner = NearbySessionOwner.create()) {
            assertEquals(0, owner.gameChoices().length);
            org.junit.Assert.assertFalse("unqueried metadata is not an empty/failed query", owner.snapshot().contentQueryAttempted);
            org.junit.Assert.assertThrows(IllegalArgumentException.class, () -> owner.selectContent(null));
            org.junit.Assert.assertThrows(IllegalArgumentException.class, () -> owner.selectContent(new byte[15]));
            org.junit.Assert.assertThrows(IllegalArgumentException.class, () -> owner.submitAction(42, new byte[16]));
            assertTrue("unknown source ref is rejected", owner.selectContent(new byte[16]) < 0);
            assertEquals(NearbySessionOwner.GAME_NOT_STARTED, owner.snapshot().gameState);
        }
    }

    @Test public void repeatedInviteLifecycleDoesNotExhaustActionNotices() {
        try (NearbySessionOwner owner = NearbySessionOwner.create()) {
            for (int cycle = 0; cycle < 12; ++cycle) {
                assertEquals("create cycle " + cycle, NearbySessionOwner.V2_ACCEPTED,
                        owner.submitAction(5, null));
                assertEquals(NearbySessionOwner.LINK_INVITING, owner.snapshot().linkState);
                assertEquals("cancel cycle " + cycle, NearbySessionOwner.V2_ACCEPTED,
                        owner.submitAction(7, null));
                assertEquals(NearbySessionOwner.LINK_IDLE, owner.snapshot().linkState);
            }
        }
    }

    @Test public void rejectedActionReturnsItsTerminalErrorInsteadOfAdmissionSuccess() {
        try (NearbySessionOwner owner = NearbySessionOwner.create()) {
            assertEquals("engine rejects a non-digit code after admission", -4,
                    owner.submitAction(9, "12X456".getBytes(java.nio.charset.StandardCharsets.US_ASCII)));
            assertEquals(NearbySessionOwner.LINK_IDLE, owner.snapshot().linkState);
            NearbySessionOwner.Snapshot rejected = owner.snapshot();
            assertTrue(rejected.lastActionRequestId > 0);
            assertEquals(2, rejected.lastActionOutcome);
            assertEquals(-4, rejected.lastActionResult);
            assertEquals("an earlier failure must not contaminate the next request",
                    NearbySessionOwner.V2_ACCEPTED, owner.submitAction(5, null));
            NearbySessionOwner.Snapshot applied = owner.snapshot();
            assertTrue(applied.lastActionRequestId > rejected.lastActionRequestId);
            assertEquals(1, applied.lastActionOutcome);
            assertEquals(0, applied.lastActionResult);
            org.junit.Assert.assertFalse(applied.shutdownComplete);
        }
    }

    private static NearbySessionOwner requireOwner() {
        FlyNesApplication app = (FlyNesApplication)
                ApplicationProvider.getApplicationContext();
        org.junit.Assert.assertTrue(app.ensureNearby().reasonKey(), app.ensureNearby().ready());
        NearbySessionOwner owner = app.nearbySessionOwner();
        org.junit.Assert.assertNotNull(owner);
        return owner;
    }

    @Test
    public void applicationReusesOneV2OwnerWhoseSnapshotIsNotALocalBoolean() {
        NearbySessionOwner owner = requireOwner();
        FlyNesApplication app = (FlyNesApplication)
                ApplicationProvider.getApplicationContext();
        assertSame(owner, app.nearbySessionOwner());
        assertTrue("create_v2 must be observable", owner.v2CreateCount() >= 1);

        NearbySessionOwner.Snapshot snap = owner.snapshot();
        assertNotNull(snap);
        assertEquals(2, snap.abiVersion);
        assertTrue(
                "fresh owner is idle or unavailable, never CONNECTED_LOBBY from a local bool",
                snap.linkState == NearbySessionOwner.LINK_IDLE
                        || snap.linkState == NearbySessionOwner.LINK_UNAVAILABLE);
        assertNotEquals(NearbySessionOwner.LINK_CONNECTED_LOBBY, snap.linkState);
        assertEquals(NearbySessionOwner.GAME_NOT_STARTED, snap.gameState);
        assertEquals(0, snap.pendingConfigLocalConfirmed);
        assertEquals(0, snap.pendingConfigPeerConfirmed);
    }

    @Test
    public void hostPublishMovesTheSameV2OwnerToInviting() {
        NearbySessionOwner owner = requireOwner();
        NearbySession session = ((FlyNesApplication)
                ApplicationProvider.getApplicationContext()).nearbySession();
        session.cancelActiveHost();
        org.junit.Assert.assertEquals(1, owner.v2CreateCount());
        org.junit.Assert.assertNotEquals(
                NearbySessionOwner.LINK_INVITING, owner.snapshot().linkState);

        org.junit.Assert.assertTrue(session.publish(
                42L, "123456", android.os.SystemClock.elapsedRealtime()));
        NearbySessionOwner.Snapshot after = owner.snapshot();
        org.junit.Assert.assertEquals(NearbySessionOwner.LINK_INVITING, after.linkState);
        org.junit.Assert.assertTrue(session.active(42L));
        org.junit.Assert.assertEquals(1, owner.v2CreateCount());

        org.junit.Assert.assertTrue(session.cancel(42L));
        org.junit.Assert.assertEquals(NearbySessionOwner.LINK_IDLE, owner.snapshot().linkState);
        org.junit.Assert.assertFalse(session.active(42L));
    }

    @Test
    public void ownerWiresProductQuinnNotAFixtureForwarder() {
        NearbySessionOwner owner = requireOwner();
        org.junit.Assert.assertEquals(
                "FlynesQuicProvider/Quinn", owner.quicProviderType());
        org.junit.Assert.assertTrue(
                "product Quinn must listen on a real localhost socket",
                owner.quicReady());
        String addr = owner.quicListenAddress();
        org.junit.Assert.assertTrue(
                "listen is 127.0.0.1 with port >= 49152, not a fixture: " + addr,
                addr.startsWith("127.0.0.1:"));
        int port = Integer.parseInt(addr.substring("127.0.0.1:".length()));
        org.junit.Assert.assertTrue("ephemeral port in IPv4 contract range: " + port,
                port >= 49152);
    }

    @Test
    public void engineVisibleQuicListenAcceptsOnThePreboundQuinnListener() {
        NearbySessionOwner owner = requireOwner();
        org.junit.Assert.assertEquals(
                "engine listen must map to Quinn accept, not UNAVAILABLE stub",
                NearbySessionOwner.V2_ACCEPTED,
                owner.quicEngineListen());
    }

    @Test
    public void twoOwnersCarryControlBytesOverQuinnSocketsNotAFixture() {
        NearbySessionOwner listener = NearbySessionOwner.create();
        NearbySessionOwner connector = NearbySessionOwner.create();
        try {
            org.junit.Assert.assertEquals(
                    "FlynesQuicProvider/Quinn", listener.quicProviderType());
            org.junit.Assert.assertEquals(
                    "FlynesQuicProvider/Quinn", connector.quicProviderType());
            org.junit.Assert.assertTrue(listener.quicReady());
            org.junit.Assert.assertTrue(connector.quicReady());
            org.junit.Assert.assertNotEquals(
                    listener.quicListenAddress(), connector.quicListenAddress());
            long[] facts = new long[5];
            org.junit.Assert.assertEquals(
                    NearbySessionOwner.V2_OK,
                    listener.quicControlRoundtripFrom(connector, facts));
            org.junit.Assert.assertEquals(
                    "listener engine listen/accept", NearbySessionOwner.V2_ACCEPTED,
                    (int) facts[0]);
            org.junit.Assert.assertEquals(
                    "connector engine connect", NearbySessionOwner.V2_ACCEPTED,
                    (int) facts[1]);
            org.junit.Assert.assertEquals(
                    "connector engine write facts=" + java.util.Arrays.toString(facts),
                    NearbySessionOwner.V2_ACCEPTED,
                    (int) facts[2]);
            org.junit.Assert.assertTrue(
                    "Control bytes must leave via Quinn write, not fixture: " + facts[3],
                    facts[3] > 0);
            org.junit.Assert.assertTrue(
                    "peer must receive Control on the Quinn socket: " + facts[4],
                    facts[4] > 0);
        } finally {
            connector.close();
            listener.close();
        }
    }

    @Test
    public void armedTimerFiresOnceWithoutSnapshotPolling() throws Exception {
        NearbySessionOwner owner = NearbySessionOwner.create();
        try {
            assertEquals(NearbySessionOwner.V2_ACCEPTED, owner.armTestTimer(7L, 40L));
            assertEquals("must not fire before the deadline", 0, owner.testTimerFires(7L));
            assertTrue("timer must run without snapshot/click pumping",
                    owner.waitTestTimer(7L, 1500L));
            assertEquals(1, owner.testTimerFires(7L));
            Thread.sleep(80L);
            assertEquals("expired task runs exactly once", 1, owner.testTimerFires(7L));
        } finally {
            owner.close();
        }
    }

    @Test
    public void cancelledTimerDoesNotFire() throws Exception {
        NearbySessionOwner owner = NearbySessionOwner.create();
        try {
            assertEquals(NearbySessionOwner.V2_ACCEPTED, owner.armTestTimer(8L, 80L));
            assertEquals(NearbySessionOwner.V2_OK, owner.cancelTestTimer(8L));
            Thread.sleep(250L);
            assertEquals(0, owner.testTimerFires(8L));
        } finally {
            owner.close();
        }
    }

    @Test
    public void sameTimerIdKeepsTheLatestDeadline() {
        NearbySessionOwner owner = NearbySessionOwner.create();
        try {
            assertEquals(NearbySessionOwner.V2_ACCEPTED, owner.armTestTimer(9L, 2000L));
            assertEquals(NearbySessionOwner.V2_ACCEPTED, owner.armTestTimer(9L, 40L));
            assertTrue(owner.waitTestTimer(9L, 1500L));
            assertEquals(1, owner.testTimerFires(9L));
        } finally {
            owner.close();
        }
    }

    @Test
    public void closedOwnerDropsLateTimersWithoutCrash() throws Exception {
        NearbySessionOwner owner = NearbySessionOwner.create();
        assertEquals(NearbySessionOwner.V2_ACCEPTED, owner.armTestTimer(10L, 80L));
        owner.close();
        Thread.sleep(250L);
        NearbySessionOwner again = NearbySessionOwner.create();
        try {
            assertEquals(0, again.testTimerFires(10L));
        } finally {
            again.close();
        }
    }
}
