package com.flynes.emu.catalog.source;

/** Pure seam around Android persisted read grants. */
public interface ReadPermissionGateway {
    int READ_FLAG = 1;

    void takeRead(String locator, int resultFlags) throws PermissionFailure;
    boolean hasPersistedRead(String locator);
    void releaseRead(String locator) throws PermissionFailure;

    final class PermissionFailure extends RuntimeException {
        private final Code code;

        public PermissionFailure(Code code) {
            super(code.name());
            this.code = code;
        }

        public PermissionFailure(Code code, Throwable cause) {
            super(code.name(), cause);
            this.code = code;
        }

        public Code code() { return code; }

        public enum Code { READ_NOT_GRANTED, TAKE_FAILED, RELEASE_FAILED }
    }
}
