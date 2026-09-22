package com.flynes.emu;

import android.content.Intent;
import android.os.SystemClock;
import android.view.MotionEvent;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.platform.app.InstrumentationRegistry;
import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.input.GamepadHitMap;
import org.junit.Test;
import java.io.File;
import java.nio.file.Files;
import java.util.concurrent.TimeUnit;
import static org.junit.Assert.*;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.action.ViewActions.replaceText;
import static androidx.test.espresso.Espresso.closeSoftKeyboard;
import static androidx.test.espresso.matcher.ViewMatchers.withId;

/** Opt-in local acceptance. User ROMs stay in the existing catalog, never fixtures. */
public final class NearbyMvpProductPlayTest {
    @Test public void hostUsesProductControls() throws Exception {
        FlyNesApplication app = ApplicationProvider.getApplicationContext();
        var args = InstrumentationRegistry.getArguments();
        String query = args.getString("localGameQuery", "");
        byte[] rom;
        if (query.isEmpty()) rom = NearbyMvpGame.load(app).rom;
        else {
            app.catalogRuntime().bootstrap().get(30, TimeUnit.SECONDS);
            GameVariant selected = null;
            for (GameCatalogEntry entry : app.catalogRuntime().gameCatalog().canonicalEntries()) {
                for (GameVariant candidate : entry.variants()) {
                    if (candidate.isLaunchable() && candidate.originalFilename().equals(query)) {
                        selected = candidate;
                        break;
                    }
                }
                if (selected != null) break;
            }
            assertNotNull("Requested local catalog game is absent", selected);
            rom = app.catalogRuntime().nearbyContentLoader().load(selected.variantId()).bytes();
        }
        assertTrue(app.nearbyMvpOwner().startHost(NearbyMvpLanAddress.current()));
        NearbyMvpSession session = app.nearbyMvpOwner().session();
        long deadline = SystemClock.elapsedRealtime() + 30_000;
        String invite;
        do { invite = session.invite(); SystemClock.sleep(20); }
        while (invite == null && SystemClock.elapsedRealtime() < deadline);
        assertNotNull("No invitation", invite);
        Files.write(new File(app.getFilesDir(), "nearby-cross-invite.txt").toPath(),
                invite.getBytes(java.nio.charset.StandardCharsets.UTF_8));
        while (session.snapshot()[0] != NearbyMvpSession.LOBBY && SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(20);
        assertEquals(NearbyMvpSession.LOBBY, session.snapshot()[0]);
        assertTrue(session.selectRom(rom));
        assertTrue(session.confirm());
        while (session.snapshot()[0] != NearbyMvpSession.RUNNING && SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(20);
        assertEquals(NearbyMvpSession.RUNNING, session.snapshot()[0]);
        byte[] connectedId = session.sessionId();
        app.nearbyMvpOwner().gameTitle(query.isEmpty() ? "Local game" : query);
        try (ActivityScenario<MainActivity> page = ActivityScenario.launch(
                new Intent(app, MainActivity.class).putExtra("nearby_mvp", true))) {
            SystemClock.sleep(6000);
            for (GamepadHitMap.Control control : new GamepadHitMap.Control[] {
                    GamepadHitMap.Control.START, GamepadHitMap.Control.SELECT,
                    GamepadHitMap.Control.START, GamepadHitMap.Control.START,
                    GamepadHitMap.Control.A, GamepadHitMap.Control.B,
                    GamepadHitMap.Control.RIGHT }) {
                long down = SystemClock.uptimeMillis();
                float[] point = new float[2];
                page.onActivity(activity -> {
                    GamepadView pad = activity.findViewById(R.id.gamepad);
                    var target = pad.hitMapForTest().target(control);
                    int[] origin = new int[2];
                    pad.getLocationOnScreen(origin);
                    point[0] = origin[0] + target.centerX();
                    point[1] = origin[1] + target.centerY();
                });
                touch(down, MotionEvent.ACTION_DOWN, point);
                page.onActivity(activity -> {
                    GamepadView pad = activity.findViewById(R.id.gamepad);
                    assertTrue("Real touch was not accepted: " + control, pad.buttons() != 0);
                });
                SystemClock.sleep(150);
                int expected = control == GamepadHitMap.Control.START ? 8 :
                        control == GamepadHitMap.Control.SELECT ? 4 :
                        control == GamepadHitMap.Control.A ? 1 : control == GamepadHitMap.Control.B ? 2 : 128;
                assertEquals("Touch must reach a completed P1 core frame", expected, session.snapshot()[9]);
                touch(down, MotionEvent.ACTION_UP, point);
                SystemClock.sleep(control == GamepadHitMap.Control.START ? 1200 : 150);
            }
            long before = session.completedFrames();
            SystemClock.sleep(2000);
            assertTrue("Product loop did not advance", session.completedFrames() > before + 30);
            long hold = Long.parseLong(args.getString("playHoldMs", "0"));
            if (hold > 0) SystemClock.sleep(hold);
            onView(withId(R.id.pause_button)).perform(click());
            SystemClock.sleep(250);
            long paused = session.completedFrames();
            SystemClock.sleep(2400);
            assertEquals("Pause must freeze the core without disconnecting", paused, session.completedFrames());
            assertEquals(NearbyMvpSession.RUNNING, session.snapshot()[0]);
            onView(withId(R.id.pause_continue)).perform(click());
            SystemClock.sleep(800);
            assertTrue("Resume must advance the same game", session.completedFrames() > paused + 10);
            onView(withId(R.id.pause_button)).perform(click());
            onView(withId(R.id.pause_game_center)).perform(click());
            waitState(session, NearbyMvpSession.LOBBY);
            SystemClock.sleep(700);
            onView(withId(R.id.nearby_lobby_row_rom_identity)).perform(click());
            SystemClock.sleep(1000);
            clearCatalogSearch();
            onView(withId(R.id.category_builtin)).perform(click());
            onView(withId(R.id.launch_selected)).perform(click());
            waitState(session, NearbyMvpSession.CONFIGURING);
            long readyDeadline = SystemClock.elapsedRealtime() + 15000;
            while (session.snapshot()[6] == 0 && SystemClock.elapsedRealtime() < readyDeadline)
                SystemClock.sleep(50);
            assertEquals("Guest must resolve the second game through its real catalog", 1, session.snapshot()[6]);
            SystemClock.sleep(500);
            onView(withId(R.id.nearby_lobby_confirm)).perform(click());
            waitState(session, NearbyMvpSession.RUNNING);
            assertArrayEquals("Changing games must retain the connection", connectedId, session.sessionId());
            SystemClock.sleep(5000);
            assertTrue("Second game must play", session.completedFrames() > 30);
            android.util.Log.i("FlyNesNearby", "event=product_lobby_switch second_game=PASS");
            if (!query.isEmpty()) {
                onView(withId(R.id.pause_button)).perform(click());
                onView(withId(R.id.pause_game_center)).perform(click());
                waitState(session, NearbyMvpSession.LOBBY);
                SystemClock.sleep(700);
                onView(withId(R.id.nearby_lobby_row_rom_identity)).perform(click());
                SystemClock.sleep(1000);
                clearCatalogSearch();
                onView(withId(R.id.category_all)).perform(click());
                onView(withId(R.id.open_search)).perform(click());
                onView(withId(R.id.search_input)).perform(replaceText(query.replaceFirst("\\.[^.]+$", "")));
                closeSoftKeyboard();
                onView(withId(R.id.launch_selected)).perform(click());
                waitState(session, NearbyMvpSession.CONFIGURING);
                readyDeadline = SystemClock.elapsedRealtime() + 15000;
                while (session.snapshot()[6] == 0 && SystemClock.elapsedRealtime() < readyDeadline)
                    SystemClock.sleep(50);
                assertEquals("Guest must resolve the user ROM from its real catalog", 1, session.snapshot()[6]);
                SystemClock.sleep(500);
                onView(withId(R.id.nearby_lobby_confirm)).perform(click());
                waitState(session, NearbyMvpSession.RUNNING);
                assertArrayEquals(connectedId, session.sessionId());
                SystemClock.sleep(5000);
                assertTrue(session.completedFrames() > 30);
                android.util.Log.i("FlyNesNearby", "event=product_lobby_switch third_game=PASS");
            }
        }
    }

    private static void waitState(NearbyMvpSession session, int wanted) {
        long deadline = SystemClock.elapsedRealtime() + 15000;
        while (session.snapshot()[0] != wanted && SystemClock.elapsedRealtime() < deadline)
            SystemClock.sleep(20);
        assertEquals("Unexpected session state", wanted, session.snapshot()[0]);
    }

    private static void clearCatalogSearch() {
        onView(withId(R.id.close_search)).check((view, error) -> {
            if (error != null) throw error;
            if (view.isShown()) view.performClick();
        });
    }

    private static void touch(long down, int action, float[] point) {
        MotionEvent event = MotionEvent.obtain(down, SystemClock.uptimeMillis(), action,
                point[0], point[1], 0);
        event.setSource(android.view.InputDevice.SOURCE_TOUCHSCREEN);
        InstrumentationRegistry.getInstrumentation().sendPointerSync(event);
        event.recycle();
    }
}
