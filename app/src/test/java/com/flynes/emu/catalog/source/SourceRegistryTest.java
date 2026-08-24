package com.flynes.emu.catalog.source;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import com.flynes.emu.catalog.GameCatalog;
import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.persistence.CatalogRepository;
import com.flynes.emu.catalog.persistence.CatalogState;
import com.flynes.emu.catalog.persistence.CatalogStateStore;

import org.junit.Test;

import java.io.IOException;
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

    private static final class FakePermissions implements ReadPermissionGateway {
        final Set<String> persisted = new HashSet<>();
        CatalogRepository repository;
        String removedId;
        boolean releasedAfterCommit;

        @Override public void takeRead(String locator, int resultFlags) {
            if ((resultFlags & READ_FLAG) == 0) throw new PermissionFailure(
                    PermissionFailure.Code.READ_NOT_GRANTED);
            persisted.add(locator);
        }
        @Override public boolean hasPersistedRead(String locator) {
            return persisted.contains(locator);
        }
        @Override public void releaseRead(String locator) {
            releasedAfterCommit = repository != null
                    && !repository.state().sources().containsKey(removedId);
            persisted.remove(locator);
        }
    }

    private static final class MemoryStore implements CatalogStateStore {
        byte[] value;
        @Override public byte[] read() { return value; }
        @Override public void writeAtomically(byte[] encoded) throws IOException {
            value = encoded.clone();
        }
    }
}
