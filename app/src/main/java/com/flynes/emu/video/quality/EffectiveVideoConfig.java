package com.flynes.emu.video.quality;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class EffectiveVideoConfig {
    private final VideoPreferences requested;
    private final String resolvedConfigurationId;
    private final VideoConfigurationKey resolvedConfigurationKey;
    private final PhysicalRefreshPolicy effectiveRefresh;
    private final DisplayModeCapability requestedDisplayMode;
    private final DisplayModeCapability systemReportedActiveMode;
    private final TemporalMode effectiveTemporal;
    private final RuntimeTemporalState runtimeTemporalState;
    private final SpatialMode effectiveSpatial;
    private final PostEffect effectivePostEffect;
    private final int videoDelayFrames;
    private final float audioDelayMs;
    private final SessionSafetyDirective sessionSafetyDirective;
    private final List<FallbackReason> fallbacks;

    public EffectiveVideoConfig(VideoPreferences requested, String resolvedConfigurationId,
                                VideoConfigurationKey resolvedConfigurationKey,
                                PhysicalRefreshPolicy effectiveRefresh,
                                DisplayModeCapability requestedDisplayMode,
                                DisplayModeCapability systemReportedActiveMode,
                                TemporalMode effectiveTemporal,
                                RuntimeTemporalState runtimeTemporalState,
                                SpatialMode effectiveSpatial, PostEffect effectivePostEffect,
                                int videoDelayFrames, float audioDelayMs,
                                SessionSafetyDirective sessionSafetyDirective,
                                List<FallbackReason> fallbacks) {
        this.requested = Objects.requireNonNull(requested, "requested");
        if ((resolvedConfigurationId == null) != (resolvedConfigurationKey == null))
            throw new IllegalArgumentException("resolved id and key must be atomic");
        this.resolvedConfigurationId = resolvedConfigurationId;
        this.resolvedConfigurationKey = resolvedConfigurationKey;
        this.effectiveRefresh = Objects.requireNonNull(effectiveRefresh, "effectiveRefresh");
        this.requestedDisplayMode = requestedDisplayMode;
        this.systemReportedActiveMode = systemReportedActiveMode;
        this.effectiveTemporal = Objects.requireNonNull(effectiveTemporal, "effectiveTemporal");
        this.runtimeTemporalState = Objects.requireNonNull(runtimeTemporalState,
                "runtimeTemporalState");
        this.effectiveSpatial = Objects.requireNonNull(effectiveSpatial, "effectiveSpatial");
        this.effectivePostEffect = Objects.requireNonNull(effectivePostEffect, "effectivePostEffect");
        this.videoDelayFrames = videoDelayFrames;
        this.audioDelayMs = audioDelayMs;
        this.sessionSafetyDirective = Objects.requireNonNull(sessionSafetyDirective,
                "sessionSafetyDirective");
        this.fallbacks = Collections.unmodifiableList(new ArrayList<>(fallbacks));
    }
    public VideoPreferences requested() { return requested; }
    public String resolvedConfigurationId() { return resolvedConfigurationId; }
    public VideoConfigurationKey resolvedConfigurationKey() { return resolvedConfigurationKey; }
    public PhysicalRefreshPolicy effectiveRefresh() { return effectiveRefresh; }
    public DisplayModeCapability requestedDisplayMode() { return requestedDisplayMode; }
    public DisplayModeCapability systemReportedActiveMode() { return systemReportedActiveMode; }
    public TemporalMode effectiveTemporal() { return effectiveTemporal; }
    public RuntimeTemporalState runtimeTemporalState() { return runtimeTemporalState; }
    public SpatialMode effectiveSpatial() { return effectiveSpatial; }
    public PostEffect effectivePostEffect() { return effectivePostEffect; }
    public int videoDelayFrames() { return videoDelayFrames; }
    public float audioDelayMs() { return audioDelayMs; }
    public SessionSafetyDirective sessionSafetyDirective() { return sessionSafetyDirective; }
    public List<FallbackReason> fallbacks() { return fallbacks; }
}
