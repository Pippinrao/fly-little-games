package com.flynes.emu.catalog;

/** Privacy-safe accounting for a raw payload or one ZIP central-directory entry. */
public record EntryOutcome(
        String packageId,
        String entryId,
        Status status,
        Reason reason) {

    public EntryOutcome {
        packageId = DomainValidation.requireNonBlank(packageId, "entry package id");
        entryId = DomainValidation.requireNonBlank(entryId, "entry outcome id");
        status = DomainValidation.requireNonNull(status, "entry outcome status");
        reason = DomainValidation.requireNonNull(reason, "entry outcome reason");
    }

    public enum Status {
        INDEXED,
        SKIPPED,
        ERROR
    }

    public enum Reason {
        INDEXED,
        DIRECTORY,
        INVALID_PATH,
        NESTED_ARCHIVE,
        EXECUTABLE,
        GAME_BOY,
        SIDECAR,
        UNKNOWN_FORMAT,
        INVALID_ROM,
        PAYLOAD_LIMIT_EXCEEDED
    }
}
