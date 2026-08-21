package com.flynes.emu.video;

import android.app.Activity;
import android.os.Build;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

public final class DisplayModeController {
    public enum ApplyResult { APPLIED, FALLBACK_AUTO }

    private DisplayModeController() { }

    public static ApplyResult apply(Activity activity, Surface surface,
                                    RefreshMode requested, float sourceFps) {
        Display display = activity.getWindowManager().getDefaultDisplay();
        Display.Mode current = display.getMode();
        Display.Mode[] supported = display.getSupportedModes();
        DisplayCandidate[] candidates = new DisplayCandidate[supported.length];
        for (int i = 0; i < supported.length; i++) {
            Display.Mode mode = supported[i];
            candidates[i] = new DisplayCandidate(mode.getModeId(), mode.getPhysicalWidth(),
                    mode.getPhysicalHeight(), mode.getRefreshRate());
        }

        int selectedId = DisplayModeSelector.select(candidates,
                current.getPhysicalWidth(), current.getPhysicalHeight(), requested);
        WindowManager.LayoutParams attributes = activity.getWindow().getAttributes();
        attributes.preferredDisplayModeId = selectedId;
        activity.getWindow().setAttributes(attributes);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                && surface != null && surface.isValid()) {
            surface.setFrameRate(sourceFps, Surface.FRAME_RATE_COMPATIBILITY_FIXED_SOURCE);
        }
        return selectedId == 0 ? ApplyResult.FALLBACK_AUTO : ApplyResult.APPLIED;
    }
}
