package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import android.content.Context;
import android.graphics.Insets;
import android.graphics.Rect;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;
import android.widget.FrameLayout;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.SdkSuppress;
import androidx.test.platform.app.InstrumentationRegistry;

import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.GamepadInputState;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.InputBits;
import com.flynes.emu.input.InputRouter;
import com.flynes.emu.settings.AppSettings;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.ArrayList;
import java.util.List;

@RunWith(AndroidJUnit4.class)
public final class GamepadTouchDispatchTest {
    private static final int WIDTH = 2340;
    private static final int HEIGHT = 1080;
    private static final int DIRECTIONS = InputBits.UP | InputBits.DOWN
            | InputBits.LEFT | InputBits.RIGHT;

    @Test public void fixedRootDispatchStaysOwnedAtFiveRadiiAndReversesWithoutZero() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadView view = fixture.view;
            GamepadHitMap map = view.hitMapForTest();
            GamepadHitMap.Bounds base = map.dpadBounds();
            float radius = map.joystickRadius();
            List<Integer> publications = new ArrayList<>();
            view.setListener(publications::add);
            long downTime = SystemClock.uptimeMillis();

            dispatch(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            assertTrue(view.joystickVisualForTest().owned());
            assertFalse(view.joystickVisualForTest().activated());

            dispatch(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    base.centerX() + radius * 5f, base.centerY());
            GamepadInputState.JoystickVisual far = view.joystickVisualForTest();
            assertEquals(base.centerX(), far.centerX(), 0f);
            assertEquals(base.centerY(), far.centerY(), 0f);
            assertTrue(far.saturated());
            assertTrue(knobDistance(far) <= map.joystickTravelRadius() + .01f);
            assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);

