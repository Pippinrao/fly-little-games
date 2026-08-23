package com.flynes.emu.catalog;

import com.flynes.emu.data.RomIdentity;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashMap;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Optional;
import java.util.Set;
import java.util.TreeSet;

public final class GameCatalog {
    private static final Comparator<CanonicalGame> CANONICAL_ORDER = Comparator
            .comparing(CanonicalGame::id)
            .thenComparing(CanonicalGame::englishTitle)
            .thenComparing(CanonicalGame::zhHansTitle);

    private volatile Snapshot snapshot = Snapshot.empty();

    public synchronized boolean applyScanResult(ScanResult result) {
        DomainValidation.requireNonNull(result, "scan result");
        if (!result.replaceExistingCatalog()) {
            return false;
        }

        Snapshot previous = snapshot;
        Map<RomIdentity, UserState> previousUserState = new HashMap<>();
        for (GameCatalogEntry entry : previous.entries) {
            previousUserState.put(
                    entry.canonicalGame().identity(),
                    new UserState(
                            entry.favorite(), entry.lastPlayedSequence(), entry.playCount()));
        }

        ArrayList<PhysicalPackage> packages = new ArrayList<>(result.packages());
        packages.sort(Comparator.comparing(PhysicalPackage::id));
        Set<String> packageIds = new HashSet<>();
        Set<String> variantIds = new HashSet<>();
        Map<RomIdentity, Group> groupsByIdentity = new HashMap<>();

        for (PhysicalPackage physicalPackage : packages) {
            if (!packageIds.add(physicalPackage.id())) {
                throw new IllegalArgumentException(
                        "duplicate package id: " + physicalPackage.id());
            }
            ArrayList<RomVariant> variants = new ArrayList<>(physicalPackage.variants());
            variants.sort(Comparator.comparing(RomVariant::id));
            for (RomVariant variant : variants) {
                if (!variantIds.add(variant.id())) {
                    throw new IllegalArgumentException("duplicate variant id: " + variant.id());
                }
                RomIdentity identity = variant.canonicalGame().identity();
                Group group = groupsByIdentity.computeIfAbsent(identity, ignored -> new Group());
                group.add(physicalPackage, variant);
            }
        }

        ArrayList<Group> groups = new ArrayList<>(groupsByIdentity.values());
        groups.sort(Comparator.comparing(Group::canonicalGame, CANONICAL_ORDER));
        ArrayList<GameCatalogEntry> entries = new ArrayList<>(groups.size());
        LinkedHashMap<String, GameCatalogEntry> entriesById = new LinkedHashMap<>();
        LinkedHashMap<RomIdentity, GameCatalogEntry> entriesByIdentity = new LinkedHashMap<>();
        LinkedHashMap<String, GameVariant> variantsById = new LinkedHashMap<>();

        for (Group group : groups) {
            CanonicalGame canonicalGame = group.canonicalGame();
            if (entriesById.containsKey(canonicalGame.id())) {
                throw new IllegalArgumentException(
                        "canonical game id maps to multiple identities: " + canonicalGame.id());
            }
            ArrayList<GameVariant> projectedVariants = new ArrayList<>();
            for (PackageVariant pair : group.packageVariants) {
                GameVariant projected = GameVariant.from(
                        canonicalGame.id(), pair.physicalPackage, pair.variant);
                projectedVariants.add(projected);
            }
            projectedVariants.sort(Comparator.comparing(GameVariant::variantId));

            UserState state = previousUserState.getOrDefault(
                    canonicalGame.identity(), UserState.EMPTY);
            GameCatalogEntry entry = new GameCatalogEntry(
                    canonicalGame,
                    projectedVariants,
                    state.favorite,
                    state.lastPlayedSequence,
                    state.playCount);
            entries.add(entry);
            entriesById.put(canonicalGame.id(), entry);
            entriesByIdentity.put(canonicalGame.identity(), entry);
            for (GameVariant variant : projectedVariants) {
                variantsById.put(variant.variantId(), variant);
            }
        }

        snapshot = new Snapshot(
                List.copyOf(entries),
                Map.copyOf(entriesById),
                Map.copyOf(entriesByIdentity),
                Map.copyOf(variantsById),
                previous.lastPlayedSequence,
                Math.addExact(previous.version, 1L));
        return true;
    }

    public List<GameCatalogEntry> canonicalEntries() {
        return snapshot.entries;
    }

