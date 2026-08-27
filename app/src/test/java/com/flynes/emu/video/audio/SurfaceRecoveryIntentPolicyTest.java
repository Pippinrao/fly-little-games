package com.flynes.emu.video.audio;

import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class SurfaceRecoveryIntentPolicyTest {
    @Test public void generationQualificationAbortCannotConsumeResumeIntent() {
        assertFalse(SurfaceRecoveryIntentPolicy.completes(true, false, 9L, 9L));
    }

    @Test public void repeatedSurfaceLossCannotLetOldEpochConsumeNewRecoveryIntent() {
        assertFalse(SurfaceRecoveryIntentPolicy.completes(true, true, 9L, 10L));
    }

    @Test public void sameEpochSafeMotionOrNativeFallbackCanCompleteRecovery() {
        assertTrue(SurfaceRecoveryIntentPolicy.completes(true, true, 10L, 10L));
    }
}
