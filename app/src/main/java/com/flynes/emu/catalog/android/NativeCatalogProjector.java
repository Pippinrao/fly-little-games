package com.flynes.emu.catalog.android;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.app.NativeCatalogEntry;
import com.flynes.emu.app.NativeSourceStatus;
import com.flynes.emu.catalog.BuiltinGames;
import com.flynes.emu.catalog.CanonicalGame;
import com.flynes.emu.catalog.CompatibilityDecision;
import com.flynes.emu.catalog.CompatibilityReason;
import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomAnalysis;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.RomVariant;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;
import com.flynes.emu.catalog.persistence.CanonicalUserState;
import com.flynes.emu.catalog.persistence.CatalogPackage;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.SourceCatalogState;
import com.flynes.emu.catalog.persistence.SourceScanResult;
import com.flynes.emu.catalog.source.DocumentLocatorShape;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Collections;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Objects;

/** Builds the JVM catalog view from a native snapshot plus platform locator maps. */
public final class NativeCatalogProjector {
    private static final int FLAG_ZIP_ENTRY = 0x00000010;

    /**
     * Locator placeholder for a package whose document locator could not be resolved. It is
     * deliberately not a content URI: the package is projected as unavailable, and an accidental
     * open attempt fails the shape check instead of provoking an "Invalid URI" provider fault.
     */
    private static final String UNRESOLVED_LOCATOR_PREFIX = "unresolved://";

    private NativeCatalogProjector() {
    }

    /**
     * Platform seam used when the scan-time locator map has no entry for a package, which is the
     * normal cold-start case: locators are process-local while the native catalog is persisted.
     */
    public interface LocatorResolver {
        /** Resolver that never derives a locator, leaving the package unavailable. */
        LocatorResolver NONE = (treeLocator, relativePath) -> null;

        /** Returns an openable document locator, or {@code null} when none can be derived. */
        String resolve(String treeLocator, String relativePath);
    }

    public static CatalogState project(
            List<NativeCatalogEntry> entries,
            List<NativeSourceStatus> sources,
            Map<String, CanonicalUserState> users,
            long lastPlayedSequence,
            AndroidUuidSafMap uuidMap,
            AndroidPackageLocatorMap locators,
            LocatorResolver locatorResolver,
            BuiltinGames builtinGames) {
        Objects.requireNonNull(entries, "entries");
        Objects.requireNonNull(sources, "sources");
        Objects.requireNonNull(users, "users");
        Objects.requireNonNull(uuidMap, "uuid map");
        Objects.requireNonNull(locators, "locators");
        Objects.requireNonNull(locatorResolver, "locator resolver");
        Objects.requireNonNull(builtinGames, "builtin games");
        LinkedHashMap<String, SourceBuilder> builders = new LinkedHashMap<>();
        RomSource builtin = AndroidBuiltinCatalogAdapter.SOURCE;
        builders.put(builtin.id(), new SourceBuilder(builtin, SourceScanResult.Completeness.FULL));
        for (NativeSourceStatus status : sources) {
            if (status.sourceScope() != FlyCatalogCommands.SOURCE_SCOPE_BUILTIN
                    && uuidMap.get(status.sourceUuid()) == null) {
                continue;
            }
            RomSource source = sourceFor(status.sourceUuid(), status.sourceScope(), uuidMap);
            builders.putIfAbsent(source.id(), new SourceBuilder(source, completeness(status.lastCompleteness())));
            builders.get(source.id()).completeness = completeness(status.lastCompleteness());
        }
        for (NativeCatalogEntry entry : entries) {
            RomSource source = sourceFor(entry.sourceUuid(), entry.sourceScope(), uuidMap);
            SourceBuilder builder = builders.computeIfAbsent(
                    source.id(), ignored -> new SourceBuilder(source, SourceScanResult.Completeness.FULL));
            addVariant(builder, source, entry, locators, locatorResolver, builtinGames);
        }
        LinkedHashMap<String, SourceCatalogState> projected = new LinkedHashMap<>();
        for (SourceBuilder builder : builders.values()) {
            projected.put(builder.source.id(), builder.build());
        }
        long revision = Math.max(0L, lastPlayedSequence);
        for (CanonicalUserState user : users.values()) {
            revision = Math.max(revision, user.favoriteUpdatedRevision());
        }
        return new CatalogState(
                CatalogState.CURRENT_SCHEMA, revision,
                builtin.id(), projected, users, lastPlayedSequence);
    }

