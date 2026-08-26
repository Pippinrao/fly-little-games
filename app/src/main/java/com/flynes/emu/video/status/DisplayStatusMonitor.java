package com.flynes.emu.video.status;

import com.flynes.emu.video.platform.DisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.DisplayObservation;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;

import java.util.Objects;
import java.util.function.LongSupplier;

/**
 * Monitors system-reported display state. A display sample is never described as physical-scan
 * evidence. Poll and safety scheduling are deliberately separate so a blocked read cannot keep
 * Motion output alive past its lease.
 */
public final class DisplayStatusMonitor implements AutoCloseable {
    public static final long MOTION_POLL_MS = 500L;
    public static final long MOTION_LEASE_MS = 1_500L;
    private static final long PERSISTENT_MISMATCH_MS = 3_000L;

    public interface Cancellable { void cancel(); }
    public interface Scheduler { Cancellable schedule(Runnable runnable, long delayMs); }
    public interface Listener {
        void onObservation(DisplayObservation observation);
        void onUnknown(long generation, long observedAtElapsedMs);
        void onMotionLeaseExpired(long surfaceEpoch, long generation);
        void onPersistentPolicyMismatch(long generation);
    }

    public static final class Snapshot {
        private final DisplayObservation observation;
        private final StatusFreshness freshness;
        Snapshot(DisplayObservation observation, StatusFreshness freshness) {
            this.observation = observation;
            this.freshness = freshness;
        }
        public DisplayObservation observation() { return observation; }
        public StatusFreshness freshness() { return freshness; }
    }

    private final DisplayPlatformFacade platform;
    private final LongSupplier clock;
    private final Scheduler pollScheduler;
    private final Scheduler safetyScheduler;
    private final Listener listener;
    private long generation;
    private long surfaceEpoch;
    private PhysicalRefreshPolicy requestedPolicy = PhysicalRefreshPolicy.FOLLOW_SYSTEM;
    private DisplayModeCapability requestedMode;
    private DisplayObservation observation;
    private long lastClockMs = Long.MIN_VALUE;
    private long stableSinceMs;
    private Long mismatchSinceMs;
    private boolean mismatchPublished;
    private boolean motionRequired;
    private boolean closed;
    private Cancellable pollTask = () -> { };
    private Cancellable safetyTask = () -> { };

    public DisplayStatusMonitor(DisplayPlatformFacade platform, LongSupplier clock,
                                Scheduler pollScheduler, Scheduler safetyScheduler,
                                Listener listener) {
        this.platform = Objects.requireNonNull(platform, "platform");
        this.clock = Objects.requireNonNull(clock, "clock");
        this.pollScheduler = Objects.requireNonNull(pollScheduler, "pollScheduler");
        this.safetyScheduler = Objects.requireNonNull(safetyScheduler, "safetyScheduler");
        this.listener = Objects.requireNonNull(listener, "listener");
    }

    public synchronized long request(long newSurfaceEpoch, PhysicalRefreshPolicy policy,
                                     DisplayModeCapability mode, boolean needsMotionLease) {
        if (closed) throw new IllegalStateException("monitor is closed");
        generation = Math.addExact(generation, 1L);
        surfaceEpoch = newSurfaceEpoch;
        requestedPolicy = Objects.requireNonNull(policy, "policy");
        requestedMode = mode;
        motionRequired = needsMotionLease;
        observation = null;
        stableSinceMs = 0L;
        mismatchSinceMs = null;
        mismatchPublished = false;
        pollTask.cancel();
        safetyTask.cancel();
        sampleLocked();
        schedulePollLocked();
        return generation;
    }

    public synchronized void onDisplayChanged() {
        if (!closed) sampleLocked();
    }

    public synchronized void invalidate() {
        if (!closed) publishUnknownLocked(safeNow());
    }

    public synchronized Snapshot snapshotAt(long nowElapsedRealtimeMs) {
        if (observation == null) return new Snapshot(null, StatusFreshness.UNKNOWN);
        return new Snapshot(observation, StatusFreshness.at(
                observation.observedAtElapsedRealtimeMs(), nowElapsedRealtimeMs,
                MOTION_LEASE_MS));
    }

