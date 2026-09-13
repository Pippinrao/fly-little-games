package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertTrue;
import static org.junit.Assume.assumeNotNull;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.flynes.emu.catalog.RomSource;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.File;
import java.util.UUID;
import java.util.concurrent.TimeUnit;

/** Opt-in real SAF integration: pass -e sourceTree <an already granted tree URI>.
 * Native catalog and preferences are isolated; existing app data and grants are preserved. */
@RunWith(AndroidJUnit4.class)
public final class AndroidNativeSourceRegistrationTest {
    @Test public void registeredSourceSurvivesProjectionRestartAndCanBeScanned() throws Exception {
        String tree = InstrumentationRegistry.getArguments().getString("sourceTree");
        assumeNotNull(tree);
        Context base = ApplicationProvider.getApplicationContext();
        assertTrue("integration tree must already have a persisted read grant",
                new PersistedReadPermissionGateway(base.getContentResolver()).hasPersistedRead(tree));
        String prefix = "native-registration-" + UUID.randomUUID();
        File root = new File(base.getCacheDir(), prefix);
        assertTrue(root.mkdirs());
        Context isolated = new ContextWrapper(base) {
            @Override public Context getApplicationContext() { return this; }
            @Override public File getFilesDir() { return new File(root, "files"); }
            @Override public File getCacheDir() { return new File(root, "cache"); }
            @Override public SharedPreferences getSharedPreferences(String name, int mode) {
                return base.getSharedPreferences(prefix + "-" + name, mode);
            }
        };
        String sourceId;
        try (AndroidCatalogRuntime runtime = new AndroidCatalogRuntime(isolated)) {
            runtime.bootstrap().get(30, TimeUnit.SECONDS);
            RomSource source = runtime.addOrReauthorizeTree(tree, 1).get(30, TimeUnit.SECONDS);
            sourceId = source.id();
            assertNotNull("registration must not disappear when the native view refreshes",
                    runtime.stateSnapshot().sources().get(sourceId));
        }
        try (AndroidCatalogRuntime runtime = new AndroidCatalogRuntime(isolated)) {
            runtime.bootstrap().get(30, TimeUnit.SECONDS);
            assertNotNull("pending source must survive a cold start before its first scan",
                    runtime.stateSnapshot().sources().get(sourceId));
            var scan = runtime.scanSource(sourceId);
            com.flynes.emu.catalog.persistence.SourceScanResult result;
            try {
                result = scan.get(10, TimeUnit.MINUTES);
            } catch (java.util.concurrent.TimeoutException slowLibrary) {
                // Never close the native owner while its scan is still using borrowed files.
                // Report the timeout after the worker has left native code.
                try { scan.get(); } catch (java.util.concurrent.ExecutionException ignored) { }
                throw slowLibrary;
            }
            assertEquals(result.candidateCount(), result.packageOutcomes().size());
            assertTrue("successful scan must return its indexed packages", !result.packages().isEmpty());
            int packages = runtime.stateSnapshot().sources().get(sourceId).packages().size();
            assertTrue("test library must contain ROM packages", packages > 0);
            runtime.addOrReauthorizeTree(tree, 1).get(30, TimeUnit.SECONDS);
            assertEquals("reauthorization must preserve scanned packages", packages,
                    runtime.stateSnapshot().sources().get(sourceId).packages().size());
        }
        try (AndroidCatalogRuntime runtime = new AndroidCatalogRuntime(isolated)) {
            runtime.bootstrap().get(30, TimeUnit.SECONDS);
            var loader = new com.flynes.emu.launch.ExactRomLoader(runtime.streamOpener());
            int loaded = 0;
            for (var game : runtime.gameCatalog().canonicalEntries()) {
                for (var variant : game.variants()) {
                    if (variant.sourceId().equals(sourceId) && variant.isLaunchable()
                            && variant.compatibility().isPlayable()) {
                        assertTrue(loader.load(com.flynes.emu.launch.LaunchRequest.forVariant(variant)).length > 0);
                        if (++loaded == 10) break;
                    }
                }
                if (loaded == 10) break;
            }
            assertTrue("cold start must reopen imported game payloads", loaded > 0);
        }
    }
}
