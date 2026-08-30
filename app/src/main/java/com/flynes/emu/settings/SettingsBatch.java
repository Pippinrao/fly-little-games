package com.flynes.emu.settings;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.LinkedHashSet;
import java.util.Map;
import java.util.Objects;
import java.util.Set;

/** One typed, all-or-nothing settings mutation. */
public final class SettingsBatch {
    private final Map<String, String> strings;
    private final Map<String, Integer> integers;
    private final Map<String, Boolean> booleans;
    private final Set<String> removals;

    private SettingsBatch(Builder builder) {
        strings = Collections.unmodifiableMap(new LinkedHashMap<>(builder.strings));
        integers = Collections.unmodifiableMap(new LinkedHashMap<>(builder.integers));
        booleans = Collections.unmodifiableMap(new LinkedHashMap<>(builder.booleans));
        removals = Collections.unmodifiableSet(new LinkedHashSet<>(builder.removals));
    }

    public Map<String, String> strings() { return strings; }
    public Map<String, Integer> integers() { return integers; }
    public Map<String, Boolean> booleans() { return booleans; }
    public Set<String> removals() { return removals; }

    public static final class Builder {
        private final Map<String, String> strings = new LinkedHashMap<>();
        private final Map<String, Integer> integers = new LinkedHashMap<>();
        private final Map<String, Boolean> booleans = new LinkedHashMap<>();
        private final Set<String> removals = new LinkedHashSet<>();
        private final Set<String> keys = new LinkedHashSet<>();

        public Builder putString(String key, String value) {
            claim(key); strings.put(key, Objects.requireNonNull(value, "value")); return this;
        }
        public Builder putInt(String key, int value) {
            claim(key); integers.put(key, value); return this;
        }
        public Builder putBoolean(String key, boolean value) {
            claim(key); booleans.put(key, value); return this;
        }
        public Builder remove(String key) {
            claim(key); removals.add(key); return this;
        }
        public SettingsBatch build() { return new SettingsBatch(this); }

        private void claim(String key) {
            Objects.requireNonNull(key, "key");
            if (!keys.add(key)) throw new IllegalArgumentException("duplicate key: " + key);
        }
    }
}
