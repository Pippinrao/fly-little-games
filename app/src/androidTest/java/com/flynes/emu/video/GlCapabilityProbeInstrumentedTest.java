package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.status.GlCapabilityProbe;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class GlCapabilityProbeInstrumentedTest {
    @Test public void boundedOwnedProbeReturnsARealEs2Identity() {
        GlCapabilityProbe probe = new GlCapabilityProbe(new GlCapabilityProbe.AndroidBackend());
        GlCapabilityProbe.Snapshot snapshot = probe.probe();
        assertEquals(GlCapabilityProbe.State.KNOWN, snapshot.state());
        assertTrue(snapshot.capabilities().maxTextureSize() > 0);
        assertTrue(!snapshot.capabilities().vendor().isEmpty());
        assertTrue(!snapshot.capabilities().renderer().isEmpty());
    }
}
