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
        return resolve(requested, aspectMode, capabilities, build, constraints,
                null, null, null, nowElapsedRealtimeMs);
    }

    public EffectiveVideoConfig resolve(VideoPreferences requested, AspectMode aspectMode,
                                        DisplayCapabilities capabilities,
                                        BuildAlgorithmAvailability build,
                                        RuntimeConstraints constraints,
                                        DeviceIdentity deviceIdentity,
                                        DeviceQualityProfile qualityProfile,
                                        TrustedEvidenceClockSnapshot evidenceClock,
                                        long nowElapsedRealtimeMs) {
        Objects.requireNonNull(requested, "requested");
        Objects.requireNonNull(aspectMode, "aspectMode");
        Objects.requireNonNull(capabilities, "capabilities");
        Objects.requireNonNull(build, "build");
        Objects.requireNonNull(constraints, "constraints");

        CustomVideoSettings axes = axesFor(requested);
        if ((requested.preset() == VideoQualityPreset.BALANCED
                || requested.preset() == VideoQualityPreset.EXTREME)
                && deviceIdentity != null && qualityProfile != null && evidenceClock != null
                && build.mmpxIncluded() && capabilities.gl().supportsMmpx2x()) {
            axes = new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                    SpatialMode.MMPX, PostEffect.NONE);
        }
        PhysicalRefreshPolicy refresh = axes.refreshPolicy();
        TemporalMode temporal = axes.temporalMode();
        SpatialMode spatial = axes.spatialMode();
        PostEffect post = axes.postEffect();
        boolean motionRequestedByUser = temporal == TemporalMode.MOTION_INTERPOLATION;
        boolean motionSessionActive = constraints.currentTemporalState()
                == RuntimeTemporalState.MOTION_COMPENSATING;
        List<FallbackReason> fallbacks = new ArrayList<>();
        SessionSafetyDirective safety = SessionSafetyDirective.NONE;
        String advancedConfigurationId = null;
        RuntimeTemporalState runtimeTemporalState = RuntimeTemporalState.IMMEDIATE_NATIVE;
        int videoDelayFrames = 0;
        float audioDelayMs = 0.0f;
        boolean transitionalOutput = false;

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
        }

        boolean certificationRequired = isAdvanced(refresh, temporal, spatial);
        if (certificationRequired) {
            FallbackReason capabilityFailure = capabilityFailure(
                    temporal, spatial, build, capabilities.gl());
            Qualification qualification = null;
            DisplayModeCapability qualificationMode = requestedMode == null
                    ? resolvedMode : requestedMode;
            if (capabilityFailure == null && qualificationMode != null
                    && deviceIdentity != null && qualityProfile != null && evidenceClock != null) {
                VideoConfigurationKey candidateKey = new VideoConfigurationKey(
                        constraints.sourceTiming(), qualificationMode.width(),
                        qualificationMode.height(), qualificationMode.modeId(),
                        qualificationMode.refreshMilliHz(), temporal, spatial, post, aspectMode);
                qualification = qualify(candidateKey, deviceIdentity, qualityProfile,
                        build, evidenceClock);
                if (qualification.invalidReason == EvidenceInvalidReason.NONE) {
                    advancedConfigurationId = qualification.certificate.configurationId();
                }
            }
            if (advancedConfigurationId == null) {
                temporal = TemporalMode.NATIVE;
                spatial = SpatialMode.SHARP_BILINEAR;
                post = PostEffect.NONE;
                if (refresh == PhysicalRefreshPolicy.HZ_120) {
                    refresh = PhysicalRefreshPolicy.HZ_60;
                    requestedMode = selectMode(refresh, capabilities,
                            constraints.displayObservation());
                    resolvedMode = requestedMode == null ? activeMode : requestedMode;
                    effectiveRefresh = requestedMode == null && activeMode != null
                            ? policyFor(activeMode.refreshMilliHz()) : refresh;
                }
                fallbacks.add(capabilityFailure != null ? capabilityFailure
                        : fallbackFor(qualification == null
                        ? EvidenceInvalidReason.CERTIFICATE_MISSING
                        : qualification.invalidReason));
            }
        }
        if (requestedMode != null && activeMode != null
                && persistentModeMismatch(refresh, requestedMode,
                constraints.displayObservation())) {
            addFallbackOnce(fallbacks, FallbackReason.DISPLAY_MODE_REJECTED);
            resolvedMode = activeMode;
            effectiveRefresh = policyFor(activeMode.refreshMilliHz());
            if (advancedConfigurationId != null) {
                advancedConfigurationId = null;
                temporal = TemporalMode.NATIVE;
                spatial = SpatialMode.SHARP_BILINEAR;
                post = PostEffect.NONE;
            }
        }
        if (!constraints.runtimeFailures().isEmpty()) {
            temporal = TemporalMode.NATIVE;
            spatial = SpatialMode.SHARP_BILINEAR;
            post = PostEffect.NONE;
            advancedConfigurationId = null;
            fallbacks.add(FallbackReason.RUNTIME_FAILURE);
        }

        if (motionRequestedByUser || motionSessionActive) {
            MotionLease lease = motionLease(constraints, requestedMode, nowElapsedRealtimeMs);
            if (motionSessionActive) {
                if (temporal == TemporalMode.MOTION_INTERPOLATION && lease.compatible) {
                    runtimeTemporalState = RuntimeTemporalState.MOTION_COMPENSATING;
                    videoDelayFrames = 1;
                    audioDelayMs = 16.7f;
                } else {
                    temporal = TemporalMode.NATIVE;
                    runtimeTemporalState = RuntimeTemporalState.BUFFERED_NATIVE_HOLD;
                    videoDelayFrames = 1;
                    audioDelayMs = 16.7f;
                    advancedConfigurationId = null;
                    transitionalOutput = true;
                    addFallbackOnce(fallbacks, lease.failureReason);
                }
            } else if (temporal == TemporalMode.MOTION_INTERPOLATION) {
                if (lease.compatible && lease.stableForMs >= 3_000L) {
                    runtimeTemporalState = RuntimeTemporalState.PRIMING;
                    transitionalOutput = true;
                } else {
                    temporal = TemporalMode.NATIVE;
                    if (spatial == SpatialMode.MMPX || spatial == SpatialMode.SCALEFX) {
                        spatial = SpatialMode.SHARP_BILINEAR;
                    }
                    advancedConfigurationId = null;
                    addFallbackOnce(fallbacks, lease.failureReason);
                }
            }
        }

        String id = null;
        VideoConfigurationKey key = null;
        if (resolvedMode != null && !transitionalOutput) {
            key = new VideoConfigurationKey(constraints.sourceTiming(), resolvedMode.width(),
                    resolvedMode.height(), resolvedMode.modeId(), resolvedMode.refreshMilliHz(),
                    temporal, spatial, post, aspectMode);
            if (advancedConfigurationId != null) id = advancedConfigurationId;
            else if (spatial == SpatialMode.NEAREST || spatial == SpatialMode.SHARP_BILINEAR)
                id = "builtin:" + key.canonicalSha256();
        }
        return new EffectiveVideoConfig(requested, id, key, effectiveRefresh, requestedMode,
                activeMode, temporal, runtimeTemporalState, spatial, post,
                videoDelayFrames, audioDelayMs, safety, fallbacks);
    }

    private static MotionLease motionLease(RuntimeConstraints constraints,
                                           DisplayModeCapability requestedMode,
                                           long nowElapsedRealtimeMs) {
        DisplayObservation observation = constraints.displayObservation();
        if (observation == null || requestedMode == null
                || observation.requestGeneration() != constraints.displayRequestGeneration()
                || observation.requestedPolicy() != PhysicalRefreshPolicy.HZ_120
                || !sameMode(observation.requestedMode(), requestedMode)) {
            return MotionLease.failed(FallbackReason.DISPLAY_OBSERVATION_STALE);
        }
        long age;
        try {
            age = Math.subtractExact(nowElapsedRealtimeMs,
                    observation.observedAtElapsedRealtimeMs());
        } catch (ArithmeticException overflow) {
            return MotionLease.failed(FallbackReason.DISPLAY_OBSERVATION_STALE);
        }
        if (age < 0L || age >= 1_500L) {
            return MotionLease.failed(FallbackReason.DISPLAY_OBSERVATION_STALE);
        }
        DisplayModeCapability active = observation.systemReportedActiveMode();
        if (constraints.sourceTiming() != SourceTiming.NTSC_60_0988 || active == null
                || active.modeId() != requestedMode.modeId()
                || active.width() != requestedMode.width()
                || active.height() != requestedMode.height()
                || active.refreshMilliHz() < 119_000
                || active.refreshMilliHz() > 121_000) {
            return MotionLease.failed(FallbackReason.DISPLAY_MODE_REJECTED);
        }
        return MotionLease.compatible(Math.max(0L, observation.stableForMs()));
    }

    private static boolean sameMode(DisplayModeCapability left, DisplayModeCapability right) {
        return left != null && right != null && left.modeId() == right.modeId()
                && left.width() == right.width() && left.height() == right.height()
                && Math.abs(left.refreshMilliHz() - right.refreshMilliHz())
                <= REFRESH_TOLERANCE_MILLIHZ;
    }

    private static boolean persistentModeMismatch(PhysicalRefreshPolicy requestedPolicy,
                                                  DisplayModeCapability requestedMode,
                                                  DisplayObservation observation) {
        if (requestedPolicy == PhysicalRefreshPolicy.FOLLOW_SYSTEM || observation == null
                || observation.systemReportedActiveMode() == null
                || observation.stableForMs() < 3_000L
                || observation.requestedPolicy() != requestedPolicy
                || !sameMode(observation.requestedMode(), requestedMode)) return false;
        return !sameMode(observation.systemReportedActiveMode(), requestedMode);
    }

    private static void addFallbackOnce(List<FallbackReason> fallbacks,
                                        FallbackReason reason) {
        if (reason != null && !fallbacks.contains(reason)) fallbacks.add(reason);
    }

    private static final class MotionLease {
        final boolean compatible;
        final long stableForMs;
        final FallbackReason failureReason;

        private MotionLease(boolean compatible, long stableForMs,
                            FallbackReason failureReason) {
            this.compatible = compatible;
            this.stableForMs = stableForMs;
            this.failureReason = failureReason;
        }

        static MotionLease compatible(long stableForMs) {
            return new MotionLease(true, stableForMs, null);
        }

        static MotionLease failed(FallbackReason reason) {
            return new MotionLease(false, 0L, reason);
        }
    }

    private static boolean isAdvanced(PhysicalRefreshPolicy refresh,
                                      TemporalMode temporal, SpatialMode spatial) {
        return refresh == PhysicalRefreshPolicy.HZ_120
                || temporal == TemporalMode.MOTION_INTERPOLATION
                || spatial == SpatialMode.MMPX || spatial == SpatialMode.SCALEFX;
    }

    private static FallbackReason capabilityFailure(TemporalMode temporal, SpatialMode spatial,
                                                    BuildAlgorithmAvailability build,
                                                    GlCapabilities gl) {
        if ((spatial == SpatialMode.MMPX && !build.mmpxIncluded())
                || (spatial == SpatialMode.SCALEFX && !build.scaleFxIncluded())
                || (temporal == TemporalMode.MOTION_INTERPOLATION
                && !build.motionCompensationIncluded())) {
            return FallbackReason.BUILD_UNAVAILABLE;
        }
        if (spatial == SpatialMode.MMPX && !gl.supportsMmpx2x()) {
            return FallbackReason.GL_CAPABILITY_UNVERIFIED;
        }
        if (spatial == SpatialMode.SCALEFX && !gl.supportsScaleFx3x()) {
            return FallbackReason.GL_CAPABILITY_UNVERIFIED;
        }
        if (temporal == TemporalMode.MOTION_INTERPOLATION
                && !(gl.supportsEs31Compute() && gl.canCreateOwnedEs31Presenter())) {
            return FallbackReason.GL_CAPABILITY_UNVERIFIED;
        }
        return null;
    }

    private static Qualification qualify(VideoConfigurationKey candidateKey,
                                         DeviceIdentity identity,
                                         DeviceQualityProfile profile,
                                         BuildAlgorithmAvailability build,
                                         TrustedEvidenceClockSnapshot clock) {
        if (!matchesDevice(identity, profile)) {
            return Qualification.invalid(EvidenceInvalidReason.BUILD_OR_DRIVER_MISMATCH);
        }
        CertifiedVideoConfiguration match = null;
        int matches = 0;
        for (CertifiedVideoConfiguration certificate : profile.certifiedConfigurations()) {
            if (candidateKey.equals(certificate.key())) {
                match = certificate;
                matches++;
            }
        }
        if (matches != 1 || match == null) {
            return Qualification.invalid(EvidenceInvalidReason.CERTIFICATE_MISSING);
        }
        if (!profile.buildImplementationHash().equals(match.buildImplementationHash())
                || !algorithmHash(candidateKey, build).equals(
                match.algorithmImplementationHash())) {
            return Qualification.invalid(EvidenceInvalidReason.IMPLEMENTATION_MISMATCH);
        }
        if (!isSha256(match.evidenceManifestSha256())) {
            return Qualification.invalid(EvidenceInvalidReason.MANIFEST_MISMATCH);
        }
        if (!sufficientEvidence(candidateKey, match.evidenceLevel())) {
            return Qualification.invalid(EvidenceInvalidReason.EVIDENCE_LEVEL_INSUFFICIENT);
        }
        EvidenceInvalidReason interval = EvidenceValidityPolicy.evaluate(
                match, Math.max(1L, match.certifiedAtEpochMs()));
        if (interval == EvidenceInvalidReason.INVALID_VALIDITY_RANGE
                || interval == EvidenceInvalidReason.VALIDITY_TOO_LONG) {
            return Qualification.invalid(interval);
        }
        if (clock.persistedState() == PersistedEvidenceClockState.CORRUPT) {
            return Qualification.invalid(EvidenceInvalidReason.CLOCK_STATE_CORRUPT);
        }
        if (hasTombstone(profile.profileId(), match, clock.persistedAnchor())) {
            return Qualification.invalid(EvidenceInvalidReason.EVIDENCE_TOMBSTONED);
        }
        if (!hasCurrentBootAnchor(clock)) {
            return Qualification.invalid(EvidenceInvalidReason.TIME_BOOTSTRAP_REQUIRED);
        }
        if (clock.trust() != EvidenceClockTrust.TRUSTED) {
            return Qualification.invalid(EvidenceInvalidReason.TIME_UNTRUSTED);
        }
        EvidenceInvalidReason validity = EvidenceValidityPolicy.evaluate(
                match, clock.evaluatedAtEpochMs());
        return validity == EvidenceInvalidReason.NONE
                ? Qualification.valid(match) : Qualification.invalid(validity);
    }

    private static boolean matchesDevice(DeviceIdentity identity, DeviceQualityProfile profile) {
        return identity.manufacturer().equals(profile.manufacturer())
                && identity.model().equals(profile.model())
                && identity.buildFingerprint().equals(profile.buildFingerprint())
                && identity.gpuVendor().equals(profile.gpuVendor())
                && identity.gpuRenderer().equals(profile.gpuRenderer())
                && identity.gpuVersion().equals(profile.gpuVersion())
                && identity.driverFingerprint().equals(profile.driverFingerprint());
    }

    private static String algorithmHash(VideoConfigurationKey key,
                                        BuildAlgorithmAvailability build) {
        String spatialHash = key.spatialMode() == SpatialMode.MMPX
                ? build.mmpxImplementationHash()
                : key.spatialMode() == SpatialMode.SCALEFX
                ? build.scaleFxImplementationHash() : "";
        if (key.temporalMode() != TemporalMode.MOTION_INTERPOLATION) return spatialHash;
        return spatialHash.isEmpty() ? build.motionImplementationHash()
                : build.motionImplementationHash() + "+" + spatialHash;
    }

    private static boolean sufficientEvidence(VideoConfigurationKey key, EvidenceLevel level) {
        boolean requiresDeviceLab = key.temporalMode() == TemporalMode.MOTION_INTERPOLATION
                || key.spatialMode() == SpatialMode.SCALEFX;
        return !requiresDeviceLab || level == EvidenceLevel.DEVICE_LAB;
    }

    private static boolean isSha256(String value) {
        return value != null && value.matches("[0-9a-fA-F]{64}");
    }

    private static boolean hasCurrentBootAnchor(TrustedEvidenceClockSnapshot clock) {
        return clock.persistedState() == PersistedEvidenceClockState.COMPLETE
                && clock.persistedAnchor() != null && clock.currentBootSession() != null
                && clock.currentBootSession().canonicalIdentitySha256().equals(
                clock.persistedAnchor().bootSessionIdentitySha256());
    }

    private static boolean hasTombstone(String profileId,
                                        CertifiedVideoConfiguration certificate,
                                        PersistedEvidenceClockAnchor anchor) {
        if (anchor == null) return false;
        for (EvidenceExpiryTombstone tombstone : anchor.expiryTombstones()) {
            if (profileId.equals(tombstone.profileId())
                    && certificate.configurationId().equals(tombstone.configurationId())
                    && certificate.evidenceManifestSha256().equals(
                    tombstone.evidenceManifestSha256())
                    && certificate.validUntilEpochMs() == tombstone.validUntilEpochMs()) {
                return true;
            }
        }
        return false;
    }

    private static FallbackReason fallbackFor(EvidenceInvalidReason reason) {
        switch (reason) {
            case TIME_BOOTSTRAP_REQUIRED:
                return FallbackReason.TIME_BOOTSTRAP_REQUIRED;
            case CLOCK_STATE_CORRUPT:
            case TIME_UNTRUSTED:
            case FUTURE_ISSUED:
                return FallbackReason.TIME_UNTRUSTED;
            case EVIDENCE_TOMBSTONED:
            case EVIDENCE_EXPIRED:
                return FallbackReason.EVIDENCE_EXPIRED;
            default:
                return FallbackReason.CONFIGURATION_UNVERIFIED;
        }
    }

    private static final class Qualification {
        final CertifiedVideoConfiguration certificate;
        final EvidenceInvalidReason invalidReason;

        private Qualification(CertifiedVideoConfiguration certificate,
                              EvidenceInvalidReason invalidReason) {
            this.certificate = certificate;
            this.invalidReason = invalidReason;
        }

        static Qualification valid(CertifiedVideoConfiguration certificate) {
            return new Qualification(certificate, EvidenceInvalidReason.NONE);
        }

        static Qualification invalid(EvidenceInvalidReason reason) {
            return new Qualification(null, reason);
        }
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
