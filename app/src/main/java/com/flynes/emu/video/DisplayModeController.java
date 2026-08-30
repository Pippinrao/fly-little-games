package com.flynes.emu.video;

import android.view.Surface;

import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.platform.DisplayPlatformFacade;
import com.flynes.emu.video.status.DisplayStatusMonitor;

public final class DisplayModeController {
    public enum ApplyResult { APPLIED, FALLBACK_AUTO, FAILED }

    private DisplayModeController() { }

    public static ApplyResult apply(DisplayPlatformFacade platform,
                                    FrameRateRequestTarget nativeTarget,
                                    RefreshMode requested, float sourceFps,
                                    long surfaceEpoch) {
        return apply(platform, nativeTarget, requested, sourceFps, surfaceEpoch, null, false);
    }

    public static ApplyResult apply(DisplayPlatformFacade platform,
                                    FrameRateRequestTarget nativeTarget,
                                    RefreshMode requested, float sourceFps,
                                    long surfaceEpoch, DisplayStatusMonitor monitor,
                                    boolean motionRequired) {
        DisplayPlatformFacade.Mode current = platform.currentMode();
        java.util.List<DisplayPlatformFacade.Mode> supported = platform.supportedModes();
        DisplayCandidate[] candidates = new DisplayCandidate[supported.size()];
        for (int i = 0; i < supported.size(); i++) {
            DisplayPlatformFacade.Mode mode = supported.get(i);
            candidates[i] = new DisplayCandidate(mode.modeId(), mode.width(),
                    mode.height(), mode.refreshHz());
        }

        int selectedId = DisplayModeSelector.select(candidates,
                current.width(), current.height(), requested);
        DisplayCandidate selected = null;
        for (DisplayCandidate candidate : candidates) if (candidate.modeId() == selectedId) {
            selected = candidate; break;
        }
        if (nativeTarget != null && !nativeTarget.clearFrameRate(surfaceEpoch)) {
            return ApplyResult.FAILED;
        }
        platform.setPreferredDisplayModeId(selectedId);
        if (nativeTarget != null && selectedId != 0
                && !nativeTarget.requestFrameRate(surfaceEpoch, sourceFps)) {
            // A timed-out platform request may complete late; issue a compensating
            // clear before releasing the window preference.
            if (nativeTarget.clearFrameRate(surfaceEpoch)) {
                platform.setPreferredDisplayModeId(0);
                if (monitor != null) {
                    monitor.request(surfaceEpoch, PhysicalRefreshPolicy.FOLLOW_SYSTEM,
                            null, false);
                }
                return ApplyResult.FALLBACK_AUTO;
            }
            return ApplyResult.FAILED;
        }

        // Window mode selection stays in Java; the game Surface vote belongs to native code.
        if (monitor != null) {
            DisplayModeCapability requestedMode = selected == null ? null
                    : new DisplayModeCapability(selected.modeId(), selected.width(),
                    selected.height(), Math.round(selected.refreshRate() * 1_000f));
            monitor.request(surfaceEpoch, policy(requested), requestedMode, motionRequired);
        }
        return selectedId == 0 ? ApplyResult.FALLBACK_AUTO : ApplyResult.APPLIED;
    }

    public static ApplyResult followSystem(DisplayPlatformFacade platform,
                                           FrameRateRequestTarget nativeTarget,
                                           long surfaceEpoch) {
        if (nativeTarget != null && !nativeTarget.clearFrameRate(surfaceEpoch)) {
            return ApplyResult.FAILED;
        }
        platform.setPreferredDisplayModeId(0);
        return ApplyResult.FALLBACK_AUTO;
    }

    public static boolean clear(DisplayPlatformFacade platform,
                                FrameRateRequestTarget nativeTarget,
                                long surfaceEpoch) {
        if (nativeTarget != null && !nativeTarget.clearFrameRate(surfaceEpoch)) return false;
        platform.setPreferredDisplayModeId(0);
        return true;
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
