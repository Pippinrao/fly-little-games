package com.flynes.emu.settings;

import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/** Maps AppSettings onto frozen C ABI enumerations (values start at 1, not Java ordinals). */
public final class FlySettingsMapper {
    public static final int ASPECT_FOUR_BY_THREE = 1;
    public static final int ASPECT_SQUARE_PIXELS = 2;
    public static final int ASPECT_INTEGER_SCALE = 3;
    public static final int VIDEO_QUALITY_POWER_SAVER = 1;
    public static final int VIDEO_QUALITY_BALANCED = 2;
    public static final int VIDEO_QUALITY_EXTREME = 3;
    public static final int VIDEO_QUALITY_CUSTOM = 4;
    public static final int REFRESH_FOLLOW_SYSTEM = 1;
    public static final int REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE = 2;
    public static final int REFRESH_HZ_60 = 3;
    public static final int REFRESH_HZ_90 = 4;
    public static final int REFRESH_HZ_120 = 5;
    public static final int TEMPORAL_NATIVE = 1;
    public static final int TEMPORAL_MOTION_INTERPOLATION = 2;
    public static final int SPATIAL_NEAREST = 1;
    public static final int SPATIAL_SHARP_BILINEAR = 2;
    public static final int SPATIAL_MMPX = 3;
    public static final int SPATIAL_SCALEFX = 4;
    public static final int POST_EFFECT_NONE = 1;
    public static final int POST_EFFECT_CRT = 2;
    public static final int LAYOUT_STANDARD_BA = 1;
    public static final int LAYOUT_MIRRORED_AB = 2;
    public static final int DIRECTION_JOYSTICK = 1;
    public static final int DIRECTION_FIXED_JOYSTICK = 2;
    public static final int DIRECTION_DPAD = 3;
    public static final int HAPTIC_OFF = 1;
    public static final int HAPTIC_LIGHT = 2;
    public static final int HAPTIC_STANDARD = 3;
    public static final int HAPTIC_STRONG = 4;
    public static final int AUDIO_FOCUS_PAUSE = 1;
    public static final int AUDIO_FOCUS_DUCK = 2;
    public static final int AUDIO_FOCUS_IGNORE = 3;

    private FlySettingsMapper() {
    }

    public static FlySettingsSnapshot toNative(AppSettings settings) {
        Objects.requireNonNull(settings, "settings");
        VideoPreferences video = settings.videoPreferences();
        CustomVideoSettings custom = video.custom();
        return new FlySettingsSnapshot(
                toNative(settings.aspectMode()),
                toNative(video.preset()),
                toNative(custom.refreshPolicy()),
                toNative(custom.temporalMode()),
                toNative(custom.spatialMode()),
                toNative(custom.postEffect()),
                video.adaptiveProtection() ? 1 : 0,
                toNative(settings.layoutPreset()),
                toNative(settings.directionControlMode()),
                settings.buttonScale(),
                settings.verticalOffset(),
                settings.controlOpacity(),
                settings.joystickScale(),
                settings.deadZone(),
                toNative(settings.hapticLevel()),
                settings.distinctABHaptics() ? 1 : 0,
                settings.audioEnabled() ? 1 : 0,
                toNative(settings.audioFocusPolicy()),
                settings.autosaveEnabled() ? 1 : 0,
                settings.localeTag(),
                settings.lastPlayedRomId());
    }

    public static AppSettings fromNative(FlySettingsSnapshot snapshot) {
        Objects.requireNonNull(snapshot, "snapshot");
        VideoPreferences video = new VideoPreferences(
                fromNativePreset(snapshot.videoQualityPreset()),
                new CustomVideoSettings(
                        fromNativeRefresh(snapshot.customRefreshPolicy()),
                        fromNativeTemporal(snapshot.customTemporalMode()),
                        fromNativeSpatial(snapshot.customSpatialMode()),
                        fromNativePost(snapshot.customPostEffect())),
                snapshot.adaptiveProtection() != 0);
        return AppSettings.defaults().toBuilder()
                .aspectMode(fromNativeAspect(snapshot.aspectMode()))
                .videoPreferences(video)
                .layoutPreset(fromNativeLayout(snapshot.layoutPreset()))
                .directionControlMode(fromNativeDirection(snapshot.directionMode()))
                .buttonScale(snapshot.buttonScale())
                .verticalOffset(snapshot.verticalOffset())
                .controlOpacity(snapshot.controlOpacity())
                .joystickScale(snapshot.joystickScale())
                .deadZone(snapshot.deadZone())
                .hapticLevel(fromNativeHaptic(snapshot.hapticLevel()))
                .distinctABHaptics(snapshot.distinctAbHaptics() != 0)
                .audioEnabled(snapshot.audioEnabled() != 0)
                .audioFocusPolicy(fromNativeAudioFocus(snapshot.audioFocusPolicy()))
                .localeTag(snapshot.localeTag())
                .autosaveEnabled(snapshot.autosaveEnabled() != 0)
                .lastPlayedRomId(snapshot.lastPlayedId())
                .build();
    }

