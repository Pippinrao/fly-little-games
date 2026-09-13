package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.persistence.CatalogRepository;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.nio.file.Files;
import java.util.concurrent.TimeUnit;

@RunWith(AndroidJUnit4.class)
public final class AndroidCatalogRuntimeTest {
    @Test
    public void restartRestoresCatalogAndCorruptionDoesNotOverwriteFile() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        context.getSharedPreferences("game_library", Context.MODE_PRIVATE).edit().clear().commit();
        context.getSharedPreferences("catalog_migration", Context.MODE_PRIVATE)
                .edit().clear().commit();
        File state = new File(context.getCacheDir(), "catalog-runtime-test.bin");
        delete(state); delete(new File(state.getPath() + ".bak"));
        try (AndroidCatalogRuntime first = new AndroidCatalogRuntime(context, state)) {
            first.bootstrap().get(30, TimeUnit.SECONDS);
            // Every bundled game is projected, so the expected count comes from the
            // shared manifest rather than a number that froze the old single title.
            assertEquals(bundledGameCount(context),
                    first.gameCatalog().canonicalEntries().size());
            String id = first.gameCatalog().canonicalEntries().get(0).canonicalGame().id();
            assertTrue(first.setFavorite(id, true).get(30, TimeUnit.SECONDS));
        }
        try (AndroidCatalogRuntime restarted = new AndroidCatalogRuntime(context, state)) {
            assertEquals(CatalogRepository.LoadStatus.LOADED,
                    restarted.bootstrap().get(30, TimeUnit.SECONDS).loadResult().status());
            assertTrue(restarted.gameCatalog().canonicalEntries().get(0).favorite());
        }

        byte[] corrupt = new byte[]{1, 2, 3, 4, 5};
        try (FileOutputStream output = new FileOutputStream(state, false)) {
            output.write(corrupt);
        }
        delete(new File(state.getPath() + ".bak"));
        try (AndroidCatalogRuntime broken = new AndroidCatalogRuntime(context, state)) {
            assertEquals(CatalogRepository.LoadStatus.RECOVERY_NEEDED,
                    broken.bootstrap().get(30, TimeUnit.SECONDS).loadResult().status());
            assertArrayEquals(corrupt, Files.readAllBytes(state.toPath()));
        }
        delete(state); delete(new File(state.getPath() + ".bak"));
    }

    /** Bundled games the shared manifest declares, which the catalog must project. */
    private static int bundledGameCount(Context context) throws Exception {
        try (java.io.InputStream manifest =
                     context.getAssets().open(com.flynes.emu.catalog.BuiltinGames.ASSET_NAME)) {
            return com.flynes.emu.catalog.BuiltinGames.parse(manifest).all().size();
        }
    }

    private static void delete(File file) {
        if (file.exists() && !file.delete()) throw new AssertionError("cleanup failed");
    }
}
