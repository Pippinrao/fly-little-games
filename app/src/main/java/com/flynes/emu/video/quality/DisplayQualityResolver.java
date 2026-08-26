package com.flynes.emu.video.quality;

import com.flynes.emu.settings.AspectMode;
import com.flynes.emu.video.power.ThermalBand;

import java.util.ArrayList;
import java.util.List;
import java.util.Objects;

/** Sole producer of a complete stable requested video configuration. */
public final class DisplayQualityResolver {
    private static final int REFRESH_TOLERANCE_MILLIHZ = 1_001;

    public EffectiveVideoConfig resolve(VideoPreferences requested, AspectMode aspectMode,
                                        DisplayCapabilities capabilities,
                                        BuildAlgorithmAvailability build,
                                        RuntimeConstraints constraints,
                                        long nowElapsedRealtimeMs) {
        Objects.requireNonNull(requested, "requested");
        Objects.requireNonNull(aspectMode, "aspectMode");
        Objects.requireNonNull(capabilities, "capabilities");
        Objects.requireNonNull(build, "build");
        Objects.requireNonNull(constraints, "constraints");

        CustomVideoSettings axes = axesFor(requested);
        PhysicalRefreshPolicy refresh = axes.refreshPolicy();
        TemporalMode temporal = axes.temporalMode();
        SpatialMode spatial = axes.spatialMode();
        PostEffect post = axes.postEffect();
        List<FallbackReason> fallbacks = new ArrayList<>();
        SessionSafetyDirective safety = SessionSafetyDirective.NONE;

        if (constraints.thermalBand() == ThermalBand.CRITICAL) {
            safety = SessionSafetyDirective.PAUSE_FOR_CRITICAL_THERMAL;
        }
        if (constraints.thermalBand() == ThermalBand.SEVERE
                || constraints.thermalBand() == ThermalBand.CRITICAL
                || constraints.batteryTemperatureCelsius() >= 42.0f) {
            refresh = PhysicalRefreshPolicy.HZ_60;
            temporal = TemporalMode.NATIVE;
            spatial = SpatialMode.NEAREST;
            post = PostEffect.NONE;
            fallbacks.add(FallbackReason.THERMAL_SAFETY);
        } else if (constraints.systemBatterySaver() || constraints.batteryPercent() <= 15) {
            refresh = PhysicalRefreshPolicy.HZ_60;
            temporal = TemporalMode.NATIVE;
            spatial = SpatialMode.NEAREST;
            post = PostEffect.NONE;
            fallbacks.add(FallbackReason.BATTERY_SAFETY);
        }

        if (spatial == SpatialMode.MMPX) {
            FallbackReason reason = !build.mmpxIncluded() ? FallbackReason.BUILD_UNAVAILABLE
                    : !capabilities.gl().knownForAdvancedRendering()
                    ? FallbackReason.GL_CAPABILITY_UNVERIFIED
                    : FallbackReason.CONFIGURATION_UNVERIFIED;
            spatial = SpatialMode.SHARP_BILINEAR;
            fallbacks.add(reason);
        } else if (spatial == SpatialMode.SCALEFX) {
            FallbackReason reason = !build.scaleFxIncluded() ? FallbackReason.BUILD_UNAVAILABLE
                    : !capabilities.gl().knownForAdvancedRendering()
                    ? FallbackReason.GL_CAPABILITY_UNVERIFIED
                    : FallbackReason.CONFIGURATION_UNVERIFIED;
            spatial = SpatialMode.SHARP_BILINEAR;
            fallbacks.add(reason);
        }
        if (temporal == TemporalMode.MOTION_INTERPOLATION) {
            temporal = TemporalMode.NATIVE;
            fallbacks.add(build.motionCompensationIncluded()
                    ? FallbackReason.CONFIGURATION_UNVERIFIED : FallbackReason.BUILD_UNAVAILABLE);
        }
        if (!constraints.runtimeFailures().isEmpty()) {
            temporal = TemporalMode.NATIVE;
            spatial = SpatialMode.SHARP_BILINEAR;
            post = PostEffect.NONE;
            fallbacks.add(FallbackReason.RUNTIME_FAILURE);
        }

        DisplayModeCapability requestedMode = selectMode(refresh, capabilities,
                constraints.displayObservation());
        DisplayModeCapability activeMode = constraints.displayObservation() == null ? null
                : constraints.displayObservation().systemReportedActiveMode();
        DisplayModeCapability resolvedMode = requestedMode;
        PhysicalRefreshPolicy effectiveRefresh = refresh;
        if (requestedMode == null) {
            fallbacks.add(FallbackReason.DISPLAY_MODE_UNAVAILABLE);
            resolvedMode = activeMode;
            if (activeMode != null) effectiveRefresh = policyFor(activeMode.refreshMilliHz());
        } else if (activeMode != null && refresh != PhysicalRefreshPolicy.FOLLOW_SYSTEM
                && Math.abs(activeMode.refreshMilliHz() - requestedMode.refreshMilliHz())
                > REFRESH_TOLERANCE_MILLIHZ) {
            fallbacks.add(FallbackReason.DISPLAY_MODE_REJECTED);
            resolvedMode = activeMode;
            effectiveRefresh = policyFor(activeMode.refreshMilliHz());
        }

        String id = null;
        VideoConfigurationKey key = null;
        if (resolvedMode != null) {
            key = new VideoConfigurationKey(constraints.sourceTiming(), resolvedMode.width(),
                    resolvedMode.height(), resolvedMode.modeId(), resolvedMode.refreshMilliHz(),
                    temporal, spatial, post, aspectMode);
            if (spatial == SpatialMode.NEAREST || spatial == SpatialMode.SHARP_BILINEAR)
                id = "builtin:" + key.canonicalSha256();
        }
        return new EffectiveVideoConfig(requested, id, key, effectiveRefresh, requestedMode,
                activeMode, temporal, RuntimeTemporalState.IMMEDIATE_NATIVE, spatial, post,
                0, 0.0f, safety, fallbacks);
    }

    private static CustomVideoSettings axesFor(VideoPreferences requested) {
        switch (requested.preset()) {
            case POWER_SAVER:
                return new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                        SpatialMode.NEAREST, PostEffect.NONE);
            case BALANCED:
            case EXTREME:
                return new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                        SpatialMode.SHARP_BILINEAR, PostEffect.NONE);
            case CUSTOM:
            default:
                return requested.custom();
        }
    }

    private static DisplayModeCapability selectMode(PhysicalRefreshPolicy refresh,
                                                     DisplayCapabilities capabilities,
                                                     DisplayObservation observation) {
        if (refresh == PhysicalRefreshPolicy.FOLLOW_SYSTEM)
            return observation == null ? null : observation.systemReportedActiveMode();
        int target = refresh == PhysicalRefreshPolicy.HZ_90 ? 90_000
                : refresh == PhysicalRefreshPolicy.HZ_120 ? 120_000 : 60_000;
        for (DisplayModeCapability mode : capabilities.sameResolutionModes()) {
            if (Math.abs(mode.refreshMilliHz() - target) <= REFRESH_TOLERANCE_MILLIHZ)
                return mode;
        }
        return null;
    }

    private static PhysicalRefreshPolicy policyFor(int milliHz) {
        if (Math.abs(milliHz - 120_000) <= REFRESH_TOLERANCE_MILLIHZ)
            return PhysicalRefreshPolicy.HZ_120;
        if (Math.abs(milliHz - 90_000) <= REFRESH_TOLERANCE_MILLIHZ)
            return PhysicalRefreshPolicy.HZ_90;
        return PhysicalRefreshPolicy.HZ_60;
    }
}
