package com.flynes.emu.session;

public final class SessionResult {
    private static final SessionResult SUCCESS = new SessionResult(true, 0, "");

    private final boolean success;
    private final int code;
    private final String message;

    private SessionResult(boolean success, int code, String message) {
        this.success = success;
        this.code = code;
        this.message = message;
    }

    public static SessionResult success() {
        return SUCCESS;
    }

    public static SessionResult failure(int code, String message) {
        return new SessionResult(false, code, message);
    }

    public boolean isSuccess() {
        return success;
    }

    public int code() {
        return code;
    }

    public String message() {
        return message;
    }
}
