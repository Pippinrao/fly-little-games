package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.RomSource;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

public record CatalogState(
        int schemaVersion,
        long revision,
        String builtinSourceId,
        Map<String, SourceCatalogState> sources,
        Map<String, CanonicalUserState> userStates,
        long lastPlayedSequence) {
    public static final int CURRENT_SCHEMA = 1;

    public CatalogState {
        if (schemaVersion != CURRENT_SCHEMA || revision < 0 || lastPlayedSequence < 0) {
            throw new IllegalArgumentException("catalog state version or revision is invalid");
        }
        builtinSourceId = DomainValidation.requireNonBlank(builtinSourceId, "builtin source id");
        sources = immutableSorted(sources);
        userStates = immutableSorted(userStates);
        SourceCatalogState builtin = sources.get(builtinSourceId);
        if (builtin == null || builtin.source().type() != RomSource.Type.BUILTIN) {
            throw new IllegalArgumentException("catalog must retain its fixed builtin source");
        }
        for (Map.Entry<String, SourceCatalogState> item : sources.entrySet()) {
            if (!item.getKey().equals(item.getValue().source().id())) {
                throw new IllegalArgumentException("source registry key does not match source id");
            }
        }
        for (Map.Entry<String, CanonicalUserState> item : userStates.entrySet()) {
            DomainValidation.requireNonBlank(item.getKey(), "canonical user-state id");
            if (item.getValue().lastPlayedSequence() > lastPlayedSequence) {
                throw new IllegalArgumentException("canonical sequence exceeds global sequence");
            }
        }
    }

    public static CatalogState empty(RomSource builtin) {
        if (builtin.type() != RomSource.Type.BUILTIN) {
            throw new IllegalArgumentException("initial source must be builtin");
        }
        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>();
        sources.put(builtin.id(), SourceCatalogState.empty(builtin));
        return new CatalogState(CURRENT_SCHEMA, 0, builtin.id(), sources,
                Collections.emptyMap(), 0);
    }

    public CatalogState withSource(RomSource source) {
        if (sources.containsKey(source.id())) throw new IllegalArgumentException("source exists");
        LinkedHashMap<String, SourceCatalogState> next = new LinkedHashMap<>(sources);
        next.put(source.id(), SourceCatalogState.empty(source));
        return new CatalogState(schemaVersion, revision + 1, builtinSourceId, next,
                userStates, lastPlayedSequence);
    }

    CatalogState replace(
            long newRevision,
            Map<String, SourceCatalogState> newSources,
            Map<String, CanonicalUserState> newUserStates,
            long sequence) {
        return new CatalogState(schemaVersion, newRevision, builtinSourceId,
                newSources, newUserStates, sequence);
    }

    private static <K, V> Map<K, V> immutableSorted(Map<K, V> values) {
        TreeMap<K, V> sorted = new TreeMap<>(values);
        return Collections.unmodifiableMap(new LinkedHashMap<>(sorted));
    }
}
