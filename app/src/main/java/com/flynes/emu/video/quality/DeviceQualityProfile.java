package com.flynes.emu.video.quality;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Objects;

public final class DeviceQualityProfile {
    private final String profileId;
    private final String manufacturer;
    private final String model;
    private final String buildFingerprint;
    private final String gpuVendor;
    private final String gpuRenderer;
    private final String gpuVersion;
    private final String driverFingerprint;
    private final String buildImplementationHash;
    private final List<CertifiedVideoConfiguration> certifiedConfigurations;
    private final AdaptiveQualityPolicy adaptivePolicy;

    public DeviceQualityProfile(String profileId, String manufacturer, String model,
                                String buildFingerprint, String gpuVendor, String gpuRenderer,
                                String gpuVersion, String driverFingerprint,
                                String buildImplementationHash,
                                List<CertifiedVideoConfiguration> certifiedConfigurations,
                                AdaptiveQualityPolicy adaptivePolicy) {
        this.profileId = required(profileId, "profileId");
        this.manufacturer = required(manufacturer, "manufacturer");
        this.model = required(model, "model");
        this.buildFingerprint = required(buildFingerprint, "buildFingerprint");
        this.gpuVendor = required(gpuVendor, "gpuVendor");
        this.gpuRenderer = required(gpuRenderer, "gpuRenderer");
        this.gpuVersion = required(gpuVersion, "gpuVersion");
        this.driverFingerprint = required(driverFingerprint, "driverFingerprint");
        this.buildImplementationHash = required(
                buildImplementationHash, "buildImplementationHash");
        this.certifiedConfigurations = Collections.unmodifiableList(new ArrayList<>(
                Objects.requireNonNull(certifiedConfigurations, "certifiedConfigurations")));
        this.adaptivePolicy = Objects.requireNonNull(adaptivePolicy, "adaptivePolicy");
    }

    private static String required(String value, String name) {
        String result = Objects.requireNonNull(value, name);
        if (result.trim().isEmpty()) throw new IllegalArgumentException(name + " is blank");
        return result;
    }

    public String profileId() { return profileId; }
    public String manufacturer() { return manufacturer; }
    public String model() { return model; }
    public String buildFingerprint() { return buildFingerprint; }
    public String gpuVendor() { return gpuVendor; }
    public String gpuRenderer() { return gpuRenderer; }
    public String gpuVersion() { return gpuVersion; }
    public String driverFingerprint() { return driverFingerprint; }
    public String buildImplementationHash() { return buildImplementationHash; }
    public List<CertifiedVideoConfiguration> certifiedConfigurations() {
        return certifiedConfigurations;
    }
    public AdaptiveQualityPolicy adaptivePolicy() { return adaptivePolicy; }
}
