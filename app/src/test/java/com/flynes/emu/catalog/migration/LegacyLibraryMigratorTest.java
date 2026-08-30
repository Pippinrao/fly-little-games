package com.flynes.emu.catalog.migration;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateStore;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;
import com.flynes.emu.catalog.scan.ScanLimits;

import org.junit.Test;

import java.io.IOException;
import java.util.List;

public final class LegacyLibraryMigratorTest {
    @Test
    public void importsOnlyUniqueRawMatchAndMarksAfterCommit() throws Exception {
        Fixture fixture = new Fixture();
        byte[] ines = ines();
        LegacyLibraryMigrator.LegacySnapshot legacy = new LegacyLibraryMigrator.LegacySnapshot(
                "content://provider/tree/root", false, List.of(
                new LegacyLibraryMigrator.LegacyRow(
                        "Old title", "content://provider/document/raw", "saf", false),
                new LegacyLibraryMigrator.LegacyRow(
                        "Zip", "content://provider/document/archive", "saf", true),
                new LegacyLibraryMigrator.LegacyRow(
                        "Builtin", "asset:///roms/from_below.nes", "assets", false)));
        List<PackageCandidate> enumerated = List.of(
                PackageCandidate.bytes("raw-id", "actual-name.nes", ines),
                new PackageCandidate("zip-id", "archive.zip",
                        "content://provider/document/archive",
                        () -> new java.io.ByteArrayInputStream(new byte[]{'P', 'K', 3, 4})),
                new PackageCandidate("raw-doc", "actual-name.nes",
                        "content://provider/document/raw",
                        () -> new java.io.ByteArrayInputStream(ines)));

        LegacyLibraryMigrator.Result result = fixture.migrator.migrate(
                legacy, true, enumerated, fixture.repository, fixture.marker, true);

        assertEquals(LegacyLibraryMigrator.Status.MIGRATED, result.status());
        assertTrue(fixture.marker.complete);
        assertEquals(1, fixture.repository.state().sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().size());
        assertEquals(PackageFormat.RAW, fixture.repository.state().sources().values().stream()
                .filter(item -> item.source().type() == RomSource.Type.SAF_TREE)
                .findFirst().orElseThrow().packages().values().iterator().next()
                .physicalPackage().packageFormat());
        assertTrue(result.rowOutcomes().contains(
                LegacyLibraryMigrator.RowOutcome.LEGACY_ZIP_AMBIGUOUS));
        assertEquals(1, fixture.store.writes);
    }

    @Test
    public void missingPermissionRegistersReauthorizeAndCommitFailureNeverMarks() throws Exception {
        Fixture missing = new Fixture();
        LegacyLibraryMigrator.LegacySnapshot legacy = new LegacyLibraryMigrator.LegacySnapshot(
                "content://provider/tree/root", false, List.of());
        LegacyLibraryMigrator.Result result = missing.migrator.migrate(
                legacy, false, List.of(), missing.repository, missing.marker, true);
        assertEquals(LegacyLibraryMigrator.Status.NEEDS_REAUTHORIZE, result.status());
        assertTrue(missing.marker.complete);
        assertTrue(missing.repository.state().sources().values().stream().anyMatch(item ->
                item.source().permissionState() == RomSource.PermissionState.NEEDS_REAUTHORIZE));

        Fixture failed = new Fixture();
        failed.store.fail = true;
        try {
            failed.migrator.migrate(legacy, false, List.of(),
                    failed.repository, failed.marker, true);
        } catch (CatalogRepository.RepositoryException expected) {
            // expected
        }
        assertFalse(failed.marker.complete);
    }

    @Test
    public void fatalMatchedCandidateReturnsTypedPermissionLossWithoutAnyMutation()
            throws Exception {
        Fixture fixture = new Fixture();
        LegacyLibraryMigrator.LegacySnapshot legacy = new LegacyLibraryMigrator.LegacySnapshot(
                "content://provider/tree/root", false, List.of(
                new LegacyLibraryMigrator.LegacyRow(
                        "Private", "content://provider/document/private", "saf", false)));
        PackageCandidate revoked = new PackageCandidate(
                "private-document", "private.nes", "content://provider/document/private",
                () -> { throw new SecurityException("sensitive provider message"); });
        CatalogState before = fixture.repository.state();

        LegacyLibraryMigrator.Result result = fixture.migrator.migrate(
                legacy, true, List.of(revoked), fixture.repository, fixture.marker, true);

        assertEquals(LegacyLibraryMigrator.Status.NEEDS_REAUTHORIZE, result.status());
        assertTrue(result.hasFatalIssue());
        assertTrue(result.issues().stream().anyMatch(issue ->
                issue.code() == com.flynes.emu.catalog.ScanIssue.Code.PERMISSION_REVOKED
                        && issue.severity()
                        == com.flynes.emu.catalog.ScanIssue.Severity.FATAL));
        assertFalse(result.toString().contains("sensitive provider message"));
        assertEquals(before, fixture.repository.state());
        assertEquals(0, fixture.store.writes);
        assertFalse(fixture.marker.complete);
    }

    private static byte[] ines() {
        byte[] rom = new byte[16 + 16_384];
        rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A; rom[4] = 1;
        return rom;
    }

    private static final class Fixture {
        final MemoryStore store = new MemoryStore();
        final CatalogRepository repository;
        final Marker marker = new Marker();
        final LegacyLibraryMigrator migrator = new LegacyLibraryMigrator(
                new RomPackageScanner(ScanLimits.defaults()));
        Fixture() {
            RomSource builtin = new RomSource(
                    "builtin", RomSource.Type.BUILTIN, "asset:///roms/from_below.nes",
                    RomSource.PermissionState.NOT_REQUIRED);
            repository = new CatalogRepository(
                    CatalogState.empty(builtin), store, new GameCatalog());
        }
    }

    private static final class Marker implements LegacyLibraryMigrator.MigrationMarker {
        boolean complete;
        @Override public boolean isComplete() { return complete; }
        @Override public void markComplete() { complete = true; }
    }

    private static final class MemoryStore implements CatalogStateStore {
        boolean fail;
        int writes;
        @Override public byte[] read() { return null; }
        @Override public void writeAtomically(byte[] encoded) throws IOException {
            writes++;
            if (fail) throw new IOException("fault");
        }
    }
}
