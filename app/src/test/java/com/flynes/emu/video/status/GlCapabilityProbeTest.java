package com.flynes.emu.video.status;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.video.quality.GlCapabilities;

import org.junit.Test;

import java.util.Collections;

public final class GlCapabilityProbeTest {
    @Test public void publishesUnknownThenKnownAndDestroysBothOwnedContexts() {
        FakeBackend backend = new FakeBackend();
        GlCapabilityProbe probe = new GlCapabilityProbe(backend);
        assertEquals(GlCapabilityProbe.State.UNKNOWN, probe.snapshot().state());

        GlCapabilityProbe.Snapshot result = probe.probe();

        assertEquals(GlCapabilityProbe.State.KNOWN, result.state());
        assertTrue(result.capabilities().supportsEs31Compute());
        assertTrue(result.capabilities().canCreateOwnedEs31Presenter());
        assertTrue(backend.es2Closed);
        assertTrue(backend.es31Attempted);
        assertTrue(backend.es31Destroyed);
    }

    @Test public void failedProbeRemainsUnknownAndStillDestroysEs2Session() {
        FakeBackend backend = new FakeBackend();
        backend.failRead = true;
        GlCapabilityProbe probe = new GlCapabilityProbe(backend);

        assertEquals(GlCapabilityProbe.State.UNKNOWN, probe.probe().state());
        assertTrue(backend.es2Closed);
        assertFalse(probe.snapshot().capabilities().knownForAdvancedRendering());
    }

    @Test public void contextLossInvalidatesPreviouslyKnownCapabilities() {
        GlCapabilityProbe probe = new GlCapabilityProbe(new FakeBackend());
        assertEquals(GlCapabilityProbe.State.KNOWN, probe.probe().state());
        probe.invalidateForContextLoss();
        assertEquals(GlCapabilityProbe.State.UNKNOWN, probe.snapshot().state());
    }

    private static final class FakeBackend implements GlCapabilityProbe.Backend {
        boolean es2Closed;
        boolean es31Attempted;
        boolean es31Destroyed;
        boolean failRead;

        @Override public GlCapabilityProbe.Es2Session openBoundedEs2Pbuffer(int width, int height) {
            assertEquals(16, width);
            assertEquals(16, height);
            return new GlCapabilityProbe.Es2Session() {
                @Override public GlCapabilityProbe.Es2Properties readProperties() {
                    if (failRead) throw new IllegalStateException("read failed");
                    return new GlCapabilityProbe.Es2Properties("Vendor", "Renderer", "OpenGL ES 2.0",
                            Collections.singleton("GL_EXT_disjoint_timer_query"), 4096, true,
                            true, true, false, true);
                }
                @Override public void close() { es2Closed = true; }
            };
        }

        @Override public GlCapabilityProbe.Es31Attempt attemptSeparateEs31Context() {
            es31Attempted = true;
            return new GlCapabilityProbe.Es31Attempt(true, true,
                    () -> es31Destroyed = true);
        }
    }
}
