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

    public enum Code {
        SUCCESS,
        VARIANT_NOT_FOUND,
        NOT_PLAYABLE,
        INVALID_REQUEST,
        SOURCE_OPEN_FAILED,
        IO_ERROR,
        INVALID_ZIP,
        ZIP_ENTRY_MISSING,
        ZIP_ENTRY_DUPLICATE,
        ZIP_ENTRY_IS_DIRECTORY,
        PAYLOAD_TOO_LARGE,
        EXECUTABLE_REJECTED,
        HASH_MISMATCH,
        SESSION_FAILED
    }
}
