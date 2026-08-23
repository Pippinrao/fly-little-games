package com.flynes.emu.catalog;

public record RomSource(
        String id,
        Type type,
        String uri,
        PermissionState permissionState) {

    public RomSource {
        id = DomainValidation.requireNonBlank(id, "source id");
        type = DomainValidation.requireNonNull(type, "source type");
        uri = DomainValidation.requireNonBlank(uri, "source URI");
        permissionState = DomainValidation.requireNonNull(permissionState, "permission state");
    }

    public enum Type {
        BUILTIN,
        SAF_TREE
    }

    public enum PermissionState {
        NOT_REQUIRED,
        GRANTED,
        NEEDS_REAUTHORIZE;

        public boolean isUsable() {
            return this == NOT_REQUIRED || this == GRANTED;
        }
    }
}
