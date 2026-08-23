package com.flynes.emu.launch;

import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;
import com.flynes.emu.data.RomIdentity;

import java.util.LinkedHashMap;
import java.util.Map;
import java.util.Objects;

public record LaunchRequest(
        String canonicalGameId,
        String variantId,
        String sourceId,
        String sourceUri,
        String entryPath,
        PackageFormat packageFormat,
        RomFormat romFormat,
        CompatibilityState compatibility,
        RomIdentity identity,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding,
        boolean legacyZipPath) {

    private static final String CANONICAL_GAME_ID = "canonicalGameId";
    private static final String VARIANT_ID = "variantId";
    private static final String SOURCE_ID = "sourceId";
    private static final String SOURCE_URI = "sourceUri";
    private static final String ENTRY_PATH = "entryPath";
    private static final String PACKAGE_FORMAT = "packageFormat";
    private static final String ROM_FORMAT = "romFormat";
    private static final String COMPATIBILITY = "compatibility";
    private static final String ROM_SHA1 = "romSha1";
    private static final String ZIP_RAW_NAME_HEX = "zipRawNameHex";
    private static final String ZIP_LOCAL_HEADER_OFFSET = "zipLocalHeaderOffset";
    private static final String ZIP_NAME_ENCODING = "zipNameEncoding";

    public LaunchRequest(
            String canonicalGameId,
            String variantId,
            String sourceId,
            String sourceUri,
            String entryPath,
            PackageFormat packageFormat,
            RomFormat romFormat,
            CompatibilityState compatibility,
            RomIdentity identity) {
        this(canonicalGameId, variantId, sourceId, sourceUri, entryPath, packageFormat,
                romFormat, compatibility, identity, null, null,
                packageFormat == PackageFormat.ZIP);
    }

    public LaunchRequest(
            String canonicalGameId,
            String variantId,
            String sourceId,
            String sourceUri,
            String entryPath,
            PackageFormat packageFormat,
            RomFormat romFormat,
            CompatibilityState compatibility,
            RomIdentity identity,
            ZipEntryIdentity zipEntryIdentity,
            ZipNameEncoding zipNameEncoding) {
        this(canonicalGameId, variantId, sourceId, sourceUri, entryPath, packageFormat,
                romFormat, compatibility, identity, zipEntryIdentity, zipNameEncoding, false);
    }

    public LaunchRequest {
        canonicalGameId = requireNonBlank(canonicalGameId, "canonical game id");
        variantId = requireNonBlank(variantId, "variant id");
        sourceId = requireNonBlank(sourceId, "source id");
        sourceUri = requireNonBlank(sourceUri, "source URI");
        packageFormat = Objects.requireNonNull(packageFormat, "package format");
        romFormat = Objects.requireNonNull(romFormat, "ROM format");
        compatibility = Objects.requireNonNull(compatibility, "compatibility");
        identity = Objects.requireNonNull(identity, "ROM identity");
        if (!compatibility.isPlayable()) {
            throw new IllegalArgumentException("launch request compatibility must be PLAYABLE");
        }
        if (packageFormat == PackageFormat.ZIP) {
            entryPath = requireNonBlank(entryPath, "ZIP entry path");
            if (legacyZipPath) {
                if (zipEntryIdentity != null || zipNameEncoding != null) {
                    throw new IllegalArgumentException(
                            "legacy ZIP path requests must not contain an exact locator");
                }
            } else if (zipEntryIdentity == null || zipNameEncoding == null) {
                throw new IllegalArgumentException(
                        "exact ZIP requests require an entry identity and name encoding");
            }
        } else {
            if (legacyZipPath) {
                throw new IllegalArgumentException(
                        "raw ROM launch requests cannot use legacy ZIP paths");
            }
            if (entryPath != null) {
                throw new IllegalArgumentException(
                        "raw ROM launch requests must not have an entry path");
            }
            if (zipEntryIdentity != null) {
                throw new IllegalArgumentException(
                        "raw ROM launch requests must not have a ZIP entry identity");
            }
            if (zipNameEncoding != null) {
                throw new IllegalArgumentException(
                        "raw ROM launch requests must not have a ZIP name encoding");
            }
        }
    }

    public Map<String, String> toMap() {
        LinkedHashMap<String, String> values = new LinkedHashMap<>();
        values.put(CANONICAL_GAME_ID, canonicalGameId);
        values.put(VARIANT_ID, variantId);
        values.put(SOURCE_ID, sourceId);
        values.put(SOURCE_URI, sourceUri);
        if (entryPath != null) {
            values.put(ENTRY_PATH, entryPath);
        }
        values.put(PACKAGE_FORMAT, packageFormat.name());
        values.put(ROM_FORMAT, romFormat.name());
        values.put(COMPATIBILITY, compatibility.name());
        values.put(ROM_SHA1, identity.sha1());
        if (zipEntryIdentity != null) {
            values.put(ZIP_RAW_NAME_HEX, zipEntryIdentity.rawNameHex());
            values.put(ZIP_LOCAL_HEADER_OFFSET,
                    Integer.toString(zipEntryIdentity.localHeaderOffset()));
            values.put(ZIP_NAME_ENCODING, zipNameEncoding.name());
        }
        return Map.copyOf(values);
    }

    public static LaunchRequest fromMap(Map<String, String> values) {
        Objects.requireNonNull(values, "values");
        ZipEntryIdentity zipEntryIdentity = decodeZipEntryIdentity(values);
        PackageFormat packageFormat = PackageFormat.valueOf(
                requireNonBlank(values.get(PACKAGE_FORMAT), PACKAGE_FORMAT));
        return new LaunchRequest(
                values.get(CANONICAL_GAME_ID),
                values.get(VARIANT_ID),
                values.get(SOURCE_ID),
                values.get(SOURCE_URI),
                values.get(ENTRY_PATH),
                packageFormat,
                RomFormat.valueOf(requireNonBlank(values.get(ROM_FORMAT), ROM_FORMAT)),
                CompatibilityState.valueOf(requireNonBlank(
                        values.get(COMPATIBILITY), COMPATIBILITY)),
                new RomIdentity(requireNonBlank(values.get(ROM_SHA1), ROM_SHA1)),
                zipEntryIdentity,
                zipEntryIdentity == null
                        ? null
                        : ZipNameEncoding.valueOf(requireNonBlank(
                                values.get(ZIP_NAME_ENCODING), ZIP_NAME_ENCODING)),
                packageFormat == PackageFormat.ZIP && zipEntryIdentity == null);
    }

    private static ZipEntryIdentity decodeZipEntryIdentity(Map<String, String> values) {
        String rawName = values.get(ZIP_RAW_NAME_HEX);
        String offset = values.get(ZIP_LOCAL_HEADER_OFFSET);
        String encoding = values.get(ZIP_NAME_ENCODING);
        if (rawName == null && offset == null && encoding == null) {
            return null;
        }
        if (rawName == null || offset == null || encoding == null) {
            throw new IllegalArgumentException("ZIP entry identity fields must be complete");
        }
        try {
            return new ZipEntryIdentity(
                    rawName,
                    Integer.parseInt(offset));
        } catch (NumberFormatException failure) {
            throw new IllegalArgumentException("ZIP local-header offset is invalid", failure);
        }
    }

    private static String requireNonBlank(String value, String name) {
        if (value == null || value.isBlank()) {
            throw new IllegalArgumentException(name + " must not be blank");
        }
        return value;
    }
}
