package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import android.content.Context;
import android.os.SystemClock;
import android.view.MotionEvent;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

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
        float density = context.getResources().getDisplayMetrics().density;
        view.layout(0, 0, width, height);
        long now = SystemClock.uptimeMillis();
        MotionEvent down = MotionEvent.obtain(now, now, MotionEvent.ACTION_DOWN,
                width - 32f * density, height - 52f * density, 0);
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
        float density = context.getResources().getDisplayMetrics().density;
        view.layout(0, 0, width, height);
        long now = SystemClock.uptimeMillis();
        float x = width - 32f * density;
        float y = height - 52f * density;
        MotionEvent down = MotionEvent.obtain(
                now, now, MotionEvent.ACTION_DOWN, x, y, 0);
        MotionEvent up = MotionEvent.obtain(
                now, now, MotionEvent.ACTION_UP, x, y, 0);

        view.onTouchEvent(down);
        view.onTouchEvent(up);
        down.recycle();
        up.recycle();

        assertEquals(InputBits.A, view.buttons());
        SystemClock.sleep(90L);
        assertEquals(0, view.buttons());
    }
}
