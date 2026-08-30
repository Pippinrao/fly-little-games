package com.flynes.emu.video.audio;

import static org.junit.Assert.assertEquals;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

public final class DisplayLeaseWatchdogTest {
    @Test public void deadlineExpiresIndependentlyOfMonitorProgress() {
        DisplayLeaseWatchdog watchdog = new DisplayLeaseWatchdog(1_500_000_000L);
        DisplayLeaseWatchdog.Token token = watchdog.arm(4L, 9L, 1_000_000_000L);
        assertEquals(DisplayLeaseWatchdog.Status.FRESH,
                watchdog.check(token, 2_499_999_999L));
        assertEquals(DisplayLeaseWatchdog.Status.EXPIRED,
                watchdog.check(token, 2_500_000_000L));
    }

    @Test public void staleEpochOrGenerationCannotReuseFreshDeadline() {
        DisplayLeaseWatchdog watchdog = new DisplayLeaseWatchdog(1_500_000_000L);
        DisplayLeaseWatchdog.Token old = watchdog.arm(2L, 3L, 10L);
        watchdog.arm(3L, 4L, 20L);
        assertEquals(DisplayLeaseWatchdog.Status.STALE_TOKEN, watchdog.check(old, 21L));
        watchdog.clear();
        assertEquals(DisplayLeaseWatchdog.Status.UNARMED, watchdog.check(old, 22L));
    }

    @Test public void independentDeadlineCallbackFiresWhenObservationThreadIsStalled() {
        FakeScheduler scheduler = new FakeScheduler();
        List<DisplayLeaseWatchdog.Token> expired = new ArrayList<>();
        DisplayLeaseWatchdog watchdog = new DisplayLeaseWatchdog(
                1_500_000_000L, scheduler, expired::add);
        DisplayLeaseWatchdog.Token old = watchdog.arm(1L, 1L, 0L);
        DisplayLeaseWatchdog.Token current = watchdog.arm(1L, 2L, 100L);
        scheduler.run(0); // cancelled old deadline cannot fire
        assertEquals(0, expired.size());
        scheduler.run(1);
        assertEquals(1, expired.size());
        assertEquals(current, expired.get(0));
        assertEquals(DisplayLeaseWatchdog.Status.UNARMED, watchdog.check(current, 200L));
        assertEquals(DisplayLeaseWatchdog.Status.UNARMED, watchdog.check(old, 200L));
    }

    private static final class FakeScheduler implements DisplayLeaseWatchdog.DeadlineScheduler {
        final List<FakeCancellation> tasks = new ArrayList<>();
        @Override public DisplayLeaseWatchdog.Cancellation schedule(Runnable callback, long delayNs) {
            FakeCancellation task = new FakeCancellation(callback);
            tasks.add(task);
            return task;
        }
        void run(int index) { tasks.get(index).run(); }
    }

    private static final class FakeCancellation implements DisplayLeaseWatchdog.Cancellation {
        final Runnable callback;
        boolean cancelled;
        FakeCancellation(Runnable callback) { this.callback = callback; }
        @Override public void cancel() { cancelled = true; }
        void run() { if (!cancelled) callback.run(); }
    }
}
