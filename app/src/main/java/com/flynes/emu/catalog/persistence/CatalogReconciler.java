package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.PackageOutcome;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.ScanIssue;

import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;

public final class CatalogReconciler {
    private CatalogReconciler() {
    }

    public static CatalogState reconcile(CatalogState current, SourceScanResult scan) {
        if (scan.baseRevision() != current.revision()) {
            throw new ReconcileException(ErrorCode.STALE_REVISION);
        }
        SourceCatalogState previous = current.sources().get(scan.sourceId());
        if (previous == null) throw new ReconcileException(ErrorCode.SOURCE_NOT_FOUND);
        if (!previous.source().equals(scan.source())) {
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
                    failedSource(scan), preserved, scan.packageOutcomes(), scan.entryOutcomes(),
                    scan.issues(), scan.completeness(), scan.scanToken()));
            return current.replace(current.revision() + 1, sources,
                    current.userStates(), current.lastPlayedSequence());
        }

        LinkedHashMap<String, CatalogPackage> next = new LinkedHashMap<>();
        if (scan.completeness() == SourceScanResult.Completeness.PARTIAL) {
            for (Map.Entry<String, CatalogPackage> item : previous.packages().entrySet()) {
                next.put(item.getKey(), new CatalogPackage(
                        item.getValue().physicalPackage(), CatalogPackage.Freshness.PRESERVED_STALE));
            }
        }
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
        Map<String, CanonicalUserState> users = migrateUserStates(
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

    private static RomSource failedSource(SourceScanResult scan) {
        boolean permissionRevoked = false;
        for (ScanIssue issue : scan.issues()) {
            permissionRevoked |= issue.code()
                    == ScanIssue.Code.PERMISSION_REVOKED;
        }
        RomSource source = scan.source();
        permissionRevoked &= source.type()
                == RomSource.Type.SAF_TREE;
        return new RomSource(
                source.id(), source.type(), source.uri(),
                permissionRevoked
                        ? RomSource.PermissionState.NEEDS_REAUTHORIZE
                        : source.permissionState(),
                permissionRevoked
                        ? RomSource.Availability.PERMISSION_REQUIRED
                        : RomSource.Availability.UNAVAILABLE);
    }

    static Map<String, CanonicalUserState> migrateUserStates(
            CatalogState oldState,
            Map<String, SourceCatalogState> newSources,
            Map<String, CanonicalUserState> existing) {
        TreeMap<String, Set<String>> oldByHash = canonicalsByHash(oldState.sources());
        TreeMap<String, Set<String>> newByHash = canonicalsByHash(newSources);
        TreeMap<String, Set<String>> predecessors = new TreeMap<>();
        for (Set<String> canonicalIds : newByHash.values()) {
            for (String id : canonicalIds) predecessors.computeIfAbsent(
                    id, ignored -> new TreeSet<>());
        }
        for (Map.Entry<String, Set<String>> item : newByHash.entrySet()) {
            Set<String> oldIds = oldByHash.get(item.getKey());
            if (oldIds == null) continue;
            for (String newId : item.getValue()) predecessors.get(newId).addAll(oldIds);
        }
        Set<String> oldRepresented = representedCanonicals(oldState.sources());
        for (String newId : predecessors.keySet()) {
            if (oldRepresented.contains(newId)) predecessors.get(newId).add(newId);
        }

        TreeMap<String, String> ownerByPredecessor = new TreeMap<>();
        for (Map.Entry<String, Set<String>> target : predecessors.entrySet()) {
            for (String predecessor : target.getValue()) {
                ownerByPredecessor.putIfAbsent(predecessor, target.getKey());
            }
        }

        LinkedHashMap<String, CanonicalUserState> migrated = new LinkedHashMap<>();
        for (Map.Entry<String, Set<String>> target : predecessors.entrySet()) {
            TreeMap<String, CanonicalUserState> states = new TreeMap<>();
            for (String predecessor : target.getValue()) {
                if (!target.getKey().equals(ownerByPredecessor.get(predecessor))) continue;
                CanonicalUserState value = existing.get(predecessor);
                if (value != null) states.put(predecessor, value);
            }
            CanonicalUserState merged = mergeUserStates(states);
            if (merged != null) migrated.put(target.getKey(), merged);
        }
        return Collections.unmodifiableMap(migrated);
    }

    private static TreeMap<String, Set<String>> canonicalsByHash(
            Map<String, SourceCatalogState> sources) {
        TreeMap<String, Set<String>> values = new TreeMap<>();
        for (SourceCatalogState source : sources.values()) {
            for (CatalogPackage item : source.packages().values()) {
                for (RomVariant variant : item.physicalPackage().variants()) {
                    values.computeIfAbsent(
                            variant.hashes().payloadSha256(), ignored -> new TreeSet<>())
                            .add(variant.canonicalGame().id());
                }
            }
        }
        return values;
    }

    private static Set<String> representedCanonicals(
            Map<String, SourceCatalogState> sources) {
        TreeSet<String> represented = new TreeSet<>();
        for (Set<String> ids : canonicalsByHash(sources).values()) represented.addAll(ids);
        return represented;
    }

    private static CanonicalUserState mergeUserStates(
            Map<String, CanonicalUserState> states) {
        if (states.isEmpty()) return null;
        boolean favorite = false;
        long favoriteRevision = -1;
        long lastPlayed = 0;
        int playCount = 0;
        for (CanonicalUserState value : states.values()) {
            if (value.favoriteUpdatedRevision() > favoriteRevision) {
                favorite = value.favorite();
                favoriteRevision = value.favoriteUpdatedRevision();
            }
            lastPlayed = Math.max(lastPlayed, value.lastPlayedSequence());
            playCount = Math.addExact(playCount, value.playCount());
        }
        return new CanonicalUserState(
                favorite, Math.max(0, favoriteRevision), lastPlayed, playCount);
    }

    public enum ErrorCode { STALE_REVISION, STALE_SCAN_TOKEN, SOURCE_NOT_FOUND, INVALID_SCAN }

    public static final class ReconcileException extends IllegalStateException {
        private final ErrorCode code;
        ReconcileException(ErrorCode code) { super(code.name()); this.code = code; }
        public ErrorCode code() { return code; }
    }
}
