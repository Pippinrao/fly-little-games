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
        RomIdentity identity) {

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
        PhysicalPackage.validateEntryPath(packageFormat, entryPath);
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
                variant.canonicalGame().identity());
    }
}
