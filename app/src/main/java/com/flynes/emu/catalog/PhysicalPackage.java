package com.flynes.emu.catalog;

import java.util.List;

public record PhysicalPackage(
        String id,
        RomSource source,
        String sourceUri,
        String originalFilename,
        PackageFormat packageFormat,
        List<RomVariant> variants) {

    public PhysicalPackage {
        id = DomainValidation.requireNonBlank(id, "package id");
        source = DomainValidation.requireNonNull(source, "source");
        sourceUri = DomainValidation.requireNonBlank(sourceUri, "package source URI");
        originalFilename = DomainValidation.requireNonBlank(originalFilename, "original filename");
        packageFormat = DomainValidation.requireNonNull(packageFormat, "package format");
        variants = List.copyOf(DomainValidation.requireNonNull(variants, "variants"));
        for (RomVariant variant : variants) {
            DomainValidation.requireNonNull(variant, "variant");
            validateEntryPath(packageFormat, variant.entryPath());
        }
    }

    static void validateEntryPath(PackageFormat packageFormat, String entryPath) {
        if (packageFormat == PackageFormat.ZIP) {
            DomainValidation.requireNonBlank(entryPath, "ZIP entry path");
        } else if (entryPath != null) {
            throw new IllegalArgumentException("raw ROM variants must not have an entry path");
        }
    }
}
