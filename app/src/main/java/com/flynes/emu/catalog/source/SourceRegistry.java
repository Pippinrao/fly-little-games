package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.SourceCatalogState;

import java.util.ArrayList;

/** Source lifecycle service. Repository commit always precedes permission release. */
public final class SourceRegistry {
    private final CatalogRepository repository;
    private final ReadPermissionGateway permissions;

    public SourceRegistry(CatalogRepository repository, ReadPermissionGateway permissions) {
        if (repository == null || permissions == null) throw new NullPointerException();
        this.repository = repository;
        this.permissions = permissions;
    }

    public RomSource addOrReauthorize(String treeLocator, int resultFlags)
            throws CatalogRepository.RepositoryException {
        permissions.takeRead(treeLocator, resultFlags);
        String id = StableIds.safSourceId(treeLocator);
        RomSource source = new RomSource(
                id, RomSource.Type.SAF_TREE, treeLocator,
                RomSource.PermissionState.GRANTED, RomSource.Availability.AVAILABLE);
        SourceCatalogState existing = repository.state().sources().get(id);
        if (existing == null) repository.addSource(source);
        else repository.reauthorizeSource(source);
        return source;
    }

    public void verifyPersistedPermissions() throws CatalogRepository.RepositoryException {
        ArrayList<RomSource> sources = new ArrayList<>();
        for (SourceCatalogState item : repository.state().sources().values()) {
            if (item.source().type() == RomSource.Type.SAF_TREE) sources.add(item.source());
        }
        for (RomSource source : sources) {
            boolean granted = permissions.hasPersistedRead(source.uri());
            RomSource verified = new RomSource(
                    source.id(), source.type(), source.uri(),
                    granted ? RomSource.PermissionState.GRANTED
                            : RomSource.PermissionState.NEEDS_REAUTHORIZE,
                    granted ? RomSource.Availability.AVAILABLE
                            : RomSource.Availability.PERMISSION_REQUIRED);
            if (!verified.equals(source)) repository.reauthorizeSource(verified);
        }
    }

    public void remove(String sourceId) throws CatalogRepository.RepositoryException {
        SourceCatalogState existing = repository.state().sources().get(sourceId);
        if (existing == null) {
            repository.removeSource(sourceId);
            return;
        }
        String locator = existing.source().uri();
        repository.removeSource(sourceId);
        permissions.releaseRead(locator);
    }
}
