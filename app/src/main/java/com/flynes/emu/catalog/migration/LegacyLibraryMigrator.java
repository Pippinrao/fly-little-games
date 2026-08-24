package com.flynes.emu.catalog.migration;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.catalog.scan.PackageCandidate;
import com.flynes.emu.catalog.scan.RomPackageScanner;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Collections;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/** Conservative one-time migration from the legacy SharedPreferences library. */
public final class LegacyLibraryMigrator {
    private final RomPackageScanner scanner;

    public LegacyLibraryMigrator(RomPackageScanner scanner) {
        this.scanner = DomainValidation.requireNonNull(scanner, "scanner");
    }

    public Result migrate(
            LegacySnapshot legacy,
            boolean persistedRead,
            List<PackageCandidate> enumerated,
            CatalogRepository repository,
            MigrationMarker marker,
            boolean newStoreAbsent) throws CatalogRepository.RepositoryException, IOException {
        if (legacy == null || enumerated == null || repository == null || marker == null) {
            throw new NullPointerException();
        }
        if (!newStoreAbsent || marker.isComplete()) {
            return new Result(Status.SKIPPED, Collections.emptyList());
        }
        if (DomainValidation.isBlank(legacy.treeLocator()) && legacy.rows().isEmpty()
                && !legacy.malformed()) {
            return new Result(Status.SKIPPED, Collections.emptyList());
        }
        if (legacy.malformed() || DomainValidation.isBlank(legacy.treeLocator())) {
            return new Result(Status.RECOVERY_NEEDED, Collections.emptyList());
        }
        String sourceId = StableIds.safSourceId(legacy.treeLocator());
        RomSource source = new RomSource(
                sourceId, RomSource.Type.SAF_TREE, legacy.treeLocator(),
                persistedRead ? RomSource.PermissionState.GRANTED
                        : RomSource.PermissionState.NEEDS_REAUTHORIZE,
                persistedRead ? RomSource.Availability.AVAILABLE
                        : RomSource.Availability.PERMISSION_REQUIRED);
        SourceCatalogState existing = repository.state().sources().get(sourceId);
        if (!persistedRead) {
            if (existing == null) repository.addSource(source);
            else repository.reauthorizeSource(source);
            marker.markComplete();
            return new Result(Status.NEEDS_REAUTHORIZE, Collections.emptyList());
        }
        ArrayList<RowOutcome> rowOutcomes = new ArrayList<>();
        ArrayList<PackageCandidate> selected = selectUniqueRaw(
                legacy.rows(), enumerated, rowOutcomes);
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        ArrayList<PackageOutcome> packageOutcomes = new ArrayList<>();
        ArrayList<EntryOutcome> entryOutcomes = new ArrayList<>();
        ArrayList<ScanIssue> issues = new ArrayList<>();
        for (PackageCandidate candidate : selected) {
            ScanResult one = scanner.scan(source, Collections.singletonList(candidate));
            if (one.hasFatalIssue()) {
                ArrayList<ScanIssue> fatalIssues = new ArrayList<>(issues);
                fatalIssues.addAll(one.issues());
                return new Result(fatalStatus(fatalIssues), rowOutcomes, fatalIssues);
            }
            boolean raw = one.packages().size() == 1
                    && one.packages().get(0).packageFormat() == PackageFormat.RAW;
            if (raw) {
                packages.addAll(one.packages());
                packageOutcomes.addAll(one.packageOutcomes());
                entryOutcomes.addAll(one.entryOutcomes());
                issues.addAll(one.issues());
            } else if (!one.packages().isEmpty()
                    && one.packages().get(0).packageFormat() == PackageFormat.ZIP) {
                String packageId = one.packageOutcomes().get(0).packageId();
                packageOutcomes.add(new PackageOutcome(
                        packageId, PackageOutcome.Status.SKIPPED,
                        PackageOutcome.Reason.LEGACY_ZIP_AMBIGUOUS));
                rowOutcomes.add(RowOutcome.LEGACY_ZIP_AMBIGUOUS);
            } else {
                packageOutcomes.addAll(one.packageOutcomes());
                entryOutcomes.addAll(one.entryOutcomes());
                issues.addAll(one.issues());
            }
        }
        ScanResult migrated = new ScanResult(
                packages, packageOutcomes, entryOutcomes, issues);
        long token = existing == null ? 1 : Math.addExact(existing.lastScanToken(), 1);
        if (existing != null) repository.reauthorizeSource(source);
        SourceScanResult sourceScan = SourceScanResult.from(
                source, repository.state().revision(), token,
                SourceScanResult.Completeness.FULL, migrated, selected.size());
        if (existing == null) repository.addSourceWithScan(sourceScan);
        else repository.commitScan(sourceScan);
        marker.markComplete();
        return new Result(Status.MIGRATED, rowOutcomes);
    }

