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
        assertTrue(fresh.pending.entries.isEmpty());

        Fixture existing = new Fixture();
        existing.registry.addOrReauthorize(
                existing.locator, ReadPermissionGateway.READ_FLAG);
        existing.store.fail = true;
        assertThrows(CatalogRepository.RepositoryException.class,
                () -> existing.registry.addOrReauthorize(
                        existing.locator, ReadPermissionGateway.READ_FLAG));
        assertTrue(existing.permissions.persisted.contains(existing.locator));
        assertEquals(0, existing.permissions.releaseCalls);
        assertTrue(existing.pending.entries.isEmpty());
    }

    @Test
    public void orphanGrantTombstoneClosesTakeToRepositoryCrashWindow() throws Exception {
        Fixture fixture = new Fixture();
        fixture.permissions.crashAfterTake = true;

        assertThrows(SimulatedProcessDeath.class, () -> fixture.registry.addOrReauthorize(
                fixture.locator, ReadPermissionGateway.READ_FLAG));

        assertTrue(fixture.permissions.persisted.contains(fixture.locator));
        assertFalse(fixture.repository.state().sources().containsKey(
                com.flynes.emu.catalog.StableIds.safSourceId(fixture.locator)));
        assertEquals(PendingRelease.Intent.RELEASE_ORPHAN_GRANT,
                fixture.pending.only().intent());
        fixture.permissions.crashAfterTake = false;
        new SourceRegistry(fixture.repository, fixture.permissions, fixture.pending)
                .retryPendingReleases();
        assertFalse(fixture.permissions.persisted.contains(fixture.locator));
        assertTrue(fixture.pending.entries.isEmpty());
    }

    @Test
    public void orphanTombstoneAfterSuccessfulCommitNeverDeletesRegisteredSource()
            throws Exception {
        Fixture fixture = new Fixture();
        fixture.pending.failRemove = true;

        RomSource registered = fixture.registry.addOrReauthorize(
                fixture.locator, ReadPermissionGateway.READ_FLAG);

        assertTrue(fixture.repository.state().sources().containsKey(registered.id()));
        assertEquals(PendingRelease.Intent.RELEASE_ORPHAN_GRANT,
                fixture.pending.only().intent());
        fixture.pending.failRemove = false;
        new SourceRegistry(fixture.repository, fixture.permissions, fixture.pending)
                .retryPendingReleases();
        assertTrue(fixture.repository.state().sources().containsKey(registered.id()));
        assertTrue(fixture.permissions.persisted.contains(fixture.locator));
        assertEquals(0, fixture.permissions.releaseCalls);
        assertTrue(fixture.pending.entries.isEmpty());
    }

    @Test
    public void failedRepositoryWriteKeepsOrphanTombstoneWhenCompensationReleaseFails()
            throws Exception {
        Fixture fixture = new Fixture();
        fixture.store.fail = true;
        fixture.permissions.failRelease = true;

        assertThrows(CatalogRepository.RepositoryException.class,
                () -> fixture.registry.addOrReauthorize(
                        fixture.locator, ReadPermissionGateway.READ_FLAG));

        assertEquals(PendingRelease.Intent.RELEASE_ORPHAN_GRANT,
                fixture.pending.only().intent());
        assertTrue(fixture.permissions.persisted.contains(fixture.locator));
        fixture.store.fail = false;
        fixture.permissions.failRelease = false;
        fixture.registry.retryPendingReleases();
        assertTrue(fixture.pending.entries.isEmpty());
        assertFalse(fixture.permissions.persisted.contains(fixture.locator));
    }

    @Test
    public void pendingRemovalSurvivesRepositoryFailureAndRestartThenRetries() throws Exception {
        Fixture fixture = new Fixture();
        RomSource source = fixture.registry.addOrReauthorize(
                fixture.locator, ReadPermissionGateway.READ_FLAG);
        fixture.store.fail = true;

        assertThrows(CatalogRepository.RepositoryException.class,
                () -> fixture.registry.remove(source.id()));
        assertEquals(PendingRelease.Intent.REMOVE_SOURCE,
                fixture.pending.only().intent());
        assertEquals(fixture.locator, fixture.pending.only().locator());
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
        assertEquals(PendingRelease.Intent.REMOVE_SOURCE,
                releaseFailure.pending.only().intent());
        releaseFailure.permissions.failRelease = false;
        releaseFailure.registry.retryPendingReleases();
        assertTrue(releaseFailure.pending.entries.isEmpty());

        Fixture clearFailure = new Fixture();
        RomSource cleared = clearFailure.registry.addOrReauthorize(
                clearFailure.locator, ReadPermissionGateway.READ_FLAG);
        clearFailure.pending.failRemove = true;
        assertThrows(IOException.class, () -> clearFailure.registry.remove(cleared.id()));
        assertFalse(clearFailure.permissions.persisted.contains(clearFailure.locator));
        assertEquals(PendingRelease.Intent.REMOVE_SOURCE,
                clearFailure.pending.only().intent());
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
        PendingRelease inUseRelease = PendingRelease.removeSource(
                "removed-source", inUse.locator);
        inUse.pending.put(inUseRelease);
        inUse.registry.retryPendingReleases();
        assertEquals(inUse.locator,
                inUse.pending.entries.get(inUseRelease.actionId()).locator());
        assertTrue(inUse.permissions.persisted.contains(inUse.locator));
        assertEquals(0, inUse.permissions.releaseCalls);
    }

    @Test
    public void retryAttemptsEveryTombstoneAndAggregatesFailures() throws Exception {
        Fixture fixture = new Fixture();
        String firstLocator = "content://provider/tree/a";
        String secondLocator = "content://provider/tree/b";
        PendingRelease first = PendingRelease.orphanGrant("a", firstLocator);
        PendingRelease second = PendingRelease.orphanGrant("b", secondLocator);
        fixture.pending.put(first);
        fixture.pending.put(second);
        fixture.permissions.persisted.add(firstLocator);
        fixture.permissions.persisted.add(secondLocator);
        fixture.permissions.failReleaseLocators.add(firstLocator);

        SourceRegistry.PendingReleaseException failure = assertThrows(
                SourceRegistry.PendingReleaseException.class,
                () -> fixture.registry.retryPendingReleases());

        assertEquals(1, failure.failureCount());
        assertTrue(fixture.pending.entries.containsKey(first.actionId()));
        assertFalse(fixture.pending.entries.containsKey(second.actionId()));
        assertTrue(fixture.permissions.persisted.contains(firstLocator));
        assertFalse(fixture.permissions.persisted.contains(secondLocator));
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
        boolean crashAfterTake;
        final Set<String> failReleaseLocators = new HashSet<>();
        int releaseCalls;

        @Override public void takeRead(String locator, int resultFlags) {
            if ((resultFlags & READ_FLAG) == 0) throw new PermissionFailure(
                    PermissionFailure.Code.READ_NOT_GRANTED);
            persisted.add(locator);
            if (crashAfterTake) throw new SimulatedProcessDeath();
        }
        @Override public boolean hasPersistedRead(String locator) {
            return persisted.contains(locator);
        }
        @Override public void releaseRead(String locator) {
            releaseCalls++;
            if (failRelease || failReleaseLocators.contains(locator)) {
                throw new PermissionFailure(
                    PermissionFailure.Code.RELEASE_FAILED);
            }
            releasedAfterCommit = repository != null
                    && !repository.state().sources().containsKey(removedId);
            persisted.remove(locator);
        }
    }

    private static final class MemoryPendingStore implements PendingReleaseStore {
        final Map<String, PendingRelease> entries = new LinkedHashMap<>();
        boolean failRemove;

        @Override public Map<String, PendingRelease> readAll() {
            return new LinkedHashMap<>(entries);
        }

        @Override public void put(PendingRelease pending) {
            entries.put(pending.actionId(), pending);
        }

        @Override public void remove(String actionId) throws IOException {
            if (failRemove) throw new IOException("simulated crash before tombstone clear");
            entries.remove(actionId);
        }

        PendingRelease only() {
            assertEquals(1, entries.size());
            return entries.values().iterator().next();
        }
    }

    private static final class SimulatedProcessDeath extends Error {
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
