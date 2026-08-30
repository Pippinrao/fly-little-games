package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.EntryOutcome;
import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.ScanIssue;

import java.util.ArrayList;
import java.util.Collections;
import java.util.Comparator;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

public record SourceCatalogState(
        RomSource source,
        Map<String, CatalogPackage> packages,
        List<PackageOutcome> packageOutcomes,
        List<EntryOutcome> entryOutcomes,
        List<ScanIssue> issues,
        SourceScanResult.Completeness lastScanCompleteness,
        long lastScanToken) {

    public SourceCatalogState {
        source = DomainValidation.requireNonNull(source, "source");
        TreeMap<String, CatalogPackage> sorted = new TreeMap<>(packages);
        packages = Collections.unmodifiableMap(new LinkedHashMap<>(sorted));
        packageOutcomes = sortedList(packageOutcomes, Comparator
                .comparing(PackageOutcome::packageId).thenComparing(PackageOutcome::status)
                .thenComparing(PackageOutcome::reason));
        entryOutcomes = sortedList(entryOutcomes, Comparator
                .comparing(EntryOutcome::packageId).thenComparing(EntryOutcome::entryId)
                .thenComparing(EntryOutcome::status).thenComparing(EntryOutcome::reason));
        issues = sortedList(issues, Comparator
                .comparing(ScanIssue::packageId, Comparator.nullsFirst(String::compareTo))
                .thenComparing(ScanIssue::code).thenComparing(ScanIssue::severity));
        lastScanCompleteness = DomainValidation.requireNonNull(
                lastScanCompleteness, "scan completeness");
        if (lastScanToken < 0) throw new IllegalArgumentException("scan token must not be negative");
        for (Map.Entry<String, CatalogPackage> entry : packages.entrySet()) {
            CatalogPackage item = entry.getValue();
            if (!entry.getKey().equals(item.physicalPackage().id())) {
                throw new IllegalArgumentException("package registry key does not match package id");
            }
            if (!item.physicalPackage().source().id().equals(source.id())) {
                throw new IllegalArgumentException("package belongs to another source");
            }
            if (item.freshness() == CatalogPackage.Freshness.FRESH
                    && !item.physicalPackage().source().equals(source)) {
                throw new IllegalArgumentException(
                        "fresh package source identity differs from registry source");
            }
        }
    }

    public static SourceCatalogState empty(RomSource source) {
        return new SourceCatalogState(source, Collections.emptyMap(), Collections.emptyList(),
                Collections.emptyList(), Collections.emptyList(),
                SourceScanResult.Completeness.FULL, 0);
    }

    private static <T> List<T> sortedList(List<T> values, Comparator<T> comparator) {
        ArrayList<T> owned = new ArrayList<>(values);
        owned.sort(comparator);
        return Collections.unmodifiableList(owned);
    }
}
