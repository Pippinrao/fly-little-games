package com.flynes.emu.catalog;

public record ScanIssue(
        Code code,
        Severity severity,
        String sourceId,
        String packageId,
        String message) {

    public ScanIssue {
        code = DomainValidation.requireNonNull(code, "scan issue code");
        severity = DomainValidation.requireNonNull(severity, "scan issue severity");
        if (sourceId != null) {
            sourceId = DomainValidation.requireNonBlank(sourceId, "scan issue source id");
        }
        if (packageId != null) {
            packageId = DomainValidation.requireNonBlank(packageId, "scan issue package id");
        }
        message = DomainValidation.requireNonBlank(message, "scan issue message");
    }

    public ScanIssue(Code code, Severity severity, String message) {
        this(code, severity, null, null, message);
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
        OTHER
    }
}
