package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotNull;
import static org.junit.Assert.assertNull;
import static org.junit.Assert.assertTrue;

import android.app.Activity;
import android.app.Application;
import android.app.Instrumentation;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Bitmap;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.widget.CompoundButton;
import android.widget.EditText;

import androidx.recyclerview.widget.RecyclerView;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry;
import androidx.test.runner.lifecycle.Stage;

import com.flynes.emu.catalog.BuiltinGames;
import com.flynes.emu.catalog.android.AndroidBuiltinCatalogAdapter;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.video.NativeFrameSource;
import com.flynes.emu.video.NativeInputSample;
import com.flynes.emu.video.PublishedFrame;

import org.junit.Test;
import org.junit.runner.RunWith;
import org.json.JSONObject;

import java.io.File;
import java.io.FileOutputStream;
import java.io.PrintWriter;
import java.io.OutputStreamWriter;
import java.lang.reflect.Field;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;

/**
 * UI-driven gameplay evidence capture. Screenshots require explicit visual review before gameplay is claimed.
 * Run this class alone in a fresh instrumentation process so the lazy Nearby owner is absent.
 * Evidence is written below app external files/out/evidence for an adb pull after the run.
 * Autosave is temporarily disabled: the current UI has no isolated manual save/load slot.
 */
@RunWith(AndroidJUnit4.class)
public final class BuiltinGameplayAcceptanceTest {
    private static final long TIMEOUT_MS = 30_000;
    private static final String[] NAVIGATION_KEYS = {"category", "query", "selected", "multiplayerOnly"};
    private final Instrumentation instrumentation = InstrumentationRegistry.getInstrumentation();

    @Test public void captureRequestedGameplayAndLifecycleEvidence()
            throws Throwable {
        FlyNesApplication app = ApplicationProvider.getApplicationContext();
        List<BuiltinGames.Entry> games = BuiltinGames.fromAssets(app).all();
        assertEquals("this acceptance run must cover all seven shipped games", 7, games.size());
        String requested = InstrumentationRegistry.getArguments().getString("playGame", "all");
        List<BuiltinGames.Entry> selectedGames = new ArrayList<>();
        for (BuiltinGames.Entry game : games) {
            if ("all".equals(requested) || game.canonicalId.endsWith(":" + requested)) {
                selectedGames.add(game);
            }
        }
        assertEquals("playGame must be all or uniquely match a shipped game: " + requested,
                "all".equals(requested) ? 7 : 1, selectedGames.size());
        assertNull("run this class alone before any Nearby entry", app.nearbySessionOwner());
        assertTrue("do not trigger migration of a user's unprocessed legacy save",
                !new File(app.getFilesDir(), "autosave.nst").exists()
                        || "success".equals(app.getSharedPreferences("save_migration", Context.MODE_PRIVATE)
                        .getString("legacy_autosave_migration", null)));
        app.catalogRuntime().bootstrap().get(30, java.util.concurrent.TimeUnit.SECONDS);
        SettingsRepository settings = app.settingsRepository();
        AppSettings original = settings.load();
        SharedPreferences navigation = app.getSharedPreferences("game_center_ui", Context.MODE_PRIVATE);
        Map<String, ?> originalNavigation = navigation.getAll();
        File evidence = new File(app.getExternalFilesDir(null),
                "out/evidence/actual-gameplay-" + System.currentTimeMillis());
        assertTrue("create evidence directory", evidence.mkdirs());
        CreatedActivities activities = new CreatedActivities();
        app.registerActivityLifecycleCallbacks(activities);
        boolean restorePreferences = false;
        Throwable primaryFailure = null;
        List<Throwable> cleanupFailures = new ArrayList<>();
        try (PrintWriter report = new PrintWriter(new File(evidence, "frames.tsv"), "UTF-8")) {
            writeRecovery(new File(evidence, "recovery.json"), original, originalNavigation);
            report.println("# original preferences, including absent keys, are in recovery.json");
            report.println("# save/load NOT TESTED: no isolated slot in the current official UI; autosave disabled");
            report.println("canonical_id\tstarted_frame\tinput_frame\tpaused_frame\tresumed_frame\tresult");
            assertFalse("write report header before modifying preferences", report.checkError());
            // Set before save: even a failed save can have updated an in-memory settings batch.
            restorePreferences = true;
            assertTrue("disable autosave before launching any ROM",
                    settings.save(original.toBuilder().autosaveEnabled(false).build()));
            try (ActivityScenario<HomeActivity> ignored = ActivityScenario.launch(HomeActivity.class)) {
                for (BuiltinGames.Entry game : selectedGames) {
                    try {
                        playOne(app, game, evidence, report);
                    } catch (Throwable failure) {
                        report.println(game.canonicalId + "\t-\t-\t-\t-\tFAIL: " + failure);
                        report.flush();
                        throw failure;
                    }
                }
            }
            report.println("# smoke play only; no claim of completion, physical latency, power or refresh rate");
            assertFalse("write complete smoke evidence", report.checkError());
        } catch (Throwable failure) {
            primaryFailure = failure;
        } finally {
            // Only finish activities created by this test; no app data or user save is removed.
            attemptCleanup(cleanupFailures, () -> instrumentation.runOnMainSync(() -> {
                for (int i = activities.created.size() - 1; i >= 0; --i) {
                    Activity activity = activities.created.get(i);
                    attemptCleanup(cleanupFailures, () -> {
                        if (!activity.isDestroyed()) activity.finish();
                    });
                }
            }));
            attemptCleanup(cleanupFailures, instrumentation::waitForIdleSync);
            attemptCleanup(cleanupFailures, () -> await("test activities finish before restoring navigation", () -> {
                AtomicReference<Boolean> destroyed = new AtomicReference<>(true);
                instrumentation.runOnMainSync(() -> {
                    for (Activity activity : activities.created) {
                        if (!activity.isDestroyed()) destroyed.set(false);
                    }
                });
                return destroyed.get();
            }));
            attemptCleanup(cleanupFailures, () -> app.unregisterActivityLifecycleCallbacks(activities));
            if (restorePreferences) {
                attemptCleanup(cleanupFailures, () -> assertTrue("restore the original autosave preference",
                        settings.save(settings.load().toBuilder()
                                .autosaveEnabled(original.autosaveEnabled()).build())));
                attemptCleanup(cleanupFailures, () -> restoreNavigation(navigation, originalNavigation));
            }
        }
        if (primaryFailure == null && !cleanupFailures.isEmpty()) {
            primaryFailure = new AssertionError("Smoke cleanup failed; original preferences are in "
                    + new File(evidence, "recovery.json"));
        }
        if (primaryFailure != null) {
            for (Throwable failure : cleanupFailures) primaryFailure.addSuppressed(failure);
            throw primaryFailure;
        }
    }

