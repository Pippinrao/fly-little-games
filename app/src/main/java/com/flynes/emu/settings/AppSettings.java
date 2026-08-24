package com.flynes.emu.settings;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.video.RefreshMode;

import java.util.Objects;

/** Immutable, validated snapshot of every user-facing emulator setting. */
public final class AppSettings {
    public static final float MIN_BUTTON_SCALE = 0.80f;
    public static final float MAX_BUTTON_SCALE = 1.40f;
    public static final float MIN_VERTICAL_OFFSET = -0.25f;
    public static final float MAX_VERTICAL_OFFSET = 0.25f;
    public static final float MIN_CONTROL_OPACITY = 0.40f;
    public static final float MAX_CONTROL_OPACITY = 1.00f;
    public static final float MIN_JOYSTICK_SCALE = 0.80f;
    public static final float MAX_JOYSTICK_SCALE = 1.40f;
    public static final float MIN_DEAD_ZONE = 0.08f;
    public static final float MAX_DEAD_ZONE = 0.45f;

    private final AspectMode aspectMode;
    private final FilterMode filterMode;
    private final RefreshMode refreshMode;
    private final LayoutPreset layoutPreset;
    private final DirectionControlMode directionControlMode;
    private final float buttonScale;
    private final float verticalOffset;
    private final float controlOpacity;
    private final float joystickScale;
    private final float deadZone;
    private final HapticLevel hapticLevel;
    private final boolean distinctABHaptics;
    private final boolean audioEnabled;
    private final AudioFocusPolicy audioFocusPolicy;
    private final String localeTag;
    private final boolean autosaveEnabled;
    private final String lastPlayedRomId;

    private AppSettings(Builder builder) {
        aspectMode = valueOr(builder.aspectMode, AspectMode.FOUR_BY_THREE);
        filterMode = valueOr(builder.filterMode, FilterMode.EDGE_ENHANCED);
        refreshMode = valueOr(builder.refreshMode, RefreshMode.AUTO);
        layoutPreset = valueOr(builder.layoutPreset, LayoutPreset.STANDARD_BA);
        directionControlMode = valueOr(builder.directionControlMode, DirectionControlMode.JOYSTICK);
        buttonScale = clamp(builder.buttonScale, MIN_BUTTON_SCALE, MAX_BUTTON_SCALE);
        verticalOffset = clamp(builder.verticalOffset, MIN_VERTICAL_OFFSET, MAX_VERTICAL_OFFSET);
        controlOpacity = clamp(builder.controlOpacity, MIN_CONTROL_OPACITY, MAX_CONTROL_OPACITY);
        joystickScale = clamp(builder.joystickScale, MIN_JOYSTICK_SCALE, MAX_JOYSTICK_SCALE);
        deadZone = clamp(builder.deadZone, MIN_DEAD_ZONE, MAX_DEAD_ZONE);
        hapticLevel = valueOr(builder.hapticLevel, HapticLevel.LIGHT);
        distinctABHaptics = builder.distinctABHaptics;
        audioEnabled = builder.audioEnabled;
        audioFocusPolicy = valueOr(builder.audioFocusPolicy, AudioFocusPolicy.PAUSE);
        localeTag = builder.localeTag == null || builder.localeTag.trim().isEmpty()
                ? "system" : builder.localeTag.trim();
        autosaveEnabled = builder.autosaveEnabled;
        lastPlayedRomId = builder.lastPlayedRomId == null ? "" : builder.lastPlayedRomId.trim();
    }

    public static AppSettings defaults() {
        return new Builder().build();
    }

    public Builder toBuilder() {
        return new Builder(this);
    }

    public AspectMode aspectMode() { return aspectMode; }
    public FilterMode filterMode() { return filterMode; }
    public RefreshMode refreshMode() { return refreshMode; }
    public LayoutPreset layoutPreset() { return layoutPreset; }
    public DirectionControlMode directionControlMode() { return directionControlMode; }
    public float buttonScale() { return buttonScale; }
    public float verticalOffset() { return verticalOffset; }
    public float controlOpacity() { return controlOpacity; }
    public float joystickScale() { return joystickScale; }
    public float deadZone() { return deadZone; }
    public HapticLevel hapticLevel() { return hapticLevel; }
    public boolean distinctABHaptics() { return distinctABHaptics; }
    public boolean audioEnabled() { return audioEnabled; }
    public AudioFocusPolicy audioFocusPolicy() { return audioFocusPolicy; }
    public String localeTag() { return localeTag; }
    public boolean autosaveEnabled() { return autosaveEnabled; }
    public String lastPlayedRomId() { return lastPlayedRomId; }

