package com.flynes.emu.app;

import com.flynes.emu.catalog.persistence.CanonicalUserState;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Objects;

/** One immutable native catalog projection transferred across a single JNI boundary. */
public record NativeCatalogSnapshot(
        long generation,
        List<NativeCatalogEntry> entries,
        Map<String, CanonicalUserState> userStates,
        List<NativeSourceStatus> sources) {
    public NativeCatalogSnapshot {
        if (generation < 0) throw new IllegalArgumentException("generation");
        entries = List.copyOf(Objects.requireNonNull(entries, "entries"));
        userStates = Collections.unmodifiableMap(new LinkedHashMap<>(
                Objects.requireNonNull(userStates, "user states")));
        sources = List.copyOf(Objects.requireNonNull(sources, "sources"));
    }
}