    public Optional<GameVariant> resolveVariant(String variantId) {
        if (variantId == null || variantId.isBlank()) {
            return Optional.empty();
        }
        return Optional.ofNullable(snapshot.variantsById.get(variantId));
    }

    public Optional<LaunchResolution> resolveVariantForLaunch(String variantId) {
        if (variantId == null || variantId.isBlank()) {
            return Optional.empty();
        }
        Snapshot current = snapshot;
        GameVariant variant = current.variantsById.get(variantId);
        if (variant == null) {
            return Optional.empty();
        }
        return Optional.of(new LaunchResolution(current.version, variant));
    }

    public List<GameCatalogEntry> search(String query) {
        String needle = normalize(query == null ? "" : query);
        if (needle.isEmpty()) {
            return canonicalEntries();
        }
        ArrayList<GameCatalogEntry> matches = new ArrayList<>();
        for (GameCatalogEntry entry : snapshot.entries) {
            if (matches(entry, needle)) {
                matches.add(entry);
            }
        }
        return List.copyOf(matches);
    }

    public List<GameCatalogEntry> favoriteEntries() {
        ArrayList<GameCatalogEntry> favorites = new ArrayList<>();
        for (GameCatalogEntry entry : snapshot.entries) {
            if (entry.favorite()) {
                favorites.add(entry);
            }
        }
        return List.copyOf(favorites);
    }

    public List<GameCatalogEntry> recentEntries() {
        ArrayList<GameCatalogEntry> recent = new ArrayList<>();
        for (GameCatalogEntry entry : snapshot.entries) {
            if (entry.isRecent()) {
                recent.add(entry);
            }
        }
        recent.sort(Comparator.comparingLong(
                GameCatalogEntry::lastPlayedSequence).reversed());
        return List.copyOf(recent);
    }

    public synchronized boolean setFavorite(String canonicalGameId, boolean favorite) {
        GameCatalogEntry current = snapshot.entriesById.get(canonicalGameId);
        if (current == null) {
            return false;
        }
        replaceEntry(current.withFavorite(favorite), snapshot.lastPlayedSequence);
        return true;
    }

    public synchronized boolean recordSuccessfulLaunch(String canonicalGameId) {
        GameCatalogEntry current = snapshot.entriesById.get(canonicalGameId);
        if (current == null) {
            return false;
        }
        long nextSequence = Math.addExact(snapshot.lastPlayedSequence, 1L);
        replaceEntry(current.withSuccessfulLaunch(nextSequence), nextSequence);
        return true;
    }

    /**
     * Revalidates a launch against the current catalog, stages its session, and records the
     * catalog launch as one catalog-locked commit. A scan may rename IDs while preserving the
     * same ROM identity; an absent identity rejects the commit before {@code sessionCommit} runs.
     */
    public synchronized <E extends Exception> boolean commitSuccessfulLaunch(
            LaunchResolution resolution,
            SessionCommit<E> sessionCommit) throws E {
        DomainValidation.requireNonNull(resolution, "launch resolution");
        DomainValidation.requireNonNull(sessionCommit, "session commit");
        Snapshot current = snapshot;
        GameVariant resolvedVariant = resolution.variant();
        GameCatalogEntry currentEntry = current.entriesByIdentity.get(resolvedVariant.identity());
        if (currentEntry == null) {
            return false;
        }
        if (current.version == resolution.catalogVersion()) {
            GameVariant currentVariant = current.variantsById.get(resolvedVariant.variantId());
            if (!resolvedVariant.equals(currentVariant)) {
                return false;
            }
        }

        long nextSequence = Math.addExact(current.lastPlayedSequence, 1L);
        GameCatalogEntry replacement = currentEntry.withSuccessfulLaunch(nextSequence);
        sessionCommit.stage();
        replaceEntry(replacement, nextSequence);
        return true;
    }

    private void replaceEntry(GameCatalogEntry replacement, long lastPlayedSequence) {
        ArrayList<GameCatalogEntry> entries = new ArrayList<>(snapshot.entries);
        for (int i = 0; i < entries.size(); i++) {
            if (entries.get(i).canonicalGame().id().equals(replacement.canonicalGame().id())) {
                entries.set(i, replacement);
                break;
            }
        }
        LinkedHashMap<String, GameCatalogEntry> entriesById =
                new LinkedHashMap<>(snapshot.entriesById);
        entriesById.put(replacement.canonicalGame().id(), replacement);
        LinkedHashMap<RomIdentity, GameCatalogEntry> entriesByIdentity =
                new LinkedHashMap<>(snapshot.entriesByIdentity);
        entriesByIdentity.put(replacement.canonicalGame().identity(), replacement);
        snapshot = new Snapshot(
                List.copyOf(entries),
                Map.copyOf(entriesById),
                Map.copyOf(entriesByIdentity),
                snapshot.variantsById,
                lastPlayedSequence,
                snapshot.version);
    }