    /**
     * Titles for a bundled game come from the shared manifest. A Java-side scan
     * already carries the manifest id; a native scan derives a content id and
     * names the entry by its asset filename, so that is the second lookup. An
     * unrecognised builtin id must never be given an invented title.
     */
    private static CanonicalGame builtinGame(BuiltinGames games, String canonicalId, String relative) {
        BuiltinGames.Entry entry = games.byCanonicalId(canonicalId);
        if (entry == null) {
            entry = games.byAssetFilename(relative);
        }
        if (entry != null) {
            // Titles come from the manifest, but the identity stays whatever the
            // scan assigned: the native catalog keys favourites and play marks by
            // that id, so renaming it here would orphan them.
            return new CanonicalGame(canonicalId, entry.titleEn, entry.titleZhHans,
                    Collections.emptyList());
        }
        return new CanonicalGame(canonicalId, relative, "", Collections.emptyList());
    }

    private static void addVariant(
            SourceBuilder builder, RomSource source, NativeCatalogEntry entry,
            AndroidPackageLocatorMap locators, LocatorResolver locatorResolver,
            BuiltinGames builtinGames) {
        String relative = entry.relativePath();
        String packageId = StableIds.packageId(source.id(), relative);
        String locator = locators.get(entry.sourceUuid(), relative);
        if (source.type() == RomSource.Type.BUILTIN) {
            if (locator == null || locator.trim().isEmpty()) {
                // Bundled assets are addressed by their own filename, so a cold
                // start can always rebuild the locator without a scan.
                locator = AndroidBuiltinCatalogAdapter.assetLocator(relative);
            }
        } else {
            if (!DocumentLocatorShape.isOpenableDocumentLocator(locator)) {
                locator = locatorResolver.resolve(source.uri(), relative);
            }
            if (!DocumentLocatorShape.isOpenableDocumentLocator(locator)) {
                locator = UNRESOLVED_LOCATOR_PREFIX + source.id() + "/" + relative;
            }
        }
        boolean resolved = DocumentLocatorShape.isOpenableDocumentLocator(locator)
                || source.type() == RomSource.Type.BUILTIN;
        PackageFormat format = entry.packageFormat() == 2 || (entry.flags() & FLAG_ZIP_ENTRY) != 0
                ? PackageFormat.ZIP : PackageFormat.RAW;
        String sha1 = hex(entry.payloadSha1());
        String sha256 = hex(entry.payloadSha256());
        String physical = hex(entry.physicalSha256());
        String crc = hex(entry.payloadCrc32());
        RomHashes hashes = new RomHashes(sha1, sha256, physical, crc);
        CanonicalGame game = source.type() == RomSource.Type.BUILTIN
                ? builtinGame(builtinGames, entry.canonicalId(), relative)
                : new CanonicalGame(entry.canonicalId(), entry.displayName(), "",
                        Collections.emptyList());
        RomFormat romFormat = romFormat(entry.romFormat());
        CompatibilityDecision compatibility = compatibility(romFormat, entry.compatibilityState(),
                entry.compatibilityReason());
        RomAnalysis analysis = new RomAnalysis(
                entry.expectedBytes(), entry.payloadSize(), entry.prgBytes(), entry.chrBytes(),
                entry.mapper(), entry.submapper(), false, false, entry.diskSides(),
                Collections.emptyList());
        ZipEntryIdentity zipIdentity = null;
        ZipNameEncoding encoding = null;
        String entryPath = null;
        if (format == PackageFormat.ZIP) {
            entryPath = entry.displayName();
            zipIdentity = ZipEntryIdentity.fromRawName(
                    entryPath.getBytes(StandardCharsets.UTF_8), 0);
            encoding = ZipNameEncoding.UTF8_EFS;
        }
        RomVariant variant = new RomVariant(
                entry.variantId().isEmpty() ? StableIds.variantId(packageId, relative, sha256)
                        : entry.variantId(),
                game, entryPath, romFormat, compatibility, hashes, analysis, zipIdentity, encoding);
        CatalogPackage existing = builder.packages.get(packageId);
        ArrayList<RomVariant> variants = new ArrayList<>();
        if (existing != null) variants.addAll(existing.physicalPackage().variants());
        variants.add(variant);
        PhysicalPackage physicalPackage = new PhysicalPackage(
                packageId, source, locator, fileName(relative), format, physical, variants);
        CatalogPackage.Freshness freshness = entry.freshness() == 2 || !resolved
                ? CatalogPackage.Freshness.PRESERVED_STALE : CatalogPackage.Freshness.FRESH;
        builder.packages.put(packageId, new CatalogPackage(physicalPackage, freshness));
    }

