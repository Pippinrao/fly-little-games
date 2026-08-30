package com.flynes.emu.catalog.source;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;

/** Durable operational tombstones for SAF grants awaiting safe release. */
public interface PendingReleaseStore {
    Map<String, PendingRelease> readAll() throws IOException;
    void put(PendingRelease pending) throws IOException;
    void remove(String actionId) throws IOException;

    static PendingReleaseStore inMemory() {
        return new PendingReleaseStore() {
            private final LinkedHashMap<String, PendingRelease> entries =
                    new LinkedHashMap<>();

            @Override public synchronized Map<String, PendingRelease> readAll() {
                return new LinkedHashMap<>(entries);
            }

            @Override public synchronized void put(PendingRelease pending) {
                entries.put(pending.actionId(), pending);
            }

            @Override public synchronized void remove(String actionId) {
                entries.remove(actionId);
            }
        };
    }
}
