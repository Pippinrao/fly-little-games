package com.flynes.emu.video.platform;

import android.view.Display;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class AndroidDisplayPlatformFacade implements DisplayPlatformFacade {
    private final Display display;

    public AndroidDisplayPlatformFacade(Display display) {
        this.display = Objects.requireNonNull(display, "display");
    }

    @Override public Mode currentMode() { return convert(display.getMode()); }

    @Override public List<Mode> supportedModes() {
        List<Mode> result = new ArrayList<>();
        for (Display.Mode mode : display.getSupportedModes()) result.add(convert(mode));
        return Collections.unmodifiableList(result);
    }

    private static Mode convert(Display.Mode mode) {
        return new Mode(mode.getModeId(), mode.getPhysicalWidth(), mode.getPhysicalHeight(),
                mode.getRefreshRate());
    }
}
