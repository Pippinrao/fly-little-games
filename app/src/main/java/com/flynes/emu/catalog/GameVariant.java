package com.flynes.emu.catalog;

import com.flynes.emu.data.RomIdentity;

public record GameVariant(
        String canonicalGameId,
        String variantId,
        String packageId,
        String sourceId,
        String sourceUri,
        String originalFilename,
        String entryPath,
        PackageFormat packageFormat,
        RomFormat romFormat,
        CompatibilityState compatibility,
        RomIdentity identity,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding,
        RomSource.PermissionState sourcePermissionState) {

    public GameVariant(
            String canonicalGameId,
            String variantId,
            String packageId,
            String sourceId,
            String sourceUri,
            String originalFilename,
            String entryPath,
            PackageFormat packageFormat,
            RomFormat romFormat,
            CompatibilityState compatibility,
            RomIdentity identity) {
        this(canonicalGameId, variantId, packageId, sourceId, sourceUri,
                originalFilename, entryPath, packageFormat, romFormat, compatibility,
                identity, null, null, RomSource.PermissionState.NOT_REQUIRED);
    }

    public GameVariant {
        canonicalGameId = DomainValidation.requireNonBlank(
                canonicalGameId, "canonical game id");
        variantId = DomainValidation.requireNonBlank(variantId, "variant id");
        packageId = DomainValidation.requireNonBlank(packageId, "package id");
        sourceId = DomainValidation.requireNonBlank(sourceId, "source id");
        sourceUri = DomainValidation.requireNonBlank(sourceUri, "source URI");
        originalFilename = DomainValidation.requireNonBlank(
                originalFilename, "original filename");
        packageFormat = DomainValidation.requireNonNull(packageFormat, "package format");
        romFormat = DomainValidation.requireNonNull(romFormat, "ROM format");
        compatibility = DomainValidation.requireNonNull(compatibility, "compatibility");
        identity = DomainValidation.requireNonNull(identity, "ROM identity");
        sourcePermissionState = DomainValidation.requireNonNull(
                sourcePermissionState, "source permission state");
        PhysicalPackage.validateEntryLocation(
                packageFormat, entryPath, zipEntryIdentity, zipNameEncoding);
    }

    static GameVariant from(
            String canonicalGameId,
            PhysicalPackage physicalPackage,
            RomVariant variant) {
        return new GameVariant(
                canonicalGameId,
                variant.id(),
                physicalPackage.id(),
                physicalPackage.source().id(),
                physicalPackage.sourceUri(),
                physicalPackage.originalFilename(),
                variant.entryPath(),
                physicalPackage.packageFormat(),
                variant.romFormat(),
                variant.compatibility(),
                variant.canonicalGame().identity(),
                variant.zipEntryIdentity(),
                variant.zipNameEncoding(),
                physicalPackage.source().permissionState());
    }
}
