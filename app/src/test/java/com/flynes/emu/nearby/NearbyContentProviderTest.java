package com.flynes.emu.nearby;

import org.junit.Test;
import static org.junit.Assert.*;
import com.flynes.emu.catalog.*;
import java.util.*;

public final class NearbyContentProviderTest {
    @Test public void immutableSelectionsKeepExactSourceAndBatchAuthority() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(pkg("one"), pkg("two")), List.of()));
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {});
        byte[] firstRef = Arrays.copyOfRange(provider.query(0), 4, 20);
        byte[] secondRef = Arrays.copyOfRange(provider.query(1), 4, 20);
        NearbyContentProvider.Selection first = provider.resolveSelection(firstRef);
        NearbyContentProvider.Selection second = provider.resolveSelection(secondRef);
        assertNotNull("capture first exact source", first);
        assertNotNull("capture second exact source", second);
        assertNotEquals(first.variant().sourceId(), second.variant().sourceId());
        assertArrayEquals(first.contentHash(), second.contentHash());
        byte[] copy = first.sourceChoiceRef(); copy[0] ^= 1;
        byte[] hash = first.contentHash(); hash[0] ^= 1;
        assertArrayEquals(firstRef, first.sourceChoiceRef());
        assertArrayEquals(first.contentHash(), second.contentHash());
        assertTrue(provider.isCurrent(first));
        provider.query(0);
        assertFalse("same catalog publication but a new batch revokes old selections", provider.isCurrent(first));
        assertNull(provider.resolveSelection(firstRef));
        provider.close();
        assertFalse(provider.isCurrent(second));
    }

    @Test public void capturedSelectionCannotReviveAfterObservedRevocationOrRevision() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(pkg("one")), List.of()));
        boolean[] allowed = {true};
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {
            if (!allowed[0]) throw new SecurityException();
        });
        NearbyContentProvider.Selection captured = provider.resolveSelection(
                Arrays.copyOfRange(provider.query(0), 4, 20));
        assertNotNull(captured);
        allowed[0] = false;
        assertFalse(provider.isCurrent(captured));
        allowed[0] = true;
        assertFalse(provider.isCurrent(captured));
        captured = provider.resolveSelection(Arrays.copyOfRange(provider.query(0), 4, 20));
        assertNotNull(captured);
        catalog.applyScanResult(ScanResult.success(List.of(pkg("two")), List.of()));
        assertFalse(provider.isCurrent(captured));
        assertNull(provider.resolveSelection(new byte[15]));
        assertNull(provider.resolveSelection(null));
        assertFalse(provider.isCurrent((NearbyContentProvider.Selection) null));
    }
    @Test public void productionMetadataProviderExists() {
        boolean present;
        try { Class.forName("com.flynes.emu.nearby.NearbyContentProvider"); present = true; }
        catch (ClassNotFoundException absent) { present = false; }
        assertTrue("production metadata provider must exist before owner registration", present);
    }

    @Test public void batchesBindExactSourcesAndRejectRevisions() {
        GameCatalog catalog = new GameCatalog();
        catalog.applyScanResult(ScanResult.success(List.of(pkg("one"), pkg("two")), List.of()));
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {});
        byte[] first = provider.query(0);
        byte[] second = provider.query(1);
        byte[] firstRef = Arrays.copyOfRange(first, 4, 20);
        assertFalse(Arrays.equals(firstRef, Arrays.copyOfRange(second, 4, 20)));
        assertTrue(provider.isCurrent(firstRef));
        assertNull(provider.query(2));
        catalog.applyScanResult(ScanResult.success(List.of(pkg("three")), List.of()));
        assertFalse(provider.isCurrent(firstRef));
        assertArrayEquals(second, provider.query(1));
        assertFalse(Arrays.equals(first, provider.query(0)));
        assertFalse(provider.isCurrent(firstRef));
        provider.close();
        assertThrows(IllegalStateException.class, () -> provider.query(0));
        assertFalse(provider.isCurrent(firstRef));
    }

    @Test public void noBootstrapSnapshotAndPermissionRevocationAreHandled() {
        GameCatalog catalog = new GameCatalog();
        boolean[] granted = {true};
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {
            if (!granted[0]) throw new SecurityException();
        });
        catalog.applyScanResult(ScanResult.success(List.of(pkg("one")), List.of()));
        byte[] record = provider.query(0);
        byte[] ref = Arrays.copyOfRange(record, 4, 20);
        assertTrue(provider.isCurrent(ref));
        granted[0] = false;
        assertFalse(provider.isCurrent(ref));
        granted[0] = true;
        assertFalse("observed permission revocation permanently invalidates the old ref", provider.isCurrent(ref));
        byte[] refreshed = Arrays.copyOfRange(provider.query(0), 4, 20);
        assertTrue(provider.isCurrent(refreshed));
        assertFalse(provider.isCurrent((byte[]) null));
        assertFalse(provider.isCurrent(new byte[15]));
        assertFalse(provider.isCurrent(new byte[16]));
        assertThrows(IllegalArgumentException.class, () -> provider.query(-1));
    }

    @Test public void emptyCatalogIsEmptyAndReturnedBytesAreOwned() {
        GameCatalog catalog = new GameCatalog();
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {});
        assertNull(provider.query(0));
        catalog.applyScanResult(ScanResult.success(List.of(pkg("one")), List.of()));
        byte[] row = provider.query(0);
        byte[] ref = Arrays.copyOfRange(row, 4, 20);
        row[4] ^= 1;
        assertTrue(provider.isCurrent(ref));
    }

    @Test public void adapterCapacityRejectsWholeBatchAndRecovers() {
        GameCatalog catalog = new GameCatalog();
        NearbyContentProvider provider = new NearbyContentProvider(catalog, (s, u) -> {});
        List<PhysicalPackage> packages = new ArrayList<>();
        for (int i = 0; i < 4096; ++i) packages.add(pkg("source" + i));
        catalog.applyScanResult(ScanResult.success(packages, List.of()));
        byte[] ref = Arrays.copyOfRange(provider.query(0), 4, 20);
        assertNotNull(provider.query(4095));
        assertNull(provider.query(4096));
        packages.add(pkg("overflow"));
        catalog.applyScanResult(ScanResult.success(packages, List.of()));
        assertThrows("overflow must be an error, never a truncated or empty catalog",
                IllegalStateException.class, () -> provider.query(0));
        assertFalse(provider.isCurrent(ref));
        catalog.applyScanResult(ScanResult.success(List.of(pkg("recovered")), List.of()));
        assertNotNull(provider.query(0));
        assertNull(provider.query(1));
    }

    private static PhysicalPackage pkg(String sourceId) {
        RomSource source = new RomSource(sourceId, RomSource.Type.SAF_TREE,
                "content://fixture/tree/" + sourceId, RomSource.PermissionState.GRANTED);
        RomHashes hashes = new RomHashes("11".repeat(20), "22".repeat(32),
                "33".repeat(32), "12345678");
        RomVariant variant = new RomVariant("variant-" + sourceId,
                new CanonicalGame("game", "Metadata fixture", null, List.of()), null,
                RomFormat.INES, CompatibilityDecision.playableNes(), hashes, RomAnalysis.basic(0), null, null);
        return new PhysicalPackage("package-" + sourceId, source,
                "content://fixture/document/" + sourceId, "fixture.nes", PackageFormat.RAW,
                hashes.physicalPackageSha256(), List.of(variant));
    }
}
