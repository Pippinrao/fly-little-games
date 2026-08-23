package com.flynes.emu.catalog;

public record RomVariant(
        String id,
        CanonicalGame canonicalGame,
        String entryPath,
        RomFormat romFormat,
        CompatibilityState compatibility,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding) {

    public RomVariant(
            String id,
            CanonicalGame canonicalGame,
            String entryPath,
            RomFormat romFormat,
            CompatibilityState compatibility) {
        this(id, canonicalGame, entryPath, romFormat, compatibility, null, null);
    }

    public RomVariant {
        id = DomainValidation.requireNonBlank(id, "variant id");
        canonicalGame = DomainValidation.requireNonNull(canonicalGame, "canonical game");
        romFormat = DomainValidation.requireNonNull(romFormat, "ROM format");
        compatibility = DomainValidation.requireNonNull(compatibility, "compatibility");
        if ((zipEntryIdentity == null) != (zipNameEncoding == null)) {
            throw new IllegalArgumentException(
                    "ZIP entry identity and name encoding must be supplied together");
        }
        if (zipEntryIdentity != null) {
            DomainValidation.requireNonBlank(entryPath, "ZIP entry path");
        }
    }
}
