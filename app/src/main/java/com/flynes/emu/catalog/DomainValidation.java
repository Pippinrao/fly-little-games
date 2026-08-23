package com.flynes.emu.catalog;

import java.util.Objects;

final class DomainValidation {
    private DomainValidation() {
    }

    static String requireNonBlank(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(name + " must not be blank");
        }
        return value;
    }

    static <T> T requireNonNull(T value, String name) {
        return Objects.requireNonNull(value, name);
    }
}
