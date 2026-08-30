package com.flynes.emu.video.quality;

import com.flynes.emu.settings.AspectMode;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.Objects;

public final class VideoConfigurationKey {
    private final SourceTiming sourceTiming;
    private final int width;
    private final int height;
    private final int displayModeId;
    private final int refreshMilliHz;
    private final TemporalMode temporalMode;
    private final SpatialMode spatialMode;
    private final PostEffect postEffect;
    private final AspectMode aspectMode;

    public VideoConfigurationKey(SourceTiming sourceTiming, int width, int height,
                                 int displayModeId, int refreshMilliHz,
                                 TemporalMode temporalMode, SpatialMode spatialMode,
                                 PostEffect postEffect, AspectMode aspectMode) {
        this.sourceTiming = Objects.requireNonNull(sourceTiming, "sourceTiming");
        this.width = width;
        this.height = height;
        this.displayModeId = displayModeId;
        this.refreshMilliHz = refreshMilliHz;
        this.temporalMode = Objects.requireNonNull(temporalMode, "temporalMode");
        this.spatialMode = Objects.requireNonNull(spatialMode, "spatialMode");
        this.postEffect = Objects.requireNonNull(postEffect, "postEffect");
        this.aspectMode = Objects.requireNonNull(aspectMode, "aspectMode");
    }
    public SourceTiming sourceTiming() { return sourceTiming; }
    public int width() { return width; }
    public int height() { return height; }
    public int displayModeId() { return displayModeId; }
    public int refreshMilliHz() { return refreshMilliHz; }
    public TemporalMode temporalMode() { return temporalMode; }
    public SpatialMode spatialMode() { return spatialMode; }
    public PostEffect postEffect() { return postEffect; }
    public AspectMode aspectMode() { return aspectMode; }
    public String canonicalSha256() {
        String canonical = sourceTiming.name() + '|' + width + '|' + height + '|'
                + displayModeId + '|' + refreshMilliHz + '|' + temporalMode.name() + '|'
                + spatialMode.name() + '|' + postEffect.name() + '|' + aspectMode.name();
        try {
            byte[] digest = MessageDigest.getInstance("SHA-256")
                    .digest(canonical.getBytes(StandardCharsets.UTF_8));
            StringBuilder hex = new StringBuilder(64);
            for (byte value : digest) hex.append(String.format("%02x", value & 0xff));
            return hex.toString();
        } catch (NoSuchAlgorithmException impossible) {
            throw new IllegalStateException(impossible);
        }
    }
    @Override public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof VideoConfigurationKey)) return false;
        VideoConfigurationKey other = (VideoConfigurationKey) object;
        return width == other.width && height == other.height
                && displayModeId == other.displayModeId && refreshMilliHz == other.refreshMilliHz
                && sourceTiming == other.sourceTiming && temporalMode == other.temporalMode
                && spatialMode == other.spatialMode && postEffect == other.postEffect
                && aspectMode == other.aspectMode;
    }
    @Override public int hashCode() {
        return Objects.hash(sourceTiming, width, height, displayModeId, refreshMilliHz,
                temporalMode, spatialMode, postEffect, aspectMode);
    }
}
