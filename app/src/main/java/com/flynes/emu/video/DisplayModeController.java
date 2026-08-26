package com.flynes.emu.video;

import android.app.Activity;
import android.os.Build;
import android.view.Display;
import android.view.Surface;
import android.view.WindowManager;

import com.flynes.emu.settings.DisplayStatus;
import com.flynes.emu.settings.DisplayStatusRepository;

public final class DisplayModeController {
    public enum ApplyResult { APPLIED, FALLBACK_AUTO }

    private DisplayModeController() { }

    public static ApplyResult followSystem(Activity activity, Surface surface, float sourceFps) {
        WindowManager.LayoutParams attributes = activity.getWindow().getAttributes();
        attributes.preferredDisplayModeId = 0;
        activity.getWindow().setAttributes(attributes);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                && surface != null && surface.isValid()) {
            surface.setFrameRate(sourceFps, frameRateCompatibility());
        }
        float actual = activity.getWindowManager().getDefaultDisplay().getRefreshRate();
        new DisplayStatusRepository(activity).save(
                new DisplayStatus(actual, actual, "FOLLOW_SYSTEM"));
        return ApplyResult.FALLBACK_AUTO;
    }

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
        float selectedHz = 0f;
        for (DisplayCandidate candidate : candidates) if (candidate.modeId() == selectedId) {
            selectedHz = candidate.refreshRate(); break;
        }
        WindowManager.LayoutParams attributes = activity.getWindow().getAttributes();
        attributes.preferredDisplayModeId = selectedId;
        activity.getWindow().setAttributes(attributes);

        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R
                && surface != null && surface.isValid()) {
            surface.setFrameRate(sourceFps, frameRateCompatibility());
        }
        float requestedHz;
        String reason;
        if (requested == RefreshMode.AUTO) {
            requestedHz = selectedHz > 0f ? selectedHz : current.getRefreshRate();
            reason = selectedHz > 0f ? "AUTO_BEST_SUPPORTED" : "AUTO_SYSTEM_FALLBACK";
        } else {
            requestedHz = requested.targetHz();
            reason = selectedId == 0 ? "MODE_UNAVAILABLE" : "";
        }
        new DisplayStatusRepository(activity).save(new DisplayStatus(
                requestedHz, current.getRefreshRate(), reason));
        final float settledRequestedHz = requestedHz;
        final String initialReason = reason;
        activity.getWindow().getDecorView().postDelayed(() -> {
            Display settledDisplay = activity.getWindowManager().getDefaultDisplay();
            float actualHz = settledDisplay.getRefreshRate();
            String settledReason = settledFallbackReason(
                    settledRequestedHz, actualHz, initialReason);
            new DisplayStatusRepository(activity).save(new DisplayStatus(
                    settledRequestedHz, actualHz, settledReason));
        }, 500L);
        return selectedId == 0 ? ApplyResult.FALLBACK_AUTO : ApplyResult.APPLIED;
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
