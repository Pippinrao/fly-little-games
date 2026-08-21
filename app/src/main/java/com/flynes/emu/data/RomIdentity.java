package com.flynes.emu.data;

import java.util.Locale;

public record RomIdentity(String sha1) {
    public RomIdentity {
        if (sha1 == null || !sha1.matches("[0-9A-Fa-f]{40}")) {
            throw new IllegalArgumentException("sha1 must be 40 hex characters");
        }
        sha1 = sha1.toUpperCase(Locale.ROOT);
    }

    public String directoryName() {
        return sha1;
    }
}
