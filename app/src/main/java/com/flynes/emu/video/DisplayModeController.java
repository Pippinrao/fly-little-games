package com.flynes.emu.video;

import android.app.Activity;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.status.DisplayStatusMonitor;

public final class DisplayModeController {
    public enum ApplyResult { APPLIED, FALLBACK_AUTO }

    private DisplayModeController() { }

    public static ApplyResult followSystem(Activity activity, Surface surface, float sourceFps) {
        return followSystem(activity, surface, sourceFps, null, 0L, false);
    }

    public static ApplyResult followSystem(Activity activity, Surface surface, float sourceFps,
                                           DisplayStatusMonitor monitor, long surfaceEpoch,
                                           boolean motionRequired) {
        WindowManager.LayoutParams attributes = activity.getWindow().getAttributes();
        attributes.preferredDisplayModeId = 0;
        activity.getWindow().setAttributes(attributes);
        // The game Surface has exactly one frame-rate writer: native PresentationCoordinator.
        if (monitor != null) monitor.request(surfaceEpoch,
                PhysicalRefreshPolicy.FOLLOW_SYSTEM, null, motionRequired);
        return ApplyResult.FALLBACK_AUTO;
    }

    public static ApplyResult apply(Activity activity, Surface surface,
                                    RefreshMode requested, float sourceFps) {
        return apply(activity, surface, requested, sourceFps, null, 0L, false);
    }

    public static ApplyResult apply(Activity activity, Surface surface,
                                    RefreshMode requested, float sourceFps,
                                    DisplayStatusMonitor monitor, long surfaceEpoch,
                                    boolean motionRequired) {
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
        DisplayCandidate selected = null;
        for (DisplayCandidate candidate : candidates) if (candidate.modeId() == selectedId) {
            selected = candidate; break;
        }
        WindowManager.LayoutParams attributes = activity.getWindow().getAttributes();
        attributes.preferredDisplayModeId = selectedId;
        activity.getWindow().setAttributes(attributes);

        // Window mode selection stays in Java; the game Surface vote belongs to native code.
        if (monitor != null) {
            DisplayModeCapability requestedMode = selected == null ? null
                    : new DisplayModeCapability(selected.modeId(), selected.width(),
                    selected.height(), Math.round(selected.refreshRate() * 1_000f));
            monitor.request(surfaceEpoch, policy(requested), requestedMode, motionRequired);
        }
        return selectedId == 0 ? ApplyResult.FALLBACK_AUTO : ApplyResult.APPLIED;
    }

    private static PhysicalRefreshPolicy policy(RefreshMode mode) {
        if (mode == RefreshMode.HZ_60) return PhysicalRefreshPolicy.HZ_60;
        if (mode == RefreshMode.HZ_90) return PhysicalRefreshPolicy.HZ_90;
        if (mode == RefreshMode.HZ_120) return PhysicalRefreshPolicy.HZ_120;
        return PhysicalRefreshPolicy.LEGACY_AUTO_INTEGER_MULTIPLE;
    }

    static int frameRateCompatibility() {
        return Surface.FRAME_RATE_COMPATIBILITY_DEFAULT;
    }

    static String settledFallbackReason(float requestedHz, float actualHz, String initialReason) {
        if (requestedHz > 0f && Math.abs(requestedHz - actualHz) > 0.6f) {
            return "SYSTEM_OR_DEVICE_FALLBACK";
        }
        return initialReason == null ? "" : initialReason;
    }
}
