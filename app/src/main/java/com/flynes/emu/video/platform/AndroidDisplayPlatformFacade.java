package com.flynes.emu.video.platform;

import android.view.Display;
import android.view.Window;
import android.view.WindowManager;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class AndroidDisplayPlatformFacade implements DisplayPlatformFacade {
    private final Display display;
    private final Window window;

    public AndroidDisplayPlatformFacade(Display display) {
        this.display = Objects.requireNonNull(display, "display");
        this.window = null;
    }

    public AndroidDisplayPlatformFacade(Window window, Display display) {
        this.display = Objects.requireNonNull(display, "display");
        this.window = Objects.requireNonNull(window, "window");
    }

    @Override public Mode currentMode() { return convert(display.getMode()); }

    @Override public List<Mode> supportedModes() {
        List<Mode> result = new ArrayList<>();
        for (Display.Mode mode : display.getSupportedModes()) result.add(convert(mode));
        return Collections.unmodifiableList(result);
    }

    @Override public void setPreferredDisplayModeId(int modeId) {
        if (window == null) throw new IllegalStateException("display facade is read-only");
        WindowManager.LayoutParams attributes = window.getAttributes();
        attributes.preferredDisplayModeId = modeId;
        window.setAttributes(attributes);
    }

    private static Mode convert(Display.Mode mode) {
        return new Mode(mode.getModeId(), mode.getPhysicalWidth(), mode.getPhysicalHeight(),
                mode.getRefreshRate());
    }
}
