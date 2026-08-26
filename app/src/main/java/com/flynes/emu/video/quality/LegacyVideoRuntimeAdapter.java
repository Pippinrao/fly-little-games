package com.flynes.emu.video.quality;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.RefreshMode;

import java.util.Objects;

/** Temporary projection into the existing renderer/display APIs. */
public final class LegacyVideoRuntimeAdapter {
    private final VideoPreferences requested;
    private final RefreshMode displayRefresh;
    private final boolean followsSystemRefresh;
    private final FilterMode rendererFilter;
    private final TemporalMode requestedTemporalMode;
    private final RuntimeTemporalState temporalState;

    private LegacyVideoRuntimeAdapter(VideoPreferences requested, RefreshMode displayRefresh,
                                      boolean followsSystemRefresh, FilterMode rendererFilter,
                                      TemporalMode requestedTemporalMode,
                                      RuntimeTemporalState temporalState) {
        this.requested = requested;
        this.displayRefresh = displayRefresh;
        this.followsSystemRefresh = followsSystemRefresh;
        this.rendererFilter = rendererFilter;
        this.requestedTemporalMode = requestedTemporalMode;
        this.temporalState = temporalState;
    }

    public static LegacyVideoRuntimeAdapter project(VideoPreferences requested) {
        Objects.requireNonNull(requested, "requested");
        CustomVideoSettings custom = requested.custom();
        if (requested.preset() == VideoQualityPreset.POWER_SAVER) {
            return new LegacyVideoRuntimeAdapter(requested, RefreshMode.HZ_60, false,
                    FilterMode.NEAREST, TemporalMode.NATIVE, RuntimeTemporalState.APPLIED);
        }
        if (requested.preset() == VideoQualityPreset.BALANCED) {
            return new LegacyVideoRuntimeAdapter(requested, RefreshMode.HZ_60, false,
                    FilterMode.SHARP_BILINEAR, TemporalMode.NATIVE, RuntimeTemporalState.APPLIED);
        }
        if (requested.preset() == VideoQualityPreset.EXTREME) {
            return new LegacyVideoRuntimeAdapter(requested, RefreshMode.HZ_60, false,
                    FilterMode.SHARP_BILINEAR, TemporalMode.MOTION_INTERPOLATION,
                    RuntimeTemporalState.FALLBACK);
        }
        boolean follow = custom.refreshPolicy() == PhysicalRefreshPolicy.FOLLOW_SYSTEM;
        RefreshMode refresh = follow ? null : refresh(custom.refreshPolicy());
        FilterMode filter = custom.postEffect() == PostEffect.CRT
                ? FilterMode.CRT : spatial(custom.spatialMode());
        RuntimeTemporalState temporal = custom.temporalMode() == TemporalMode.NATIVE
                ? RuntimeTemporalState.APPLIED : RuntimeTemporalState.FALLBACK;
        return new LegacyVideoRuntimeAdapter(requested, refresh, follow, filter,
                custom.temporalMode(), temporal);
    }

    private static RefreshMode refresh(PhysicalRefreshPolicy policy) {
        switch (policy) {
            case LEGACY_AUTO_INTEGER_MULTIPLE: return RefreshMode.AUTO;
            case HZ_90: return RefreshMode.HZ_90;
            case HZ_120: return RefreshMode.HZ_120;
            case HZ_60:
            default: return RefreshMode.HZ_60;
        }
    }

    private static FilterMode spatial(SpatialMode mode) {
        return mode == SpatialMode.NEAREST ? FilterMode.NEAREST : FilterMode.SHARP_BILINEAR;
    }

    public VideoPreferences requested() { return requested; }
    public RefreshMode displayRefresh() { return displayRefresh; }
    public boolean followsSystemRefresh() { return followsSystemRefresh; }
    public FilterMode rendererFilter() { return rendererFilter; }
    public TemporalMode requestedTemporalMode() { return requestedTemporalMode; }
    public TemporalMode runtimeTemporalMode() { return TemporalMode.NATIVE; }
    public RuntimeTemporalState temporalState() { return temporalState; }
}
