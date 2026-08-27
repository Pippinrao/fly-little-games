package com.flynes.emu.video.audio;

/** Defines when a paused Surface recovery may consume the prior running intent. */
public final class SurfaceRecoveryIntentPolicy {
    private SurfaceRecoveryIntentPolicy() { }

    public static boolean completes(boolean recoveryPending, boolean safePacingOwner,
                                    long attemptedEpoch, long currentEpoch) {
        return recoveryPending && safePacingOwner && attemptedEpoch > 0L
                && attemptedEpoch == currentEpoch;
    }
}