    private static Status fatalStatus(List<ScanIssue> issues) {
        for (ScanIssue issue : issues) {
            if (issue.code() == ScanIssue.Code.PERMISSION_REVOKED
                    || issue.code() == ScanIssue.Code.SOURCE_UNAVAILABLE) {
                return Status.NEEDS_REAUTHORIZE;
            }
        }
        return Status.RECOVERY_NEEDED;
    }

    private static ArrayList<PackageCandidate> selectUniqueRaw(
            List<LegacyRow> rows,
            List<PackageCandidate> candidates,
            List<RowOutcome> outcomes) {
        Map<String, Integer> rowCounts = new HashMap<>();
        Map<String, Integer> candidateCounts = new HashMap<>();
        Map<String, PackageCandidate> candidateByLocator = new HashMap<>();
        for (LegacyRow row : rows) {
            if (eligibleRaw(row)) rowCounts.put(
                    row.contentLocator(), rowCounts.getOrDefault(row.contentLocator(), 0) + 1);
        }
        for (PackageCandidate candidate : candidates) {
            candidateByLocator.put(candidate.contentLocator(), candidate);
            candidateCounts.put(candidate.contentLocator(),
                    candidateCounts.getOrDefault(candidate.contentLocator(), 0) + 1);
        }
        ArrayList<PackageCandidate> selected = new ArrayList<>();
        Set<String> selectedLocators = new HashSet<>();
        for (LegacyRow row : rows) {
            if ("assets".equals(row.source())) {
                outcomes.add(RowOutcome.IGNORED_BUILTIN);
            } else if (row.zipped()) {
                outcomes.add(RowOutcome.LEGACY_ZIP_AMBIGUOUS);
            } else if (!eligibleRaw(row)) {
                outcomes.add(RowOutcome.INVALID_ROW);
            } else if (rowCounts.get(row.contentLocator()) != 1
                    || candidateCounts.getOrDefault(row.contentLocator(), 0) != 1) {
                outcomes.add(RowOutcome.AMBIGUOUS_MATCH);
            } else {
                outcomes.add(RowOutcome.RAW_SELECTED);
                if (selectedLocators.add(row.contentLocator())) {
                    selected.add(candidateByLocator.get(row.contentLocator()));
                }
            }
        }
        return selected;
    }

    private static boolean eligibleRaw(LegacyRow row) {
        return "saf".equals(row.source()) && !row.zipped()
                && !DomainValidation.isBlank(row.contentLocator());
    }

    public enum Status { SKIPPED, MIGRATED, NEEDS_REAUTHORIZE, RECOVERY_NEEDED }
    public enum RowOutcome {
        RAW_SELECTED, LEGACY_ZIP_AMBIGUOUS, IGNORED_BUILTIN, INVALID_ROW, AMBIGUOUS_MATCH
    }

    public record LegacySnapshot(
            String treeLocator, boolean malformed, List<LegacyRow> rows) {
        public LegacySnapshot {
            rows = DomainValidation.immutableList(rows, "legacy rows");
        }
    }

    public record LegacyRow(
            String title, String contentLocator, String source, boolean zipped) {
    }

    public record Result(
            Status status, List<RowOutcome> rowOutcomes, List<ScanIssue> issues) {
        public Result(Status status, List<RowOutcome> rowOutcomes) {
            this(status, rowOutcomes, Collections.emptyList());
        }

        public Result {
            status = DomainValidation.requireNonNull(status, "migration status");
            rowOutcomes = DomainValidation.immutableList(rowOutcomes, "legacy outcomes");
            issues = DomainValidation.immutableList(issues, "migration issues");
        }

        public boolean hasFatalIssue() {
            for (ScanIssue issue : issues) {
                if (issue.severity() == ScanIssue.Severity.FATAL) return true;
            }
            return false;
        }
    }

    public interface MigrationMarker {
        boolean isComplete();
        void markComplete() throws IOException;
    }
}
