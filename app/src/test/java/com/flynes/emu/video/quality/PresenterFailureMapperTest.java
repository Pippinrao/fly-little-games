package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import org.junit.Test;

public final class PresenterFailureMapperTest {
    @Test public void nativeSpatialFailuresRemainTypedForResolverFeedback() {
        assertNull(PresenterFailureMapper.fromNativeCode(0));
        assertEquals(RuntimeFailure.SHADER, PresenterFailureMapper.fromNativeCode(1));
        assertEquals(RuntimeFailure.FRAMEBUFFER, PresenterFailureMapper.fromNativeCode(2));
        assertEquals(RuntimeFailure.GL, PresenterFailureMapper.fromNativeCode(3));
        assertEquals(RuntimeFailure.CONTEXT, PresenterFailureMapper.fromNativeCode(99));
    }
}
