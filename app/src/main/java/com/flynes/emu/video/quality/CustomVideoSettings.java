package com.flynes.emu.video.quality;

import java.util.Objects;

public final class CustomVideoSettings {
    private final PhysicalRefreshPolicy refreshPolicy;
    private final TemporalMode temporalMode;
    private final SpatialMode spatialMode;
    private final PostEffect postEffect;

    public CustomVideoSettings(PhysicalRefreshPolicy refreshPolicy, TemporalMode temporalMode,
                               SpatialMode spatialMode, PostEffect postEffect) {
        this.refreshPolicy = Objects.requireNonNull(refreshPolicy, "refreshPolicy");
        this.temporalMode = Objects.requireNonNull(temporalMode, "temporalMode");
        this.spatialMode = Objects.requireNonNull(spatialMode, "spatialMode");
        this.postEffect = Objects.requireNonNull(postEffect, "postEffect");
    }

    public static CustomVideoSettings defaults() {
        return new CustomVideoSettings(PhysicalRefreshPolicy.HZ_60, TemporalMode.NATIVE,
                SpatialMode.SHARP_BILINEAR, PostEffect.NONE);
    }
    public PhysicalRefreshPolicy refreshPolicy() { return refreshPolicy; }
    public TemporalMode temporalMode() { return temporalMode; }
    public SpatialMode spatialMode() { return spatialMode; }
    public PostEffect postEffect() { return postEffect; }
    @Override public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof CustomVideoSettings)) return false;
        CustomVideoSettings other = (CustomVideoSettings) object;
        return refreshPolicy == other.refreshPolicy && temporalMode == other.temporalMode
                && spatialMode == other.spatialMode && postEffect == other.postEffect;
    }
    @Override public int hashCode() {
        return Objects.hash(refreshPolicy, temporalMode, spatialMode, postEffect);
    }
}
