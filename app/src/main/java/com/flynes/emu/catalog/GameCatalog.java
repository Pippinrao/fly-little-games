package com.flynes.emu.catalog;

import java.text.Normalizer;
import java.util.ArrayList;
import java.util.Collections;
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
import java.util.function.Supplier;

public final class GameCatalog {
    private static final Comparator<CanonicalGame> CANONICAL_ORDER = Comparator
            .comparing(CanonicalGame::id)
            .thenComparing(CanonicalGame::englishTitle)
            .thenComparing(CanonicalGame::zhHansTitle);

    private volatile Snapshot snapshot = Snapshot.empty();
    private final Object scanCommitGate = new Object();
    private final Object launchSequenceGate = new Object();

    public boolean applyScanResult(ScanResult result) {
        DomainValidation.requireNonNull(result, "scan result");
        synchronized (scanCommitGate) {
            return applyScanResultUnderCommitGate(result);
        }
    }

    private synchronized boolean applyScanResultUnderCommitGate(ScanResult result) {
        if (!result.replaceExistingCatalog()) {
            return false;
        }

        Snapshot previous = snapshot;
        Map<String, UserState> previousUserState = new HashMap<>();
        for (GameCatalogEntry entry : previous.entries) {
            previousUserState.put(
                    entry.canonicalGame().id(),
                    new UserState(
                            entry.favorite(), entry.lastPlayedSequence(), entry.playCount()));
        }

        ArrayList<PhysicalPackage> packages = new ArrayList<>(result.packages());
        packages.sort(Comparator.comparing(PhysicalPackage::id));
        Set<String> packageIds = new HashSet<>();
        Set<String> variantIds = new HashSet<>();
        Map<String, Group> groupsByCanonicalId = new HashMap<>();

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
                String canonicalGameId = variant.canonicalGame().id();
                Group group = groupsByCanonicalId.computeIfAbsent(
                        canonicalGameId, ignored -> new Group());
                group.add(physicalPackage, variant);
            }
        }

        ArrayList<Group> groups = new ArrayList<>(groupsByCanonicalId.values());
        groups.sort(Comparator.comparing(Group::canonicalGame, CANONICAL_ORDER));
        ArrayList<GameCatalogEntry> entries = new ArrayList<>(groups.size());
        LinkedHashMap<String, GameCatalogEntry> entriesById = new LinkedHashMap<>();
        LinkedHashMap<String, GameVariant> variantsById = new LinkedHashMap<>();

        for (Group group : groups) {
            CanonicalGame canonicalGame = group.canonicalGame();
            ArrayList<GameVariant> projectedVariants = new ArrayList<>();
            for (PackageVariant pair : group.packageVariants) {
                GameVariant projected = GameVariant.from(
                        canonicalGame.id(), pair.physicalPackage, pair.variant);
                projectedVariants.add(projected);
            }
            projectedVariants.sort(Comparator.comparing(GameVariant::variantId));

            UserState state = previousUserState.getOrDefault(
                    canonicalGame.id(), UserState.EMPTY);
            GameCatalogEntry entry = new GameCatalogEntry(
                    canonicalGame,
                    projectedVariants,
                    state.favorite,
                    state.lastPlayedSequence,
                    state.playCount);
            entries.add(entry);
            entriesById.put(canonicalGame.id(), entry);
            for (GameVariant variant : projectedVariants) {
                variantsById.put(variant.variantId(), variant);
            }
        }

        snapshot = new Snapshot(
                immutableList(entries),
                immutableMap(entriesById),
                immutableMap(variantsById),
                previous.lastPlayedSequence,
                Math.addExact(previous.version, 1L));
        return true;
    }

    public List<GameCatalogEntry> canonicalEntries() {
        return snapshot.entries;
    }

    public Optional<GameVariant> resolveVariant(String variantId) {
        if (DomainValidation.isBlank(variantId)) {
            return Optional.empty();
        }
        return Optional.ofNullable(snapshot.variantsById.get(variantId));
    }

    public Optional<LaunchResolution> resolveVariantForLaunch(String variantId) {
        if (DomainValidation.isBlank(variantId)) {
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
        return immutableList(matches);
    }

    public List<GameCatalogEntry> favoriteEntries() {
        ArrayList<GameCatalogEntry> favorites = new ArrayList<>();
        for (GameCatalogEntry entry : snapshot.entries) {
            if (entry.favorite()) {
                favorites.add(entry);
            }
        }
        return immutableList(favorites);
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
        return immutableList(recent);
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
        return recordSuccessfulLaunchLocked(canonicalGameId);
    }

    private boolean recordSuccessfulLaunchLocked(String canonicalGameId) {
        GameCatalogEntry current = snapshot.entriesById.get(canonicalGameId);
        if (current == null) {
            return false;
        }
        long nextSequence = Math.addExact(snapshot.lastPlayedSequence, 1L);
        replaceEntry(current.withSuccessfulLaunch(nextSequence), nextSequence);
        return true;
    }

    /**
     * Serializes an entire launch, including its post-commit history notification, across every
     * coordinator sharing this catalog. Normal catalog reads, favorites, and scans do not acquire
     * this gate.
     */
    public <T> T serializeLaunch(Supplier<T> launch) {
        DomainValidation.requireNonNull(launch, "launch");
        synchronized (launchSequenceGate) {
            return launch.get();
        }
    }

    /**
     * Revalidates the exact loaded variant, stages its session, and records its catalog state
     * while scan publication is excluded. The gateway runs without holding this object's monitor.
     * A canonical metadata rename may rebase the returned variant; payload location changes do
     * not.
     */
    public <E extends Exception> Optional<GameVariant> commitSuccessfulLaunch(
            LaunchResolution resolution,
            SessionCommit<E> sessionCommit) throws E {
        DomainValidation.requireNonNull(resolution, "launch resolution");
        DomainValidation.requireNonNull(sessionCommit, "session commit");
        synchronized (scanCommitGate) {
            GameVariant currentVariant;
            synchronized (this) {
                currentVariant = rebaseExactLaunchVariant(resolution);
            }
            if (currentVariant == null) {
                return Optional.empty();
            }

            sessionCommit.stage(currentVariant);

            synchronized (this) {
                if (!recordSuccessfulLaunchLocked(currentVariant.canonicalGameId())) {
                    throw new IllegalStateException(
                            "validated catalog launch could not be recorded");
                }
            }
            return Optional.of(currentVariant);
        }
    }

    private GameVariant rebaseExactLaunchVariant(LaunchResolution resolution) {
        GameVariant loaded = resolution.variant();
        GameVariant current = snapshot.variantsById.get(loaded.variantId());
        if (current == null
                || !current.isLaunchable()
                || !sameExactPayloadVariant(loaded, current)) {
            return null;
        }
        return current;
    }

    private static boolean sameExactPayloadVariant(GameVariant loaded, GameVariant current) {
        return loaded.variantId().equals(current.variantId())
                && loaded.packageId().equals(current.packageId())
                && loaded.sourceId().equals(current.sourceId())
                && loaded.sourceUri().equals(current.sourceUri())
                && java.util.Objects.equals(loaded.entryPath(), current.entryPath())
                && loaded.packageFormat() == current.packageFormat()
                && loaded.romFormat() == current.romFormat()
                && loaded.hashes().payloadSha256().equals(current.hashes().payloadSha256())
                && loaded.hashes().payloadSha1().equals(current.hashes().payloadSha1())
                && loaded.hashes().physicalPackageSha256().equals(
                        current.hashes().physicalPackageSha256())
                && java.util.Objects.equals(
                        loaded.zipEntryIdentity(), current.zipEntryIdentity())
                && loaded.zipNameEncoding() == current.zipNameEncoding();
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
        snapshot = new Snapshot(
                immutableList(entries),
                immutableMap(entriesById),
                snapshot.variantsById,
                lastPlayedSequence,
                snapshot.version);
    }

    private static boolean matches(GameCatalogEntry entry, String needle) {
        CanonicalGame game = entry.canonicalGame();
        if (contains(game.englishTitle(), needle) || contains(game.zhHansTitle(), needle)) {
            return true;
        }
        for (TitleCandidate candidate : game.titleCandidates()) {
            if (contains(candidate.value(), needle)) {
                return true;
            }
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
        private final Map<TitleCandidate.Language, String> verifiedTitles = new HashMap<>();
        private CanonicalGame baseCanonicalGame;
        private CanonicalGame mergedCanonicalGame;

        void add(PhysicalPackage physicalPackage, RomVariant variant) {
            validateVerifiedTitles(variant.canonicalGame());
            packageVariants.add(new PackageVariant(physicalPackage, variant));
            if (baseCanonicalGame == null
                    || CANONICAL_ORDER.compare(
                    variant.canonicalGame(), baseCanonicalGame) < 0) {
                baseCanonicalGame = variant.canonicalGame();
            }
            mergedCanonicalGame = null;
        }

        private void validateVerifiedTitles(CanonicalGame canonicalGame) {
            for (TitleCandidate candidate : canonicalGame.titleCandidates()) {
                if (candidate.reviewState() != TitleCandidate.ReviewState.VERIFIED
                        || candidate.language() == TitleCandidate.Language.UNKNOWN) {
                    continue;
                }
                String previous = verifiedTitles.get(candidate.language());
                if (previous != null && !previous.equals(candidate.value())) {
                    throw new IllegalArgumentException(
                            "one canonical game id has conflicting verified titles");
                }
                verifiedTitles.put(candidate.language(), candidate.value());
            }
        }

        CanonicalGame canonicalGame() {
            if (mergedCanonicalGame == null) {
                mergedCanonicalGame = mergeCanonicalMetadata();
            }
            return mergedCanonicalGame;
        }

        private CanonicalGame mergeCanonicalMetadata() {
            TreeSet<String> aliases = new TreeSet<>();
            TreeSet<TitleCandidate> titleCandidates = new TreeSet<>(Comparator
                    .comparing(TitleCandidate::language)
                    .thenComparing(TitleCandidate::origin)
                    .thenComparing(TitleCandidate::confidence)
                    .thenComparing(TitleCandidate::reviewState)
                    .thenComparing(TitleCandidate::value));
            for (PackageVariant pair : packageVariants) {
                CanonicalGame game = pair.variant.canonicalGame();
                aliases.addAll(game.aliases());
                titleCandidates.addAll(game.titleCandidates());
            }
            return new CanonicalGame(
                    baseCanonicalGame.id(),
                    immutableList(titleCandidates),
                    immutableList(aliases));
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
            Map<String, GameVariant> variantsById,
            long lastPlayedSequence,
            long version) {

        static Snapshot empty() {
            return new Snapshot(
                    Collections.<GameCatalogEntry>emptyList(),
                    Collections.<String, GameCatalogEntry>emptyMap(),
                    Collections.<String, GameVariant>emptyMap(),
                    0L,
                    0L);
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
        void stage(GameVariant currentVariant) throws E;
    }

    private static <T> List<T> immutableList(java.util.Collection<? extends T> values) {
        return Collections.unmodifiableList(new ArrayList<>(values));
    }

    private static <K, V> Map<K, V> immutableMap(Map<K, V> values) {
        return Collections.unmodifiableMap(new LinkedHashMap<>(values));
    }
}
