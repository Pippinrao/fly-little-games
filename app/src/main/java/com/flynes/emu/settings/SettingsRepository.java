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

import java.util.Map;

/** Versioned settings loader with one-snapshot reads and atomic canonical commits. */
public final class SettingsRepository {
    public static final int SCHEMA_VERSION = 4;
    public static final String PREFERENCES_NAME = "flynes_settings";

    private final SettingsStore store;
    private SettingsBatch pendingBatch;
    private AppSettings pendingSettings;

    public SettingsRepository(SettingsStore store) {
        if (store == null) throw new IllegalArgumentException("store must not be null");
        this.store = store;
    }

    public AppSettings load() {
        Map<String, ?> raw = store.snapshot();
        if (pendingBatch != null) {
            if (store.commit(pendingBatch)) {
                AppSettings result = pendingSettings;
                pendingBatch = null;
                pendingSettings = null;
                return result;
            }
            return pendingSettings;
        }

        Object schemaValue = raw.get(SettingsKeys.SCHEMA);
        if (schemaValue instanceof Integer && (Integer) schemaValue > SCHEMA_VERSION) {
            return AppSettings.defaults();
        }
        if (schemaValue instanceof Integer && (Integer) schemaValue == SCHEMA_VERSION) {
            return loadSchemaFour(raw);
        }
        return migrateLegacy(raw);
    }

    public boolean save(AppSettings settings) {
        if (settings == null) throw new IllegalArgumentException("settings must not be null");
        Map<String, ?> raw = store.snapshot();
        Object schema = raw.get(SettingsKeys.SCHEMA);
        if (schema instanceof Integer && (Integer) schema > SCHEMA_VERSION) return false;
        return commitOrRemember(settings, canonicalBatch(settings, nextGeneration(raw)));
    }

    private AppSettings migrateLegacy(Map<String, ?> raw) {
        boolean hasDisplay = raw.containsKey(SettingsKeys.ASPECT)
                || raw.containsKey(SettingsKeys.LEGACY_FILTER)
                || raw.containsKey(SettingsKeys.LEGACY_REFRESH);
        VideoPreferences video = hasDisplay ? migrateLegacyVideo(raw) : VideoPreferences.defaults();
        AppSettings settings = readCommon(raw, video);
        commitOrRemember(settings, canonicalBatch(settings, nextGeneration(raw)));
        return settings;
    }

    private AppSettings loadSchemaFour(Map<String, ?> raw) {
        boolean repair = false;
        VideoPreferences video;
        try {
            video = new VideoPreferences(
                    requiredEnum(raw, SettingsKeys.VIDEO_QUALITY_PRESET, VideoQualityPreset.class),
                    new CustomVideoSettings(
                            requiredEnum(raw, SettingsKeys.CUSTOM_REFRESH_POLICY,
                                    PhysicalRefreshPolicy.class),
                            requiredEnum(raw, SettingsKeys.CUSTOM_TEMPORAL_MODE, TemporalMode.class),
                            requiredEnum(raw, SettingsKeys.CUSTOM_SPATIAL_MODE, SpatialMode.class),
                            requiredEnum(raw, SettingsKeys.CUSTOM_POST_EFFECT, PostEffect.class)),
                    requiredBoolean(raw, SettingsKeys.ADAPTIVE_PROTECTION));
        } catch (IllegalArgumentException exception) {
            video = VideoPreferences.defaults();
            repair = true;
        }
        AppSettings settings = readCommon(raw, video);
        if (!(raw.get(SettingsKeys.COMMIT_GENERATION) instanceof Integer)
                || !validEnum(raw, SettingsKeys.ASPECT, AspectMode.class)) repair = true;
        if (repair) commitOrRemember(settings, canonicalBatch(settings, nextGeneration(raw)));
        return settings;
    }

