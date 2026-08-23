package com.flynes.emu.catalog;

public record RomVariant(
        String id,
        CanonicalGame canonicalGame,
        String entryPath,
        RomFormat romFormat,
        CompatibilityState compatibility) {

    public RomVariant {
        id = DomainValidation.requireNonBlank(id, "variant id");
        canonicalGame = DomainValidation.requireNonNull(canonicalGame, "canonical game");
        romFormat = DomainValidation.requireNonNull(romFormat, "ROM format");
        compatibility = DomainValidation.requireNonNull(compatibility, "compatibility");
    }
}