            dispatch(fixture.root, downTime, downTime + 20, MotionEvent.ACTION_MOVE,
                    base.centerX(), base.centerY());
            assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);
            dispatch(fixture.root, downTime, downTime + 30, MotionEvent.ACTION_MOVE,
                    base.centerX() - radius * 5f, base.centerY());
            assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
            assertFalse("direction published a neutral frame after activation",
                    publications.contains(0));

            dispatch(fixture.root, downTime, downTime + 40, MotionEvent.ACTION_UP,
                    base.centerX() - radius * 5f, base.centerY());
            assertEquals(0, view.buttons() & DIRECTIONS);
            assertTrue(view.joystickReturnActiveForTest());
        });
    }

    @Test public void historicalSamplesProduceOneDirectionTickForTheWholeEvent() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatch(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());

            MotionEvent move = MotionEvent.obtain(downTime, downTime + 10,
                    MotionEvent.ACTION_MOVE, base.centerX() + radius, base.centerY(), 0);
            move.addBatch(downTime + 20, base.centerX(), base.centerY(), 1f, 1f, 0);
            move.addBatch(downTime + 30, base.centerX() - radius, base.centerY(), 1f, 1f, 0);
            fixture.root.dispatchTouchEvent(move);
            move.recycle();

            assertEquals(InputBits.LEFT, fixture.view.buttons() & DIRECTIONS);
            assertEquals(1, fixture.view.directionHapticCount);
        });
    }

    @Test public void newPrimaryDownClearsStaleTouchButPreservesKeyboardSource() {
        onMain(() -> {
            Fixture fixture = fixture();
            InputRouter router = new InputRouter(mask -> { }, action -> { });
            fixture.view.setInputRouter(router);
            fixture.view.onKeyDown(KeyEvent.KEYCODE_DPAD_UP,
                    new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_UP));
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long firstDown = SystemClock.uptimeMillis();
            dispatch(fixture.root, firstDown, firstDown, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, firstDown, firstDown + 10, MotionEvent.ACTION_MOVE,
                    base.centerX() + radius, base.centerY());
            assertEquals(InputBits.UP | InputBits.RIGHT, router.currentMask());

            long replacementDown = firstDown + 20;
            dispatch(fixture.root, replacementDown, replacementDown, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());

            assertEquals(InputBits.UP, router.currentMask());
            assertTrue(fixture.view.joystickVisualForTest().owned());
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);
        });
    }

    @SdkSuppress(minSdkVersion = 33)
    @Test public void flaggedPointerUpAndFrameworkCancelResetWithoutReturnAnimation() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatch(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    base.centerX() + radius, base.centerY());

            MotionEvent canceledUp = flaggedSinglePointerEvent(
                    downTime, downTime + 20, MotionEvent.ACTION_UP,
                    base.centerX() + radius, base.centerY());
            fixture.root.dispatchTouchEvent(canceledUp);
            canceledUp.recycle();
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);
            assertFalse(fixture.view.joystickReturnActiveForTest());

            long nextDown = downTime + 30;
            dispatch(fixture.root, nextDown, nextDown, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, nextDown, nextDown + 10, MotionEvent.ACTION_MOVE,
                    base.centerX() - radius, base.centerY());
            dispatch(fixture.root, nextDown, nextDown + 20, MotionEvent.ACTION_CANCEL,
                    base.centerX() - radius, base.centerY());
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);
            assertFalse(fixture.view.joystickReturnActiveForTest());
        });
    }

    @Test public void stablePointerIdsSurviveIndexReorderingAndKeepDirectionRoleAcrossButtons() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap map = fixture.view.hitMapForTest();
            GamepadHitMap.Bounds base = map.dpadBounds();
            GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
            GamepadHitMap.Target b = map.target(GamepadHitMap.Control.B);
            float radius = map.joystickRadius();
            long downTime = SystemClock.uptimeMillis();

            dispatchPointers(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    new int[]{7}, new float[]{base.centerX()}, new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    new int[]{7}, new float[]{a.centerX()}, new float[]{a.centerY()});
            int crossingDirection = fixture.view.buttons() & DIRECTIONS;
            assertTrue(crossingDirection != 0);
            assertEquals(0, fixture.view.buttons() & (InputBits.A | InputBits.B));

            dispatchPointers(fixture.root, downTime, downTime + 20,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{7, 42}, new float[]{a.centerX(), a.centerX()},
                    new float[]{a.centerY(), a.centerY()});
            assertEquals(crossingDirection | InputBits.A, fixture.view.buttons());

            dispatchPointers(fixture.root, downTime, downTime + 40,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (2 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{7, 42, 99},
                    new float[]{a.centerX(), a.centerX(), b.centerX()},
                    new float[]{a.centerY(), a.centerY(), b.centerY()});
            assertEquals(crossingDirection | InputBits.A | InputBits.B,
                    fixture.view.buttons());

            dispatchPointers(fixture.root, downTime, downTime + 60, MotionEvent.ACTION_MOVE,
                    new int[]{99, 7, 42},
                    new float[]{b.centerX(), base.centerX() - radius * 3f, a.centerX()},
                    new float[]{b.centerY(), base.centerY(), a.centerY()});
            assertEquals(InputBits.LEFT | InputBits.A | InputBits.B,
                    fixture.view.buttons());
            assertTrue(fixture.view.joystickVisualForTest().owned());
            assertTrue(fixture.root.lastInterceptionRequest());

            dispatchPointers(fixture.root, downTime, downTime + 80,
                    MotionEvent.ACTION_POINTER_UP
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{99, 7, 42},
                    new float[]{b.centerX(), base.centerX() - radius * 3f, a.centerX()},
                    new float[]{b.centerY(), base.centerY(), a.centerY()});
            assertEquals(InputBits.A | InputBits.B, fixture.view.buttons());
            assertFalse(fixture.view.joystickVisualForTest().owned());
            assertTrue(fixture.view.joystickReturnActiveForTest());
            assertTrue(fixture.root.lastInterceptionRequest());

            dispatchPointers(fixture.root, downTime, downTime + 100,
                    MotionEvent.ACTION_POINTER_UP
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{99, 42}, new float[]{b.centerX(), a.centerX()},
                    new float[]{b.centerY(), a.centerY()});
            assertEquals(InputBits.B, fixture.view.buttons());
            dispatchPointers(fixture.root, downTime, downTime + 120, MotionEvent.ACTION_UP,
                    new int[]{99}, new float[]{b.centerX()}, new float[]{b.centerY()});
            assertEquals(0, fixture.view.buttons());
            assertFalse(fixture.root.lastInterceptionRequest());
        });
    }

    @Test public void secondDirectionPointerCannotStealThroughRootDispatch() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatchPointers(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    new int[]{11}, new float[]{base.centerX()}, new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 10,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{11, 22},
                    new float[]{base.centerX(), base.centerX() + radius * .5f},
                    new float[]{base.centerY(), base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 20, MotionEvent.ACTION_MOVE,
                    new int[]{22, 11},
                    new float[]{base.centerX() - radius * 4f,
                            base.centerX() + radius * 4f},
                    new float[]{base.centerY(), base.centerY()});

            assertEquals(InputBits.RIGHT, fixture.view.buttons() & DIRECTIONS);
            assertTrue(fixture.view.joystickVisualForTest().owned());
            dispatchPointers(fixture.root, downTime, downTime + 30, MotionEvent.ACTION_CANCEL,
                    new int[]{22, 11},
                    new float[]{base.centerX() - radius * 4f,
                            base.centerX() + radius * 4f},
                    new float[]{base.centerY(), base.centerY()});
            assertEquals(0, fixture.view.buttons());
        });
    }

    @Test public void dpadRootDispatchOrbitsThreeRadiiAndReturnsWithoutDroppingOwner() {
        onMain(() -> {
            Fixture fixture = fixture(DirectionControlMode.DPAD, HapticLevel.LIGHT);
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float distance = fixture.view.hitMapForTest().joystickRadius() * 3f;
            int[] expected = {
                    InputBits.RIGHT,
                    InputBits.RIGHT | InputBits.DOWN,
                    InputBits.DOWN,
                    InputBits.LEFT | InputBits.DOWN,
                    InputBits.LEFT,
                    InputBits.LEFT | InputBits.UP,
                    InputBits.UP,
                    InputBits.RIGHT | InputBits.UP
            };
            List<Integer> publications = new ArrayList<>();
            fixture.view.setListener(publications::add);
            long downTime = SystemClock.uptimeMillis();
            dispatch(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            assertTrue(fixture.view.joystickVisualForTest().owned());
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);

            for (int sector = 0; sector < expected.length; sector++) {
                double radians = Math.toRadians(sector * 45d);
                dispatch(fixture.root, downTime, downTime + 10L + sector * 10L,
                        MotionEvent.ACTION_MOVE,
                        base.centerX() + (float) Math.cos(radians) * distance,
                        base.centerY() + (float) Math.sin(radians) * distance);
                assertEquals(expected[sector], fixture.view.buttons() & DIRECTIONS);
                assertTrue(fixture.view.joystickVisualForTest().owned());
            }
            dispatch(fixture.root, downTime, downTime + 100, MotionEvent.ACTION_MOVE,
                    base.centerX(), base.centerY());
            assertEquals(InputBits.RIGHT | InputBits.UP,
                    fixture.view.buttons() & DIRECTIONS);
            dispatch(fixture.root, downTime, downTime + 110, MotionEvent.ACTION_MOVE,
                    base.centerX() - distance, base.centerY() + distance);
            assertEquals(InputBits.LEFT | InputBits.DOWN,
                    fixture.view.buttons() & DIRECTIONS);
            assertFalse("D-pad emitted neutral after its first activation",
                    publications.contains(0));

            dispatch(fixture.root, downTime, downTime + 130, MotionEvent.ACTION_UP,
                    base.centerX() - distance, base.centerY() + distance);
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);
            assertFalse(fixture.view.joystickVisualForTest().owned());
        });
    }

    @Test public void directionHapticsTickOnActivationAndSectorChangesWithEightyMsCooldown() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatch(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    base.centerX() + radius, base.centerY());
            assertEquals(1, fixture.view.directionHapticCount);
            dispatch(fixture.root, downTime, downTime + 89, MotionEvent.ACTION_MOVE,
                    base.centerX(), base.centerY() + radius);
            assertEquals(1, fixture.view.directionHapticCount);
            dispatch(fixture.root, downTime, downTime + 90, MotionEvent.ACTION_MOVE,
                    base.centerX() - radius, base.centerY());
            assertEquals(2, fixture.view.directionHapticCount);
            dispatch(fixture.root, downTime, downTime + 100, MotionEvent.ACTION_MOVE,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, downTime, downTime + 110, MotionEvent.ACTION_UP,
                    base.centerX(), base.centerY());
            assertEquals(2, fixture.view.directionHapticCount);

            fixture.view.setControlSettings(AppSettings.defaults().toBuilder()
                    .hapticLevel(HapticLevel.OFF).build());
            long nextDown = downTime + 200;
            dispatch(fixture.root, nextDown, nextDown, MotionEvent.ACTION_DOWN,
                    base.centerX(), base.centerY());
            dispatch(fixture.root, nextDown, nextDown + 10, MotionEvent.ACTION_MOVE,
                    base.centerX(), base.centerY() - radius);
            assertEquals(2, fixture.view.directionHapticCount);
        });
    }

    @SdkSuppress(minSdkVersion = 30)
    @Test public void insetsAreIdempotentModeSwitchCancelsDirectionAndResizeEndsSession() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            GamepadHitMap.Target a = fixture.view.hitMapForTest()
                    .target(GamepadHitMap.Control.A);
            float radius = fixture.view.hitMapForTest().joystickRadius();
            List<Integer> publications = new ArrayList<>();
            fixture.view.setListener(publications::add);
            long downTime = SystemClock.uptimeMillis();
            dispatchPointers(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    new int[]{3}, new float[]{base.centerX()}, new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    new int[]{3}, new float[]{base.centerX() + radius},
                    new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 20,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{3, 8}, new float[]{base.centerX() + radius, a.centerX()},
                    new float[]{base.centerY(), a.centerY()});
            assertEquals(InputBits.RIGHT | InputBits.A, fixture.view.buttons());
            publications.clear();

            WindowInsets changed = new WindowInsets.Builder()
                    .setSystemWindowInsets(Insets.of(40, 20, 80, 30))
                    .setInsets(WindowInsets.Type.mandatorySystemGestures(),
                            Insets.of(0, 0, 0, 96))
                    .build();
            fixture.view.onApplyWindowInsets(changed);
            GamepadInputState.JoystickVisual once = fixture.view.joystickVisualForTest();
            fixture.view.onApplyWindowInsets(changed);
            GamepadInputState.JoystickVisual twice = fixture.view.joystickVisualForTest();
            assertEquals(InputBits.RIGHT | InputBits.A, fixture.view.buttons());
            assertTrue(twice.owned());
            assertEquals(once.centerX(), twice.centerX(), 0f);
            assertEquals(once.centerY(), twice.centerY(), 0f);
            assertEquals(once.knobX(), twice.knobX(), 0f);
            assertEquals(once.knobY(), twice.knobY(), 0f);
            assertTrue(publications.isEmpty());
            Rect exclusion = fixture.view.getSystemGestureExclusionRects().get(0);
            assertTrue(exclusion.bottom <= HEIGHT - 96);
            assertTrue(exclusion.height() <= Math.round(200f
                    * fixture.view.getResources().getDisplayMetrics().density));

            fixture.view.setControlSettings(AppSettings.defaults().toBuilder()
                    .directionControlMode(DirectionControlMode.DPAD).build());
            assertEquals(InputBits.A, fixture.view.buttons());
            assertFalse(fixture.view.joystickVisualForTest().owned());
            assertTrue(fixture.root.lastInterceptionRequest());
            dispatchPointers(fixture.root, downTime, downTime + 30, MotionEvent.ACTION_CANCEL,
                    new int[]{3, 8}, new float[]{base.centerX() + radius, a.centerX()},
                    new float[]{base.centerY(), a.centerY()});
            assertEquals(0, fixture.view.buttons());
            assertFalse(fixture.root.lastInterceptionRequest());

            GamepadHitMap.Bounds dpad = fixture.view.hitMapForTest().dpadBounds();
            long nextDown = downTime + 100;
            dispatch(fixture.root, nextDown, nextDown, MotionEvent.ACTION_DOWN,
                    dpad.centerX(), dpad.centerY());
            dispatch(fixture.root, nextDown, nextDown + 10, MotionEvent.ACTION_MOVE,
                    dpad.centerX() + dpad.width(), dpad.centerY());
            assertEquals(InputBits.RIGHT, fixture.view.buttons() & DIRECTIONS);
            measureAndLayout(fixture.root, 1800, 900);
            assertEquals(0, fixture.view.buttons() & DIRECTIONS);
            assertFalse(fixture.view.joystickVisualForTest().owned());
            assertFalse(fixture.root.lastInterceptionRequest());
        });
    }

    @Test public void cancelClearsMinimumTapPulseImmediatelyButPreservesKeyboardSource() {
        onMain(() -> {
            Fixture fixture = fixture();
            InputRouter router = new InputRouter(mask -> { }, action -> { });
            fixture.view.setInputRouter(router);
            fixture.view.onKeyDown(KeyEvent.KEYCODE_DPAD_UP,
                    new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_UP));
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            GamepadHitMap.Target a = fixture.view.hitMapForTest()
                    .target(GamepadHitMap.Control.A);
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatchPointers(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    new int[]{1}, new float[]{base.centerX()}, new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 1, MotionEvent.ACTION_MOVE,
                    new int[]{1}, new float[]{base.centerX() + radius},
                    new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 2,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{1, 2}, new float[]{base.centerX() + radius, a.centerX()},
                    new float[]{base.centerY(), a.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 2,
                    MotionEvent.ACTION_POINTER_UP
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{1, 2}, new float[]{base.centerX() + radius, a.centerX()},
                    new float[]{base.centerY(), a.centerY()});
            assertEquals(InputBits.RIGHT | InputBits.A, fixture.view.buttons());

            dispatchPointers(fixture.root, downTime, downTime + 3, MotionEvent.ACTION_CANCEL,
                    new int[]{1}, new float[]{base.centerX() + radius},
                    new float[]{base.centerY()});

            assertEquals(0, fixture.view.buttons());
            assertEquals(InputBits.UP, router.currentMask());
        });
    }

    @Test public void releasingButtonDoesNotConsumeSurvivingDirectionSectorFeedback() {
        onMain(() -> {
            Fixture fixture = fixture();
            GamepadHitMap.Bounds base = fixture.view.hitMapForTest().dpadBounds();
            GamepadHitMap.Target a = fixture.view.hitMapForTest()
                    .target(GamepadHitMap.Control.A);
            float radius = fixture.view.hitMapForTest().joystickRadius();
            long downTime = SystemClock.uptimeMillis();
            dispatchPointers(fixture.root, downTime, downTime, MotionEvent.ACTION_DOWN,
                    new int[]{5}, new float[]{base.centerX()}, new float[]{base.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 10, MotionEvent.ACTION_MOVE,
                    new int[]{5}, new float[]{base.centerX() + radius},
                    new float[]{base.centerY()});
            assertEquals(1, fixture.view.directionHapticCount);
            dispatchPointers(fixture.root, downTime, downTime + 100,
                    MotionEvent.ACTION_POINTER_DOWN
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{5, 6}, new float[]{base.centerX() + radius, a.centerX()},
                    new float[]{base.centerY(), a.centerY()});
            dispatchPointers(fixture.root, downTime, downTime + 200,
                    MotionEvent.ACTION_POINTER_UP
                            | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                    new int[]{5, 6}, new float[]{base.centerX(), a.centerX()},
                    new float[]{base.centerY() + radius, a.centerY()});

            assertEquals(InputBits.DOWN, fixture.view.buttons() & DIRECTIONS);
            assertEquals(2, fixture.view.directionHapticCount);
        });
    }

    @Test public void dpadHighlightsAccessibilityAndKeyboardDirectionFeedback() {
        onMain(() -> {
            Fixture fixture = fixture(DirectionControlMode.DPAD, HapticLevel.OFF);

            assertTrue(fixture.view.performVirtualControlClickForTest(0));
            assertTrue((fixture.view.dpadHighlightBitsForTest() & InputBits.UP) != 0);
            fixture.view.onKeyDown(KeyEvent.KEYCODE_DPAD_RIGHT,
                    new KeyEvent(KeyEvent.ACTION_DOWN, KeyEvent.KEYCODE_DPAD_RIGHT));
            assertTrue((fixture.view.dpadHighlightBitsForTest() & InputBits.RIGHT) != 0);
            fixture.view.onKeyUp(KeyEvent.KEYCODE_DPAD_RIGHT,
                    new KeyEvent(KeyEvent.ACTION_UP, KeyEvent.KEYCODE_DPAD_RIGHT));
            assertEquals(0, fixture.view.dpadHighlightBitsForTest() & InputBits.RIGHT);
        });
    }

    private static Fixture fixture() {
        return fixture(DirectionControlMode.FIXED_JOYSTICK, HapticLevel.LIGHT);
    }

    private static Fixture fixture(DirectionControlMode mode, HapticLevel hapticLevel) {
        Context context = ApplicationProvider.getApplicationContext();
        RecordingRoot root = new RecordingRoot(context);
        root.addView(new View(context), new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        RecordingGamepadView view = new RecordingGamepadView(context);
        view.setControlSettings(AppSettings.defaults().toBuilder()
                .directionControlMode(mode).hapticLevel(hapticLevel).build());
        root.addView(view, new FrameLayout.LayoutParams(
                FrameLayout.LayoutParams.MATCH_PARENT, FrameLayout.LayoutParams.MATCH_PARENT));
        measureAndLayout(root, WIDTH, HEIGHT);
        return new Fixture(root, view);
    }

    private static void dispatch(View root, long downTime, long eventTime,
                                 int action, float x, float y) {
        MotionEvent event = MotionEvent.obtain(downTime, eventTime, action, x, y, 0);
        root.dispatchTouchEvent(event);
        event.recycle();
    }

    private static void dispatchPointers(View root, long downTime, long eventTime,
                                         int action, int[] ids, float[] xs, float[] ys) {
        MotionEvent.PointerProperties[] properties =
                new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] coordinates = new MotionEvent.PointerCoords[ids.length];
        for (int index = 0; index < ids.length; index++) {
            MotionEvent.PointerProperties pointer = new MotionEvent.PointerProperties();
            pointer.id = ids[index];
            pointer.toolType = MotionEvent.TOOL_TYPE_FINGER;
            properties[index] = pointer;
            MotionEvent.PointerCoords coordinate = new MotionEvent.PointerCoords();
            coordinate.x = xs[index];
            coordinate.y = ys[index];
            coordinate.pressure = 1f;
            coordinate.size = 1f;
            coordinates[index] = coordinate;
        }
        MotionEvent event = MotionEvent.obtain(downTime, eventTime, action, ids.length,
                properties, coordinates, 0, 0, 1f, 1f,
                0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        root.dispatchTouchEvent(event);
        event.recycle();
    }

    private static void measureAndLayout(View view, int width, int height) {
        view.measure(View.MeasureSpec.makeMeasureSpec(width, View.MeasureSpec.EXACTLY),
                View.MeasureSpec.makeMeasureSpec(height, View.MeasureSpec.EXACTLY));
        view.layout(0, 0, width, height);
    }

    private static MotionEvent flaggedSinglePointerEvent(long downTime, long eventTime,
                                                          int action, float x, float y) {
        MotionEvent.PointerProperties properties = new MotionEvent.PointerProperties();
        properties.id = 0;
        properties.toolType = MotionEvent.TOOL_TYPE_FINGER;
        MotionEvent.PointerCoords coordinates = new MotionEvent.PointerCoords();
        coordinates.x = x;
        coordinates.y = y;
        coordinates.pressure = 1f;
        coordinates.size = 1f;
        return MotionEvent.obtain(downTime, eventTime, action, 1,
                new MotionEvent.PointerProperties[]{properties},
                new MotionEvent.PointerCoords[]{coordinates}, 0, 0,
                1f, 1f, 0, 0, InputDevice.SOURCE_TOUCHSCREEN,
                MotionEvent.FLAG_CANCELED);
    }

    private static float knobDistance(GamepadInputState.JoystickVisual visual) {
        return (float) Math.hypot(visual.knobX() - visual.centerX(),
                visual.knobY() - visual.centerY());
    }

    private static void onMain(Runnable runnable) {
        InstrumentationRegistry.getInstrumentation().runOnMainSync(runnable);
    }

    private static final class Fixture {
        final RecordingRoot root;
        final RecordingGamepadView view;

        Fixture(RecordingRoot root, RecordingGamepadView view) {
            this.root = root;
            this.view = view;
        }
    }

    private static final class RecordingRoot extends FrameLayout {
        private final List<Boolean> interceptionRequests = new ArrayList<>();

        RecordingRoot(Context context) {
            super(context);
        }

        @Override public void requestDisallowInterceptTouchEvent(boolean disallowIntercept) {
            interceptionRequests.add(disallowIntercept);
            super.requestDisallowInterceptTouchEvent(disallowIntercept);
        }

        boolean lastInterceptionRequest() {
            assertFalse(interceptionRequests.isEmpty());
            return interceptionRequests.get(interceptionRequests.size() - 1);
        }
    }

    private static final class RecordingGamepadView extends GamepadView {
        int directionHapticCount;

        RecordingGamepadView(Context context) {
            super(context);
        }

        @Override public boolean performHapticFeedback(int feedbackConstant) {
            directionHapticCount++;
            return true;
        }
    }
}
