package com.flynes.emu.gamecenter;

import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.SourceScanResult;

import java.util.Arrays;
import java.util.HashSet;
import java.util.List;
import java.util.Objects;

/** Immutable, rebuildable Game Center projection. It never contains ROM payload bytes. */
public record GameCenterSnapshot(
        int schemaVersion,
        long nativeGeneration,
        String builtinManifestSha256,
        long sourceEpoch,
        List<Row> rows,
        List<SourceRow> sources,
        byte[] catalogStateBytes) {
    public static final int CURRENT_SCHEMA = 1;

    public GameCenterSnapshot {
        if (schemaVersion != CURRENT_SCHEMA || nativeGeneration < 0 || sourceEpoch < 0) {
            throw new IllegalArgumentException("snapshot version or generation is invalid");
        }
        if (!isLowerSha256(builtinManifestSha256)) {
            throw new IllegalArgumentException("builtin manifest fingerprint is invalid");
        }
        rows = Objects.requireNonNull(rows, "rows");
        boolean trustedLazyRows = rows instanceof LazyRows;
        if (!trustedLazyRows) rows = List.copyOf(rows);
        sources = List.copyOf(Objects.requireNonNull(sources, "sources"));
        catalogStateBytes = Objects.requireNonNull(
                catalogStateBytes, "catalog state bytes").clone();
        if (!trustedLazyRows) {
            HashSet<String> rowIds = new HashSet<>();
            for (Row row : rows) {
                Objects.requireNonNull(row, "row");
                if (!rowIds.add(row.canonicalId())) {
                    throw new IllegalArgumentException("duplicate canonical id");
                }
            }
        }
        HashSet<String> sourceIds = new HashSet<>();
        for (SourceRow source : sources) {
            Objects.requireNonNull(source, "source");
            if (!sourceIds.add(source.id())) {
                throw new IllegalArgumentException("duplicate source id");
            }
        }
    }

    /** Marker for checksum-validated, immutable row lists decoded on first access. */
    interface LazyRows { }

    @Override public byte[] catalogStateBytes() {
        return catalogStateBytes.clone();
    }

    @Override public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof GameCenterSnapshot value)) return false;
        return schemaVersion == value.schemaVersion
                && nativeGeneration == value.nativeGeneration
                && sourceEpoch == value.sourceEpoch
                && builtinManifestSha256.equals(value.builtinManifestSha256)
                && rows.equals(value.rows)
                && sources.equals(value.sources)
                && Arrays.equals(catalogStateBytes, value.catalogStateBytes);
    }

    @Override public int hashCode() {
        int result = Objects.hash(schemaVersion, nativeGeneration, builtinManifestSha256,
                sourceEpoch, rows, sources);
        return 31 * result + Arrays.hashCode(catalogStateBytes);
    }

    public record Row(
            String canonicalId,
            String titleEn,
            String titleZhHans,
            String fallbackTitle,
            String originalFilename,
            String searchText,
            boolean builtin,
            boolean favorite,
            long lastPlayedSequence,
            int playCount,
            int variantCount,
            boolean launchable,
            int popularityScore) {
        public Row {
            canonicalId = DomainValidation.requireNonBlank(canonicalId, "canonical id");
            titleEn = emptyIfNull(titleEn);
            titleZhHans = emptyIfNull(titleZhHans);
            fallbackTitle = emptyIfNull(fallbackTitle);
            originalFilename = emptyIfNull(originalFilename);
            searchText = emptyIfNull(searchText);
            if (lastPlayedSequence < 0 || playCount < 0 || variantCount < 0) {
                throw new IllegalArgumentException("row counters must not be negative");
            }
            if (launchable && variantCount == 0) {
                throw new IllegalArgumentException("launchable row must have a variant");
            }
            if (popularityScore < -1 || popularityScore > 100) {
                throw new IllegalArgumentException("popularity score is invalid");
            }
        }

        public GameCenterItem item() {
            return new GameCenterItem(canonicalId, titleEn, titleZhHans, builtin, favorite,
                    lastPlayedSequence, originalFilename, popularityScore, searchText);
        }
    }

    public record SourceRow(
            String id,
            RomSource.Type type,
            RomSource.PermissionState permissionState,
            RomSource.Availability availability,
            SourceScanResult.Completeness completeness,
            long scanToken,
            int packageCount) {
        public SourceRow {
            id = DomainValidation.requireNonBlank(id, "source id");
            type = Objects.requireNonNull(type, "source type");
            permissionState = Objects.requireNonNull(permissionState, "permission state");
            availability = Objects.requireNonNull(availability, "availability");
            completeness = Objects.requireNonNull(completeness, "completeness");
            if (scanToken < 0 || packageCount < 0) {
                throw new IllegalArgumentException("source counters must not be negative");
            }
        }
    }

    private static boolean isLowerSha256(String value) {
        if (value == null || value.length() != 64) return false;
        for (int index = 0; index < value.length(); index++) {
            char item = value.charAt(index);
            if (!((item >= '0' && item <= '9') || (item >= 'a' && item <= 'f'))) return false;
        }
        return true;
    }

    private static String emptyIfNull(String value) {
        return value == null ? "" : value;
    }
}
