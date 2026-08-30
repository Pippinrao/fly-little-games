package com.flynes.emu.catalog.persistence;

import com.flynes.emu.catalog.PhysicalPackage;
import com.flynes.emu.catalog.RomSource;

public record CatalogPackage(PhysicalPackage physicalPackage, Freshness freshness) {
    public CatalogPackage {
        if (physicalPackage == null || freshness == null) throw new NullPointerException();
    }

    public PhysicalPackage projectedPackage() {
        if (freshness == Freshness.FRESH) return physicalPackage;
        RomSource source = physicalPackage.source();
        RomSource unavailable = new RomSource(
                source.id(), source.type(), source.uri(), source.permissionState(),
                source.permissionState() == RomSource.PermissionState.NEEDS_REAUTHORIZE
                        ? RomSource.Availability.PERMISSION_REQUIRED
                        : RomSource.Availability.UNAVAILABLE);
        return new PhysicalPackage(
                physicalPackage.id(), unavailable, physicalPackage.sourceUri(),
                physicalPackage.originalFilename(), physicalPackage.packageFormat(),
                physicalPackage.physicalPackageSha256(), physicalPackage.variants());
    }

    public enum Freshness { FRESH, PRESERVED_STALE }
}
