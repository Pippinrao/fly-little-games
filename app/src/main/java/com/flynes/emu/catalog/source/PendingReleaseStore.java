package com.flynes.emu.catalog.source;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

/** Durable operational tombstones for SAF grants awaiting safe release. */
public interface PendingReleaseStore {
    Map<String, String> readAll() throws IOException;
    void put(String sourceId, String locator) throws IOException;
    void remove(String sourceId) throws IOException;

    static PendingReleaseStore inMemory() {
        return new PendingReleaseStore() {
            private final LinkedHashMap<String, String> entries = new LinkedHashMap<>();

            @Override public synchronized Map<String, String> readAll() {
                return new LinkedHashMap<>(entries);
            }

            @Override public synchronized void put(String sourceId, String locator) {
                entries.put(sourceId, locator);
            }

            @Override public synchronized void remove(String sourceId) {
                entries.remove(sourceId);
            }
        };
    }
}
