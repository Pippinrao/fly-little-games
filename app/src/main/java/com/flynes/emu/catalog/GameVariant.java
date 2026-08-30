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
        CompatibilityDecision compatibilityDecision,
        RomHashes hashes,
        RomAnalysis analysis,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding,
        RomSource.PermissionState sourcePermissionState,
        RomSource.Availability sourceAvailability) {

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
        compatibilityDecision = DomainValidation.requireNonNull(
                compatibilityDecision, "compatibility decision");
        compatibilityDecision = CompatibilityDecision.requireValidFor(
                romFormat, compatibilityDecision);
        hashes = DomainValidation.requireNonNull(hashes, "ROM hashes");
        analysis = DomainValidation.requireNonNull(analysis, "ROM analysis");
        sourcePermissionState = DomainValidation.requireNonNull(
                sourcePermissionState, "source permission state");
        sourceAvailability = DomainValidation.requireNonNull(
                sourceAvailability, "source availability");
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
                variant.compatibilityDecision(),
                variant.hashes(),
                variant.analysis(),
                variant.zipEntryIdentity(),
                variant.zipNameEncoding(),
                physicalPackage.source().permissionState(),
                physicalPackage.source().availability());
    }

    public CompatibilityState compatibility() {
        return compatibilityDecision.state();
    }

    public RomIdentity identity() {
        return hashes.romIdentity();
    }

    public boolean isLaunchable() {
        return compatibilityDecision.isPlayable()
                && sourcePermissionState.isUsable()
                && sourceAvailability == RomSource.Availability.AVAILABLE;
    }
}
