package com.flynes.emu.video.status;

import static org.junit.Assert.assertEquals;

import com.flynes.emu.video.platform.DisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayCapabilities;
import com.flynes.emu.video.quality.GlCapabilities;

import java.util.Arrays;

import org.junit.Test;

public final class DisplayCapabilitiesReaderTest {
    @Test public void retainsFullModeIdentityAndFiltersDifferentResolution() {
        DisplayPlatformFacade facade = new FakeFacade(
                mode(7, 2340, 1080, 119.9996f),
                mode(1, 2340, 1080, 60.0004f),
                mode(7, 2340, 1080, 119.9996f),
                mode(9, 1920, 1080, 120.0f));

        DisplayCapabilities capabilities = new DisplayCapabilitiesReader().read(
                facade, GlCapabilities.unknown());

        assertEquals(2340, capabilities.currentWidth());
        assertEquals(1080, capabilities.currentHeight());
        assertEquals(2, capabilities.sameResolutionModes().size());
        assertEquals(60_000, capabilities.sameResolutionModes().get(0).refreshMilliHz());
        assertEquals(120_000, capabilities.sameResolutionModes().get(1).refreshMilliHz());
        assertEquals(7, capabilities.sameResolutionModes().get(1).modeId());
    }

    @Test(expected = IllegalStateException.class)
    public void missingCurrentModeFailsClosed() {
        new DisplayCapabilitiesReader().read(new FakeFacade(null), GlCapabilities.unknown());
    }

    private static DisplayPlatformFacade.Mode mode(int id, int width, int height, float hz) {
        return new DisplayPlatformFacade.Mode(id, width, height, hz);
    }

    private static final class FakeFacade implements DisplayPlatformFacade {
        private final Mode current;
        private final java.util.List<Mode> supported;

        FakeFacade(Mode current, Mode... supported) {
            this.current = current;
            this.supported = Arrays.asList(supported);
        }

        @Override public Mode currentMode() { return current; }
        @Override public java.util.List<Mode> supportedModes() { return supported; }
    }
}
