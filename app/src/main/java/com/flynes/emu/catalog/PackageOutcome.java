package com.flynes.emu.catalog;

/** Exactly one deterministic outcome is emitted for each enumerated physical package. */
public record PackageOutcome(
        String packageId,
        Status status,
        Reason reason) {

    public PackageOutcome {
        packageId = DomainValidation.requireNonBlank(packageId, "package outcome id");
        status = DomainValidation.requireNonNull(status, "package outcome status");
        reason = DomainValidation.requireNonNull(reason, "package outcome reason");
    }

    public boolean accounted() {
        return true;
    }

    public enum Status {
        INDEXED,
        SKIPPED,
        ERROR
    }

    public enum Reason {
        INDEXED,
        NO_SUPPORTED_PAYLOADS,
        UNKNOWN_FORMAT,
        DUPLICATE_DOCUMENT_KEY,
        SOURCE_UNAVAILABLE,
        OPEN_FAILED,
        IO_ERROR,
        PACKAGE_LIMIT_EXCEEDED,
        PAYLOAD_LIMIT_EXCEEDED,
        INVALID_ZIP,
        ZIP_ENTRY_LIMIT_EXCEEDED,
        ZIP_INFLATED_LIMIT_EXCEEDED,
        ZIP_NAME_LIMIT_EXCEEDED,
        ZIP_RATIO_LIMIT_EXCEEDED
    }
}
