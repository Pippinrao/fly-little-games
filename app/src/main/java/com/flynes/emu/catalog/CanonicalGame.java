package com.flynes.emu.catalog;

import com.flynes.emu.data.RomIdentity;

import java.util.ArrayList;
import java.util.List;

public record CanonicalGame(
        String id,
        RomIdentity identity,
        String englishTitle,
        String zhHansTitle,
        List<String> aliases) {

    public CanonicalGame {
        id = DomainValidation.requireNonBlank(id, "canonical game id");
        identity = DomainValidation.requireNonNull(identity, "ROM identity");
        englishTitle = englishTitle == null ? "" : englishTitle;
        zhHansTitle = zhHansTitle == null ? "" : zhHansTitle;
        DomainValidation.requireNonNull(aliases, "aliases");
        ArrayList<String> ownedAliases = new ArrayList<>(aliases.size());
        for (String alias : aliases) {
            ownedAliases.add(DomainValidation.requireNonBlank(alias, "alias"));
        }
        aliases = List.copyOf(ownedAliases);
    }
}
