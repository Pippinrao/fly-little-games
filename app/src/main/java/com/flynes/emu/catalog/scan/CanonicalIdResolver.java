package com.flynes.emu.catalog.scan;

import com.flynes.emu.catalog.RomFormat;

/** Optional reviewed override layer; unmatched payloads use a SHA-256 provisional game ID. */
@FunctionalInterface
public interface CanonicalIdResolver {
    String canonicalGameId(String payloadSha256, RomFormat format);
}