    @Override public synchronized void close() {
        if (closed) return;
        closed = true;
        pollTask.cancel();
        safetyTask.cancel();
        publishUnknownLocked(safeNow());
    }

    private void sampleLocked() {
        long now = safeNow();
        if (lastClockMs != Long.MIN_VALUE && now < lastClockMs) {
            publishUnknownLocked(now);
            return;
        }
        lastClockMs = now;
        final DisplayModeCapability active;
        try {
            active = convert(platform.currentMode());
        } catch (RuntimeException failure) {
            publishUnknownLocked(now);
            return;
        }
        boolean sameActive = observation != null
                && active.equals(observation.systemReportedActiveMode());
        if (!sameActive) stableSinceMs = now;
        long stableFor = Math.max(0L, now - stableSinceMs);
        observation = new DisplayObservation(generation, requestedPolicy, requestedMode,
                active, now, stableFor);
        listener.onObservation(observation);
        updateMismatchLocked(active, now);
        if (motionRequired) {
            if (!isMotionCompatible(active)) {
                safetyTask.cancel();
                listener.onMotionLeaseExpired(surfaceEpoch, generation);
            } else {
                replaceSafetyDeadlineLocked(surfaceEpoch, generation);
            }
        }
    }

    private void updateMismatchLocked(DisplayModeCapability active, long now) {
        boolean mismatch = requestedPolicy != PhysicalRefreshPolicy.FOLLOW_SYSTEM
                && requestedMode != null
                && Math.abs(requestedMode.refreshMilliHz() - active.refreshMilliHz()) > 1_000;
        if (!mismatch) {
            mismatchSinceMs = null;
            mismatchPublished = false;
            return;
        }
        if (mismatchSinceMs == null) mismatchSinceMs = now;
        if (!mismatchPublished && now - mismatchSinceMs >= PERSISTENT_MISMATCH_MS) {
            mismatchPublished = true;
            listener.onPersistentPolicyMismatch(generation);
        }
    }

    private boolean isMotionCompatible(DisplayModeCapability active) {
        return requestedMode != null && requestedMode.equals(active)
                && active.refreshMilliHz() >= 119_000 && active.refreshMilliHz() <= 121_000;
    }

    private void replaceSafetyDeadlineLocked(long epoch, long requestGeneration) {
        safetyTask.cancel();
        safetyTask = safetyScheduler.schedule(() -> {
            synchronized (DisplayStatusMonitor.this) {
                if (closed || epoch != surfaceEpoch || requestGeneration != generation
                        || !motionRequired) return;
                listener.onMotionLeaseExpired(epoch, requestGeneration);
            }
        }, MOTION_LEASE_MS);
    }

    private void schedulePollLocked() {
        if (!motionRequired || closed) return;
        long expectedGeneration = generation;
        pollTask = pollScheduler.schedule(() -> {
            synchronized (DisplayStatusMonitor.this) {
                if (closed || expectedGeneration != generation || !motionRequired) return;
                sampleLocked();
                schedulePollLocked();
            }
        }, MOTION_POLL_MS);
    }

    private void publishUnknownLocked(long now) {
        observation = null;
        stableSinceMs = 0L;
        mismatchSinceMs = null;
        mismatchPublished = false;
        safetyTask.cancel();
        listener.onUnknown(generation, now);
    }

    private long safeNow() {
        try {
            return clock.getAsLong();
        } catch (RuntimeException failure) {
            return lastClockMs == Long.MIN_VALUE ? 0L : lastClockMs;
        }
    }

    private static DisplayModeCapability convert(DisplayPlatformFacade.Mode mode) {
        if (mode == null) throw new IllegalStateException("current display mode unavailable");
        long milliHz = Math.round((double) mode.refreshHz() * 1_000.0d);
        if (milliHz <= 0L || milliHz > Integer.MAX_VALUE)
            throw new IllegalStateException("invalid current refresh rate");
        return new DisplayModeCapability(mode.modeId(), mode.width(), mode.height(),
                (int) milliHz);
    }
}
