package com.flynes.emu.launch;

import com.flynes.emu.catalog.CompatibilityState;
import com.flynes.emu.catalog.DomainValidation;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.catalog.PackageFormat;
import com.flynes.emu.catalog.RomFormat;
import com.flynes.emu.catalog.RomHashes;
import com.flynes.emu.catalog.ZipEntryIdentity;
import com.flynes.emu.catalog.ZipNameEncoding;
import com.flynes.emu.data.RomIdentity;

import java.util.LinkedHashMap;
import java.util.Map;

public record LaunchRequest(
        String canonicalGameId,
        String variantId,
        String sourceId,
        String sourceUri,
        String entryPath,
        PackageFormat packageFormat,
        RomFormat romFormat,
        CompatibilityState compatibility,
        RomHashes hashes,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding) {

    private static final String CANONICAL_GAME_ID = "canonicalGameId";
    private static final String VARIANT_ID = "variantId";
    private static final String SOURCE_ID = "sourceId";
    private static final String SOURCE_URI = "sourceUri";
    private static final String ENTRY_PATH = "entryPath";
    private static final String PACKAGE_FORMAT = "packageFormat";
    private static final String ROM_FORMAT = "romFormat";
    private static final String COMPATIBILITY = "compatibility";
    private static final String PAYLOAD_SHA1 = "payloadSha1";
    private static final String PAYLOAD_SHA256 = "payloadSha256";
    private static final String PHYSICAL_SHA256 = "physicalPackageSha256";
    private static final String CRC32 = "crc32";
    private static final String ZIP_RAW_NAME_HEX = "zipRawNameHex";
    private static final String ZIP_LOCAL_HEADER_OFFSET = "zipLocalHeaderOffset";
    private static final String ZIP_NAME_ENCODING = "zipNameEncoding";

    public LaunchRequest {
        canonicalGameId = DomainValidation.requireNonBlank(
                canonicalGameId, "canonical game id");
        variantId = DomainValidation.requireNonBlank(variantId, "variant id");
        sourceId = DomainValidation.requireNonBlank(sourceId, "source id");
        sourceUri = DomainValidation.requireNonBlank(sourceUri, "source URI");
        packageFormat = DomainValidation.requireNonNull(packageFormat, "package format");
        romFormat = DomainValidation.requireNonNull(romFormat, "ROM format");
        compatibility = DomainValidation.requireNonNull(compatibility, "compatibility");
        hashes = DomainValidation.requireNonNull(hashes, "ROM hashes");
        if (!compatibility.isPlayable()) {
            throw new IllegalArgumentException("launch request compatibility must be PLAYABLE");
        }
        if (packageFormat == PackageFormat.ZIP) {
            entryPath = DomainValidation.requireNonBlank(entryPath, "ZIP entry path");
            if (zipEntryIdentity == null || zipNameEncoding == null) {
                throw new IllegalArgumentException(
                        "ZIP launch requests require an exact raw-name and offset locator");
            }
        } else {
            if (entryPath != null || zipEntryIdentity != null || zipNameEncoding != null) {
                throw new IllegalArgumentException(
                        "raw launch requests must not contain ZIP entry metadata");
            }
        }
    }

    public static LaunchRequest forVariant(GameVariant variant) {
        DomainValidation.requireNonNull(variant, "game variant");
        return new LaunchRequest(
                variant.canonicalGameId(),
                variant.variantId(),
                variant.sourceId(),
                variant.sourceUri(),
                variant.entryPath(),
                variant.packageFormat(),
                variant.romFormat(),
                variant.compatibility(),
                variant.hashes(),
                variant.zipEntryIdentity(),
                variant.zipNameEncoding());
    }

    public RomIdentity identity() {
        return hashes.romIdentity();
    }

    /** Exact-locator requests are mandatory; retained only as a source-compatible query. */
    @Deprecated
    public boolean legacyZipPath() {
        return false;
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
        values.put(PAYLOAD_SHA1, hashes.payloadSha1());
        values.put(PAYLOAD_SHA256, hashes.payloadSha256());
        values.put(PHYSICAL_SHA256, hashes.physicalPackageSha256());
        values.put(CRC32, hashes.crc32());
        if (zipEntryIdentity != null) {
            values.put(ZIP_RAW_NAME_HEX, zipEntryIdentity.rawNameHex());
            values.put(ZIP_LOCAL_HEADER_OFFSET,
                    Integer.toString(zipEntryIdentity.localHeaderOffset()));
            values.put(ZIP_NAME_ENCODING, zipNameEncoding.name());
        }
        return DomainValidation.immutableMap(values);
    }

    public static LaunchRequest fromMap(Map<String, String> values) {
        DomainValidation.requireNonNull(values, "launch request values");
        PackageFormat packageFormat = PackageFormat.valueOf(
                DomainValidation.requireNonBlank(values.get(PACKAGE_FORMAT), PACKAGE_FORMAT));
        ZipEntryIdentity locator = decodeZipEntryIdentity(values, packageFormat);
        return new LaunchRequest(
                values.get(CANONICAL_GAME_ID),
                values.get(VARIANT_ID),
                values.get(SOURCE_ID),
                values.get(SOURCE_URI),
                values.get(ENTRY_PATH),
                packageFormat,
                RomFormat.valueOf(DomainValidation.requireNonBlank(
                        values.get(ROM_FORMAT), ROM_FORMAT)),
                CompatibilityState.valueOf(DomainValidation.requireNonBlank(
                        values.get(COMPATIBILITY), COMPATIBILITY)),
                new RomHashes(
                        values.get(PAYLOAD_SHA1),
                        values.get(PAYLOAD_SHA256),
                        values.get(PHYSICAL_SHA256),
                        values.get(CRC32)),
                locator,
                locator == null ? null : ZipNameEncoding.valueOf(
                        DomainValidation.requireNonBlank(
                                values.get(ZIP_NAME_ENCODING), ZIP_NAME_ENCODING)));
    }

    private static ZipEntryIdentity decodeZipEntryIdentity(
            Map<String, String> values, PackageFormat packageFormat) {
        String rawName = values.get(ZIP_RAW_NAME_HEX);
        String offset = values.get(ZIP_LOCAL_HEADER_OFFSET);
        String encoding = values.get(ZIP_NAME_ENCODING);
        if (packageFormat == PackageFormat.RAW
                && rawName == null && offset == null && encoding == null) {
            return null;
        }
        if (rawName == null || offset == null || encoding == null) {
            throw new IllegalArgumentException("ZIP entry identity fields must be complete");
        }
        try {
            return new ZipEntryIdentity(rawName, Integer.parseInt(offset));
        } catch (NumberFormatException failure) {
            throw new IllegalArgumentException("ZIP local-header offset is invalid", failure);
        }
    }
}
