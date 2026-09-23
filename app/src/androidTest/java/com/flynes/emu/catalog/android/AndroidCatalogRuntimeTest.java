package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertSame;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.persistence.CatalogRepository;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.nio.file.Files;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.Executors;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;

@RunWith(AndroidJUnit4.class)
public final class AndroidCatalogRuntimeTest {
    @Test public void cachedRowsPublishBeforeBlockedNativeCreationAndStartIsIdempotent()
            throws Exception {
        try (TwoPhaseFixture fixture = new TwoPhaseFixture()) {
            int expected = bundledGameCount(fixture);
            try (AndroidCatalogRuntime first = new AndroidCatalogRuntime(fixture)) {
                first.bootstrap().get(30, TimeUnit.SECONDS);
                assertEquals(expected, first.gameCenterSnapshot().rows().size());
            }

            CountDownLatch opened = new CountDownLatch(1);
            CountDownLatch release = new CountDownLatch(1);
            AtomicInteger opens = new AtomicInteger();
            AndroidCatalogRuntime.NativeAppFactory slowFactory = (data, cache) -> {
                opens.incrementAndGet();
                opened.countDown();
                try {
                    if (!release.await(30, TimeUnit.SECONDS)) {
                        throw new IllegalStateException("native factory release timed out");
                    }
                } catch (InterruptedException interrupted) {
                    Thread.currentThread().interrupt();
                    throw new IllegalStateException(interrupted);
                }
                return com.flynes.emu.app.FlyNesApp.create(
                        data.getAbsolutePath(), cache.getAbsolutePath());
            };
            try (AndroidCatalogRuntime restarted = new AndroidCatalogRuntime(
                    fixture, AndroidCatalogRuntime.defaultStateFile(fixture), slowFactory,
                    Executors.newSingleThreadExecutor())) {
                AndroidCatalogRuntime.Startup startup = restarted.start();
                assertSame(startup, restarted.start());
                assertEquals(AndroidCatalogRuntime.CacheStatus.HIT,
                        startup.cacheReady().get(2, TimeUnit.SECONDS).status());
                assertEquals(expected, restarted.gameCenterSnapshot().rows().size());
                assertFalse("cache phase must not wait for native", startup.nativeReady().isDone());
                assertTrue(opened.await(2, TimeUnit.SECONDS));
                assertEquals(1, opens.get());
                release.countDown();
                startup.nativeReady().get(30, TimeUnit.SECONDS);
                assertEquals(expected, restarted.gameCenterSnapshot().rows().size());
            } finally {
                release.countDown();
            }
        }
    }

    @Test public void nearbyFactoryLoadsRealCatalogContentWithoutLaunchSideEffects() throws Exception {
        try (CatalogFixture fixture = new CatalogFixture();
             AndroidCatalogRuntime runtime = new AndroidCatalogRuntime(fixture, fixture.state)) {
            File state = fixture.state;
            runtime.bootstrap().get(30, TimeUnit.SECONDS);
            var variant = runtime.gameCatalog().canonicalEntries().get(0).variants().get(0);
            var before = runtime.stateSnapshot();
            Object pendingLaunchBefore = pendingLaunch();
            byte[] persistedBefore = Files.readAllBytes(state.toPath());
            var loader = runtime.nearbyContentLoader();
            org.junit.Assert.assertNotNull("runtime wires an exact nearby content loader", loader);
            var loaded = loader.load(variant.variantId());
            assertEquals(variant, loaded.variant());
            assertArrayEquals(new com.flynes.emu.launch.ExactRomLoader(runtime.streamOpener())
                    .load(com.flynes.emu.launch.LaunchRequest.forVariant(variant)), loaded.bytes());
            org.junit.Assert.assertSame(before, runtime.stateSnapshot());
            assertArrayEquals(persistedBefore, Files.readAllBytes(state.toPath()));
            assertSame("nearby loading must preserve any pending launch", pendingLaunchBefore,
                    pendingLaunch());
            assertEquals(0, runtime.gameCatalog().canonicalEntries().get(0).playCount());
            fixture.assertPreferencesIsolated();
        }
    }

    @Test
    public void restartRestoresCatalogAndCorruptionDoesNotOverwriteFile() throws Exception {
        try (CatalogFixture fixture = new CatalogFixture()) {
            File state = fixture.state;
            try (AndroidCatalogRuntime first = new AndroidCatalogRuntime(fixture, state)) {
                first.bootstrap().get(30, TimeUnit.SECONDS);
                // Every bundled game is projected, so the expected count comes from the
                // shared manifest rather than a number that froze the old single title.
                assertEquals(bundledGameCount(fixture),
                        first.gameCatalog().canonicalEntries().size());
                String id = first.gameCatalog().canonicalEntries().get(0).canonicalGame().id();
                assertTrue(first.setFavorite(id, true).get(30, TimeUnit.SECONDS));
            }
            try (AndroidCatalogRuntime restarted = new AndroidCatalogRuntime(fixture, state)) {
                assertEquals(CatalogRepository.LoadStatus.LOADED,
                        restarted.bootstrap().get(30, TimeUnit.SECONDS).loadResult().status());
                assertTrue(restarted.gameCatalog().canonicalEntries().get(0).favorite());
            }

            byte[] corrupt = new byte[]{1, 2, 3, 4, 5};
            try (FileOutputStream output = new FileOutputStream(state, false)) {
                output.write(corrupt);
            }
            fixture.deleteStateFile(fixture.backup);
            try (AndroidCatalogRuntime broken = new AndroidCatalogRuntime(fixture, state)) {
                assertEquals(CatalogRepository.LoadStatus.RECOVERY_NEEDED,
                        broken.bootstrap().get(30, TimeUnit.SECONDS).loadResult().status());
                assertArrayEquals(corrupt, Files.readAllBytes(state.toPath()));
            }
            fixture.assertPreferencesIsolated();
        }
    }

