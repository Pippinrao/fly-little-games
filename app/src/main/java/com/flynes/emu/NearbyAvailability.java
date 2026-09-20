package com.flynes.emu;

/**
 * Process-scoped nearby owner bootstrap. Catalog and single-player start
 * without creating a session engine; nearby entry calls {@link #ensure()}.
 * Create failures become a reason key. Linkage errors from the shared
 * emulator library are not converted into "nearby unavailable".
 */
public final class NearbyAvailability<T extends AutoCloseable> {
    public interface Factory<T extends AutoCloseable> {
        T create() throws CreateFailed;
    }

    public static final class CreateFailed extends Exception {
        public final String reasonKey;

        public CreateFailed(String reasonKey) {
            this(reasonKey, null);
        }

        public CreateFailed(String reasonKey, Throwable cause) {
            super(reasonKey, cause);
            this.reasonKey = reasonKey == null ? "nearby_blocked_session_read" : reasonKey;
        }
    }

    public static final class Status {
        private final boolean ready;
        private final String reasonKey;

        private Status(boolean ready, String reasonKey) {
            this.ready = ready;
            this.reasonKey = reasonKey;
        }

        public boolean ready() { return ready; }
        public String reasonKey() { return reasonKey; }

        static Status ok() { return new Status(true, null); }
        public static Status unavailable(String reasonKey) {
            return new Status(false, reasonKey);
        }
    }

    private final Factory<T> factory;
    private T owner;
    private String reasonKey;

    public NearbyAvailability(Factory<T> factory) {
        if (factory == null) throw new NullPointerException("factory");
        this.factory = factory;
    }

    public static <T extends AutoCloseable> Factory<T> fromIllegalState(
            Factory<T> inner, String reasonKey) {
        if (inner == null) throw new NullPointerException("inner");
        final String mapped = reasonKey == null ? "nearby_blocked_session_read" : reasonKey;
        return () -> {
            try {
                return inner.create();
            } catch (IllegalStateException failed) {
                throw new CreateFailed(mapped, failed);
            }
        };
    }

    public Status ensure() {
        if (owner != null) return Status.ok();
        try {
            T created = factory.create();
            if (created == null) {
                reasonKey = "nearby_blocked_session_read";
                return Status.unavailable(reasonKey);
            }
            owner = created;
            reasonKey = null;
            return Status.ok();
        } catch (CreateFailed failed) {
            reasonKey = failed.reasonKey;
            return Status.unavailable(reasonKey);
        }
    }

    public boolean ready() { return owner != null; }
    public boolean ownerPresent() { return owner != null; }
    public T ownerOrNull() { return owner; }
    public String reasonKey() { return reasonKey; }
}