    public static Map<String, Object> toSchemaFourMap(AppSettings settings, int generation) {
        Objects.requireNonNull(settings, "settings");
        VideoPreferences video = settings.videoPreferences();
        CustomVideoSettings custom = video.custom();
        LinkedHashMap<String, Object> raw = new LinkedHashMap<>();
        raw.put(SettingsKeys.ASPECT, settings.aspectMode().name());
        raw.put(SettingsKeys.VIDEO_QUALITY_PRESET, video.preset().name());
        raw.put(SettingsKeys.CUSTOM_REFRESH_POLICY, custom.refreshPolicy().name());
        raw.put(SettingsKeys.CUSTOM_TEMPORAL_MODE, custom.temporalMode().name());
        raw.put(SettingsKeys.CUSTOM_SPATIAL_MODE, custom.spatialMode().name());
        raw.put(SettingsKeys.CUSTOM_POST_EFFECT, custom.postEffect().name());
        raw.put(SettingsKeys.ADAPTIVE_PROTECTION, video.adaptiveProtection());
        raw.put(SettingsKeys.LAYOUT, settings.layoutPreset().name());
        raw.put(SettingsKeys.DIRECTION_MODE, settings.directionControlMode().name());
        raw.put(SettingsKeys.BUTTON_SCALE, Float.toString(settings.buttonScale()));
        raw.put(SettingsKeys.VERTICAL_OFFSET, Float.toString(settings.verticalOffset()));
        raw.put(SettingsKeys.CONTROL_OPACITY, Float.toString(settings.controlOpacity()));
        raw.put(SettingsKeys.JOYSTICK_SCALE, Float.toString(settings.joystickScale()));
        raw.put(SettingsKeys.DEAD_ZONE, Float.toString(settings.deadZone()));
        raw.put(SettingsKeys.HAPTIC_LEVEL, settings.hapticLevel().name());
        raw.put(SettingsKeys.DISTINCT_AB, settings.distinctABHaptics());
        raw.put(SettingsKeys.AUDIO_ENABLED, settings.audioEnabled());
        raw.put(SettingsKeys.AUDIO_FOCUS, settings.audioFocusPolicy().name());
        raw.put(SettingsKeys.LOCALE_TAG, settings.localeTag());
        raw.put(SettingsKeys.AUTOSAVE, settings.autosaveEnabled());
        raw.put(SettingsKeys.LAST_ROM, settings.lastPlayedRomId());
        raw.put(SettingsKeys.SCHEMA, SettingsRepository.SCHEMA_VERSION);
        raw.put(SettingsKeys.COMMIT_GENERATION, generation);
        return raw;
    }