    private static boolean matches(GameCatalogEntry entry, String needle) {
        CanonicalGame game = entry.canonicalGame();
        if (contains(game.englishTitle(), needle) || contains(game.zhHansTitle(), needle)) {
            return true;
        }
        for (String alias : game.aliases()) {
            if (contains(alias, needle)) {
                return true;
            }
        }
        for (GameVariant variant : entry.variants()) {
            if (contains(variant.originalFilename(), needle)) {
                return true;
            }
        }
        return false;
    }

    private static boolean contains(String value, String needle) {
        return normalize(value).contains(needle);
    }

    private static String normalize(String value) {
        return Normalizer.normalize(value, Normalizer.Form.NFKC)
                .toLowerCase(Locale.ROOT)
                .trim();
    }

    private static final class Group {
        private final ArrayList<PackageVariant> packageVariants = new ArrayList<>();
        private CanonicalGame baseCanonicalGame;
        private CanonicalGame mergedCanonicalGame;

        void add(PhysicalPackage physicalPackage, RomVariant variant) {
            packageVariants.add(new PackageVariant(physicalPackage, variant));
            if (baseCanonicalGame == null
                    || CANONICAL_ORDER.compare(
                    variant.canonicalGame(), baseCanonicalGame) < 0) {
                baseCanonicalGame = variant.canonicalGame();
            }
            mergedCanonicalGame = null;
        }

        CanonicalGame canonicalGame() {
            if (mergedCanonicalGame == null) {
                mergedCanonicalGame = mergeCanonicalMetadata();
            }
            return mergedCanonicalGame;
        }

        private CanonicalGame mergeCanonicalMetadata() {
            TreeSet<String> englishTitles = new TreeSet<>();
            TreeSet<String> chineseTitles = new TreeSet<>();
            TreeSet<String> aliases = new TreeSet<>();
            for (PackageVariant pair : packageVariants) {
                CanonicalGame game = pair.variant.canonicalGame();
                addIfNotBlank(englishTitles, game.englishTitle());
                addIfNotBlank(chineseTitles, game.zhHansTitle());
                aliases.addAll(game.aliases());
            }

            String englishTitle = preferredTitle(
                    baseCanonicalGame.englishTitle(), englishTitles);
            String zhHansTitle = preferredTitle(
                    baseCanonicalGame.zhHansTitle(), chineseTitles);
            aliases.addAll(englishTitles);
            aliases.addAll(chineseTitles);
            aliases.remove(englishTitle);
            aliases.remove(zhHansTitle);
            return new CanonicalGame(
                    baseCanonicalGame.id(),
                    baseCanonicalGame.identity(),
                    englishTitle,
                    zhHansTitle,
                    List.copyOf(aliases));
        }

        private static String preferredTitle(String preferred, TreeSet<String> candidates) {
            return preferred.isBlank() && !candidates.isEmpty() ? candidates.first() : preferred;
        }

        private static void addIfNotBlank(Set<String> values, String value) {
            if (!value.isBlank()) {
                values.add(value);
            }
        }
    }

    private record PackageVariant(PhysicalPackage physicalPackage, RomVariant variant) {
    }

    private record UserState(boolean favorite, long lastPlayedSequence, int playCount) {
        private static final UserState EMPTY = new UserState(false, 0L, 0);
    }

    private record Snapshot(
            List<GameCatalogEntry> entries,
            Map<String, GameCatalogEntry> entriesById,
            Map<RomIdentity, GameCatalogEntry> entriesByIdentity,
            Map<String, GameVariant> variantsById,
            long lastPlayedSequence,
            long version) {

        static Snapshot empty() {
            return new Snapshot(List.of(), Map.of(), Map.of(), Map.of(), 0L, 0L);
        }
    }

    public record LaunchResolution(long catalogVersion, GameVariant variant) {
        public LaunchResolution {
            if (catalogVersion < 0) {
                throw new IllegalArgumentException("catalog version must not be negative");
            }
            variant = DomainValidation.requireNonNull(variant, "variant");
        }
    }

    @FunctionalInterface
    public interface SessionCommit<E extends Exception> {
        void stage() throws E;
    }
}