    /** Bundled games the shared manifest declares, which the catalog must project. */
    private static int bundledGameCount(Context context) throws Exception {
        try (java.io.InputStream manifest =
                     context.getAssets().open(com.flynes.emu.catalog.BuiltinGames.ASSET_NAME)) {
            return com.flynes.emu.catalog.BuiltinGames.parse(manifest).all().size();
        }
    }

    /** Every runtime preference, including pending SAF releases, belongs to this test. */
    private static final class CatalogFixture extends ContextWrapper implements AutoCloseable {
        private final String namespace = "catalog-runtime-" + UUID.randomUUID();
        private final Map<String, SharedPreferences> requestedPreferences = new ConcurrentHashMap<>();
        private final File cacheRoot;
        final File state;
        final File backup;

        CatalogFixture() throws IOException {
            super(ApplicationProvider.getApplicationContext());
            cacheRoot = getCacheDir().getCanonicalFile();
            state = new File(cacheRoot, namespace + ".bin");
            backup = new File(cacheRoot, namespace + ".bin.bak");
            assertSame("application context must retain preference isolation", this,
                    getApplicationContext());
        }

        @Override public Context getApplicationContext() { return this; }

        @Override public SharedPreferences getSharedPreferences(String name, int mode) {
            SharedPreferences preferences = super.getSharedPreferences(namespace + "-" + name, mode);
            requestedPreferences.put(name, preferences);
            return preferences;
        }

        void assertPreferencesIsolated() {
            assertSame(this, getApplicationContext());
            assertTrue(requestedPreferences.containsKey("game_library"));
            assertTrue(requestedPreferences.containsKey("catalog_migration"));
            assertTrue(requestedPreferences.containsKey("catalog_pending_releases"));
            assertTrue(requestedPreferences.containsKey("flynes_settings"));
            for (Map.Entry<String, SharedPreferences> requested : requestedPreferences.entrySet()) {
                assertSame("runtime preference must use the test namespace: " + requested.getKey(),
                        super.getSharedPreferences(namespace + "-" + requested.getKey(), MODE_PRIVATE),
                        requested.getValue());
            }
        }

        void deleteStateFile(File file) throws IOException {
            File resolved = file.getCanonicalFile();
            assertEquals("cleanup stays inside the app cache", cacheRoot, resolved.getParentFile());
            assertTrue("cleanup only removes this fixture's state or backup",
                    resolved.getName().equals(namespace + ".bin")
                            || resolved.getName().equals(namespace + ".bin.bak"));
            if (resolved.exists() && !resolved.delete()) throw new AssertionError("cleanup failed");
        }

        @Override public void close() throws IOException {
            try {
                for (String name : requestedPreferences.keySet()) {
                    assertTrue("test preference cleanup failed",
                            super.deleteSharedPreferences(namespace + "-" + name));
                }
            } finally {
                try {
                    deleteStateFile(state);
                } finally {
                    deleteStateFile(backup);
                }
            }
        }
    }

    private static final class TwoPhaseFixture extends ContextWrapper implements AutoCloseable {
        private final String namespace = "two-phase-" + UUID.randomUUID();
        private final File root;

        TwoPhaseFixture() {
            super(ApplicationProvider.getApplicationContext());
            root = new File(super.getCacheDir(), namespace);
            assertTrue(root.mkdirs());
        }

        @Override public Context getApplicationContext() { return this; }
        @Override public File getFilesDir() {
            File value = new File(root, "files");
            if (!value.isDirectory() && !value.mkdirs()) throw new AssertionError("files dir");
            return value;
        }
        @Override public File getCacheDir() {
            File value = new File(root, "cache");
            if (!value.isDirectory() && !value.mkdirs()) throw new AssertionError("cache dir");
            return value;
        }
        @Override public SharedPreferences getSharedPreferences(String name, int mode) {
            return super.getSharedPreferences(namespace + "-" + name, mode);
        }

        @Override public void close() throws IOException {
            for (String name : new String[]{"flynes_source_uuids", "flynes_migration_log",
                    "flynes_catalog_projection", "catalog_pending_releases", "flynes_settings",
                    "game_library", "catalog_migration"}) {
                super.deleteSharedPreferences(namespace + "-" + name);
            }
            try (java.util.stream.Stream<java.nio.file.Path> paths = Files.walk(root.toPath())) {
                paths.sorted(java.util.Comparator.reverseOrder()).forEach(path -> {
                    try { Files.deleteIfExists(path); }
                    catch (IOException failure) { throw new java.io.UncheckedIOException(failure); }
                });
            }
        }
    }

    /** Observe the handoff without consuming or changing an existing user launch. */
    private static Object pendingLaunch() throws ReflectiveOperationException {
        java.lang.reflect.Field pending = com.flynes.emu.PendingGameLaunch.class
                .getDeclaredField("PENDING");
        pending.setAccessible(true);
        return ((java.util.concurrent.atomic.AtomicReference<?>) pending.get(null)).get();
    }
}