    @Override public boolean equals(Object object) {
        if (this == object) return true;
        if (!(object instanceof AppSettings)) return false;
        AppSettings other = (AppSettings) object;
        return Float.compare(buttonScale, other.buttonScale) == 0
                && Float.compare(verticalOffset, other.verticalOffset) == 0
                && Float.compare(controlOpacity, other.controlOpacity) == 0
                && Float.compare(joystickScale, other.joystickScale) == 0
                && Float.compare(deadZone, other.deadZone) == 0
                && distinctABHaptics == other.distinctABHaptics
                && audioEnabled == other.audioEnabled
                && autosaveEnabled == other.autosaveEnabled
                && aspectMode == other.aspectMode
                && filterMode == other.filterMode
                && refreshMode == other.refreshMode
                && layoutPreset == other.layoutPreset
                && directionControlMode == other.directionControlMode
                && hapticLevel == other.hapticLevel
                && audioFocusPolicy == other.audioFocusPolicy
                && localeTag.equals(other.localeTag)
                && lastPlayedRomId.equals(other.lastPlayedRomId);
    }

    @Override public int hashCode() {
        return Objects.hash(aspectMode, filterMode, refreshMode, layoutPreset, directionControlMode, buttonScale,
                verticalOffset, controlOpacity, joystickScale, deadZone, hapticLevel,
                distinctABHaptics, audioEnabled, audioFocusPolicy, localeTag,
                autosaveEnabled, lastPlayedRomId);
    }

    private static float clamp(float value, float minimum, float maximum) {
        if (Float.isNaN(value)) return minimum;
        return Math.max(minimum, Math.min(maximum, value));
    }

    private static <T> T valueOr(T value, T fallback) {
        return value == null ? fallback : value;
    }

    public static final class Builder {
        private AspectMode aspectMode = AspectMode.FOUR_BY_THREE;
        private FilterMode filterMode = FilterMode.EDGE_ENHANCED;
        private RefreshMode refreshMode = RefreshMode.AUTO;
        private LayoutPreset layoutPreset = LayoutPreset.STANDARD_BA;
        private DirectionControlMode directionControlMode = DirectionControlMode.JOYSTICK;
        private float buttonScale = 1f;
        private float verticalOffset = 0f;
        private float controlOpacity = 0.78f;
        private float joystickScale = 1f;
        private float deadZone = 0.22f;
        private HapticLevel hapticLevel = HapticLevel.LIGHT;
        private boolean distinctABHaptics = true;
        private boolean audioEnabled = true;
        private AudioFocusPolicy audioFocusPolicy = AudioFocusPolicy.PAUSE;
        private String localeTag = "system";
        private boolean autosaveEnabled = true;
        private String lastPlayedRomId = "";

        public Builder() { }

        private Builder(AppSettings settings) {
            aspectMode = settings.aspectMode;
            filterMode = settings.filterMode;
            refreshMode = settings.refreshMode;
            layoutPreset = settings.layoutPreset;
            directionControlMode = settings.directionControlMode;
            buttonScale = settings.buttonScale;
            verticalOffset = settings.verticalOffset;
            controlOpacity = settings.controlOpacity;
            joystickScale = settings.joystickScale;
            deadZone = settings.deadZone;
            hapticLevel = settings.hapticLevel;
            distinctABHaptics = settings.distinctABHaptics;
            audioEnabled = settings.audioEnabled;
            audioFocusPolicy = settings.audioFocusPolicy;
            localeTag = settings.localeTag;
            autosaveEnabled = settings.autosaveEnabled;
            lastPlayedRomId = settings.lastPlayedRomId;
        }

        public Builder aspectMode(AspectMode value) { aspectMode = value; return this; }
        public Builder filterMode(FilterMode value) { filterMode = value; return this; }
        public Builder refreshMode(RefreshMode value) { refreshMode = value; return this; }
        public Builder layoutPreset(LayoutPreset value) { layoutPreset = value; return this; }
        public Builder directionControlMode(DirectionControlMode value) { directionControlMode = value; return this; }
        public Builder buttonScale(float value) { buttonScale = value; return this; }
        public Builder verticalOffset(float value) { verticalOffset = value; return this; }
        public Builder controlOpacity(float value) { controlOpacity = value; return this; }
        public Builder joystickScale(float value) { joystickScale = value; return this; }
        public Builder deadZone(float value) { deadZone = value; return this; }
        public Builder hapticLevel(HapticLevel value) { hapticLevel = value; return this; }
        public Builder distinctABHaptics(boolean value) { distinctABHaptics = value; return this; }
        public Builder audioEnabled(boolean value) { audioEnabled = value; return this; }
        public Builder audioFocusPolicy(AudioFocusPolicy value) { audioFocusPolicy = value; return this; }
        public Builder localeTag(String value) { localeTag = value; return this; }
        public Builder autosaveEnabled(boolean value) { autosaveEnabled = value; return this; }
        public Builder lastPlayedRomId(String value) { lastPlayedRomId = value; return this; }

        public AppSettings build() { return new AppSettings(this); }
    }
}
