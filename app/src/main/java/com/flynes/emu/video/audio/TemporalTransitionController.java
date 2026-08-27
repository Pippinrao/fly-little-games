package com.flynes.emu.video.audio;

import com.flynes.emu.video.quality.RuntimeTemporalState;

/** Complete fail-closed Motion exit and qualified hold-recovery state policy. */
public final class TemporalTransitionController {
    public enum ExitReason {
        DISPLAY_MISMATCH,
        LEASE_EXPIRED,
        ARTIFACT_RATIO,
        SOURCE_SEQUENCE_GAP,
        STAGING_OVERFLOW,
        SWAPPY_FAILURE,
        ES31_FAILURE,
        MOTION_SHADER_FAILURE,
        CONTEXT_FAILURE,
        SURFACE_LOSS,
        SYSTEM_BATTERY_SAVER,
        LOW_BATTERY,
        ADAPTIVE_EDGE,
        SEVERE_THERMAL,
        CRITICAL_THERMAL,
        USER_PAUSED
    }

    public enum QueueAction { PRESERVE, FREEZE, CLEAR_ATOMICALLY }

    public static final class Decision {
        private final RuntimeTemporalState target;
        private final QueueAction queueAction;
        private Decision(RuntimeTemporalState target, QueueAction queueAction) {
            this.target = target;
            this.queueAction = queueAction;
        }
        public RuntimeTemporalState target() { return target; }
        public QueueAction queueAction() { return queueAction; }
    }

    private static final long HOLD_COOLDOWN_NS = 120_000_000_000L;
    private static final long SHADOW_WINDOW_NS = 2_000_000_000L;
    private static final long STABLE_LEASE_NS = 3_000_000_000L;
    private RuntimeTemporalState state = RuntimeTemporalState.IMMEDIATE_NATIVE;
    private long holdStartedNs;
    private long shadowStartedNs = Long.MIN_VALUE;
    private long shadowQualifiedNs = Long.MIN_VALUE;

    public static Decision onExit(RuntimeTemporalState from, ExitReason reason,
                                  boolean pausedSurfaceRecoveryFailure) {
        if (pausedSurfaceRecoveryFailure
                && from == RuntimeTemporalState.SURFACE_SUSPENDED_HOLD
                && (reason == ExitReason.SURFACE_LOSS || reason == ExitReason.CONTEXT_FAILURE)) {
            return new Decision(RuntimeTemporalState.IMMEDIATE_NATIVE,
                    QueueAction.CLEAR_ATOMICALLY);
        }
        if (reason == ExitReason.SURFACE_LOSS || reason == ExitReason.CONTEXT_FAILURE) {
            return new Decision(RuntimeTemporalState.SURFACE_SUSPENDED_HOLD, QueueAction.FREEZE);
        }
        if (reason == ExitReason.CRITICAL_THERMAL || reason == ExitReason.USER_PAUSED) {
            return new Decision(RuntimeTemporalState.DRAINING, QueueAction.CLEAR_ATOMICALLY);
        }
        if (reason == ExitReason.SEVERE_THERMAL) {
            return new Decision(RuntimeTemporalState.DRAINING, QueueAction.PRESERVE);
        }
        return new Decision(RuntimeTemporalState.BUFFERED_NATIVE_HOLD, QueueAction.PRESERVE);
    }

    public synchronized void enterHold(long nowNs) {
        if (nowNs < 0L) throw new IllegalArgumentException("negative time");
        state = RuntimeTemporalState.BUFFERED_NATIVE_HOLD;
        holdStartedNs = nowNs;
        shadowStartedNs = Long.MIN_VALUE;
        shadowQualifiedNs = Long.MIN_VALUE;
    }

    public synchronized RuntimeTemporalState beginShadowIfCooldownComplete(long nowNs) {
        if (nowNs < 0L) throw new IllegalArgumentException("negative time");
        if (state == RuntimeTemporalState.BUFFERED_NATIVE_HOLD
                && nowNs >= holdStartedNs
                && nowNs - holdStartedNs >= HOLD_COOLDOWN_NS) {
            state = RuntimeTemporalState.PRIMING_SHADOW;
            shadowStartedNs = nowNs;
            shadowQualifiedNs = Long.MIN_VALUE;
        }
        return state;
    }

    public synchronized RuntimeTemporalState observeShadow(
            long nowNs, int adjacentPairCount, double artifactRatio, double peakArtifactRatio,
            boolean sequencesContinuous, boolean gpuBudgetPass, boolean displayLeaseFresh) {
        if (state != RuntimeTemporalState.PRIMING_SHADOW) return state;
        boolean invalidMetric = nowNs < shadowStartedNs || adjacentPairCount < 0
                || !Double.isFinite(artifactRatio) || artifactRatio < 0.0
                || !Double.isFinite(peakArtifactRatio) || peakArtifactRatio < 0.0;
        if (invalidMetric || !sequencesContinuous || !gpuBudgetPass || !displayLeaseFresh
                || peakArtifactRatio > 0.25) {
            enterHold(Math.max(0L, nowNs));
            return state;
        }
        if (nowNs - shadowStartedNs < SHADOW_WINDOW_NS || adjacentPairCount < 120) {
            return state;
        }
        if (artifactRatio >= 0.10) {
            enterHold(nowNs);
            return state;
        }
        if (shadowQualifiedNs == Long.MIN_VALUE) shadowQualifiedNs = nowNs;
        if (nowNs - shadowQualifiedNs >= STABLE_LEASE_NS) {
            state = RuntimeTemporalState.MOTION_COMPENSATING;
        }
        return state;
    }

    public synchronized RuntimeTemporalState state() { return state; }
}
