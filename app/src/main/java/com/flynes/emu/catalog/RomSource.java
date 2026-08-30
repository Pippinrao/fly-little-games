package com.flynes.emu.catalog;

public record RomSource(
        String id,
        Type type,
        String uri,
        PermissionState permissionState,
        Availability availability) {

    public RomSource {
        id = DomainValidation.requireNonBlank(id, "source id");
        type = DomainValidation.requireNonNull(type, "source type");
        uri = DomainValidation.requireNonBlank(uri, "source URI");
        permissionState = DomainValidation.requireNonNull(permissionState, "permission state");
        availability = DomainValidation.requireNonNull(availability, "source availability");
    }

    public RomSource(String id, Type type, String uri, PermissionState permissionState) {
        this(id, type, uri, permissionState,
                permissionState == PermissionState.NEEDS_REAUTHORIZE
                        ? Availability.PERMISSION_REQUIRED : Availability.AVAILABLE);
    }

    public boolean isUsable() {
        return permissionState.isUsable() && availability == Availability.AVAILABLE;
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

    public enum Availability {
        AVAILABLE,
        PERMISSION_REQUIRED,
        UNAVAILABLE
    }
}
