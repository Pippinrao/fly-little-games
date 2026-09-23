package com.flynes.emu.catalog.android;

import android.content.Context;
import android.content.ContextWrapper;
import android.content.SharedPreferences;
import android.os.ParcelFileDescriptor;
import android.os.SystemClock;
import android.util.Log;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.core.app.ActivityScenario;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.recyclerview.widget.RecyclerView;
import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;
import com.flynes.emu.app.FlyNesApp;
import com.flynes.emu.app.FlyCatalogCommands;
import com.flynes.emu.gamecenter.GameCenterItem;
import com.flynes.emu.gamecenter.GameCenterState;
import org.junit.Test;
import org.junit.runner.RunWith;
import java.io.File;
import java.io.FileOutputStream;
import java.util.ArrayList;
import java.util.UUID;
import static org.junit.Assert.*;

/** Synthetic private library only: no device ROM directory or user library is modified. */
@RunWith(AndroidJUnit4.class)
public final class AndroidLargeCatalogPerformanceTest {
    @Test public void coldLibraryAndRepeatedNavigationWith2224Games() throws Exception {
        Context base = ApplicationProvider.getApplicationContext();
        String prefix = "catalog-perf-" + UUID.randomUUID();
        File root = new File(base.getCacheDir(), prefix);
        assertTrue(root.mkdirs());
        java.util.concurrent.atomic.AtomicBoolean failBuiltin = new java.util.concurrent.atomic.AtomicBoolean();
        Context isolated = new ContextWrapper(base) {
            @Override public Context getApplicationContext() { return this; }
            @Override public File getFilesDir() { return new File(root, "files"); }
            @Override public File getCacheDir() { return new File(root, "cache"); }
            @Override public android.content.res.AssetManager getAssets() {
                if (failBuiltin.get()) throw new IllegalStateException("injected builtin copy failure");
                return base.getAssets();
            }
            @Override public SharedPreferences getSharedPreferences(String name, int mode) {
                return base.getSharedPreferences(prefix + name, mode);
            }
        };
        final int expectedGames = 2224
                + com.flynes.emu.catalog.BuiltinGames.fromAssets(isolated).all().size();
        // Initialize the bundled game, then seed synthetic entries through the real native scanner.
        try (var runtime = new AndroidCatalogRuntime(isolated)) { runtime.bootstrap().get(); }
        byte[] uuid = new byte[16]; uuid[0] = 42;
        var prefs = isolated.getSharedPreferences("flynes_source_uuids", 0);
        new AndroidUuidSafMap(key -> prefs.getString(key, null),
                (key, value) -> prefs.edit().putString(key, value).commit(),
                key -> prefs.edit().remove(key).commit())
                .put(uuid, "content://synthetic.performance/tree/library");
        byte[] rom = new byte[16 + 16384 + 8192];
        rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1a; rom[4] = 1; rom[5] = 1;
        File input = new File(root, "synthetic.nes");
        try (var app = FlyNesApp.create(AndroidCatalogRuntime.nativeDataRoot(isolated).getPath(),
                AndroidCatalogRuntime.nativeCacheRoot(isolated).getPath())) {
            assertEquals(0, app.scanBegin(uuid, FlyCatalogCommands.SOURCE_SCOPE_USER_DIRECTORY));
            for (int i = 0; i < 2224; ++i) {
                rom[16] = (byte) i; rom[17] = (byte) (i >> 8);
                try (var out = new FileOutputStream(input)) { out.write(rom); }
                try (var fd = ParcelFileDescriptor.open(input, ParcelFileDescriptor.MODE_READ_ONLY)) {
                    String name = (i % 2 == 0 ? "魂斗罗 " : "Unknown ") + i + ".nes";
                    assertEquals(0, app.scanAddFile(name, name, fd.getFd(), null));
                }
            }
            assertEquals(0, app.scanCommit(FlyCatalogCommands.SCAN_FULL));
        }
        // The synthetic native scan bypasses AndroidCatalogRuntime, so reconcile that new native
        // generation once and persist the same projection a real explicit import would write.
        try (var reconciler = new AndroidCatalogRuntime(isolated)) {
            reconciler.bootstrap().get();
            assertEquals(expectedGames, reconciler.gameCenterSnapshot().rows().size());
        }
        for (int run = 0; run < 3; ++run) {
            long start = SystemClock.elapsedRealtime();
            try (var runtime = new AndroidCatalogRuntime(isolated)) {
                AndroidCatalogRuntime.Startup startup = runtime.start();
                assertEquals(AndroidCatalogRuntime.CacheStatus.HIT,
                        startup.cacheReady().get().status());
                long cacheLoaded = SystemClock.elapsedRealtime();
                assertEquals(expectedGames, runtime.gameCenterSnapshot().rows().size());
                startup.nativeReady().get();
                long loaded = SystemClock.elapsedRealtime();
                assertEquals(expectedGames, runtime.gameCatalog().canonicalEntries().size());
                ArrayList<GameCenterItem> rows = new ArrayList<>();
                for (var entry : runtime.gameCatalog().canonicalEntries()) {
                    rows.add(new GameCenterItem(entry.canonicalGame().id(),
                            entry.canonicalGame().englishTitle(), entry.canonicalGame().zhHansTitle(),
                            false, entry.favorite(), entry.lastPlayedSequence(),
                            entry.variants().get(0).originalFilename()));
                }
                GameCenterState state = new GameCenterState();
                long firstStart = SystemClock.elapsedRealtime();
                assertEquals(expectedGames, state.filtered(rows).size());
                long firstEnd = SystemClock.elapsedRealtime();
                for (int i = 0; i < 20; ++i) {
                    state.select(rows.get(i).canonicalId());
                    assertEquals(expectedGames, state.filtered(rows).size());
                }
                long end = SystemClock.elapsedRealtime();
                Log.i("FlyNesCatalogPerf", "run=" + run + " cacheMs=" + (cacheLoaded-start)
                        + " nativeMs=" + (loaded-cacheLoaded) + " coldMs=" + (loaded-start)
                        + " firstSortMs=" + (firstEnd-firstStart)
                        + " navigation20Ms=" + (end-firstEnd));
                // Allow emulator scheduling jitter; the old 5.6–6.0 s path still fails this budget.
                assertTrue(expectedGames + "-game cold load must finish within 2.5 seconds: "
                                + (loaded-start),
                        loaded-start < 2500);
                assertTrue("selection must reuse ordering, 20 selections within 200 ms: " + (end-firstEnd),
                        end-firstEnd < 200);
                if (run == 2) verifyRealHome(runtime, base, expectedGames);
            }
        }
        failBuiltin.set(false);
        try (var runtime = new AndroidCatalogRuntime(isolated)) {
            // Let construction read the shared manifest, then prove an unchanged restart never
            // opens or copies bundled ROM assets.
            failBuiltin.set(true);
            AndroidCatalogRuntime.Startup startup = runtime.start();
            assertEquals(AndroidCatalogRuntime.CacheStatus.HIT,
                    startup.cacheReady().get().status());
            startup.nativeReady().get();
            assertEquals("unchanged startup must keep the cached external library", expectedGames,
                    runtime.gameCatalog().canonicalEntries().size());
        } finally {
            failBuiltin.set(false);
        }
    }