    private void playOne(FlyNesApplication app, BuiltinGames.Entry game, File evidence,
                         PrintWriter report) throws Exception {
        String catalogId = catalogIdFor(app, game);
        report.println("# identity\t" + game.canonicalId + "\t" + catalogId);
        HomeActivity home = awaitActivity(HomeActivity.class);
        instrumentation.runOnMainSync(() -> {
            ((CompoundButton) home.findViewById(R.id.multiplayer_filter)).setChecked(false);
            home.findViewById(R.id.category_builtin).performClick();
            ((EditText) home.findViewById(R.id.search_input)).setText(game.titleEn);
        });
        await("manifest game card appears: " + game.canonicalId, () -> {
            AtomicReference<Boolean> selected = new AtomicReference<>(false);
            instrumentation.runOnMainSync(() -> {
                RecyclerView grid = home.findViewById(R.id.game_grid);
                if (grid.getAdapter() == null || grid.getAdapter().getItemCount() != 1) return;
                RecyclerView.ViewHolder holder = grid.findViewHolderForAdapterPosition(0);
                if (holder == null) return;
                assertTrue("select the real library card", holder.itemView.performClick());
                selected.set(catalogId.equals(home.findViewById(R.id.detail_cover).getTag()));
                selected.set(selected.get() && home.findViewById(R.id.launch_selected).isEnabled());
            });
            return selected.get();
        });
        instrumentation.runOnMainSync(() -> assertTrue("launch through the official library button",
                home.findViewById(R.id.launch_selected).performClick()));
        MainActivity player = awaitActivity(MainActivity.class);
        assertEquals("exact catalog handoff, not the direct-launch fallback", catalogId,
                field(player, "currentCoverGameId", String.class));
        NesCore core = field(player, "core", NesCore.class);
        FrameProbe frames = new FrameProbe(core);
        await("real core frames for " + game.canonicalId,
                () -> frames.sequence() >= 60 && frames.hasPicture);
        long started = frames.sequence();
        assertSolo(app, player);

        SystemClock.sleep(1000);
        String name = game.canonicalId.replace(':', '_');
        screenshot(new File(evidence, name + "-00-boot.png"));
        String keys = InstrumentationRegistry.getArguments().getString("playKeys", defaultKeys(game.assetFilename));
        report.println("# keys\t" + game.canonicalId + "\t" + keys);
        int step = 0;
        for (String token : keys.split(",")) {
            step++;
            if (token.startsWith("WAIT")) {
                SystemClock.sleep(Integer.parseInt(token.substring(4)));
            } else {
                String[] parts = token.split(":");
                GamepadHitMap.Control control = GamepadHitMap.Control.valueOf(parts[0]);
                int bits = GamepadView.class.getField(parts[0]).getInt(null);
                holdMillis = parts.length > 1 ? Integer.parseInt(parts[1]) : 150;
                press(player, core, control, bits);
                SystemClock.sleep(400);
            }
            screenshot(new File(evidence, name + String.format("-%02d-", step) + token.replace(':', '_') + ".png"));
            report.println("# input\t" + game.canonicalId + "\t" + step + "\t" + token + "\t" + frames.sequence());
            report.flush();
        }
        await("frames continue after gameplay input", () -> frames.sequence() >= started + 60);
        long afterInput = frames.sequence();
        screenshot(new File(evidence, game.canonicalId.replace(':', '_') + "-playing.png"));

        click(player, R.id.pause_button);
        await("pause drawer is visible", () -> visible(player, R.id.pause_drawer));
        await("pause stops the real audio/core loop", () -> {
            Thread audio = field(player, "audio", Thread.class);
            return audio == null || !audio.isAlive();
        });
        long paused = frames.sequence();
        SystemClock.sleep(350);
        assertEquals("core frame sequence must stay fixed while paused", paused, frames.sequence());
        assertSolo(app, player);
        screenshot(new File(evidence, name + "-paused.png"));
        click(player, R.id.pause_continue);
        await("resume produces real core frames", () -> frames.sequence() >= paused + 30);
        long resumed = frames.sequence();
        assertFalse("resume dismisses the drawer", visible(player, R.id.pause_drawer));

        click(player, R.id.pause_button);
        await("return control is visible", () -> visible(player, R.id.pause_game_center));
        click(player, R.id.pause_game_center);
        HomeActivity returned = awaitActivity(HomeActivity.class);
        assertTrue("normal pause navigation returns to a visible library",
                visible(returned, R.id.game_grid));
        assertNull("single-player must not create the Nearby owner", app.nearbySessionOwner());
        screenshot(new File(evidence, name + "-library.png"));
        // Normal return keeps the old player in the Android back stack. Release only this test's
        // stopped player after proving the real UI returned, avoiding seven retained cores.
        instrumentation.runOnMainSync(player::finish);
        report.println(game.canonicalId + "\t" + started + "\t" + afterInput + "\t"
                + paused + "\t" + resumed + "\tPASS");
        report.flush();
    }

