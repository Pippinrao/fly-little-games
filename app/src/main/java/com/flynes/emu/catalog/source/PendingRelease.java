package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.DomainValidation;

/** Typed durable intent; orphan cleanup can never delete a registered source. */
public record PendingRelease(Intent intent, String sourceId, String locator) {
    public PendingRelease {
        intent = DomainValidation.requireNonNull(intent, "pending release intent");
        sourceId = DomainValidation.requireNonBlank(sourceId, "pending source id");
        locator = DomainValidation.requireNonBlank(locator, "pending locator");
    }

    public static PendingRelease removeSource(String sourceId, String locator) {
        return new PendingRelease(Intent.REMOVE_SOURCE, sourceId, locator);
    }

    public static PendingRelease orphanGrant(String sourceId, String locator) {
        return new PendingRelease(Intent.RELEASE_ORPHAN_GRANT, sourceId, locator);
    }

    public String actionId() {
        return (intent == Intent.REMOVE_SOURCE ? "remove:" : "orphan:") + sourceId;
    }

    public enum Intent { REMOVE_SOURCE, RELEASE_ORPHAN_GRANT }
}
