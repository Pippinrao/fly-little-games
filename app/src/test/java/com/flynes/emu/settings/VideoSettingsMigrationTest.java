package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.video.quality.CustomVideoSettings;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.PostEffect;
import com.flynes.emu.video.quality.SpatialMode;
import com.flynes.emu.video.quality.TemporalMode;
import com.flynes.emu.video.quality.VideoPreferences;
import com.flynes.emu.video.quality.VideoQualityPreset;

import org.junit.Test;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;

public final class VideoSettingsMigrationTest {
    @Test public void cleanInstallCreatesBalancedCanonicalSettings() {
        AtomicMemoryStore store = new AtomicMemoryStore();
        AppSettings settings = new SettingsRepository(store).load();

        assertEquals(VideoPreferences.defaults(), settings.videoPreferences());
        assertEquals(Integer.valueOf(4), store.values.get(SettingsKeys.SCHEMA));
        assertEquals("BALANCED", store.values.get(SettingsKeys.VIDEO_QUALITY_PRESET));
        assertEquals("HZ_60", store.values.get(SettingsKeys.CUSTOM_REFRESH_POLICY));
        assertEquals(Integer.valueOf(1), store.values.get(SettingsKeys.COMMIT_GENERATION));
        assertEquals(1, store.commitCount);
    }

    @Test public void migratesEveryEvidencedLegacyFilterAndRefreshSpelling() {
        String[][] filterCases = {
                {"HQ4X", "SHARP_BILINEAR", "NONE"},
                {"EDGE_ENHANCED", "SHARP_BILINEAR", "NONE"},
                {"SMOOTH", "SHARP_BILINEAR", "NONE"},
                {"SHARP_BILINEAR", "SHARP_BILINEAR", "NONE"},
                {"NEAREST", "NEAREST", "NONE"},
                {"CRT", "SHARP_BILINEAR", "CRT"}
        };
        String[][] refreshCases = {
                {"AUTO", "LEGACY_AUTO_INTEGER_MULTIPLE"},
                {"HZ_60", "HZ_60"}, {"HZ_90", "HZ_90"}, {"HZ_120", "HZ_120"}
        };
        for (int schema = 0; schema <= 3; schema++) {
            for (String[] filter : filterCases) for (String[] refresh : refreshCases) {
                AtomicMemoryStore store = new AtomicMemoryStore()
                        .put(SettingsKeys.SCHEMA, schema)
                        .put(SettingsKeys.ASPECT, "SQUARE_PIXELS")
                        .put(SettingsKeys.LEGACY_FILTER, filter[0])
                        .put(SettingsKeys.LEGACY_REFRESH, refresh[0]);
                AppSettings migrated = new SettingsRepository(store).load();
                VideoPreferences video = migrated.videoPreferences();
                assertEquals(VideoQualityPreset.CUSTOM, video.preset());
                assertEquals(SpatialMode.valueOf(filter[1]), video.custom().spatialMode());
                assertEquals(PostEffect.valueOf(filter[2]), video.custom().postEffect());
                assertEquals(PhysicalRefreshPolicy.valueOf(refresh[1]),
                        video.custom().refreshPolicy());
                assertEquals(TemporalMode.NATIVE, video.custom().temporalMode());
                assertEquals(AspectMode.SQUARE_PIXELS, migrated.aspectMode());
                assertEquals(filter[0], store.values.get(SettingsKeys.LEGACY_FILTER));
                assertEquals(refresh[0], store.values.get(SettingsKeys.LEGACY_REFRESH));
            }
        }
    }

    @Test public void markerlessLegacyFootprintMigratesButNoDisplayFootprintUsesBalanced() {
        AtomicMemoryStore legacy = new AtomicMemoryStore()
                .put(SettingsKeys.HAPTIC_LEVEL, "STANDARD")
                .put(SettingsKeys.LEGACY_REFRESH, "HZ_90");
        AppSettings migrated = new SettingsRepository(legacy).load();
        assertEquals(VideoQualityPreset.CUSTOM, migrated.videoPreferences().preset());
        assertEquals(HapticLevel.STANDARD, migrated.hapticLevel());

        AtomicMemoryStore controlsOnly = new AtomicMemoryStore()
                .put(SettingsKeys.HAPTIC_LEVEL, "STANDARD");
        AppSettings balanced = new SettingsRepository(controlsOnly).load();
        assertEquals(VideoQualityPreset.BALANCED, balanced.videoPreferences().preset());
    }

    @Test public void ignoresInterruptedSchemaFourFieldsDuringLegacyMigration() {
        AtomicMemoryStore store = new AtomicMemoryStore()
                .put(SettingsKeys.SCHEMA, 2)
                .put(SettingsKeys.LEGACY_FILTER, "NEAREST")
                .put(SettingsKeys.VIDEO_QUALITY_PRESET, "EXTREME")
                .put(SettingsKeys.CUSTOM_SPATIAL_MODE, "MMPX");
        VideoPreferences video = new SettingsRepository(store).load().videoPreferences();
        assertEquals(VideoQualityPreset.CUSTOM, video.preset());
        assertEquals(SpatialMode.NEAREST, video.custom().spatialMode());
    }

    @Test public void corruptSchemaFourVideoAtomicallyDefaultsAndPreservesOtherSettings() {
        AtomicMemoryStore store = validSchemaFour(VideoQualityPreset.CUSTOM)
                .put(SettingsKeys.ASPECT, "INTEGER_SCALE")
                .put(SettingsKeys.HAPTIC_LEVEL, "STRONG")
                .put(SettingsKeys.CUSTOM_POST_EFFECT, "BROKEN");
        AppSettings loaded = new SettingsRepository(store).load();

        assertEquals(VideoPreferences.defaults(), loaded.videoPreferences());
        assertEquals(AspectMode.INTEGER_SCALE, loaded.aspectMode());
        assertEquals(HapticLevel.STRONG, loaded.hapticLevel());
        assertEquals("BALANCED", store.values.get(SettingsKeys.VIDEO_QUALITY_PRESET));
        assertEquals("NONE", store.values.get(SettingsKeys.CUSTOM_POST_EFFECT));
        assertEquals(1, store.commitCount);
    }