    private static String catalogIdFor(FlyNesApplication app, BuiltinGames.Entry game) {
        String found = null;
        for (var entry : app.catalogRuntime().gameCatalog().canonicalEntries()) {
            for (var variant : entry.variants()) {
                if (AndroidBuiltinCatalogAdapter.SOURCE.id().equals(variant.sourceId())
                        && game.assetFilename.equals(variant.originalFilename())
                        && AndroidBuiltinCatalogAdapter.assetLocator(game.assetFilename)
                                .equals(variant.sourceUri())) {
                    assertNull("one exact bundled variant for " + game.canonicalId, found);
                    assertTrue("the manifest asset must be launchable", variant.isLaunchable());
                    found = entry.canonicalGame().id();
                }
            }
        }
        assertNotNull("manifest asset exists in the real catalog: " + game.canonicalId, found);
        return found;
    }

    private long holdMillis = 150;

    private static String defaultKeys(String asset) {
        switch (asset) {
            case "super_tilt_bro.nes": return "START,START,START,WAIT2000,A,START,WAIT1000,A,WAIT2500,RIGHT:600,UP,A,B,LEFT:500";
            case "twin_dragons.nes": return "START,WAIT1500,RIGHT:600,A,RIGHT:600,B,LEFT:400";
            case "rhde.nes": return "START,A,START,A,WAIT2000,RIGHT:600,A,B";
            case "zap_ruder.nes": return "START,DOWN,DOWN,DOWN,DOWN,A,A,A,A,WAIT1500,UP:500,DOWN:600,A,WAIT2000";
            case "concentration_room.nes": return "START,A,WAIT800,UP:200,DOWN:50,A,RIGHT:80,A,WAIT1000,DOWN:200,A";
            case "thwaite.nes": return "START,A,START,WAIT1000,RIGHT:500,A,B,A,A,WAIT2000";
            case "dabg.nes": return "START,A,A,A,A,A,WAIT2000,RIGHT:600,A,B,LEFT:400";
            default: throw new AssertionError("Unreviewed manifest game: " + asset);
        }
    }

