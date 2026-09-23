package com.flynes.emu.gamecenter;

import com.flynes.emu.Popularity;
import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.TitleCandidate;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateCodec;
import com.flynes.emu.catalog.persistence.SourceCatalogState;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;

/** Builds the immutable, pre-ranked Game Center projection from one catalog generation. */
public final class GameCenterSnapshotProjector {
    public GameCenterSnapshot project(
            long nativeGeneration,
            String builtinFingerprint,
            long sourceEpoch,
            CatalogState state) throws CatalogStateCodec.CodecException {
        if (state == null) throw new NullPointerException("catalog state");
        GameCatalog catalog = new GameCatalog();
        catalog.publishPersistentState(
                projectedPackages(state), state.userStates(), state.lastPlayedSequence());
        ArrayList<GameCenterSnapshot.Row> rows = rows(
                catalog.canonicalEntries(), state.builtinSourceId());
        rows.sort(Comparator.comparingInt(GameCenterSnapshot.Row::popularityScore).reversed()
                .thenComparing(GameCenterSnapshot.Row::canonicalId));
        return new GameCenterSnapshot(GameCenterSnapshot.CURRENT_SCHEMA, nativeGeneration,
                builtinFingerprint, sourceEpoch, rows, sourceRows(state),
                CatalogStateCodec.encode(state));
    }

    private static ArrayList<GameCenterSnapshot.Row> rows(
            List<GameCatalogEntry> entries, String builtinSourceId) {
        ArrayList<GameCenterSnapshot.Row> rows = new ArrayList<>(entries.size());
        for (GameCatalogEntry entry : entries) {
            boolean builtin = false;
            boolean launchable = false;
            int popularity = 0;
            String originalFilename = "";
            LinkedHashSet<String> searchValues = new LinkedHashSet<>();
            addSearch(searchValues, entry.canonicalGame().englishTitle());
            addSearch(searchValues, entry.canonicalGame().zhHansTitle());
            for (TitleCandidate candidate : entry.canonicalGame().titleCandidates()) {
                addSearch(searchValues, candidate.value());
            }
            for (String alias : entry.canonicalGame().aliases()) addSearch(searchValues, alias);
            for (GameVariant variant : entry.variants()) {
                builtin |= builtinSourceId.equals(variant.sourceId());
                launchable |= variant.isLaunchable();
                if (originalFilename.isEmpty()
                        || variant.originalFilename().compareTo(originalFilename) < 0) {
                    originalFilename = variant.originalFilename();
                }
                addSearch(searchValues, variant.originalFilename());
                popularity = Math.max(popularity, Popularity.scorePackage(
                        variant.originalFilename(), variant.entryPath()));
            }
            rows.add(new GameCenterSnapshot.Row(
                    entry.canonicalGame().id(),
                    entry.canonicalGame().englishTitle(),
                    entry.canonicalGame().zhHansTitle(),
                    fallbackTitle(entry),
                    originalFilename,
                    String.join("\n", searchValues),
                    builtin,
                    entry.favorite(),
                    entry.lastPlayedSequence(),
                    entry.playCount(),
                    entry.variants().size(),
                    launchable,
                    popularity));
        }
        return rows;
    }

    private static String fallbackTitle(GameCatalogEntry entry) {
        for (TitleCandidate candidate : entry.canonicalGame().titleCandidates()) {
            if (candidate.language() == TitleCandidate.Language.UNKNOWN) {
                return candidate.value();
            }
        }
        return "";
    }

    private static void addSearch(LinkedHashSet<String> values, String value) {
        String normalized = normalize(value);
        if (!normalized.isEmpty()) values.add(normalized);
    }

    private static String normalize(String value) {
        return Normalizer.normalize(value == null ? "" : value, Normalizer.Form.NFKC)
                .toLowerCase(Locale.ROOT).trim();
    }

    private static ArrayList<GameCenterSnapshot.SourceRow> sourceRows(CatalogState state) {
        ArrayList<GameCenterSnapshot.SourceRow> rows = new ArrayList<>(state.sources().size());
        for (SourceCatalogState source : state.sources().values()) {
            rows.add(new GameCenterSnapshot.SourceRow(
                    source.source().id(), source.source().type(),
                    source.source().permissionState(), source.source().availability(),
                    source.lastScanCompleteness(), source.lastScanToken(),
                    source.packages().size()));
        }
        return rows;
    }

    private static ArrayList<PhysicalPackage> projectedPackages(CatalogState state) {
        ArrayList<PhysicalPackage> packages = new ArrayList<>();
        for (SourceCatalogState source : state.sources().values()) {
            for (CatalogPackage item : source.packages().values()) {
                CatalogPackage projected = item;
                if (!source.source().isUsable()) {
                    projected = new CatalogPackage(
                            item.physicalPackage(), CatalogPackage.Freshness.PRESERVED_STALE);
                }
                packages.add(projected.projectedPackage());
            }
        }
        return packages;
    }
}
