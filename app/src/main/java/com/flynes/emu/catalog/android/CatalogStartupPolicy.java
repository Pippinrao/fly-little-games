package com.flynes.emu.catalog.android;

import java.util.Objects;

/** Pure startup decision table; no filesystem or native work belongs here. */
public final class CatalogStartupPolicy {
    public enum NativeAction { HYDRATE_CACHED_STATE, REBUILD_PROJECTION, RESCAN_BUILTIN }

    public record CacheFacts(
            boolean usable, long generation, String manifestSha256, long sourceEpoch) { }

    public record NativeFacts(long generation, String manifestSha256, long sourceEpoch) { }

    private CatalogStartupPolicy() { }

    public static NativeAction decide(CacheFacts cache, NativeFacts nativeFacts) {
        Objects.requireNonNull(cache, "cache facts");
        Objects.requireNonNull(nativeFacts, "native facts");
        if (!cache.usable()) return NativeAction.RESCAN_BUILTIN;
        if (!Objects.equals(cache.manifestSha256(), nativeFacts.manifestSha256())) {
            return NativeAction.RESCAN_BUILTIN;
        }
        if (cache.generation() != nativeFacts.generation()
                || cache.sourceEpoch() != nativeFacts.sourceEpoch()) {
            return NativeAction.REBUILD_PROJECTION;
        }
        return NativeAction.HYDRATE_CACHED_STATE;
    }
}
