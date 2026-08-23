package com.flynes.emu.catalog;

import java.util.List;

public record ScanResult(
        List<PhysicalPackage> packages,
        List<ScanIssue> issues) {

    public ScanResult {
        packages = List.copyOf(DomainValidation.requireNonNull(packages, "packages"));
        issues = List.copyOf(DomainValidation.requireNonNull(issues, "scan issues"));
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
        return issues.stream().anyMatch(issue -> issue.severity() == ScanIssue.Severity.FATAL);
    }

    public boolean replaceExistingCatalog() {
        return !hasFatalIssue();
    }
}
