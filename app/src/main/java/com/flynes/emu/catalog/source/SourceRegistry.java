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
            throws CatalogRepository.RepositoryException, IOException {
        boolean persistedBefore = permissions.hasPersistedRead(treeLocator);
        String id = StableIds.safSourceId(treeLocator);
        PendingRelease orphan = persistedBefore
                ? null : PendingRelease.orphanGrant(id, treeLocator);
        if (orphan != null) pendingReleases.put(orphan);
        try {
            permissions.takeRead(treeLocator, resultFlags);
        } catch (RuntimeException failure) {
            cleanupFailedTake(orphan, treeLocator, failure);
            throw failure;
        }
        RomSource source = new RomSource(
                id, RomSource.Type.SAF_TREE, treeLocator,
                RomSource.PermissionState.GRANTED, RomSource.Availability.AVAILABLE);
        SourceCatalogState existing = repository.state().sources().get(id);
        try {
            if (existing == null) repository.addSource(source);
            else repository.reauthorizeSource(source);
        } catch (CatalogRepository.RepositoryException failure) {
            compensateKnownOrphan(orphan, treeLocator, failure);
            throw failure;
        }
        if (orphan != null) {
            try { pendingReleases.remove(orphan.actionId()); }
            catch (IOException ignored) {
                // Registration is committed; retry recognizes the usable source and only clears.
            }
        }
        return source;
    }

    private void cleanupFailedTake(
            PendingRelease orphan, String locator, RuntimeException failure) {
        if (orphan == null || permissions.hasPersistedRead(locator)) return;
        try { pendingReleases.remove(orphan.actionId()); }
        catch (IOException cleanupFailure) { failure.addSuppressed(cleanupFailure); }
    }

    private void compensateKnownOrphan(
            PendingRelease orphan, String locator, Throwable failure) {
        if (orphan == null) return;
        try {
            if (permissions.hasPersistedRead(locator)) permissions.releaseRead(locator);
            pendingReleases.remove(orphan.actionId());
        } catch (ReadPermissionGateway.PermissionFailure | IOException cleanupFailure) {
            failure.addSuppressed(cleanupFailure);
        }
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
        PendingRelease pending = PendingRelease.removeSource(sourceId, locator);
        pendingReleases.put(pending);
        repository.removeSource(sourceId);
        retryRemoveSource(pending);
    }

    /** Retries deletion/release after a failed write, release, or process-death window. */
    public void retryPendingReleases()
            throws PendingReleaseException {
        ArrayList<Throwable> failures = new ArrayList<>();
        final Map<String, PendingRelease> stored;
        try {
            stored = pendingReleases.readAll();
        } catch (IOException failure) {
            failures.add(failure);
            throw new PendingReleaseException(failures);
        }
        for (Map.Entry<String, PendingRelease> item
                : new TreeMap<>(stored).entrySet()) {
            try {
                PendingRelease pending = item.getValue();
                if (pending.intent() == PendingRelease.Intent.REMOVE_SOURCE) {
                    retryRemoveSource(pending);
                } else {
                    retryOrphanGrant(pending);
                }
            } catch (CatalogRepository.RepositoryException
                    | IOException
                    | ReadPermissionGateway.PermissionFailure failure) {
                failures.add(failure);
            }
        }
        if (!failures.isEmpty()) throw new PendingReleaseException(failures);
    }

    private void retryRemoveSource(PendingRelease pending)
            throws CatalogRepository.RepositoryException, IOException {
        SourceCatalogState current = repository.state().sources().get(pending.sourceId());
        if (current != null && pending.locator().equals(current.source().uri())) {
            repository.removeSource(pending.sourceId());
        }
        for (SourceCatalogState other : repository.state().sources().values()) {
            if (pending.locator().equals(other.source().uri())) return;
        }
        releaseAndClear(pending);
    }

    private void retryOrphanGrant(PendingRelease pending) throws IOException {
        for (SourceCatalogState current : repository.state().sources().values()) {
            if (pending.locator().equals(current.source().uri())
                    && current.source().isUsable()) {
                pendingReleases.remove(pending.actionId());
                return;
            }
        }
        releaseAndClear(pending);
    }

    private void releaseAndClear(PendingRelease pending) throws IOException {
        if (permissions.hasPersistedRead(pending.locator())) {
            permissions.releaseRead(pending.locator());
        }
        pendingReleases.remove(pending.actionId());
    }

    public static final class PendingReleaseException extends IOException {
        private final int failureCount;

        PendingReleaseException(java.util.List<Throwable> failures) {
            super("one or more pending grant releases failed");
            failureCount = failures.size();
            for (Throwable failure : failures) addSuppressed(failure);
        }

        public int failureCount() { return failureCount; }
    }
}
