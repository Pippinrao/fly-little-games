package com.flynes.emu.catalog.android;

import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.app.NativeCatalogEntry;
import com.flynes.emu.app.NativeSourceStatus;
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

    private NativeCatalogProjector() {
    }

    public static CatalogState project(
            List<NativeCatalogEntry> entries,
            List<NativeSourceStatus> sources,
            Map<String, CanonicalUserState> users,
            long lastPlayedSequence,
            AndroidUuidSafMap uuidMap,
            AndroidPackageLocatorMap locators) {
        Objects.requireNonNull(entries, "entries");
        Objects.requireNonNull(sources, "sources");
        Objects.requireNonNull(users, "users");
        Objects.requireNonNull(uuidMap, "uuid map");
        Objects.requireNonNull(locators, "locators");
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
            addVariant(builder, source, entry, locators);
        }
        LinkedHashMap<String, SourceCatalogState> projected = new LinkedHashMap<>();
        for (SourceBuilder builder : builders.values()) {
            projected.put(builder.source.id(), builder.build());
        }
        return new CatalogState(
                CatalogState.CURRENT_SCHEMA, Math.max(0L, lastPlayedSequence),
                builtin.id(), projected, users, lastPlayedSequence);
    }

    private static void addVariant(
            SourceBuilder builder, RomSource source, NativeCatalogEntry entry,
            AndroidPackageLocatorMap locators) {
        String relative = entry.relativePath();
        String packageId = StableIds.packageId(source.id(), relative);
        String locator = locators.get(entry.sourceUuid(), relative);
        if (locator == null || locator.trim().isEmpty()) {
            locator = source.type() == RomSource.Type.BUILTIN
                    ? AndroidBuiltinCatalogAdapter.ASSET_LOCATOR : source.uri() + "/" + relative;
        }
        PackageFormat format = entry.packageFormat() == 2 || (entry.flags() & FLAG_ZIP_ENTRY) != 0
                ? PackageFormat.ZIP : PackageFormat.RAW;
        String sha1 = hex(entry.payloadSha1());
        String sha256 = hex(entry.payloadSha256());
        String physical = hex(entry.physicalSha256());
        String crc = hex(entry.payloadCrc32());
        RomHashes hashes = new RomHashes(sha1, sha256, physical, crc);
        CanonicalGame game = new CanonicalGame(entry.canonicalId(), entry.displayName(), "",
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
        CatalogPackage.Freshness freshness = entry.freshness() == 2
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