    private void press(MainActivity player, NesCore core, GamepadHitMap.Control control, int bits) {
        long downTime = SystemClock.uptimeMillis();
        AtomicReference<float[]> gesture = new AtomicReference<>();
        try {
            instrumentation.runOnMainSync(() -> {
                GamepadView pad = player.findViewById(R.id.gamepad);
                GamepadHitMap map = pad.hitMapForTest();
                GamepadHitMap.Target target = map.target(control);
                float x = target.centerX(), y = target.centerY();
                boolean drag = (control == GamepadHitMap.Control.RIGHT || control == GamepadHitMap.Control.LEFT
                        || control == GamepadHitMap.Control.UP || control == GamepadHitMap.Control.DOWN)
                        && map.directionMode() != DirectionControlMode.DPAD;
                if (drag) {
                    x = map.dpadBounds().centerX();
                    y = map.dpadBounds().centerY();
                    if (map.directionMode() == DirectionControlMode.JOYSTICK) {
                        x = map.clampJoystickCenterX(x);
                        y = map.clampJoystickCenterY(y);
                    }
                }
                float dx = control == GamepadHitMap.Control.RIGHT ? 1 : control == GamepadHitMap.Control.LEFT ? -1 : 0;
                float dy = control == GamepadHitMap.Control.DOWN ? 1 : control == GamepadHitMap.Control.UP ? -1 : 0;
                float endX = drag ? x + dx * map.joystickTravelRadius() : x;
                float endY = drag ? y + dy * map.joystickTravelRadius() : y;
                gesture.set(new float[]{endX, endY});
                dispatch(pad, x, y, downTime, MotionEvent.ACTION_DOWN);
                // Following capture starts neutral at the down point. A real move crosses the
                // dead zone; fixed joystick uses the same drag from its fixed centre.
                if (drag) dispatch(pad, endX, endY, downTime, MotionEvent.ACTION_MOVE);
            });
            await("native core consumes the on-screen " + control + " input", () -> {
                NativeInputSample sample = core.lastInputSample();
                return sample != null && (sample.padBits(0) & bits) == bits;
            });
            SystemClock.sleep(holdMillis);
        } finally {
            if (gesture.get() != null) {
                instrumentation.runOnMainSync(() -> dispatch(player.findViewById(R.id.gamepad),
                        gesture.get()[0], gesture.get()[1], downTime, MotionEvent.ACTION_UP));
            }
        }
        await("native input is released", () -> {
            NativeInputSample sample = core.lastInputSample();
            return sample != null && sample.padBits(0) == 0;
        });
    }