    public static AppSettings fromSchemaFourBatch(SettingsBatch batch) {
        Objects.requireNonNull(batch, "batch");
        Map<String, String> strings = batch.strings();
        Map<String, Boolean> booleans = batch.booleans();
        VideoPreferences video = new VideoPreferences(
                VideoQualityPreset.valueOf(strings.get(SettingsKeys.VIDEO_QUALITY_PRESET)),
                new CustomVideoSettings(
                        PhysicalRefreshPolicy.valueOf(strings.get(SettingsKeys.CUSTOM_REFRESH_POLICY)),
                        TemporalMode.valueOf(strings.get(SettingsKeys.CUSTOM_TEMPORAL_MODE)),
                        SpatialMode.valueOf(strings.get(SettingsKeys.CUSTOM_SPATIAL_MODE)),
                        PostEffect.valueOf(strings.get(SettingsKeys.CUSTOM_POST_EFFECT))),
                booleans.get(SettingsKeys.ADAPTIVE_PROTECTION));
        return AppSettings.defaults().toBuilder()
                .aspectMode(AspectMode.valueOf(strings.get(SettingsKeys.ASPECT)))
                .videoPreferences(video)
                .layoutPreset(LayoutPreset.valueOf(strings.get(SettingsKeys.LAYOUT)))
                .directionControlMode(DirectionControlMode.valueOf(
                        strings.get(SettingsKeys.DIRECTION_MODE)))
                .buttonScale(Float.parseFloat(strings.get(SettingsKeys.BUTTON_SCALE)))
                .verticalOffset(Float.parseFloat(strings.get(SettingsKeys.VERTICAL_OFFSET)))
                .controlOpacity(Float.parseFloat(strings.get(SettingsKeys.CONTROL_OPACITY)))
                .joystickScale(Float.parseFloat(strings.get(SettingsKeys.JOYSTICK_SCALE)))
                .deadZone(Float.parseFloat(strings.get(SettingsKeys.DEAD_ZONE)))
                .hapticLevel(HapticLevel.valueOf(strings.get(SettingsKeys.HAPTIC_LEVEL)))
                .distinctABHaptics(booleans.get(SettingsKeys.DISTINCT_AB))
                .audioEnabled(booleans.get(SettingsKeys.AUDIO_ENABLED))
                .audioFocusPolicy(AudioFocusPolicy.valueOf(strings.get(SettingsKeys.AUDIO_FOCUS)))
                .localeTag(strings.get(SettingsKeys.LOCALE_TAG))
                .autosaveEnabled(booleans.get(SettingsKeys.AUTOSAVE))
                .lastPlayedRomId(strings.get(SettingsKeys.LAST_ROM))
                .build();
    }

    private static int toNative(AspectMode value) {
        return switch (value) {
            case FOUR_BY_THREE -> ASPECT_FOUR_BY_THREE;
            case SQUARE_PIXELS -> ASPECT_SQUARE_PIXELS;
            case INTEGER_SCALE -> ASPECT_INTEGER_SCALE;
        };
    }

    private static int toNative(VideoQualityPreset value) {
        return switch (value) {
            case POWER_SAVER -> VIDEO_QUALITY_POWER_SAVER;
            case BALANCED -> VIDEO_QUALITY_BALANCED;
            case EXTREME -> VIDEO_QUALITY_EXTREME;
            case CUSTOM -> VIDEO_QUALITY_CUSTOM;
        };
    }

    private static int toNative(PhysicalRefreshPolicy value) {
        return switch (value) {
            case FOLLOW_SYSTEM -> REFRESH_FOLLOW_SYSTEM;
            case LEGACY_AUTO_INTEGER_MULTIPLE -> REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE;
            case HZ_60 -> REFRESH_HZ_60;
            case HZ_90 -> REFRESH_HZ_90;
            case HZ_120 -> REFRESH_HZ_120;
        };
    }

    private static int toNative(TemporalMode value) {
        return value == TemporalMode.MOTION_INTERPOLATION
                ? TEMPORAL_MOTION_INTERPOLATION : TEMPORAL_NATIVE;
    }

    private static int toNative(SpatialMode value) {
        return switch (value) {
            case NEAREST -> SPATIAL_NEAREST;
            case SHARP_BILINEAR -> SPATIAL_SHARP_BILINEAR;
            case MMPX -> SPATIAL_MMPX;
            case SCALEFX -> SPATIAL_SCALEFX;
        };
    }

    private static int toNative(PostEffect value) {
        return value == PostEffect.CRT ? POST_EFFECT_CRT : POST_EFFECT_NONE;
    }

    private static int toNative(LayoutPreset value) {
        return value == LayoutPreset.MIRRORED_AB ? LAYOUT_MIRRORED_AB : LAYOUT_STANDARD_BA;
    }

    private static int toNative(DirectionControlMode value) {
        return switch (value) {
            case JOYSTICK -> DIRECTION_JOYSTICK;
            case FIXED_JOYSTICK -> DIRECTION_FIXED_JOYSTICK;
            case DPAD -> DIRECTION_DPAD;
        };
    }

