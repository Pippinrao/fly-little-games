package com.flynes.emu.catalog;

import com.flynes.emu.data.RomIdentity;

public record RomVariant(
        String id,
        CanonicalGame canonicalGame,
        String entryPath,
        RomFormat romFormat,
        CompatibilityDecision compatibilityDecision,
        RomHashes hashes,
        RomAnalysis analysis,
        ZipEntryIdentity zipEntryIdentity,
        ZipNameEncoding zipNameEncoding) {

    public RomVariant(
            String id,
            CanonicalGame canonicalGame,
            String entryPath,
            RomFormat romFormat,
            CompatibilityDecision compatibilityDecision,
            RomHashes hashes) {
        this(id, canonicalGame, entryPath, romFormat, compatibilityDecision, hashes,
                RomAnalysis.basic(0), null, null);
    }

    public RomVariant {
        id = DomainValidation.requireNonBlank(id, "variant id");
        canonicalGame = DomainValidation.requireNonNull(canonicalGame, "canonical game");
        romFormat = DomainValidation.requireNonNull(romFormat, "ROM format");
        compatibilityDecision = DomainValidation.requireNonNull(
                compatibilityDecision, "compatibility decision");
        hashes = DomainValidation.requireNonNull(hashes, "ROM hashes");
        analysis = DomainValidation.requireNonNull(analysis, "ROM analysis");
        if ((zipEntryIdentity == null) != (zipNameEncoding == null)) {
            throw new IllegalArgumentException(
                    "ZIP entry identity and name encoding must be supplied together");
        }
        if (zipEntryIdentity != null) {
            DomainValidation.requireNonBlank(entryPath, "ZIP entry path");
        }
    }

    public CompatibilityState compatibility() {
        return compatibilityDecision.state();
    }

    public RomIdentity identity() {
        return hashes.romIdentity();
    }
}