    private static void dispatch(GamepadView pad, float x, float y, long downTime, int action) {
        MotionEvent event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action,
                x, y, 0);
        try { assertTrue("gamepad consumes touch", pad.dispatchTouchEvent(event)); }
        finally { event.recycle(); }
    }

    private void assertSolo(FlyNesApplication app, MainActivity player) {
        instrumentation.runOnMainSync(() -> {
            assertNull("single-player requires no Nearby owner", app.nearbySessionOwner());
            assertNull(player.findViewById(R.id.nearby_ingame_banner));
            for (int id : NearbyInGameStatus.allRowIds()) assertNull(player.findViewById(id));
        });
    }

    private void click(Activity activity, int id) {
        instrumentation.runOnMainSync(() -> assertTrue("UI button " + id,
                activity.findViewById(id).performClick()));
    }

    private boolean visible(Activity activity, int id) {
        AtomicReference<Boolean> result = new AtomicReference<>(false);
        instrumentation.runOnMainSync(() -> result.set(activity.findViewById(id) != null
                && activity.findViewById(id).isShown()));
        return result.get();
    }

    private <T extends Activity> T awaitActivity(Class<T> type) {
        AtomicReference<T> result = new AtomicReference<>();
        await("resumed " + type.getSimpleName(), () -> {
            instrumentation.runOnMainSync(() -> {
                for (Activity activity : ActivityLifecycleMonitorRegistry.getInstance()
                        .getActivitiesInStage(Stage.RESUMED)) {
                    if (type.isInstance(activity)) result.set(type.cast(activity));
                }
            });
            return result.get() != null;
        });
        return result.get();
    }

    private static void await(String reason, BooleanSupplier condition) {
        long deadline = SystemClock.uptimeMillis() + TIMEOUT_MS;
        while (SystemClock.uptimeMillis() < deadline) {
            if (condition.getAsBoolean()) return;
            SystemClock.sleep(50);
        }
        throw new AssertionError("Timed out: " + reason);
    }

    private void screenshot(File file) throws Exception {
        Bitmap bitmap = instrumentation.getUiAutomation().takeScreenshot();
        assertNotNull("screenshot available", bitmap);
        try (FileOutputStream out = new FileOutputStream(file)) {
            assertTrue(bitmap.compress(Bitmap.CompressFormat.PNG, 100, out));
        } finally { bitmap.recycle(); }
    }

    private static <T> T field(Object object, String name, Class<T> type) {
        try {
            Field field = object.getClass().getDeclaredField(name);
            field.setAccessible(true);
            return type.cast(field.get(object));
        } catch (ReflectiveOperationException failure) { throw new AssertionError(failure); }
    }

    /** Reads published snapshots only; never drives or substitutes for the Activity's core. */
    private static final class FrameProbe {
        final NativeFrameSource source;
        long last;
        boolean hasPicture;
        FrameProbe(NesCore core) { source = new NativeFrameSource(core, 4 * 1024 * 1024); }
        long sequence() {
            try (PublishedFrame frame = source.copyLatest()) {
                if (frame != null) {
                    assertEquals(256, frame.width());
                    assertEquals(240, frame.height());
                    last = frame.sequence();
                    ByteBuffer bytes = frame.pixels();
                    // Compare complete pixels, not RGB565 bytes within a uniform colour.
                    int bpp = frame.format() == PublishedFrame.Format.RGB565 ? 2
                            : frame.format() == PublishedFrame.Format.RGB888 ? 3 : 4;
                    for (int i = bpp; i < frame.width() * bpp; i += bpp) {
                        for (int c = 0; c < bpp; c++) {
                            if (bytes.get(i + c) != bytes.get(c)) hasPicture = true;
                        }
                    }
                    for (int y = 1; y < frame.height(); y++) {
                        for (int x = 0; x < frame.width() * bpp; x++) {
                            if (bytes.get(y * frame.pitch() + x) != bytes.get(x)) hasPicture = true;
                        }
                    }
                }
            }
            return last;
        }
    }

    private static void restoreNavigation(SharedPreferences prefs, Map<String, ?> original) {
        SharedPreferences.Editor edit = prefs.edit();
        for (String key : NAVIGATION_KEYS) {
            Object value = original.get(key);
            if (value instanceof String) edit.putString(key, (String) value);
            else if (value instanceof Boolean) edit.putBoolean(key, (Boolean) value);
            else edit.remove(key);
        }
        assertTrue("restore only the navigation preferences touched by this test", edit.commit());
    }

    private static void writeRecovery(File file, AppSettings original, Map<String, ?> navigation)
            throws Exception {
        JSONObject recovery = new JSONObject();
        recovery.put("autosaveEnabled", original.autosaveEnabled());
        JSONObject keys = new JSONObject();
        for (String key : NAVIGATION_KEYS) {
            Object value = navigation.get(key);
            boolean present = navigation.containsKey(key);
            assertTrue("unexpected original preference type for " + key,
                    !present || value instanceof String || value instanceof Boolean);
            JSONObject entry = new JSONObject();
            entry.put("present", present);
            entry.put("type", !present ? "absent" : value instanceof Boolean ? "boolean" : "string");
            entry.put("value", present ? value : JSONObject.NULL);
            keys.put(key, entry);
        }
        recovery.put("game_center_ui", keys);
        try (FileOutputStream output = new FileOutputStream(file);
             PrintWriter writer = new PrintWriter(new OutputStreamWriter(output, StandardCharsets.UTF_8))) {
            writer.println(recovery.toString(2));
            assertFalse("persist the complete recovery record before modifying preferences", writer.checkError());
            output.getFD().sync();
        }
    }

    private static void attemptCleanup(List<Throwable> failures, Runnable cleanup) {
        try { cleanup.run(); }
        catch (Throwable failure) { failures.add(failure); }
    }

    private static final class CreatedActivities implements Application.ActivityLifecycleCallbacks {
        final List<Activity> created = new ArrayList<>();
        @Override public void onActivityCreated(Activity activity, Bundle state) { created.add(activity); }
        @Override public void onActivityStarted(Activity activity) { }
        @Override public void onActivityResumed(Activity activity) { }
        @Override public void onActivityPaused(Activity activity) { }
        @Override public void onActivityStopped(Activity activity) { }
        @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) { }
        @Override public void onActivityDestroyed(Activity activity) { }
    }
}
