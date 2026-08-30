package com.flynes.emu.video.audio;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotEquals;

import com.flynes.emu.video.quality.RuntimeTemporalState;

import org.junit.Test;

import java.util.ArrayList;
import java.util.List;

public final class TemporalTransitionControllerTest {
    @Test public void allOrdinaryMotionFailuresEnterBufferedHold() {
        for (TemporalTransitionController.ExitReason reason
                : TemporalTransitionController.ExitReason.values()) {
            if (reason == TemporalTransitionController.ExitReason.SURFACE_LOSS
                    || reason == TemporalTransitionController.ExitReason.CONTEXT_FAILURE
                    || reason == TemporalTransitionController.ExitReason.SEVERE_THERMAL
                    || reason == TemporalTransitionController.ExitReason.CRITICAL_THERMAL
                    || reason == TemporalTransitionController.ExitReason.USER_PAUSED) continue;
            TemporalTransitionController.Decision decision =
                    TemporalTransitionController.onExit(
                            RuntimeTemporalState.MOTION_COMPENSATING, reason, false);
            assertEquals(reason.name(), RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                    decision.target());
            assertEquals(TemporalTransitionController.QueueAction.PRESERVE, decision.queueAction());
        }
    }

    @Test public void surfaceAndContextLossFreezeQueuesUntilPausedRecoveryTransaction() {
        for (TemporalTransitionController.ExitReason reason : new TemporalTransitionController.ExitReason[] {
                TemporalTransitionController.ExitReason.SURFACE_LOSS,
                TemporalTransitionController.ExitReason.CONTEXT_FAILURE}) {
            TemporalTransitionController.Decision active = TemporalTransitionController.onExit(
                    RuntimeTemporalState.PRIMING, reason, false);
            assertEquals(RuntimeTemporalState.SURFACE_SUSPENDED_HOLD, active.target());
            assertEquals(TemporalTransitionController.QueueAction.FREEZE, active.queueAction());

            TemporalTransitionController.Decision pausedRecoveryFailure =
                    TemporalTransitionController.onExit(
                            RuntimeTemporalState.SURFACE_SUSPENDED_HOLD, reason, true);
            assertEquals(RuntimeTemporalState.IMMEDIATE_NATIVE, pausedRecoveryFailure.target());
            assertEquals(TemporalTransitionController.QueueAction.CLEAR_ATOMICALLY,
                    pausedRecoveryFailure.queueAction());
        }
    }

    @Test public void thermalDrainAndNoRunningFailureJumpsStraightToImmediateNative() {
        assertEquals(RuntimeTemporalState.DRAINING,
                TemporalTransitionController.onExit(RuntimeTemporalState.MOTION_COMPENSATING,
                        TemporalTransitionController.ExitReason.SEVERE_THERMAL, false).target());
        assertEquals(TemporalTransitionController.QueueAction.CLEAR_ATOMICALLY,
                TemporalTransitionController.onExit(RuntimeTemporalState.MOTION_COMPENSATING,
                        TemporalTransitionController.ExitReason.CRITICAL_THERMAL, false)
                        .queueAction());
        for (TemporalTransitionController.ExitReason reason
                : TemporalTransitionController.ExitReason.values()) {
            assertNotEquals(RuntimeTemporalState.IMMEDIATE_NATIVE,
                    TemporalTransitionController.onExit(
                            RuntimeTemporalState.MOTION_COMPENSATING, reason, false).target());
        }
    }

