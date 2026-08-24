package com.flynes.emu.catalog.source;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertThrows;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateStore;

import org.junit.Test;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.HashSet;
import java.util.Set;

public final class SourceRegistryTest {
    @Test
    public void sameTreeReusesIdPermissionLossIsTypedAndRemoveCommitsBeforeRelease()
            throws Exception {
        RomSource builtin = new RomSource(
                "builtin", RomSource.Type.BUILTIN, "asset:///roms/from_below.nes",
                RomSource.PermissionState.NOT_REQUIRED);
        CatalogRepository repository = new CatalogRepository(
                CatalogState.empty(builtin), new MemoryStore(), new GameCatalog());
        FakePermissions permissions = new FakePermissions();
        SourceRegistry registry = new SourceRegistry(repository, permissions);
        String locator = "content://provider/tree/root";

        RomSource first = registry.addOrReauthorize(locator, ReadPermissionGateway.READ_FLAG);
        RomSource second = registry.addOrReauthorize(locator, ReadPermissionGateway.READ_FLAG);
        assertEquals(first.id(), second.id());
        assertEquals(RomSource.Type.SAF_TREE, second.type());
        assertEquals(2, repository.state().sources().size());

        permissions.persisted.clear();
        registry.verifyPersistedPermissions();
        RomSource unavailable = repository.state().sources().get(first.id()).source();
        assertEquals(RomSource.PermissionState.NEEDS_REAUTHORIZE,
                unavailable.permissionState());
        assertEquals(RomSource.Availability.PERMISSION_REQUIRED,
                unavailable.availability());

        permissions.persisted.add(locator);
        registry.addOrReauthorize(locator, ReadPermissionGateway.READ_FLAG);
        permissions.repository = repository;
        permissions.removedId = first.id();
        registry.remove(first.id());
        assertTrue(permissions.releasedAfterCommit);
        assertFalse(repository.state().sources().containsKey(first.id()));
    }

    @Test
    public void failedRepositoryWriteReleasesOnlyGrantAcquiredByThisAttempt() throws Exception {
        Fixture fresh = new Fixture();
        fresh.store.fail = true;
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> fresh.registry.addOrReauthorize(
                        fresh.locator, ReadPermissionGateway.READ_FLAG));
        assertFalse(fresh.permissions.persisted.contains(fresh.locator));
        assertEquals(1, fresh.permissions.releaseCalls);

