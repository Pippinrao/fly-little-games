package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;
import com.flynes.emu.catalog.ScanResult;

import java.util.List;
import java.util.HashSet;
import java.util.Set;

public record SourceScanResult(
        String sourceId,
        long baseRevision,
        long scanToken,
        Completeness completeness,
        RomSource source,
        List<PhysicalPackage> packages,
        List<PackageOutcome> packageOutcomes,
        List<EntryOutcome> entryOutcomes,
        List<ScanIssue> issues,
        int candidateCount) {
    public SourceScanResult {
        sourceId = DomainValidation.requireNonBlank(sourceId, "scan source id");
        if (baseRevision < 0 || scanToken <= 0 || candidateCount < 0) {
            throw new IllegalArgumentException("scan revision, token, or count is invalid");
        }
        completeness = DomainValidation.requireNonNull(completeness, "scan completeness");
        source = DomainValidation.requireNonNull(source, "scan source");
        packages = DomainValidation.immutableList(packages, "scan packages");
        packageOutcomes = DomainValidation.immutableList(packageOutcomes, "package outcomes");
        entryOutcomes = DomainValidation.immutableList(entryOutcomes, "entry outcomes");
        issues = DomainValidation.immutableList(issues, "scan issues");
        if (!sourceId.equals(source.id()) || packageOutcomes.size() != candidateCount) {
            throw new IllegalArgumentException("scan source or candidate accounting is invalid");
        }
        Set<String> packageIds = new HashSet<>();
        for (PhysicalPackage item : packages) {
            if (!source.equals(item.source())) {
                throw new IllegalArgumentException("scan package source identity differs");
            }
            if (!packageIds.add(item.id())) {
                throw new IllegalArgumentException("scan contains duplicate packages");
            }
        }
        Set<String> packageOutcomeIds = new HashSet<>();
        for (PackageOutcome item : packageOutcomes) {
            if (!packageOutcomeIds.add(item.packageId())) {
                throw new IllegalArgumentException("scan contains duplicate package outcomes");
            }
        }
        Set<String> entryOutcomeIds = new HashSet<>();
        for (EntryOutcome item : entryOutcomes) {
            String key = item.packageId().length() + ":" + item.packageId() + item.entryId();
            if (!entryOutcomeIds.add(key)) {
                throw new IllegalArgumentException("scan contains duplicate entry outcomes");
            }
        }
        boolean fatal = false;
        for (ScanIssue issue : issues) fatal |= issue.severity() == ScanIssue.Severity.FATAL;
        if ((completeness == Completeness.FATAL) != fatal) {
            throw new IllegalArgumentException("fatal completeness and issue severity disagree");
        }
    }

    public enum Completeness { FULL, PARTIAL, FATAL }

    public static SourceScanResult from(
            RomSource source,
            long baseRevision,
            long scanToken,
            Completeness completeness,
            ScanResult result,
            int candidateCount) {
        return new SourceScanResult(
                source.id(), baseRevision, scanToken, completeness, source,
                result.packages(), result.packageOutcomes(), result.entryOutcomes(),
                result.issues(), candidateCount);
    }
}