    @Test public void holdWaitsFullCooldownBeforeStartingNonPresentedShadow() {
        TemporalTransitionController controller = new TemporalTransitionController();
        controller.enterHold(0L);
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                controller.beginShadowIfCooldownComplete(119_999_999_999L));
        assertEquals(RuntimeTemporalState.PRIMING_SHADOW,
                controller.beginShadowIfCooldownComplete(120_000_000_000L));
    }

    @Test public void recoveryRequiresTwoSecondShadowThenThreeSecondFreshLease() {
        TemporalTransitionController controller = new TemporalTransitionController();
        controller.enterHold(0L);
        controller.beginShadowIfCooldownComplete(120_000_000_000L);
        assertEquals(RuntimeTemporalState.PRIMING_SHADOW,
                controller.observeShadow(121_999_999_999L,
                        119, 0.01, 0.08, true, true, true));
        assertEquals(RuntimeTemporalState.PRIMING_SHADOW,
                controller.observeShadow(122_000_000_000L,
                        120, 0.01, 0.08, true, true, true));
        assertEquals(RuntimeTemporalState.PRIMING_SHADOW,
                controller.observeShadow(124_999_999_999L,
                        300, 0.01, 0.08, true, true, true));
        assertEquals(RuntimeTemporalState.MOTION_COMPENSATING,
                controller.observeShadow(125_000_000_000L,
                        301, 0.01, 0.08, true, true, true));
    }

    @Test public void badShadowResetsFullCooldown() {
        TemporalTransitionController controller = new TemporalTransitionController();
        controller.enterHold(0L);
        controller.beginShadowIfCooldownComplete(120_000_000_000L);

        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                controller.observeShadow(122_000_000_000L,
                        120, 0.10, 0.10, true, true, true));
        assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                controller.beginShadowIfCooldownComplete(241_999_999_999L));
        assertEquals(RuntimeTemporalState.PRIMING_SHADOW,
                controller.beginShadowIfCooldownComplete(242_000_000_000L));
    }

    @Test public void discontinuityGpuFailureOrLeaseLossFailsClosedImmediately() {
        for (int failedAxis = 0; failedAxis < 3; failedAxis++) {
            TemporalTransitionController controller = new TemporalTransitionController();
            controller.enterHold(0L);
            controller.beginShadowIfCooldownComplete(120_000_000_000L);
            boolean continuous = failedAxis != 0;
            boolean gpuPass = failedAxis != 1;
            boolean leaseFresh = failedAxis != 2;
            assertEquals(RuntimeTemporalState.BUFFERED_NATIVE_HOLD,
                    controller.observeShadow(120_100_000_000L,
                            6, 0.01, 0.08, continuous, gpuPass, leaseFresh));
        }
    }

    @Test public void shadowHeartbeatOutlivesTwoSecondWindowAndThreeSecondQualification() {
        TemporalTransitionController controller = new TemporalTransitionController();
        controller.enterHold(0L);
        controller.beginShadowIfCooldownComplete(120_000_000_000L);
        FakeDeadlineScheduler scheduler = new FakeDeadlineScheduler();
        List<DisplayLeaseWatchdog.Token> expired = new ArrayList<>();
        DisplayLeaseWatchdog watchdog = new DisplayLeaseWatchdog(
                1_500_000_000L, scheduler, expired::add);

        for (int heartbeat = 1; heartbeat <= 10; heartbeat++) {
            long nowNs = 120_000_000_000L + heartbeat * 500_000_000L;
            watchdog.arm(7L, 11L, nowNs);
            int pairs = heartbeat * 30;
            assertEquals(heartbeat < 10 ? RuntimeTemporalState.PRIMING_SHADOW
                            : RuntimeTemporalState.MOTION_COMPENSATING,
                    controller.observeShadow(nowNs, pairs, 0.01, 0.08,
                            true, true, true));
        }
        scheduler.runAll();
        assertEquals("replaced 500ms heartbeats must not expire", 1, expired.size());
        assertEquals(7L, expired.get(0).surfaceEpoch());
        assertEquals(11L, expired.get(0).displayGeneration());
    }

    private static final class FakeDeadlineScheduler
            implements DisplayLeaseWatchdog.DeadlineScheduler {
        final List<FakeCancellation> tasks = new ArrayList<>();
        @Override public DisplayLeaseWatchdog.Cancellation schedule(
                Runnable callback, long delayNs) {
            FakeCancellation task = new FakeCancellation(callback);
            tasks.add(task);
            return task;
        }
        void runAll() { for (FakeCancellation task : tasks) task.run(); }
    }

    private static final class FakeCancellation implements DisplayLeaseWatchdog.Cancellation {
        final Runnable callback;
        boolean cancelled;
        FakeCancellation(Runnable callback) { this.callback = callback; }
        @Override public void cancel() { cancelled = true; }
        void run() { if (!cancelled) callback.run(); }
    }
}
