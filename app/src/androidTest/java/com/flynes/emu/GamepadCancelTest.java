package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import android.content.Context;
import android.os.SystemClock;
import android.view.MotionEvent;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import org.junit.Test;
import org.junit.runner.RunWith;

import com.flynes.emu.input.InputBits;
import com.flynes.emu.input.InputRouter;

@RunWith(AndroidJUnit4.class)
public final class GamepadCancelTest {
    @Test
    public void resetClearsEveryPointerAndPublishesZero() {
        Context context = ApplicationProvider.getApplicationContext();
        GamepadView view = new GamepadView(context);
        InputRouter router = new InputRouter(mask -> { }, action -> { });
        view.setInputRouter(router);
        int width = 2340;
        int height = 1080;
        view.layout(0, 0, width, height);
        com.flynes.emu.input.GamepadHitMap.Target a =
                view.hitMapForTest().target(com.flynes.emu.input.GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN,
                a.centerX(), a.centerY(), 0);
        view.onTouchEvent(down);
        down.recycle();

        assertEquals(InputBits.A, view.buttons());
        assertEquals(InputBits.A, router.currentMask());

        view.reset();

        assertEquals(0, router.currentMask());
        assertEquals(0, view.buttons());
    }

    @Test
    public void zeroDurationATapSurvivesLongEnoughForTheCoreToSampleIt() {
        Context context = ApplicationProvider.getApplicationContext();
        GamepadView view = new GamepadView(context);
        int width = 2340;
        int height = 1080;
        view.layout(0, 0, width, height);
        com.flynes.emu.input.GamepadHitMap.Target a =
                view.hitMapForTest().target(com.flynes.emu.input.GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();
        float x = a.centerX();
        float y = a.centerY();
        MotionEvent down = MotionEvent.obtain(
                now, now, MotionEvent.ACTION_DOWN, x, y, 0);
        MotionEvent up = MotionEvent.obtain(
                now, now, MotionEvent.ACTION_UP, x, y, 0);

        view.onTouchEvent(down);
        view.onTouchEvent(up);
        down.recycle();
        up.recycle();

        assertEquals(InputBits.A, view.buttons());
        SystemClock.sleep(40L);
        assertEquals(0, view.buttons());
    }

    @Test
    public void minimumTapReleaseDoesNotWaitForTheMainLooper() {
        Context context = ApplicationProvider.getApplicationContext();
        GamepadView view = new GamepadView(context);
        view.layout(0, 0, 2340, 1080);
        com.flynes.emu.input.GamepadHitMap.Target a =
                view.hitMapForTest().target(com.flynes.emu.input.GamepadHitMap.Control.A);

        InstrumentationRegistry.getInstrumentation().runOnMainSync(() -> {
            long now = SystemClock.uptimeMillis();
            MotionEvent down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN,
                    a.centerX(), a.centerY(), 0);
            MotionEvent up = MotionEvent.obtain(now, now, MotionEvent.ACTION_UP,
                    a.centerX(), a.centerY(), 0);
            view.onTouchEvent(down);
            view.onTouchEvent(up);
            down.recycle();
            up.recycle();
            assertEquals(InputBits.A, view.buttons());

            // Rendering or a window transition may briefly occupy the UI thread. The
            // emulated button must still be released on time so a tap cannot become a hold.
            SystemClock.sleep(80L);
            assertEquals(0, view.buttons());
        });
    }
}
