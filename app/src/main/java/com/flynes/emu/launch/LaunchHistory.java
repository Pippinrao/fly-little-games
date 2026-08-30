package com.flynes.emu.launch;

import java.util.Objects;

@FunctionalInterface
public interface LaunchHistory {
    /**
     * Records an already-committed launch. Implementations should make their own write atomic and
     * throw {@link HistoryException} when it cannot be persisted.
     */
    void recordSuccessfulLaunch(LaunchRequest request) throws HistoryException;

    final class HistoryException extends Exception {
        public HistoryException(String message) {
            super(Objects.requireNonNull(message, "message"));
        }

        public HistoryException(String message, Throwable cause) {
            super(Objects.requireNonNull(message, "message"), cause);
        }
    }
}
