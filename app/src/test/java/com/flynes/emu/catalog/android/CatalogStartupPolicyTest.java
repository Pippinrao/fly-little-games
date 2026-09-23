package com.flynes.emu.catalog.android;

import static com.flynes.emu.catalog.android.CatalogStartupPolicy.NativeAction.HYDRATE_CACHED_STATE;
import static com.flynes.emu.catalog.android.CatalogStartupPolicy.NativeAction.REBUILD_PROJECTION;
import static com.flynes.emu.catalog.android.CatalogStartupPolicy.NativeAction.RESCAN_BUILTIN;
import static org.junit.Assert.assertEquals;

import org.junit.Test;

public final class CatalogStartupPolicyTest {
    private static final String HASH = "ab".repeat(32);
    private static final String OTHER_HASH = "cd".repeat(32);

    @Test public void exactMetadataMatchHydratesWithoutEnumerationOrScan() {
        assertEquals(HYDRATE_CACHED_STATE, CatalogStartupPolicy.decide(
                new CatalogStartupPolicy.CacheFacts(true, 8, HASH, 4),
                new CatalogStartupPolicy.NativeFacts(8, HASH, 4)));
    }

    @Test public void manifestChangeRequiresOnlyBuiltinRescan() {
        assertEquals(RESCAN_BUILTIN, CatalogStartupPolicy.decide(
                new CatalogStartupPolicy.CacheFacts(true, 8, HASH, 4),
                new CatalogStartupPolicy.NativeFacts(8, OTHER_HASH, 4)));
    }

    @Test public void generationOrEpochChangeRebuildsWithoutRomDirectoryScan() {
        assertEquals(REBUILD_PROJECTION, CatalogStartupPolicy.decide(
                new CatalogStartupPolicy.CacheFacts(true, 7, HASH, 4),
                new CatalogStartupPolicy.NativeFacts(8, HASH, 5)));
    }

    @Test public void invalidCacheRescansBuiltinOnce() {
        assertEquals(RESCAN_BUILTIN, CatalogStartupPolicy.decide(
                new CatalogStartupPolicy.CacheFacts(false, 0, HASH, 0),
                new CatalogStartupPolicy.NativeFacts(8, HASH, 4)));
    }
}
