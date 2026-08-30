package com.flynes.emu.catalog;

import java.util.List;

public record PhysicalPackage(
        String id,
        RomSource source,
        String sourceUri,
        String originalFilename,
        PackageFormat packageFormat,
        String physicalPackageSha256,
        List<RomVariant> variants) {

    public PhysicalPackage {
        id = DomainValidation.requireNonBlank(id, "package id");
        source = DomainValidation.requireNonNull(source, "source");
        sourceUri = DomainValidation.requireNonBlank(sourceUri, "package source URI");
        originalFilename = DomainValidation.requireNonBlank(originalFilename, "original filename");
        packageFormat = DomainValidation.requireNonNull(packageFormat, "package format");
        physicalPackageSha256 = RomHashes.normalizedSha256(
                physicalPackageSha256, "physical package SHA-256");
        variants = DomainValidation.immutableList(variants, "variants");
        for (RomVariant variant : variants) {
            validateEntryLocation(
                    packageFormat,
                    variant.entryPath(),
                    variant.zipEntryIdentity(),
                    variant.zipNameEncoding());
            if (!physicalPackageSha256.equals(variant.hashes().physicalPackageSha256())) {
                throw new IllegalArgumentException(
                        "variant physical hash does not match its physical package");
            }
        }
    }

    static void validateEntryLocation(
            PackageFormat packageFormat,
            String entryPath,
            ZipEntryIdentity zipEntryIdentity,
            ZipNameEncoding zipNameEncoding) {
        if (packageFormat == PackageFormat.ZIP) {
            DomainValidation.requireNonBlank(entryPath, "ZIP entry path");
            if (zipEntryIdentity == null || zipNameEncoding == null) {
                throw new IllegalArgumentException(
                        "ZIP variants require an exact entry identity and name encoding");
            }
        } else {
            if (entryPath != null) {
                throw new IllegalArgumentException("raw ROM variants must not have an entry path");
            }
            if (zipEntryIdentity != null || zipNameEncoding != null) {
                throw new IllegalArgumentException(
                        "raw ROM variants must not have ZIP entry metadata");
            }
        }
    }
}