    private static int toNative(HapticLevel value) {
        return switch (value) {
            case OFF -> HAPTIC_OFF;
            case LIGHT -> HAPTIC_LIGHT;
            case STANDARD -> HAPTIC_STANDARD;
            case STRONG -> HAPTIC_STRONG;
        };
    }

    private static int toNative(AudioFocusPolicy value) {
        return switch (value) {
            case PAUSE -> AUDIO_FOCUS_PAUSE;
            case DUCK -> AUDIO_FOCUS_DUCK;
            case IGNORE -> AUDIO_FOCUS_IGNORE;
        };
    }

    private static AspectMode fromNativeAspect(int value) {
        return switch (value) {
            case ASPECT_SQUARE_PIXELS -> AspectMode.SQUARE_PIXELS;
            case ASPECT_INTEGER_SCALE -> AspectMode.INTEGER_SCALE;
            default -> AspectMode.FOUR_BY_THREE;
        };
    }

    private static VideoQualityPreset fromNativePreset(int value) {
        return switch (value) {
            case VIDEO_QUALITY_POWER_SAVER -> VideoQualityPreset.POWER_SAVER;
            case VIDEO_QUALITY_EXTREME -> VideoQualityPreset.EXTREME;
            case VIDEO_QUALITY_CUSTOM -> VideoQualityPreset.CUSTOM;
            default -> VideoQualityPreset.BALANCED;
        };
    }

    private static PhysicalRefreshPolicy fromNativeRefresh(int value) {
        return switch (value) {
            case REFRESH_FOLLOW_SYSTEM -> PhysicalRefreshPolicy.FOLLOW_SYSTEM;
            case REFRESH_LEGACY_AUTO_INTEGER_MULTIPLE ->
                    PhysicalRefreshPolicy.LEGACY_AUTO_INTEGER_MULTIPLE;
            case REFRESH_HZ_90 -> PhysicalRefreshPolicy.HZ_90;
            case REFRESH_HZ_120 -> PhysicalRefreshPolicy.HZ_120;
            default -> PhysicalRefreshPolicy.HZ_60;
        };
    }

    private static TemporalMode fromNativeTemporal(int value) {
        return value == TEMPORAL_MOTION_INTERPOLATION
                ? TemporalMode.MOTION_INTERPOLATION : TemporalMode.NATIVE;
    }

    private static SpatialMode fromNativeSpatial(int value) {
        return switch (value) {
            case SPATIAL_NEAREST -> SpatialMode.NEAREST;
            case SPATIAL_MMPX -> SpatialMode.MMPX;
            case SPATIAL_SCALEFX -> SpatialMode.SCALEFX;
            default -> SpatialMode.SHARP_BILINEAR;
        };
    }

    private static PostEffect fromNativePost(int value) {
        return value == POST_EFFECT_CRT ? PostEffect.CRT : PostEffect.NONE;
    }

    private static LayoutPreset fromNativeLayout(int value) {
        return value == LAYOUT_MIRRORED_AB ? LayoutPreset.MIRRORED_AB : LayoutPreset.STANDARD_BA;
    }

    private static DirectionControlMode fromNativeDirection(int value) {
        return switch (value) {
            case DIRECTION_JOYSTICK -> DirectionControlMode.JOYSTICK;
            case DIRECTION_DPAD -> DirectionControlMode.DPAD;
            default -> DirectionControlMode.FIXED_JOYSTICK;
        };
    }

    private static HapticLevel fromNativeHaptic(int value) {
        return switch (value) {
            case HAPTIC_OFF -> HapticLevel.OFF;
            case HAPTIC_STANDARD -> HapticLevel.STANDARD;
            case HAPTIC_STRONG -> HapticLevel.STRONG;
            default -> HapticLevel.LIGHT;
        };
    }

    private static AudioFocusPolicy fromNativeAudioFocus(int value) {
        return switch (value) {
            case AUDIO_FOCUS_DUCK -> AudioFocusPolicy.DUCK;
            case AUDIO_FOCUS_IGNORE -> AudioFocusPolicy.IGNORE;
            default -> AudioFocusPolicy.PAUSE;
        };
    }
}