    private static VideoPreferences migrateLegacyVideo(Map<String, ?> raw) {
        String filter = string(raw, SettingsKeys.LEGACY_FILTER, "");
        SpatialMode spatial = SpatialMode.SHARP_BILINEAR;
        PostEffect effect = PostEffect.NONE;
        if ("NEAREST".equals(filter)) spatial = SpatialMode.NEAREST;
        else if ("CRT".equals(filter)) effect = PostEffect.CRT;

        String refresh = string(raw, SettingsKeys.LEGACY_REFRESH, "AUTO");
        PhysicalRefreshPolicy policy;
        if ("AUTO".equals(refresh)) policy = PhysicalRefreshPolicy.LEGACY_AUTO_INTEGER_MULTIPLE;
        else if ("HZ_90".equals(refresh)) policy = PhysicalRefreshPolicy.HZ_90;
        else if ("HZ_120".equals(refresh)) policy = PhysicalRefreshPolicy.HZ_120;
        else policy = PhysicalRefreshPolicy.HZ_60;
        return new VideoPreferences(VideoQualityPreset.CUSTOM,
                new CustomVideoSettings(policy, TemporalMode.NATIVE, spatial, effect), true);
    }

    private static AppSettings readCommon(Map<String, ?> raw, VideoPreferences video) {
        AppSettings defaults = AppSettings.defaults();
        return defaults.toBuilder()
                .aspectMode(enumValue(raw, SettingsKeys.ASPECT, AspectMode.class,
                        defaults.aspectMode()))
                .videoPreferences(video)
                .layoutPreset(enumValue(raw, SettingsKeys.LAYOUT, LayoutPreset.class,
                        defaults.layoutPreset()))
                .directionControlMode(enumValue(raw, SettingsKeys.DIRECTION_MODE,
                        DirectionControlMode.class, defaults.directionControlMode()))
                .buttonScale(floatValue(raw, SettingsKeys.BUTTON_SCALE, defaults.buttonScale()))
                .verticalOffset(floatValue(raw, SettingsKeys.VERTICAL_OFFSET,
                        defaults.verticalOffset()))
                .controlOpacity(floatValue(raw, SettingsKeys.CONTROL_OPACITY,
                        defaults.controlOpacity()))
                .joystickScale(floatValue(raw, SettingsKeys.JOYSTICK_SCALE,
                        defaults.joystickScale()))
                .deadZone(floatValue(raw, SettingsKeys.DEAD_ZONE, defaults.deadZone()))
                .hapticLevel(enumValue(raw, SettingsKeys.HAPTIC_LEVEL, HapticLevel.class,
                        defaults.hapticLevel()))
                .distinctABHaptics(booleanValue(raw, SettingsKeys.DISTINCT_AB,
                        defaults.distinctABHaptics()))
                .audioEnabled(booleanValue(raw, SettingsKeys.AUDIO_ENABLED,
                        defaults.audioEnabled()))
                .audioFocusPolicy(enumValue(raw, SettingsKeys.AUDIO_FOCUS,
                        AudioFocusPolicy.class, defaults.audioFocusPolicy()))
                .localeTag(string(raw, SettingsKeys.LOCALE_TAG, defaults.localeTag()))
                .autosaveEnabled(booleanValue(raw, SettingsKeys.AUTOSAVE,
                        defaults.autosaveEnabled()))
                .lastPlayedRomId(string(raw, SettingsKeys.LAST_ROM,
                        defaults.lastPlayedRomId()))
                .build();
    }

    private boolean commitOrRemember(AppSettings settings, SettingsBatch batch) {
        if (store.commit(batch)) {
            pendingSettings = null;
            pendingBatch = null;
            return true;
        }
        pendingSettings = settings;
        pendingBatch = batch;
        return false;
    }

