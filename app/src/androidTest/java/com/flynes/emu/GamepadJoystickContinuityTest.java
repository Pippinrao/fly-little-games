package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertNotEquals;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.SystemClock;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SdkSuppress;

import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.GamepadInputState;
import com.flynes.emu.input.InputBits;
import com.flynes.emu.settings.AppSettings;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;

@RunWith(AndroidJUnit4.class)
public final class GamepadJoystickContinuityTest {
    private static final int WIDTH = 2340;
    private static final int HEIGHT = 1080;
    private static final int DIRECTIONS = InputBits.UP | InputBits.DOWN
            | InputBits.LEFT | InputBits.RIGHT;

    @Test
    public void dragCanLeaveOriginalBaseAndReverseWithoutAnotherDown() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);

        send(view, now, now + 20, MotionEvent.ACTION_MOVE, 10f, base.centerY());
        assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);

        send(view, now, now + 30, MotionEvent.ACTION_UP, 10f, base.centerY());
        assertEquals(0, view.buttons() & DIRECTIONS);
    }

    @Test
    public void coalescedMoveHistoryIsProcessedChronologicallyAndPublishedOnce() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        List<Integer> publications = new ArrayList<>();
        view.setListener(publications::add);
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        MotionEvent move = MotionEvent.obtain(now, now + 10, MotionEvent.ACTION_MOVE,
                1800f, base.centerY(), 0);
        move.addBatch(now + 20, base.centerX(), base.centerY(), 1f, 1f, 0);
        view.onTouchEvent(move);
        move.recycle();

        assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
        assertEquals(1, publications.size());
        assertEquals(InputBits.LEFT, publications.get(0) & DIRECTIONS);
    }

    @Test
    public void coalescedMoveProcessesEveryPointerBeforeCurrentPositions() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        GamepadHitMap.Target a = view.hitMapForTest().target(GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();

        sendPointers(view, now, now, MotionEvent.ACTION_DOWN,
                new int[] {0}, new float[] {base.centerX()}, new float[] {base.centerY()});
        sendPointers(view, now, now + 5,
                MotionEvent.ACTION_POINTER_DOWN
                        | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[] {0, 1}, new float[] {base.centerX(), a.centerX()},
                new float[] {base.centerY(), a.centerY()});
        assertEquals(InputBits.A, view.buttons());

        MotionEvent move = obtainPointers(now, now + 10, MotionEvent.ACTION_MOVE,
                new int[] {0, 1}, new float[] {1800f, WIDTH * .75f},
                new float[] {base.centerY(), 100f});
        move.addBatch(now + 20,
                pointerCoordinates(new float[] {base.centerX(), a.centerX()},
                        new float[] {base.centerY(), a.centerY()}), 0);
        view.onTouchEvent(move);
        move.recycle();

        assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
        assertEquals(0, view.buttons() & InputBits.A);
    }

    @Test
    public void pointerDownAppliesSurvivorCoordinatesBeforeAddingNewRole() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        GamepadHitMap.Target a = view.hitMapForTest().target(GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);

        sendPointers(view, now, now + 20,
                MotionEvent.ACTION_POINTER_DOWN
                        | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[] {0, 1}, new float[] {10f, a.centerX()},
                new float[] {base.centerY(), a.centerY()});

        assertEquals(InputBits.LEFT | InputBits.A, view.buttons());
    }

    @Test
    public void actionUpUsesFinalCoordinateBeforeMinimumTapDecision() {
        GamepadView view = joystickView();
        GamepadHitMap.Target a = view.hitMapForTest().target(GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, a.centerX(), a.centerY());
        assertEquals(InputBits.A, view.buttons());

        send(view, now, now, MotionEvent.ACTION_UP, WIDTH * .75f, 100f);

        assertEquals(0, view.buttons());
    }

    @Test
    public void pointerUpAppliesSurvivorCoordinatesBeforeRemovingRole() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        GamepadHitMap.Target a = view.hitMapForTest().target(GamepadHitMap.Control.A);
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 5, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        sendPointers(view, now, now + 10,
                MotionEvent.ACTION_POINTER_DOWN
                        | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[] {0, 1}, new float[] {1800f, a.centerX()},
                new float[] {base.centerY(), a.centerY()});
        assertEquals(InputBits.RIGHT | InputBits.A, view.buttons());

        sendPointers(view, now, now + 40,
                MotionEvent.ACTION_POINTER_UP
                        | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[] {0, 1}, new float[] {10f, a.centerX()},
                new float[] {base.centerY(), a.centerY()});

        assertEquals(InputBits.LEFT, view.buttons());
    }

    @SdkSuppress(minSdkVersion = 29)
    @Test
    public void insetsUpdateKeepsActiveDirectionVisualAndPublishedSession() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        List<Integer> publications = new ArrayList<>();
        view.setListener(publications::add);
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        GamepadInputState.JoystickVisual before = joystickVisual(view);
        publications.clear();

        WindowInsets changed = new WindowInsets.Builder()
                .setSystemWindowInsets(Insets.of(40, 20, 80, 30))
                .build();
        view.onApplyWindowInsets(changed);

        assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);
        assertTrue(before.active());
        assertTrue(joystickVisual(view).active());
        assertTrue("Insets update transiently published a replacement state", publications.isEmpty());

        send(view, now, now + 20, MotionEvent.ACTION_MOVE, 10f, base.centerY());
        assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
        send(view, now, now + 30, MotionEvent.ACTION_UP, 10f, base.centerY());
        assertEquals(0, view.buttons() & DIRECTIONS);
    }

    @Test
    public void longMoveExposesTheSameBoundedVisualSessionUsedForDrawing() {
        GamepadView view = joystickView();
        GamepadHitMap hitMap = view.hitMapForTest();
        GamepadHitMap.Bounds idleBase = hitMap.dpadBounds();
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN,
                idleBase.centerX(), idleBase.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE,
                1800f, idleBase.centerY());

        GamepadInputState.JoystickVisual visual = joystickVisual(view);
        float knobDistance = (float) Math.hypot(
                visual.knobX() - visual.centerX(), visual.knobY() - visual.centerY());
        float distanceFromIdleCenter = (float) Math.hypot(
                visual.knobX() - idleBase.centerX(), visual.knobY() - idleBase.centerY());
        assertTrue(visual.active());
        assertTrue(knobDistance <= hitMap.joystickTravelRadius() + .5f);
        assertTrue("visual snapped back to the old fixed base",
                distanceFromIdleCenter > hitMap.joystickRadius());
    }

    @SdkSuppress(minSdkVersion = 29)
    @Test
    public void systemGestureExclusionTracksIdleActiveInsetsAndSize() {
        GamepadView view = joystickView();
        Rect idle = onlyExclusion(view);
        assertBoundedLocalExclusion(view, idle);

        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();
        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 10f, 10f);
        Rect active = onlyExclusion(view);
        GamepadInputState.JoystickVisual activeVisual = joystickVisual(view);
        assertBoundedLocalExclusion(view, active);
        assertContainsJoystickFootprint(view, active, activeVisual);
        assertNotEquals(idle, active);

        WindowInsets changed = new WindowInsets.Builder()
                .setSystemWindowInsets(Insets.of(40, 20, 80, 30))
                .build();
        view.onApplyWindowInsets(changed);
        Rect inset = onlyExclusion(view);
        assertBoundedLocalExclusion(view, inset);
        assertContainsJoystickFootprint(view, inset, joystickVisual(view));
        assertNotEquals(active, inset);

        measureAndLayout(view, 1800, 900);
        Rect resized = onlyExclusion(view);
        assertBoundedLocalExclusion(view, resized);
        assertFalse(joystickVisual(view).active());
        assertEquals(0, view.buttons() & DIRECTIONS);
        assertNotEquals(inset, resized);
    }

    @SdkSuppress(minSdkVersion = 29)
    @Test
    public void dpadModeHasBoundedLocalSystemGestureExclusion() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();
        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        assertTrue(joystickVisual(view).active());
        AppSettings dpad = AppSettings.defaults().toBuilder()
                .directionControlMode(DirectionControlMode.DPAD)
                .build();

        view.setControlSettings(dpad);

        assertEquals(0, view.buttons());
        assertFalse(joystickVisual(view).active());
        Rect exclusion = onlyExclusion(view);
        assertBoundedLocalExclusion(view, exclusion);
        GamepadHitMap.Bounds dpadBounds = view.hitMapForTest().dpadBounds();
        assertTrue(exclusion.contains(Math.round(dpadBounds.left),
                Math.round(dpadBounds.top), Math.round(dpadBounds.right),
                Math.round(dpadBounds.bottom)));
    }

    @SdkSuppress(minSdkVersion = 29)
    @Test
    public void farRightExclusionRemainsLocalWithoutTrimmingActiveBase() {
        GamepadView view = joystickView();
        WindowInsets changed = new WindowInsets.Builder()
                .setSystemWindowInsets(Insets.of(40, 20, 80, 30))
                .build();
        view.onApplyWindowInsets(changed);
        Rect idle = onlyExclusion(view);
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());

        Rect active = onlyExclusion(view);
        GamepadInputState.JoystickVisual visual = joystickVisual(view);
        float padding = 16f * view.getResources().getDisplayMetrics().density;
        float baseRight = visual.centerX() + view.hitMapForTest().joystickRadius()
                + padding;
        assertTrue("exclusion trimmed the active joystick base", active.right >= baseRight);
        assertTrue("exclusion unexpectedly covered the whole view",
                active.width() < view.getWidth());

        send(view, now, now + 20, MotionEvent.ACTION_UP, 1800f, base.centerY());
        assertEquals(idle, onlyExclusion(view));
    }

    @Test
    public void directionReleaseIsImmediateAndNeverMinimumTapPulsed() {
        GamepadView view = joystickView();
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();

        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);

        send(view, now, now, MotionEvent.ACTION_UP, 1800f, base.centerY());

        assertEquals(0, view.buttons() & DIRECTIONS);
    }

    @Test
    public void acceptedTouchOwnsParentInterceptionUntilEveryTerminalPath() {
        Context context = ApplicationProvider.getApplicationContext();
        RecordingParent parent = new RecordingParent(context);
        ExposedGamepadView view = new ExposedGamepadView(context);
        view.setControlSettings(AppSettings.defaults());
        parent.addView(view);
        measureAndLayout(parent, WIDTH, HEIGHT);
        long now = SystemClock.uptimeMillis();

        startRightSession(view, now);
        assertTrue(parent.lastInterceptionRequest());
        send(view, now, now + 20, MotionEvent.ACTION_UP, 1800f,
                view.hitMapForTest().dpadBounds().centerY());
        assertFalse(parent.lastInterceptionRequest());

        startRightSession(view, now + 30);
        send(view, now + 30, now + 50, MotionEvent.ACTION_CANCEL, 1800f,
                view.hitMapForTest().dpadBounds().centerY());
        assertTerminalState(view, parent);

        startRightSession(view, now + 60);
        view.reset();
        assertTerminalState(view, parent);

        startRightSession(view, now + 90);
        view.onWindowFocusChanged(false);
        assertTerminalState(view, parent);

        startRightSession(view, now + 120);
        view.detachForTest();
        assertTerminalState(view, parent);
    }

    @Test
    public void pointerUpKeepsParentInterceptionUntilTheFinalPointerEnds() {
        Context context = ApplicationProvider.getApplicationContext();
        RecordingParent parent = new RecordingParent(context);
        GamepadView view = new GamepadView(context);
        view.setControlSettings(AppSettings.defaults());
        parent.addView(view);
        measureAndLayout(parent, WIDTH, HEIGHT);
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        long now = SystemClock.uptimeMillis();

        sendPointers(view, now, now, MotionEvent.ACTION_DOWN,
                new int[] {0}, new float[] {base.centerX()}, new float[] {base.centerY()});
        assertTrue(parent.lastInterceptionRequest());

        sendPointers(view, now, now + 10,
                MotionEvent.ACTION_POINTER_DOWN
                        | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[] {0, 1}, new float[] {base.centerX(), WIDTH * .75f},
                new float[] {base.centerY(), 100f});
        sendPointers(view, now, now + 20, MotionEvent.ACTION_POINTER_UP,
                new int[] {0, 1}, new float[] {base.centerX(), WIDTH * .75f},
                new float[] {base.centerY(), 100f});

        assertTrue("interception released while a physical pointer remained",
                parent.lastInterceptionRequest());

        sendPointers(view, now, now + 30, MotionEvent.ACTION_UP,
                new int[] {1}, new float[] {WIDTH * .75f}, new float[] {100f});
        assertFalse(parent.lastInterceptionRequest());
    }

    private static GamepadView joystickView() {
        Context context = ApplicationProvider.getApplicationContext();
        GamepadView view = new GamepadView(context);
        view.setControlSettings(AppSettings.defaults().toBuilder()
                .directionControlMode(DirectionControlMode.JOYSTICK)
                .build());
        measureAndLayout(view, WIDTH, HEIGHT);
        return view;
    }

    private static void startRightSession(GamepadView view, long now) {
        GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
        send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
        send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
        assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);
    }

    private static void assertTerminalState(GamepadView view, RecordingParent parent) {
        assertEquals(0, view.buttons());
        assertFalse(joystickVisual(view).active());
        assertFalse(parent.lastInterceptionRequest());
    }

    private static void send(GamepadView view, long downTime, long eventTime,
                             int action, float x, float y) {
        MotionEvent event = MotionEvent.obtain(downTime, eventTime, action, x, y, 0);
        view.onTouchEvent(event);
        event.recycle();
    }

    private static void sendPointers(GamepadView view, long downTime, long eventTime,
                                     int action, int[] ids, float[] xs, float[] ys) {
        MotionEvent event = obtainPointers(downTime, eventTime, action, ids, xs, ys);
        view.onTouchEvent(event);
        event.recycle();
    }

    private static MotionEvent obtainPointers(long downTime, long eventTime, int action,
                                              int[] ids, float[] xs, float[] ys) {
        MotionEvent.PointerProperties[] properties =
                new MotionEvent.PointerProperties[ids.length];
        for (int index = 0; index < ids.length; index++) {
            MotionEvent.PointerProperties pointer = new MotionEvent.PointerProperties();
            pointer.id = ids[index];
            pointer.toolType = MotionEvent.TOOL_TYPE_FINGER;
            properties[index] = pointer;
        }
        return MotionEvent.obtain(downTime, eventTime, action, ids.length,
                properties, pointerCoordinates(xs, ys), 0, 0, 1f, 1f,
                0, 0, 0, 0);
    }

    private static MotionEvent.PointerCoords[] pointerCoordinates(float[] xs, float[] ys) {
        MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[xs.length];
        for (int index = 0; index < xs.length; index++) {
            MotionEvent.PointerCoords coordinate = new MotionEvent.PointerCoords();
            coordinate.x = xs[index];
            coordinate.y = ys[index];
            coordinate.pressure = 1f;
            coordinate.size = 1f;
            coordinates[index] = coordinate;
        }
        return coordinates;
    }

    private static void measureAndLayout(View view, int width, int height) {
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
        view.layout(0, 0, width, height);
    }

    private static GamepadInputState.JoystickVisual joystickVisual(GamepadView view) {
        return view.joystickVisualForTest();
    }

    private static Rect onlyExclusion(GamepadView view) {
        List<Rect> exclusions = view.getSystemGestureExclusionRects();
        assertEquals(1, exclusions.size());
        return new Rect(exclusions.get(0));
    }

    private static void assertBoundedLocalExclusion(GamepadView view, Rect exclusion) {
        assertEquals(0, exclusion.left);
        assertTrue(exclusion.top >= 0);
        assertTrue(exclusion.right <= view.getWidth());
        assertTrue(exclusion.bottom <= view.getHeight());
        assertTrue(exclusion.width() < view.getWidth());
        assertTrue(exclusion.height() < view.getHeight());
        assertTrue(exclusion.height() <= Math.round(200f
                * view.getResources().getDisplayMetrics().density));
    }

    private static void assertContainsJoystickFootprint(
            GamepadView view, Rect exclusion, GamepadInputState.JoystickVisual visual) {
        float radius = view.hitMapForTest().joystickRadius();
        assertTrue(exclusion.right >= (int) Math.ceil(visual.centerX() + radius));
        assertTrue(exclusion.top <= (int) Math.floor(visual.centerY() - radius));
        assertTrue(exclusion.bottom >= (int) Math.ceil(visual.centerY() + radius));
    }

    private static final class RecordingParent extends ViewGroup {
        private final List<Boolean> interceptionRequests = new ArrayList<>();

        RecordingParent(Context context) {
            super(context);
        }

        @Override
        public void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {
            interceptionRequests.add(disallowIntercept);
            super.requestDisallowInterceptTouchEvent(disallowIntercept);
        }

        boolean lastInterceptionRequest() {
            assertFalse("parent received no interception request", interceptionRequests.isEmpty());
            return interceptionRequests.get(interceptionRequests.size() - 1);
        }

        @Override
        protected void onMeasure(int widthMeasureSpec, int heightMeasureSpec) {
            int width = MeasureSpec.getSize(widthMeasureSpec);
            int height = MeasureSpec.getSize(heightMeasureSpec);
            if (getChildCount() != 0) {
                getChildAt(0).measure(
                        MeasureSpec.makeMeasureSpec(width, MeasureSpec.EXACTLY),
                        MeasureSpec.makeMeasureSpec(height, MeasureSpec.EXACTLY));
            }
            setMeasuredDimension(width, height);
        }

        @Override
        protected void onLayout(boolean changed, int left, int top, int right, int bottom) {
            if (getChildCount() != 0) getChildAt(0).layout(0, 0, right - left, bottom - top);
        }
    }

    private static final class ExposedGamepadView extends GamepadView {
        ExposedGamepadView(Context context) {
            super(context);
        }

        void detachForTest() {
            super.onDetachedFromWindow();
        }
    }
}
