package com.flynes.emu.catalog;

/** Persistable scan issue. It deliberately contains no path, URI, provider error, or message. */
public record ScanIssue(
        Code code,
        Severity severity,
        String sourceId,
        String packageId) {

    public ScanIssue {
        code = DomainValidation.requireNonNull(code, "scan issue code");
        severity = DomainValidation.requireNonNull(severity, "scan issue severity");
        if (sourceId != null) {
            sourceId = DomainValidation.requireNonBlank(sourceId, "scan issue source id");
        }
        if (packageId != null) {
            packageId = DomainValidation.requireNonBlank(packageId, "scan issue package id");
        }
    }

    /** Compatibility constructor for Task 1 callers; the free-form message is not retained. */
    @Deprecated
    public ScanIssue(Code code, Severity severity, String ignoredMessage) {
        this(code, severity, null, null);
    }

    public enum Severity {
        WARNING,
        FATAL
    }

    public enum Code {
        PERMISSION_REVOKED,
        SOURCE_UNAVAILABLE,
        IO_ERROR,
        INVALID_PACKAGE,
        OVERSIZE,
        NO_PLAYABLE_VARIANTS,
        DUPLICATE_ENTRY_NAME,
        CASE_COLLISION,
        UNICODE_PATH_REJECTED,
        OTHER
    }
}
