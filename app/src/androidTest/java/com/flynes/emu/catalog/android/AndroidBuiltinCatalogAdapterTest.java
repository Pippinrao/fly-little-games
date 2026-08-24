package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.assertThrows;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateStore;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchRequest;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidBuiltinCatalogAdapterTest {
    @Test
    public void licensedBuiltinUsesSharedScannerVerifiedEnglishAndStrictLoader() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        AndroidBuiltinCatalogAdapter adapter = new AndroidBuiltinCatalogAdapter(context);
        ScanResult scanned = adapter.scan();
        assertEquals(1, scanned.packages().size());
        assertEquals(1, scanned.packages().get(0).variants().size());
        assertTrue(scanned.packages().get(0).variants().get(0)
                .compatibilityDecision().isPlayable());
        assertEquals("From Below", scanned.packages().get(0).variants().get(0)
                .canonicalGame().englishTitle());
        assertEquals("", scanned.packages().get(0).variants().get(0)
                .canonicalGame().zhHansTitle());
        assertTrue(scanned.packages().get(0).variants().get(0).canonicalGame()
                .titleCandidates().stream().allMatch(item ->
                        item.origin() == TitleCandidate.Origin.BUILTIN_MANIFEST
                                && item.reviewState() == TitleCandidate.ReviewState.VERIFIED));

        GameCatalog catalog = new GameCatalog();
        CatalogRepository repository = new CatalogRepository(
                CatalogState.empty(AndroidBuiltinCatalogAdapter.SOURCE),
                new MemoryStore(), catalog);
        repository.commitScan(SourceScanResult.from(
                AndroidBuiltinCatalogAdapter.SOURCE, repository.state().revision(), 1,
                SourceScanResult.Completeness.FULL, scanned, 1));
        GameVariant variant = catalog.canonicalEntries().get(0).variants().get(0);
        byte[] loaded = new ExactRomLoader(new AndroidCatalogStreamOpener(
                context, repository, locator -> true)).load(LaunchRequest.forVariant(variant));
        assertTrue(loaded.length > 16);
        assertFalse(variant.hashes().payloadSha256().isEmpty());
    }

    @Test
    public void strictOpenerReportsPersistedPermissionLossBeforeProviderOpen() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        RomSource tree = new RomSource(
                "tree", RomSource.Type.SAF_TREE, "content://provider/tree/root",
                RomSource.PermissionState.GRANTED);
        CatalogState state = CatalogState.empty(AndroidBuiltinCatalogAdapter.SOURCE)
                .withSource(tree);
        CatalogRepository repository = new CatalogRepository(
                state, new MemoryStore(), new GameCatalog());
        byte[] rom = new byte[16 + 16_384];
        rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A; rom[4] = 1;
        String locator = "content://provider/document/game";
        ScanResult scan = new RomPackageScanner(ScanLimits.defaults()).scan(
                tree, java.util.Collections.singletonList(new PackageCandidate(
                        "document-id", "game.nes", locator,
                        () -> new java.io.ByteArrayInputStream(rom))));
        repository.commitScan(SourceScanResult.from(
                tree, repository.state().revision(), 1,
                SourceScanResult.Completeness.FULL, scan, 1));

        AndroidCatalogStreamOpener.SourceOpenException failure = assertThrows(
                AndroidCatalogStreamOpener.SourceOpenException.class,
                () -> new AndroidCatalogStreamOpener(context, repository, ignored -> false)
                        .open(tree.id(), locator));
        assertEquals(AndroidCatalogStreamOpener.FailureCode.PERMISSION_LOST, failure.code());
    }

    private static final class MemoryStore implements CatalogStateStore {
        @Override public byte[] read() { return null; }
        @Override public void writeAtomically(byte[] encoded) { }
    }
}
