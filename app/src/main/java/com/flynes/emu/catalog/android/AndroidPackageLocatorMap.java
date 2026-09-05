package com.flynes.emu.catalog.android;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

/** Platform-only (source UUID hex, relative path) → document URI. Not stored in FLYCAT01. */
public final class AndroidPackageLocatorMap {
    private final LinkedHashMap<String, String> locators = new LinkedHashMap<>();

    public void put(byte[] sourceUuid, String relativePath, String documentUri) {
        locators.put(key(sourceUuid, relativePath),
                Objects.requireNonNull(documentUri, "document URI").trim());
    }

    public String get(byte[] sourceUuid, String relativePath) {
        return locators.get(key(sourceUuid, relativePath));
    }

    public Map<String, String> snapshot() {
        return Collections.unmodifiableMap(new LinkedHashMap<String, String>(locators));
    }

    private static String key(byte[] sourceUuid, String relativePath) {
        return AndroidUuidSafMap.toHex(sourceUuid) + "|"
                + Objects.requireNonNull(relativePath, "relative path");
    }
}