        Fixture existing = new Fixture();
        existing.registry.addOrReauthorize(
                existing.locator, ReadPermissionGateway.READ_FLAG);
        existing.store.fail = true;
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> existing.registry.addOrReauthorize(
                        existing.locator, ReadPermissionGateway.READ_FLAG));
        assertTrue(existing.permissions.persisted.contains(existing.locator));
        assertEquals(0, existing.permissions.releaseCalls);
    }

    @Test
    public void pendingRemovalSurvivesRepositoryFailureAndRestartThenRetries() throws Exception {
        Fixture fixture = new Fixture();
        RomSource source = fixture.registry.addOrReauthorize(
                fixture.locator, ReadPermissionGateway.READ_FLAG);
        fixture.store.fail = true;

        assertThrows(CatalogRepository.RepositoryException.class,
                () -> fixture.registry.remove(source.id()));
        assertEquals(fixture.locator, fixture.pending.entries.get(source.id()));
        assertTrue(fixture.repository.state().sources().containsKey(source.id()));
        assertEquals(0, fixture.permissions.releaseCalls);

        fixture.store.fail = false;
        new SourceRegistry(fixture.repository, fixture.permissions, fixture.pending)
                .retryPendingReleases();
        assertFalse(fixture.repository.state().sources().containsKey(source.id()));
        assertFalse(fixture.permissions.persisted.contains(fixture.locator));
        assertTrue(fixture.pending.entries.isEmpty());
    }

    @Test
    public void pendingRemovalRetriesBothCrashWindowsAndNeverReleasesInUseLocator()
            throws Exception {
        Fixture releaseFailure = new Fixture();
        RomSource removed = releaseFailure.registry.addOrReauthorize(
                releaseFailure.locator, ReadPermissionGateway.READ_FLAG);
        releaseFailure.permissions.failRelease = true;
        assertThrows(ReadPermissionGateway.PermissionFailure.class,
                () -> releaseFailure.registry.remove(removed.id()));
        assertFalse(releaseFailure.repository.state().sources().containsKey(removed.id()));
        assertTrue(releaseFailure.pending.entries.containsKey(removed.id()));
        releaseFailure.permissions.failRelease = false;
        releaseFailure.registry.retryPendingReleases();
        assertTrue(releaseFailure.pending.entries.isEmpty());

        Fixture clearFailure = new Fixture();
        RomSource cleared = clearFailure.registry.addOrReauthorize(
                clearFailure.locator, ReadPermissionGateway.READ_FLAG);
        clearFailure.pending.failRemove = true;
        assertThrows(IOException.class, () -> clearFailure.registry.remove(cleared.id()));
        assertFalse(clearFailure.permissions.persisted.contains(clearFailure.locator));
        assertTrue(clearFailure.pending.entries.containsKey(cleared.id()));
        assertEquals(1, clearFailure.permissions.releaseCalls);
        clearFailure.pending.failRemove = false;
        clearFailure.registry.retryPendingReleases();
        assertEquals(1, clearFailure.permissions.releaseCalls);
        assertTrue(clearFailure.pending.entries.isEmpty());

        Fixture inUse = new Fixture();
        RomSource current = new RomSource(
                "current", RomSource.Type.SAF_TREE, inUse.locator,
                RomSource.PermissionState.GRANTED);
        inUse.repository.addSource(current);
        inUse.permissions.persisted.add(inUse.locator);
        inUse.pending.put("removed-source", inUse.locator);
        inUse.registry.retryPendingReleases();
        assertEquals(inUse.locator, inUse.pending.entries.get("removed-source"));
        assertTrue(inUse.permissions.persisted.contains(inUse.locator));
        assertEquals(0, inUse.permissions.releaseCalls);
    }

    private static final class Fixture {
        final String locator = "content://provider/tree/root";
        final MemoryStore store = new MemoryStore();
        final CatalogRepository repository = new CatalogRepository(
                CatalogState.empty(new RomSource(
                        "builtin", RomSource.Type.BUILTIN,
                        "asset:///roms/from_below.nes",
                        RomSource.PermissionState.NOT_REQUIRED)),
                store, new GameCatalog());
        final FakePermissions permissions = new FakePermissions();
        final MemoryPendingStore pending = new MemoryPendingStore();
        final SourceRegistry registry = new SourceRegistry(repository, permissions, pending);
    }

    private static final class FakePermissions implements ReadPermissionGateway {
        final Set<String> persisted = new HashSet<>();
        CatalogRepository repository;
        String removedId;
        boolean releasedAfterCommit;
        boolean failRelease;
        int releaseCalls;

        @Override public void takeRead(String locator, int resultFlags) {
            if ((resultFlags & READ_FLAG) == 0) throw new PermissionFailure(
                    PermissionFailure.Code.READ_NOT_GRANTED);
            persisted.add(locator);
        }
        @Override public boolean hasPersistedRead(String locator) {
            return persisted.contains(locator);
        }
        @Override public void releaseRead(String locator) {
            releaseCalls++;
            if (failRelease) throw new PermissionFailure(
                    PermissionFailure.Code.RELEASE_FAILED);
            releasedAfterCommit = repository != null
                    && !repository.state().sources().containsKey(removedId);
            persisted.remove(locator);
        }
    }

    private static final class MemoryPendingStore implements PendingReleaseStore {
        final Map<String, String> entries = new LinkedHashMap<>();
        boolean failRemove;

        @Override public Map<String, String> readAll() {
            return new LinkedHashMap<>(entries);
        }

        @Override public void put(String sourceId, String locator) {
            entries.put(sourceId, locator);
        }

        @Override public void remove(String sourceId) throws IOException {
            if (failRemove) throw new IOException("simulated crash before tombstone clear");
            entries.remove(sourceId);
        }
    }

    private static final class MemoryStore implements CatalogStateStore {
        byte[] value;
        boolean fail;
        @Override public byte[] read() { return value; }
        @Override public void writeAtomically(byte[] encoded) throws IOException {
            if (fail) throw new IOException("simulated catalog write failure");
            value = encoded.clone();
        }
    }
}
