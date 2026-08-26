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
                && build.mmpxIncluded() && capabilities.gl().knownForAdvancedRendering()) {
            axes = new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                    SpatialMode.MMPX, PostEffect.NONE);
        }
        PhysicalRefreshPolicy refresh = axes.refreshPolicy();
        TemporalMode temporal = axes.temporalMode();
        SpatialMode spatial = axes.spatialMode();
        PostEffect post = axes.postEffect();
        List<FallbackReason> fallbacks = new ArrayList<>();
        SessionSafetyDirective safety = SessionSafetyDirective.NONE;
        String advancedConfigurationId = null;

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
        } else if (activeMode != null && refresh != PhysicalRefreshPolicy.FOLLOW_SYSTEM
                && Math.abs(activeMode.refreshMilliHz() - requestedMode.refreshMilliHz())
                > REFRESH_TOLERANCE_MILLIHZ) {
            fallbacks.add(FallbackReason.DISPLAY_MODE_REJECTED);
            resolvedMode = activeMode;
            effectiveRefresh = policyFor(activeMode.refreshMilliHz());
        }

        if (isAdvanced(temporal, spatial)) {
            FallbackReason capabilityFailure = capabilityFailure(
                    temporal, spatial, build, capabilities.gl());
            Qualification qualification = null;
            if (capabilityFailure == null && resolvedMode != null
                    && deviceIdentity != null && qualityProfile != null && evidenceClock != null) {
                VideoConfigurationKey candidateKey = new VideoConfigurationKey(
                        constraints.sourceTiming(), resolvedMode.width(), resolvedMode.height(),
                        resolvedMode.modeId(), resolvedMode.refreshMilliHz(), temporal, spatial,
                        post, aspectMode);
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
                fallbacks.add(capabilityFailure != null ? capabilityFailure
                        : fallbackFor(qualification == null
                        ? EvidenceInvalidReason.CERTIFICATE_MISSING
                        : qualification.invalidReason));
            }
        }
        if (!constraints.runtimeFailures().isEmpty()) {
            temporal = TemporalMode.NATIVE;
            spatial = SpatialMode.SHARP_BILINEAR;
            post = PostEffect.NONE;
            advancedConfigurationId = null;
            fallbacks.add(FallbackReason.RUNTIME_FAILURE);
        }

        String id = null;
        VideoConfigurationKey key = null;
        if (resolvedMode != null) {
            key = new VideoConfigurationKey(constraints.sourceTiming(), resolvedMode.width(),
                    resolvedMode.height(), resolvedMode.modeId(), resolvedMode.refreshMilliHz(),
                    temporal, spatial, post, aspectMode);
            if (advancedConfigurationId != null) id = advancedConfigurationId;
            else if (spatial == SpatialMode.NEAREST || spatial == SpatialMode.SHARP_BILINEAR)
                id = "builtin:" + key.canonicalSha256();
        }
        return new EffectiveVideoConfig(requested, id, key, effectiveRefresh, requestedMode,
                activeMode, temporal, RuntimeTemporalState.IMMEDIATE_NATIVE, spatial, post,
                0, 0.0f, safety, fallbacks);
    }

    private static boolean isAdvanced(TemporalMode temporal, SpatialMode spatial) {
        return temporal == TemporalMode.MOTION_INTERPOLATION
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
        if (!gl.knownForAdvancedRendering()) return FallbackReason.GL_CAPABILITY_UNVERIFIED;
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
