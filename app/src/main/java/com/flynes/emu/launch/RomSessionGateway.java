package com.flynes.emu.launch;

import java.util.Objects;

@FunctionalInterface
public interface RomSessionGateway {
    /**
     * Stages and atomically replaces the active ROM session. Implementations must leave the
     * previous session untouched when this method throws.
     */
    void stageAndReplace(LaunchRequest request, byte[] romBytes) throws SessionException;

    final class SessionException extends Exception {
        public SessionException(String message) {
            super(Objects.requireNonNull(message, "message"));
        }

        public SessionException(String message, Throwable cause) {
            super(Objects.requireNonNull(message, "message"), cause);
        }
    }
}
