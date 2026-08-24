package com.flynes.emu.catalog.source;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.StableIds;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.SourceCatalogState;

import java.io.IOException;
import java.util.ArrayList;
import java.util.Map;
import java.util.TreeMap;

/** Source lifecycle service. Repository commit always precedes permission release. */
public final class SourceRegistry {
    private final CatalogRepository repository;
    private final ReadPermissionGateway permissions;
    private final PendingReleaseStore pendingReleases;

    public SourceRegistry(CatalogRepository repository, ReadPermissionGateway permissions) {
        this(repository, permissions, PendingReleaseStore.inMemory());
    }

    public SourceRegistry(
            CatalogRepository repository,
            ReadPermissionGateway permissions,
            PendingReleaseStore pendingReleases) {
        if (repository == null || permissions == null || pendingReleases == null) {
            throw new NullPointerException();
        }
        this.repository = repository;
        this.permissions = permissions;
        this.pendingReleases = pendingReleases;
    }

    public RomSource addOrReauthorize(String treeLocator, int resultFlags)
            throws CatalogRepository.RepositoryException {
        boolean persistedBefore = permissions.hasPersistedRead(treeLocator);
        permissions.takeRead(treeLocator, resultFlags);
        String id = StableIds.safSourceId(treeLocator);
        RomSource source = new RomSource(
                id, RomSource.Type.SAF_TREE, treeLocator,
                RomSource.PermissionState.GRANTED, RomSource.Availability.AVAILABLE);
        SourceCatalogState existing = repository.state().sources().get(id);
        try {
            if (existing == null) repository.addSource(source);
            else repository.reauthorizeSource(source);
        } catch (CatalogRepository.RepositoryException failure) {
            if (!persistedBefore) {
                try { permissions.releaseRead(treeLocator); }
                catch (ReadPermissionGateway.PermissionFailure compensationFailure) {
                    failure.addSuppressed(compensationFailure);
                }
            }
            throw failure;
        }
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

    public void remove(String sourceId)
            throws CatalogRepository.RepositoryException, IOException {
        SourceCatalogState existing = repository.state().sources().get(sourceId);
        if (existing == null) {
            repository.removeSource(sourceId);
            return;
        }
        String locator = existing.source().uri();
        pendingReleases.put(sourceId, locator);
        repository.removeSource(sourceId);
        completePendingRelease(sourceId, locator);
    }

    /** Retries deletion/release after a failed write, release, or process-death window. */
    public void retryPendingReleases()
            throws CatalogRepository.RepositoryException, IOException {
        for (Map.Entry<String, String> pending
                : new TreeMap<>(pendingReleases.readAll()).entrySet()) {
            String sourceId = pending.getKey();
            String locator = pending.getValue();
            SourceCatalogState current = repository.state().sources().get(sourceId);
            if (current != null && locator.equals(current.source().uri())) {
                repository.removeSource(sourceId);
            }
            completePendingRelease(sourceId, locator);
        }
    }

    private void completePendingRelease(String sourceId, String locator) throws IOException {
        for (SourceCatalogState current : repository.state().sources().values()) {
            if (locator.equals(current.source().uri())) return;
        }
        if (permissions.hasPersistedRead(locator)) permissions.releaseRead(locator);
        pendingReleases.remove(sourceId);
    }
}