    private static SettingsBatch canonicalBatch(AppSettings settings, int generation) {
        VideoPreferences video = settings.videoPreferences();
        CustomVideoSettings custom = video.custom();
        return new SettingsBatch.Builder()
                .putString(SettingsKeys.ASPECT, settings.aspectMode().name())
                .putString(SettingsKeys.VIDEO_QUALITY_PRESET, video.preset().name())
                .putString(SettingsKeys.CUSTOM_REFRESH_POLICY, custom.refreshPolicy().name())
                .putString(SettingsKeys.CUSTOM_TEMPORAL_MODE, custom.temporalMode().name())
                .putString(SettingsKeys.CUSTOM_SPATIAL_MODE, custom.spatialMode().name())
                .putString(SettingsKeys.CUSTOM_POST_EFFECT, custom.postEffect().name())
                .putBoolean(SettingsKeys.ADAPTIVE_PROTECTION, video.adaptiveProtection())
                .putString(SettingsKeys.LAYOUT, settings.layoutPreset().name())
                .putString(SettingsKeys.DIRECTION_MODE, settings.directionControlMode().name())
                .putString(SettingsKeys.BUTTON_SCALE, Float.toString(settings.buttonScale()))
                .putString(SettingsKeys.VERTICAL_OFFSET, Float.toString(settings.verticalOffset()))
                .putString(SettingsKeys.CONTROL_OPACITY, Float.toString(settings.controlOpacity()))
                .putString(SettingsKeys.JOYSTICK_SCALE, Float.toString(settings.joystickScale()))
                .putString(SettingsKeys.DEAD_ZONE, Float.toString(settings.deadZone()))
                .putString(SettingsKeys.HAPTIC_LEVEL, settings.hapticLevel().name())
                .putBoolean(SettingsKeys.DISTINCT_AB, settings.distinctABHaptics())
                .putBoolean(SettingsKeys.AUDIO_ENABLED, settings.audioEnabled())
                .putString(SettingsKeys.AUDIO_FOCUS, settings.audioFocusPolicy().name())
                .putString(SettingsKeys.LOCALE_TAG, settings.localeTag())
                .putBoolean(SettingsKeys.AUTOSAVE, settings.autosaveEnabled())
                .putString(SettingsKeys.LAST_ROM, settings.lastPlayedRomId())
                .putInt(SettingsKeys.SCHEMA, SCHEMA_VERSION)
                .putInt(SettingsKeys.COMMIT_GENERATION, generation)
                .build();
    }

    private static int nextGeneration(Map<String, ?> raw) {
        Object value = raw.get(SettingsKeys.COMMIT_GENERATION);
        return value instanceof Integer && (Integer) value >= 0 ? (Integer) value + 1 : 1;
    }

    private static String string(Map<String, ?> raw, String key, String fallback) {
        Object value = raw.get(key); return value instanceof String ? (String) value : fallback;
    }
    private static boolean booleanValue(Map<String, ?> raw, String key, boolean fallback) {
        Object value = raw.get(key); return value instanceof Boolean ? (Boolean) value : fallback;
    }
    private static float floatValue(Map<String, ?> raw, String key, float fallback) {
        Object value = raw.get(key);
        if (!(value instanceof String)) return fallback;
        try { return Float.parseFloat((String) value); }
        catch (NumberFormatException ignored) { return fallback; }
    }
    private static <T extends Enum<T>> T enumValue(Map<String, ?> raw, String key,
                                                    Class<T> type, T fallback) {
        Object value = raw.get(key);
        if (!(value instanceof String)) return fallback;
        try { return Enum.valueOf(type, (String) value); }
        catch (IllegalArgumentException ignored) { return fallback; }
    }
    private static <T extends Enum<T>> T requiredEnum(Map<String, ?> raw, String key,
                                                       Class<T> type) {
        Object value = raw.get(key);
        if (!(value instanceof String)) throw new IllegalArgumentException(key);
        try { return Enum.valueOf(type, (String) value); }
        catch (IllegalArgumentException exception) { throw new IllegalArgumentException(key); }
    }
    private static boolean requiredBoolean(Map<String, ?> raw, String key) {
        Object value = raw.get(key);
        if (!(value instanceof Boolean)) throw new IllegalArgumentException(key);
        return (Boolean) value;
    }
    private static <T extends Enum<T>> boolean validEnum(Map<String, ?> raw, String key,
                                                          Class<T> type) {
        Object value = raw.get(key);
        if (!(value instanceof String)) return false;
        try { Enum.valueOf(type, (String) value); return true; }
        catch (IllegalArgumentException exception) { return false; }
    }
}
