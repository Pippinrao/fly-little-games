package com.flynes.emu.app;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.os.ParcelFileDescriptor;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.persistence.CanonicalUserState;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.util.List;
import java.util.UUID;

@RunWith(AndroidJUnit4.class)
public final class FlyNesAppBulkSnapshotTest {
    @Test public void returnsTwoThousandEntriesUsersAndSourcesInOneProjection() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        File root = new File(context.getCacheDir(), "bulk-snapshot-" + UUID.randomUUID());
        File data = new File(root, "data");
        File cache = new File(root, "cache");
        assertTrue(data.mkdirs());
        assertTrue(cache.mkdirs());
        File input = new File(root, "synthetic.nes");
        byte[] rom = new byte[16 + 16_384 + 8_192];
        rom[0] = 'N';
        rom[1] = 'E';
        rom[2] = 'S';
        rom[3] = 0x1a;
        rom[4] = 1;
        rom[5] = 1;
        byte[] sourceUuid = new byte[16];
        sourceUuid[0] = 73;

        try (FlyNesApp app = FlyNesApp.create(data.getPath(), cache.getPath())) {
            assertEquals(FlyCatalogCommands.OK,
                    app.scanBegin(sourceUuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY));
            for (int index = 0; index < 2_000; index++) {
                rom[16] = (byte) index;
                rom[17] = (byte) (index >>> 8);
                try (FileOutputStream output = new FileOutputStream(input)) {
                    output.write(rom);
                }
                try (ParcelFileDescriptor fd = ParcelFileDescriptor.open(
                        input, ParcelFileDescriptor.MODE_READ_ONLY)) {
                    String name = "Synthetic " + index + ".nes";
                    assertEquals(FlyCatalogCommands.OK,
                            app.scanAddFile(name, name, fd.getFd(), null));
                }
            }
            assertEquals(FlyCatalogCommands.OK, app.scanCommit(FlyCatalogCommands.SCAN_FULL));

            List<NativeCatalogEntry> seeded = app.catalogEntries();
            assertEquals(2_000, seeded.size());
            String favoriteId = seeded.get(0).canonicalId();
            String playedId = seeded.get(1).canonicalId();
            assertEquals(FlyCatalogCommands.OK, app.favoriteSet(favoriteId, true));
            assertEquals(FlyCatalogCommands.OK, app.markPlayed(playedId));

            NativeCatalogSnapshot snapshot = app.catalogSnapshot();

            assertEquals(2_000, snapshot.entries().size());
            assertEquals(1, snapshot.sources().size());
            assertEquals(2, snapshot.userStates().size());
            CanonicalUserState favorite = snapshot.userStates().get(favoriteId);
            CanonicalUserState played = snapshot.userStates().get(playedId);
            assertTrue(favorite.favorite());
            assertEquals(1L, favorite.favoriteUpdatedRevision());
            assertFalse(played.favorite());
            assertEquals(1, played.playCount());
            assertEquals(1L, played.lastPlayedSequence());
            assertTrue(snapshot.generation() >= 1L);
        }
    }
}
