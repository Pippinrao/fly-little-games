package com.flynes.emu.settings;

import java.util.Objects;

/** Java mirror of fly_settings_snapshot numeric/string fields. */
public record FlySettingsSnapshot(
        int aspectMode,
        int videoQualityPreset,
        int customRefreshPolicy,
        int customTemporalMode,
        int customSpatialMode,
        int customPostEffect,
        int adaptiveProtection,
        int layoutPreset,
        int directionMode,
        float buttonScale,
        float verticalOffset,
        float controlOpacity,
        float joystickScale,
        float deadZone,
        int hapticLevel,
        int distinctAbHaptics,
        int audioEnabled,
        int audioFocusPolicy,
        int autosaveEnabled,
        String localeTag,
        String lastPlayedId) {
    public FlySettingsSnapshot {
        localeTag = Objects.requireNonNull(localeTag, "locale tag");
        lastPlayedId = lastPlayedId == null ? "" : lastPlayedId;
    }
}