    @Test public void wrongRawTypesAreCorruptAndMissingGenerationRepairsOnlyMetadata() {
        AtomicMemoryStore wrongType = validSchemaFour(VideoQualityPreset.CUSTOM)
                .put(SettingsKeys.ADAPTIVE_PROTECTION, "true");
        assertEquals(VideoPreferences.defaults(),
                new SettingsRepository(wrongType).load().videoPreferences());

        AtomicMemoryStore missingGeneration = validSchemaFour(VideoQualityPreset.CUSTOM);
        missingGeneration.values.remove(SettingsKeys.COMMIT_GENERATION);
        AppSettings loaded = new SettingsRepository(missingGeneration).load();
        assertEquals(VideoQualityPreset.CUSTOM, loaded.videoPreferences().preset());
        assertEquals(SpatialMode.MMPX, loaded.videoPreferences().custom().spatialMode());
        assertEquals(Integer.valueOf(1),
                missingGeneration.values.get(SettingsKeys.COMMIT_GENERATION));
    }

    @Test public void futureSchemaUsesMemoryDefaultsWithoutWriting() {
        AtomicMemoryStore store = validSchemaFour(VideoQualityPreset.EXTREME)
                .put(SettingsKeys.SCHEMA, 99);
        Map<String, Object> before = new LinkedHashMap<>(store.values);

        assertEquals(AppSettings.defaults(), new SettingsRepository(store).load());
        assertEquals(before, store.values);
        assertEquals(0, store.commitCount);
    }

    @Test public void failedCanonicalCommitIsRetriedWithoutExposingPartialState() {
        AtomicMemoryStore store = new AtomicMemoryStore()
                .put(SettingsKeys.SCHEMA, 1)
                .put(SettingsKeys.LEGACY_FILTER, "CRT")
                .put(SettingsKeys.LEGACY_REFRESH, "HZ_120");
        Map<String, Object> before = new LinkedHashMap<>(store.values);
        store.failNext = true;
        SettingsRepository repository = new SettingsRepository(store);

        AppSettings first = repository.load();
        assertEquals(VideoQualityPreset.CUSTOM, first.videoPreferences().preset());
        assertEquals(before, store.values);
        assertEquals(1, store.commitAttempts);

        AppSettings second = repository.load();
        assertEquals(first, second);
        assertEquals(Integer.valueOf(1), store.values.get(SettingsKeys.COMMIT_GENERATION));
        assertEquals(2, store.commitAttempts);
        assertEquals(1, store.commitCount);
    }

    @Test public void balancedRoundTripPreservesCustomRequestWithoutInjectingMmpx() {
        CustomVideoSettings custom = new CustomVideoSettings(
                PhysicalRefreshPolicy.HZ_120, TemporalMode.MOTION_INTERPOLATION,
                SpatialMode.SCALEFX, PostEffect.CRT);
        VideoPreferences video = new VideoPreferences(VideoQualityPreset.BALANCED, custom, false);
        AppSettings requested = AppSettings.defaults().toBuilder().videoPreferences(video).build();
        AtomicMemoryStore store = new AtomicMemoryStore();
        SettingsRepository repository = new SettingsRepository(store);

        repository.save(requested);
        VideoPreferences loaded = repository.load().videoPreferences();
        assertEquals(VideoQualityPreset.BALANCED, loaded.preset());
        assertEquals(custom, loaded.custom());
        assertFalse(loaded.adaptiveProtection());
        assertEquals("SCALEFX", store.values.get(SettingsKeys.CUSTOM_SPATIAL_MODE));
    }

    private static AtomicMemoryStore validSchemaFour(VideoQualityPreset preset) {
        return new AtomicMemoryStore()
                .put(SettingsKeys.SCHEMA, 4)
                .put(SettingsKeys.COMMIT_GENERATION, 9)
                .put(SettingsKeys.ASPECT, "FOUR_BY_THREE")
                .put(SettingsKeys.VIDEO_QUALITY_PRESET, preset.name())
                .put(SettingsKeys.CUSTOM_REFRESH_POLICY, "HZ_90")
                .put(SettingsKeys.CUSTOM_TEMPORAL_MODE, "MOTION_INTERPOLATION")
                .put(SettingsKeys.CUSTOM_SPATIAL_MODE, "MMPX")
                .put(SettingsKeys.CUSTOM_POST_EFFECT, "CRT")
                .put(SettingsKeys.ADAPTIVE_PROTECTION, true);
    }

    static final class AtomicMemoryStore implements SettingsStore {
        final Map<String, Object> values = new LinkedHashMap<>();
        int commitAttempts;
        int commitCount;
        boolean failNext;

        AtomicMemoryStore put(String key, Object value) { values.put(key, value); return this; }

        @Override public Map<String, ?> snapshot() {
            return Collections.unmodifiableMap(new LinkedHashMap<>(values));
        }

        @Override public boolean commit(SettingsBatch batch) {
            commitAttempts++;
            if (failNext) { failNext = false; return false; }
            Map<String, Object> next = new LinkedHashMap<>(values);
            for (String key : batch.removals()) next.remove(key);
            next.putAll(batch.strings());
            next.putAll(batch.integers());
            next.putAll(batch.booleans());
            values.clear();
            values.putAll(next);
            commitCount++;
            return true;
        }
    }
}
