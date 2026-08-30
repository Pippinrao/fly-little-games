package com.flynes.emu.video.status;

import com.flynes.emu.video.platform.DisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayCapabilities;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.GlCapabilities;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Objects;

public final class DisplayCapabilitiesReader {
    public DisplayCapabilities read(DisplayPlatformFacade facade, GlCapabilities gl) {
        Objects.requireNonNull(facade, "facade");
        DisplayPlatformFacade.Mode current = facade.currentMode();
        if (current == null) throw new IllegalStateException("current display mode unavailable");
        List<DisplayModeCapability> modes = new ArrayList<>();
        for (DisplayPlatformFacade.Mode mode : facade.supportedModes()) {
            if (mode.width() == current.width() && mode.height() == current.height()) {
                modes.add(convert(mode));
            }
        }
        modes.sort(Comparator.comparingInt(DisplayModeCapability::refreshMilliHz)
                .thenComparingInt(DisplayModeCapability::modeId));
        return new DisplayCapabilities(current.width(), current.height(), modes,
                Objects.requireNonNull(gl, "gl"));
    }

    private static DisplayModeCapability convert(DisplayPlatformFacade.Mode mode) {
        long canonical = Math.round((double) mode.refreshHz() * 1_000.0d);
        if (canonical <= 0L || canonical > Integer.MAX_VALUE) {
            throw new IllegalStateException("display refresh is out of range");
        }
        return new DisplayModeCapability(mode.modeId(), mode.width(), mode.height(),
                (int) canonical);
    }
}
