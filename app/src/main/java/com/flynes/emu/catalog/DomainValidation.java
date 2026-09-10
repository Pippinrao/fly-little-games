package com.flynes.emu.catalog;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;
import java.util.Map;
import java.util.LinkedHashMap;
import java.util.Objects;

public final class DomainValidation {
    private DomainValidation() {
    }

    public static String requireNonBlank(String value, String name) {
        if (isBlank(value)) {
            throw new IllegalArgumentException(name + " must not be blank");
        }
        return value;
    }

    public static boolean isBlank(String value) {
        if (value == null || value.length() == 0) {
            return true;
        }
        for (int offset = 0; offset < value.length();) {
            int codePoint = value.codePointAt(offset);
            if (!isFrozenBlankCodePoint(codePoint)) {
                return false;
            }
            offset += Character.charCount(codePoint);
        }
        return true;
    }

    private static boolean isFrozenBlankCodePoint(int codePoint) {
        // Keep persisted IDs stable across Android ICU and host-JDK Unicode table updates.
        return (codePoint >= 0x0009 && codePoint <= 0x000D)
                || (codePoint >= 0x001C && codePoint <= 0x0020)
                || codePoint == 0x00A0
                || codePoint == 0x1680
                || (codePoint >= 0x2000 && codePoint <= 0x200A)
                || codePoint == 0x2028
                || codePoint == 0x2029
                || codePoint == 0x202F
                || codePoint == 0x205F
                || codePoint == 0x3000;
    }

    public static <T> T requireNonNull(T value, String name) {
        return Objects.requireNonNull(value, name);
    }

    public static <T> List<T> immutableList(List<? extends T> values, String name) {
        requireNonNull(values, name);
        ArrayList<T> owned = new ArrayList<>(values.size());
        for (T value : values) {
            owned.add(requireNonNull(value, name + " item"));
        }
        return Collections.unmodifiableList(owned);
    }

    public static <K, V> Map<K, V> immutableMap(Map<? extends K, ? extends V> values) {
        requireNonNull(values, "map");
        return Collections.unmodifiableMap(new LinkedHashMap<>(values));
    }
}
