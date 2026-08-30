package com.flynes.emu.video.quality;

import java.util.Objects;

public final class VideoPreferences {
    private final VideoQualityPreset preset;
    private final CustomVideoSettings custom;
    private final boolean adaptiveProtection;

    public VideoPreferences(VideoQualityPreset preset, CustomVideoSettings custom,
                            boolean adaptiveProtection) {
        this.preset = Objects.requireNonNull(preset, "preset");
        this.custom = Objects.requireNonNull(custom, "custom");
        this.adaptiveProtection = adaptiveProtection;
    }
    public static VideoPreferences defaults() {
        return new VideoPreferences(VideoQualityPreset.BALANCED,
                CustomVideoSettings.defaults(), true);
    }
    public VideoQualityPreset preset() { return preset; }
    public CustomVideoSettings custom() { return custom; }
    public boolean adaptiveProtection() { return adaptiveProtection; }
    @Override public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof VideoPreferences)) return false;
        VideoPreferences other = (VideoPreferences) object;
        return adaptiveProtection == other.adaptiveProtection && preset == other.preset
                && custom.equals(other.custom);
    }
    @Override public int hashCode() { return Objects.hash(preset, custom, adaptiveProtection); }
}
