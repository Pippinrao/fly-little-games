package com.flynes.emu.video.quality;

import java.util.Objects;

public final class DeviceIdentity {
    private final String manufacturer;
    private final String model;
    private final String buildFingerprint;
    private final String gpuVendor;
    private final String gpuRenderer;
    private final String gpuVersion;
    private final String driverFingerprint;

    public DeviceIdentity(String manufacturer, String model, String buildFingerprint,
                          String gpuVendor, String gpuRenderer, String gpuVersion,
                          String driverFingerprint) {
        this.manufacturer = required(manufacturer, "manufacturer");
        this.model = required(model, "model");
        this.buildFingerprint = required(buildFingerprint, "buildFingerprint");
        this.gpuVendor = required(gpuVendor, "gpuVendor");
        this.gpuRenderer = required(gpuRenderer, "gpuRenderer");
        this.gpuVersion = required(gpuVersion, "gpuVersion");
        this.driverFingerprint = required(driverFingerprint, "driverFingerprint");
    }

    private static String required(String value, String name) {
        String result = Objects.requireNonNull(value, name);
        if (result.trim().isEmpty()) throw new IllegalArgumentException(name + " is blank");
        return result;
    }

    public String manufacturer() { return manufacturer; }
    public String model() { return model; }
    public String buildFingerprint() { return buildFingerprint; }
    public String gpuVendor() { return gpuVendor; }
    public String gpuRenderer() { return gpuRenderer; }
    public String gpuVersion() { return gpuVersion; }
    public String driverFingerprint() { return driverFingerprint; }
}
