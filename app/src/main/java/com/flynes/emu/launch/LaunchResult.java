package com.flynes.emu.launch;

import java.util.Objects;
import java.util.Optional;

public final class LaunchResult {
    private final Code code;
    private final LaunchRequest request;
    private final String message;

    private LaunchResult(Code code, LaunchRequest request, String message) {
        this.code = Objects.requireNonNull(code, "code");
        this.request = request;
        this.message = Objects.requireNonNull(message, "message");
    }

    public static LaunchResult success(LaunchRequest request) {
        return new LaunchResult(Code.SUCCESS, Objects.requireNonNull(request, "request"), "");
    }

    static LaunchResult failure(Code code, LaunchRequest request, String message) {
        if (code == Code.SUCCESS) {
            throw new IllegalArgumentException("failure code must not be SUCCESS");
        }
        return new LaunchResult(code, request, Objects.requireNonNull(message, "message"));
    }

    /**
     * Reports a failure that escaped the launch pipeline entirely. The launch executor is a bare
     * {@code execute} call, so an unreported runtime fault there terminates the process.
     */
    public static LaunchResult unexpectedFailure(String message) {
        return new LaunchResult(
                Code.SOURCE_OPEN_FAILED, null,
                message == null || message.trim().isEmpty()
                        ? "unexpected launch failure" : message);
    }

    /** Cached lobby identity no longer resolves to a live, launchable catalog entry. */
    public static LaunchResult catalogChanged(String message) {
        return new LaunchResult(
                Code.CATALOG_CHANGED, null,
                message == null || message.trim().isEmpty()
                        ? "cached game is no longer launchable" : message);
    }

    public Code code() {
        return code;
    }

    public Optional<LaunchRequest> request() {
        return Optional.ofNullable(request);
    }

    public String message() {
        return message;
    }

    public boolean isSuccess() {
        return code == Code.SUCCESS;
    }

    /**
     * Returns whether the requested ROM session was committed. A history failure happens after
     * the session and catalog commit and must not be retried as though launch never occurred.
     */
    public boolean sessionCommitted() {
        return code == Code.SUCCESS || code == Code.HISTORY_FAILED;
    }

    public enum Code {
        SUCCESS,
        VARIANT_NOT_FOUND,
        NOT_PLAYABLE,
        INVALID_REQUEST,
        CATALOG_CHANGED,
        SOURCE_OPEN_FAILED,
        IO_ERROR,
        INVALID_ZIP,
        ZIP_SOURCE_LIMIT_EXCEEDED,
        ZIP_ENTRY_LIMIT_EXCEEDED,
        ZIP_INFLATED_LIMIT_EXCEEDED,
        ZIP_ENTRY_MISSING,
        ZIP_ENTRY_DUPLICATE,
        ZIP_ENTRY_IS_DIRECTORY,
        PAYLOAD_TOO_LARGE,
        EXECUTABLE_REJECTED,
        HASH_MISMATCH,
        SESSION_FAILED,
        HISTORY_FAILED
    }
}
