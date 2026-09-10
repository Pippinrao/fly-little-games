package com.flynes.emu.settings;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/**
 * SettingsStore view over the shared FLYSET01 snapshot. CatalogRepository-style JVM tests keep
 * using in-memory stores; production wires this to JNI.
 */
public final class NativeSettingsStore implements SettingsStore {
    public interface Backend {
        FlySettingsSnapshot get();
        boolean apply(FlySettingsSnapshot snapshot);
    }

    private final Backend backend;
    private int generation;

    public NativeSettingsStore(Backend backend) {
        this.backend = Objects.requireNonNull(backend, "backend");
    }

    @Override
    public Map<String, ?> snapshot() {
        FlySettingsSnapshot nativeSnapshot = backend.get();
        AppSettings settings = FlySettingsMapper.fromNative(nativeSnapshot);
        return Collections.unmodifiableMap(new LinkedHashMap<>(
                FlySettingsMapper.toSchemaFourMap(settings, generation)));
    }

    @Override
    public boolean commit(SettingsBatch batch) {
        AppSettings settings = FlySettingsMapper.fromSchemaFourBatch(batch);
        Integer nextGeneration = batch.integers().get(SettingsKeys.COMMIT_GENERATION);
        if (!backend.apply(FlySettingsMapper.toNative(settings))) return false;
        if (nextGeneration != null) generation = nextGeneration;
        return true;
    }
}
