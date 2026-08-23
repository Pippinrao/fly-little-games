package com.flynes.emu.catalog;

import java.util.Collections;
import java.util.List;

public record ScanResult(
        List<PhysicalPackage> packages,
        List<PackageOutcome> packageOutcomes,
        List<EntryOutcome> entryOutcomes,
        List<ScanIssue> issues) {

    public ScanResult {
        packages = DomainValidation.immutableList(packages, "packages");
        packageOutcomes = DomainValidation.immutableList(
                packageOutcomes, "package outcomes");
        entryOutcomes = DomainValidation.immutableList(entryOutcomes, "entry outcomes");
        issues = DomainValidation.immutableList(issues, "scan issues");
    }

    public ScanResult(List<PhysicalPackage> packages, List<ScanIssue> issues) {
        this(packages,
                Collections.<PackageOutcome>emptyList(),
                Collections.<EntryOutcome>emptyList(),
                issues);
    }

    public static ScanResult success(
            List<PhysicalPackage> packages,
            List<ScanIssue> nonFatalIssues) {
        ScanResult result = new ScanResult(packages, nonFatalIssues);
        if (result.hasFatalIssue()) {
            throw new IllegalArgumentException("successful scan cannot contain a fatal issue");
        }
        return result;
    }

    public boolean hasFatalIssue() {
        for (ScanIssue issue : issues) {
            if (issue.severity() == ScanIssue.Severity.FATAL) {
                return true;
            }
        }
        return false;
    }

    public boolean replaceExistingCatalog() {
        return !hasFatalIssue();
    }
}