    private static RomSource sourceFor(byte[] uuid, int scope, AndroidUuidSafMap map) {
        if (scope == FlyCatalogCommands.SOURCE_SCOPE_BUILTIN) {
            return AndroidBuiltinCatalogAdapter.SOURCE;
        }
        String uri = map.get(uuid);
        if (uri == null || uri.trim().isEmpty()) uri = "missing://" + AndroidUuidSafMap.toHex(uuid);
        boolean granted = !uri.startsWith("missing://");
        return new RomSource(
                StableIds.safSourceId(uri), RomSource.Type.SAF_TREE, uri,
                granted ? RomSource.PermissionState.GRANTED
                        : RomSource.PermissionState.NEEDS_REAUTHORIZE,
                granted ? RomSource.Availability.AVAILABLE
                        : RomSource.Availability.PERMISSION_REQUIRED);
    }

    private static SourceScanResult.Completeness completeness(int value) {
        if (value == FlyCatalogCommands.SCAN_PARTIAL) return SourceScanResult.Completeness.PARTIAL;
        if (value == FlyCatalogCommands.SCAN_FATAL) return SourceScanResult.Completeness.FATAL;
        return SourceScanResult.Completeness.FULL;
    }

    private static RomFormat romFormat(int value) {
        return switch (value) {
            case 2 -> RomFormat.NES2;
            case 3 -> RomFormat.FDS;
            case 4 -> RomFormat.UNIF;
            default -> RomFormat.INES;
        };
    }

    private static CompatibilityDecision compatibility(RomFormat format, int state, int reason) {
        CompatibilityState mappedState = switch (state) {
            case 2 -> CompatibilityState.UNSUPPORTED;
            case 3 -> CompatibilityState.INVALID;
            default -> CompatibilityState.PLAYABLE;
        };
        CompatibilityReason mappedReason = reason >= 1 && reason <= 12
                ? CompatibilityReason.values()[reason - 1] : CompatibilityReason.PLAYABLE_NES;
        try {
            return CompatibilityDecision.requireValidFor(
                    format, new CompatibilityDecision(mappedState, mappedReason));
        } catch (IllegalArgumentException invalid) {
            return switch (format) {
                case FDS -> new CompatibilityDecision(
                        CompatibilityState.UNSUPPORTED,
                        CompatibilityReason.FDS_BIOS_API_NOT_IMPLEMENTED);
                case UNIF -> new CompatibilityDecision(
                        CompatibilityState.UNSUPPORTED,
                        CompatibilityReason.UNIF_PRODUCT_DISABLED);
                default -> CompatibilityDecision.playableNes();
            };
        }
    }

    private static String hex(byte[] bytes) {
        StringBuilder hex = new StringBuilder(bytes.length * 2);
        for (byte value : bytes) {
            hex.append(String.format(Locale.ROOT, "%02X", value & 0xFF));
        }
        return hex.toString();
    }

    private static String fileName(String relativePath) {
        int slash = relativePath.lastIndexOf('/');
        return slash < 0 ? relativePath : relativePath.substring(slash + 1);
    }

    private static final class SourceBuilder {
        final RomSource source;
        SourceScanResult.Completeness completeness;
        final LinkedHashMap<String, CatalogPackage> packages = new LinkedHashMap<>();

        SourceBuilder(RomSource source, SourceScanResult.Completeness completeness) {
            this.source = source;
            this.completeness = completeness;
        }

        SourceCatalogState build() {
            return new SourceCatalogState(
                    source, packages, Collections.emptyList(), Collections.emptyList(),
                    Collections.emptyList(), completeness, 1);
        }
    }
}