    private static void verifyRealHome(AndroidCatalogRuntime fixture, Context base,
                                       int expectedGames) throws Exception {
        var instrumentation = InstrumentationRegistry.getInstrumentation();
        // Only swap this Activity's runtime. The user's persisted catalog and source grants stay intact.
        try (var scenario = ActivityScenario.launch(HomeActivity.class)) {
            ((com.flynes.emu.FlyNesApplication) base).catalogRuntime().bootstrap().get();
            instrumentation.waitForIdleSync();
            scenario.onActivity(activity -> {
                try {
                    var runtimeField = HomeActivity.class.getDeclaredField("runtime");
                    runtimeField.setAccessible(true);
                    runtimeField.set(activity, fixture);
                    var navigationField = HomeActivity.class.getDeclaredField("navigation");
                    navigationField.setAccessible(true);
                    navigationField.set(activity, new GameCenterState());
                    var refresh = HomeActivity.class.getDeclaredMethod("refreshSnapshot");
                    refresh.setAccessible(true);
                    refresh.invoke(activity);
                } catch (ReflectiveOperationException failure) { throw new AssertionError(failure); }
            });
            instrumentation.waitForIdleSync();
            scenario.onActivity(activity -> {
                RecyclerView grid = activity.findViewById(R.id.game_grid);
                assertEquals(expectedGames, grid.getAdapter().getItemCount());
                assertTrue(grid.getChildCount() > 1);
                assertTrue("highest popularity must be visible first",
                        grid.getChildAt(0).getContentDescription().toString().contains("魂斗罗"));
                java.util.concurrent.atomic.AtomicInteger fullRefresh = new java.util.concurrent.atomic.AtomicInteger();
                grid.getAdapter().registerAdapterDataObserver(new RecyclerView.AdapterDataObserver() {
                    @Override public void onChanged() { fullRefresh.incrementAndGet(); }
                });
                grid.getChildAt(1).performClick();
                assertEquals("selection must not invalidate every card", 0, fullRefresh.get());
                grid.scrollToPosition(expectedGames - 1);
            });
            instrumentation.waitForIdleSync();
            scenario.onActivity(activity -> {
                RecyclerView grid = activity.findViewById(R.id.game_grid);
                assertNotNull("last card remains reachable",
                        grid.findViewHolderForAdapterPosition(expectedGames - 1));
                grid.scrollToPosition(0);
            });
            instrumentation.waitForIdleSync();
            java.util.concurrent.CountDownLatch animations = new java.util.concurrent.CountDownLatch(1);
            scenario.onActivity(activity -> {
                RecyclerView grid = activity.findViewById(R.id.game_grid);
                if (grid.getItemAnimator() == null) animations.countDown();
                else grid.getItemAnimator().isRunning(animations::countDown);
            });
            assertTrue(animations.await(5, java.util.concurrent.TimeUnit.SECONDS));
            java.util.concurrent.CountDownLatch frames = new java.util.concurrent.CountDownLatch(1);
            scenario.onActivity(activity -> activity.findViewById(R.id.game_grid).postOnAnimation(
                    () -> activity.findViewById(R.id.game_grid).postOnAnimation(frames::countDown)));
            assertTrue(frames.await(5, java.util.concurrent.TimeUnit.SECONDS));
            scenario.onActivity(activity -> {
                RecyclerView grid = activity.findViewById(R.id.game_grid);
                assertNotNull("first card is laid out after scrolling back", grid.findViewHolderForAdapterPosition(0));
                android.view.View card = grid.findViewHolderForAdapterPosition(0).itemView;
                android.graphics.Rect rect = new android.graphics.Rect();
                assertTrue("first card has visible bounds", card.getGlobalVisibleRect(rect));
                assertTrue("first card has finished fading in", card.getAlpha() > 0.99f);
                Log.i("FlyNesCatalogPerf", "card rect=" + rect + " alpha=" + card.getAlpha()
                        + " translation=" + card.getTranslationX() + "," + card.getTranslationY());
            });
            android.graphics.Bitmap bitmap = instrumentation.getUiAutomation().takeScreenshot();
            assertNotNull(bitmap);
                try (var output = new FileOutputStream(new File(
                        base.getCacheDir(), "catalog-" + expectedGames + ".png"))) {
                    bitmap.compress(android.graphics.Bitmap.CompressFormat.PNG, 100, output);
                } catch (Exception failure) { throw new AssertionError(failure); }
                bitmap.recycle();
        }
    }
}
