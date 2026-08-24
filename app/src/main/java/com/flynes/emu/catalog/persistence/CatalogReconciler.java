package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomVariant;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.TreeMap;

public final class CatalogReconciler {
    private CatalogReconciler() {
    }

    public static CatalogState reconcile(CatalogState current, SourceScanResult scan) {
        if (scan.baseRevision() != current.revision()) {
            throw new ReconcileException(ErrorCode.STALE_REVISION);
        }
        SourceCatalogState previous = current.sources().get(scan.sourceId());
        if (previous == null) throw new ReconcileException(ErrorCode.SOURCE_NOT_FOUND);
        if (previous.source().type() == com.flynes.emu.catalog.RomSource.Type.BUILTIN
                && !previous.source().equals(scan.source())) {
            throw new ReconcileException(ErrorCode.INVALID_SCAN);
        }
        if (scan.scanToken() <= previous.lastScanToken()) {
            throw new ReconcileException(ErrorCode.STALE_SCAN_TOKEN);
        }

        LinkedHashMap<String, SourceCatalogState> sources = new LinkedHashMap<>(current.sources());
        if (scan.completeness() == SourceScanResult.Completeness.FATAL) {
            LinkedHashMap<String, CatalogPackage> preserved = new LinkedHashMap<>();
            for (Map.Entry<String, CatalogPackage> item : previous.packages().entrySet()) {
                preserved.put(item.getKey(), new CatalogPackage(
                        item.getValue().physicalPackage(), CatalogPackage.Freshness.PRESERVED_STALE));
            }
            sources.put(scan.sourceId(), new SourceCatalogState(
                    scan.source(), preserved, scan.packageOutcomes(), scan.entryOutcomes(),
                    scan.issues(), scan.completeness(), scan.scanToken()));
            return current.replace(current.revision() + 1, sources,
                    current.userStates(), current.lastPlayedSequence());
        }

        LinkedHashMap<String, CatalogPackage> next = scan.completeness()
                == SourceScanResult.Completeness.FULL
                ? new LinkedHashMap<>()
                : new LinkedHashMap<>(previous.packages());
        Map<String, PhysicalPackage> scannedPackages = new TreeMap<>();
        for (PhysicalPackage item : scan.packages()) scannedPackages.put(item.id(), item);
        for (PackageOutcome outcome : scan.packageOutcomes()) {
            PhysicalPackage indexed = scannedPackages.get(outcome.packageId());
            if (outcome.status() == PackageOutcome.Status.INDEXED) {
                if (indexed == null) throw new ReconcileException(ErrorCode.INVALID_SCAN);
                next.put(outcome.packageId(), new CatalogPackage(
                        indexed, CatalogPackage.Freshness.FRESH));
            } else if (outcome.status() == PackageOutcome.Status.ERROR) {
                CatalogPackage old = previous.packages().get(outcome.packageId());
                if (old != null) {
                    next.put(outcome.packageId(), new CatalogPackage(
                            old.physicalPackage(), CatalogPackage.Freshness.PRESERVED_STALE));
                }
            } else {
                next.remove(outcome.packageId());
            }
        }
        for (PhysicalPackage item : scan.packages()) {
            if (!hasIndexedOutcome(scan, item.id())) {
                throw new ReconcileException(ErrorCode.INVALID_SCAN);
            }
        }
        sources.put(scan.sourceId(), new SourceCatalogState(
                scan.source(), next, scan.packageOutcomes(), scan.entryOutcomes(), scan.issues(),
                scan.completeness(), scan.scanToken()));
        Map<String, CanonicalUserState> users = migrateByPayloadHash(
                current, sources, current.userStates());
        return current.replace(current.revision() + 1, sources, users,
                current.lastPlayedSequence());
    }

    private static boolean hasIndexedOutcome(SourceScanResult scan, String id) {
        for (PackageOutcome outcome : scan.packageOutcomes()) {
            if (outcome.packageId().equals(id)
                    && outcome.status() == PackageOutcome.Status.INDEXED) return true;
        }
        return false;
    }

    private static Map<String, CanonicalUserState> migrateByPayloadHash(
            CatalogState oldState,
            Map<String, SourceCatalogState> newSources,
            Map<String, CanonicalUserState> existing) {
        TreeMap<String, String> oldCanonicalByHash = canonicalByHash(oldState.sources());
        TreeMap<String, String> newCanonicalByHash = canonicalByHash(newSources);
        LinkedHashMap<String, CanonicalUserState> users = new LinkedHashMap<>(existing);
        for (Map.Entry<String, String> item : newCanonicalByHash.entrySet()) {
            String oldCanonical = oldCanonicalByHash.get(item.getKey());
            String newCanonical = item.getValue();
            if (oldCanonical != null && !users.containsKey(newCanonical)) {
                CanonicalUserState oldUser = users.get(oldCanonical);
                if (oldUser != null) users.put(newCanonical, oldUser);
            }
        }
        return Collections.unmodifiableMap(users);
    }

    private static TreeMap<String, String> canonicalByHash(
            Map<String, SourceCatalogState> sources) {
        TreeMap<String, String> values = new TreeMap<>();
        for (SourceCatalogState source : sources.values()) {
            for (CatalogPackage item : source.packages().values()) {
                for (RomVariant variant : item.physicalPackage().variants()) {
                    values.putIfAbsent(
                            variant.hashes().payloadSha256(), variant.canonicalGame().id());
                }
            }
        }
        return values;
    }

    public enum ErrorCode { STALE_REVISION, STALE_SCAN_TOKEN, SOURCE_NOT_FOUND, INVALID_SCAN }

    public static final class ReconcileException extends IllegalStateException {
        private final ErrorCode code;
        ReconcileException(ErrorCode code) { super(code.name()); this.code = code; }
        public ErrorCode code() { return code; }
    }
}
