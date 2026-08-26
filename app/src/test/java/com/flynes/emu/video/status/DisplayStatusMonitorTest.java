package com.flynes.emu.video.status;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.platform.DisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.DisplayObservation;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class DisplayStatusMonitorTest {
    @Test public void freshnessUsesHalfOpenLeaseAndReadFailureIsImmediatelyUnknown() {
        Fixture fixture = new Fixture();
        long generation = fixture.monitor.request(7L, PhysicalRefreshPolicy.HZ_120,
                mode(2, 120_000), true);
        assertEquals(1L, generation);
        assertEquals(StatusFreshness.FRESH, fixture.monitor.snapshotAt(1_499L).freshness());
        assertEquals(StatusFreshness.STALE, fixture.monitor.snapshotAt(1_500L).freshness());

        fixture.platform.fail = true;
        fixture.clock.now = 500L;
        fixture.monitor.onDisplayChanged();
        assertEquals(1, fixture.listener.unknownCount);
        assertNull(fixture.monitor.snapshotAt(500L).observation());
        assertEquals(StatusFreshness.UNKNOWN, fixture.monitor.snapshotAt(500L).freshness());
    }

    @Test public void blockedPollCannotPreventIndependentLeaseDeadline() {
        Fixture fixture = new Fixture();
        fixture.monitor.request(9L, PhysicalRefreshPolicy.HZ_120, mode(2, 120_000), true);
        assertEquals(1, fixture.poll.pending());
        assertEquals(1, fixture.safety.pending());

        fixture.clock.now = 1_500L;
        fixture.safety.runDue();
        assertEquals(1, fixture.listener.expiredCount);
        assertEquals(9L, fixture.listener.expiredEpoch);
        assertEquals(1L, fixture.listener.expiredGeneration);
    }

    @Test public void newGenerationInvalidatesOldDeadlineAndIncompatibleModeFailsImmediately() {
        Fixture fixture = new Fixture();
        fixture.monitor.request(4L, PhysicalRefreshPolicy.HZ_120, mode(2, 120_000), true);
        fixture.clock.now = 100L;
        fixture.monitor.request(5L, PhysicalRefreshPolicy.HZ_60, mode(1, 60_000), false);
        fixture.clock.now = 1_500L;
        fixture.safety.runDue();
        assertEquals(0, fixture.listener.expiredCount);

        fixture.platform.current = platformMode(1, 60f);
        fixture.clock.now = 2_000L;
        fixture.monitor.request(6L, PhysicalRefreshPolicy.HZ_120, mode(2, 120_000), true);
        assertEquals(1, fixture.listener.expiredCount);
        assertEquals(6L, fixture.listener.expiredEpoch);
    }

    @Test public void fixedMismatchBecomesPersistentAtThreeSecondsButFollowSystemDoesNot() {
        Fixture fixture = new Fixture();
        fixture.platform.current = platformMode(1, 60f);
        fixture.monitor.request(1L, PhysicalRefreshPolicy.HZ_120, mode(2, 120_000), false);
        fixture.clock.now = 2_999L;
        fixture.monitor.onDisplayChanged();
        assertFalse(fixture.listener.persistentMismatch);
        fixture.clock.now = 3_000L;
        fixture.monitor.onDisplayChanged();
        assertTrue(fixture.listener.persistentMismatch);

        Fixture followSystem = new Fixture();
        followSystem.monitor.request(1L, PhysicalRefreshPolicy.FOLLOW_SYSTEM, null, false);
        followSystem.clock.now = 4_000L;
        followSystem.monitor.onDisplayChanged();
        assertFalse(followSystem.listener.persistentMismatch);
    }

    @Test public void timeRollbackPublishesUnknown() {
        Fixture fixture = new Fixture();
        fixture.clock.now = 100L;
        fixture.monitor.request(1L, PhysicalRefreshPolicy.HZ_120, mode(2, 120_000), false);
        fixture.clock.now = 99L;
        fixture.monitor.onDisplayChanged();
        assertEquals(1, fixture.listener.unknownCount);
    }

    private static DisplayModeCapability mode(int id, int milliHz) {
        return new DisplayModeCapability(id, 2340, 1080, milliHz);
    }

    private static DisplayPlatformFacade.Mode platformMode(int id, float hz) {
        return new DisplayPlatformFacade.Mode(id, 2340, 1080, hz);
    }

    private static final class Fixture {
        final FakeClock clock = new FakeClock();
        final FakeScheduler poll = new FakeScheduler(clock);
        final FakeScheduler safety = new FakeScheduler(clock);
        final FakePlatform platform = new FakePlatform();
        final RecordingListener listener = new RecordingListener();
        final DisplayStatusMonitor monitor = new DisplayStatusMonitor(platform, clock::now,
                poll, safety, listener);
    }

    private static final class FakeClock { long now; long now() { return now; } }

    private static final class FakePlatform implements DisplayPlatformFacade {
        Mode current = platformMode(2, 120f);
        boolean fail;
        @Override public Mode currentMode() {
            if (fail) throw new IllegalStateException("read failed");
            return current;
        }
        @Override public List<Mode> supportedModes() { return Collections.singletonList(current); }
    }

    private static final class FakeScheduler implements DisplayStatusMonitor.Scheduler {
        private final FakeClock clock;
        private final List<Task> tasks = new ArrayList<>();
        FakeScheduler(FakeClock clock) { this.clock = clock; }
        @Override public DisplayStatusMonitor.Cancellable schedule(Runnable runnable, long delayMs) {
            Task task = new Task(runnable, clock.now + delayMs);
            tasks.add(task);
            return () -> task.cancelled = true;
        }
        int pending() {
            int count = 0;
            for (Task task : tasks) if (!task.cancelled) count++;
            return count;
        }
        void runDue() {
            List<Task> copy = new ArrayList<>(tasks);
            for (Task task : copy) if (!task.cancelled && task.dueAt <= clock.now) {
                task.cancelled = true;
                task.runnable.run();
            }
        }
        private static final class Task {
            final Runnable runnable; final long dueAt; boolean cancelled;
            Task(Runnable runnable, long dueAt) { this.runnable = runnable; this.dueAt = dueAt; }
        }
    }

    private static final class RecordingListener implements DisplayStatusMonitor.Listener {
        int unknownCount;
        int expiredCount;
        long expiredEpoch;
        long expiredGeneration;
        boolean persistentMismatch;
        @Override public void onObservation(DisplayObservation observation) { }
        @Override public void onUnknown(long generation, long observedAtElapsedMs) {
            unknownCount++;
        }
        @Override public void onMotionLeaseExpired(long surfaceEpoch, long generation) {
            expiredCount++;
            expiredEpoch = surfaceEpoch;
            expiredGeneration = generation;
        }
        @Override public void onPersistentPolicyMismatch(long generation) {
            persistentMismatch = true;
        }
    }
}
