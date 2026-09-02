package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Rect;
import android.graphics.RectF;
import android.os.Build;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.ViewParent;
import android.view.WindowInsets;

import androidx.core.view.ViewCompat;
import androidx.core.view.accessibility.AccessibilityNodeInfoCompat;
import androidx.customview.widget.ExploreByTouchHelper;

import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.GamepadInputState;
import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.DirectionControlMode;
import com.flynes.emu.input.HapticController;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.InputBits;
import com.flynes.emu.input.InputRouter;
import com.flynes.emu.input.MinimumTap;
import com.flynes.emu.input.ControlVisualGeometry;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.ControlLayoutRepository;

import java.util.Collections;
import java.util.List;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Classic NES controls. Every pointer is independently owned and always cancellable. */
public class GamepadView extends View {
    public static final int A = InputBits.A, B = InputBits.B, SELECT = InputBits.SELECT,
            START = InputBits.START, UP = InputBits.UP, DOWN = InputBits.DOWN,
            LEFT = InputBits.LEFT, RIGHT = InputBits.RIGHT;

    public interface Listener {
        void onButtons(int buttons);
        default void onPauseMenu() { }
    }

    private static final int MIN_FRAME_MS = 17;
    private static final int COLOR_IDLE = 0xFF25282F;
    private static final int COLOR_PRESSED = 0xFFF4EFE6;
    private static final int COLOR_OUTLINE = 0xFFBEB8AE;
    private static final int COLOR_CORAL = 0xFFFF6B5E;
    private static final int DIRECTION_BITS = InputBits.UP | InputBits.DOWN
            | InputBits.LEFT | InputBits.RIGHT;
    private static final int TAP_PULSE_BITS = InputBits.A | InputBits.B
            | InputBits.SELECT | InputBits.START;
    private static final float GESTURE_EXCLUSION_PADDING_DP = 16f;
    private static final float MAX_GESTURE_EXCLUSION_HEIGHT_DP = 200f;
    private static final ScheduledExecutorService TAP_RELEASES =
            Executors.newSingleThreadScheduledExecutor(runnable -> {
                Thread thread = new Thread(runnable, "flynes-tap-release");
                thread.setDaemon(true);
                return thread;
            });

    private final float density;
    private final Object inputStateLock = new Object();
    private final int[] pulseVersions = new int[8];
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final HapticController haptics;
    private final DirectionFeedbackGate directionFeedback = new DirectionFeedbackGate(0L);
    private final JoystickReturnAnimation joystickReturn = new JoystickReturnAnimation();
    private final GamepadAccessibilityHelper accessibility;
    private AppSettings controlSettings = AppSettings.defaults();
    private ControlLayoutV2 controlLayout;
    private GamepadHitMap hitMap;
    private GamepadInputState touchState;
    private InputRouter inputRouter;
    private Listener listener;
    private volatile int buttons;
    private int pulseBits;
    private int touchBits;
    private int keyboardBits;
    private volatile long lastInputEventElapsedNs;
    private int insetLeft, insetTop, insetRight, insetBottom;
    private int mandatoryGestureInsetBottom;
    private boolean parentInterceptionDisallowed;

    public GamepadView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        haptics = new HapticController(this);
        controlLayout = new ControlLayoutRepository(context).load();
        accessibility = new GamepadAccessibilityHelper(this);
        ViewCompat.setAccessibilityDelegate(this, accessibility);
        setWillNotDraw(false);
        setFocusable(true);
        setClickable(true);
        setContentDescription(context.getString(R.string.virtual_game_controls));
    }

    public void setListener(Listener listener) { this.listener = listener; }
    public void setInputRouter(InputRouter inputRouter) {
        synchronized (inputStateLock) {
            this.inputRouter = inputRouter;
            publishButtonsLocked(System.nanoTime());
        }
    }
    public void setHapticPreferences(HapticLevel level, boolean distinguishAB) {
        haptics.configure(level, distinguishAB);
    }
    public void setControlSettings(AppSettings settings) {
        AppSettings replacement = settings == null ? AppSettings.defaults() : settings;
        boolean modeChanged = controlSettings.directionControlMode()
                != replacement.directionControlMode();
        controlSettings = replacement;
        controlLayout = new ControlLayoutRepository(getContext()).load();
        haptics.configure(controlSettings.hapticLevel(), controlSettings.distinctABHaptics());
        if (modeChanged) joystickReturn.cancel();
        rebuildHitMap(true);
        if (modeChanged && touchState != null) {
            directionFeedback.reset(touchState.directionFeedbackRevision());
        }
    }
    public int buttons() { return buttons; }
    public GamepadHitMap hitMapForTest() { return hitMap; }
    GamepadInputState.JoystickVisual joystickVisualForTest() {
        return touchState == null ? null : touchState.joystickVisual();
    }
    boolean joystickReturnActiveForTest() { return joystickReturn.active(); }
    int virtualControlCountForTest() { return 8; }
    CharSequence virtualControlNameForTest(int id) { return accessibility.controlName(accessibility.order[id]); }
    boolean performVirtualControlClickForTest(int id) {
        return accessibility.onPerformActionForVirtualView(id, AccessibilityNodeInfoCompat.ACTION_CLICK, null);
    }

    public void reset() {
        joystickReturn.cancel();
        if (touchState != null) touchState.cancelAll();
        directionFeedback.reset(touchState == null
                ? 0L : touchState.directionFeedbackRevision());
        releaseParentInterception();
        synchronized (inputStateLock) {
            for (int i = 0; i < pulseVersions.length; i++) pulseVersions[i]++;
            pulseBits = 0;
            touchBits = 0;
            keyboardBits = 0;
            if (buttons != 0) {
                buttons = 0;
                publishButtonsLocked(System.nanoTime());
            } else if (inputRouter != null) {
                inputRouter.cancel(InputRouter.Source.TOUCH);
            }
            if (inputRouter != null) inputRouter.cancel(InputRouter.Source.KEYBOARD);
        }
        updateSystemGestureExclusion();
        invalidate();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        if (touchState != null) cancelTouchSession(true);
        rebuildHitMap(false);
    }

    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            mandatoryGestureInsetBottom = insets.getInsets(
                    WindowInsets.Type.mandatorySystemGestures()).bottom;
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
            mandatoryGestureInsetBottom = insets.getMandatorySystemGestureInsets().bottom;
        } else {
            mandatoryGestureInsetBottom = 0;
        }
        rebuildHitMap(true);
        return insets;
    }

    private void rebuildHitMap(boolean preserveTouch) {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        GamepadInputState.JoystickVisual previousVisual = touchState == null
                ? null : touchState.joystickVisual();
        GamepadHitMap replacement = GamepadHitMap.fromLayout(getWidth(), getHeight(), density,
                insetLeft, insetRight, insetTop, insetBottom, controlLayout,
                controlSettings.directionControlMode(), controlSettings.deadZone());
        boolean created = !preserveTouch || touchState == null;
        if (preserveTouch && touchState != null) {
            touchState.reconfigure(replacement);
        } else {
            touchState = new GamepadInputState(replacement);
        }
        hitMap = replacement;
        GamepadInputState.JoystickVisual currentVisual = touchState.joystickVisual();
        if (joystickReturn.active() && previousVisual != null
                && (previousVisual.centerX() != currentVisual.centerX()
                || previousVisual.centerY() != currentVisual.centerY())) {
            joystickReturn.cancel();
        }
        if (created) directionFeedback.reset(touchState.directionFeedbackRevision());
        accessibility.invalidateRoot();
        recompute();
        updateSystemGestureExclusion();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (hitMap == null) return;
        if (hitMap.joystickMode()) {
            drawJoystick(canvas);
        } else {
            drawDpad(canvas);
        }
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.B), "B");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.A), "A");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.SELECT), "SELECT");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.START), "START");
    }

    private void drawDpad(Canvas canvas) {
        GamepadInputState.JoystickVisual visual = touchState.joystickVisual();
        GamepadHitMap.Bounds d = hitMap.dpadBounds();
        float arm = ControlVisualGeometry.dpadArmPx(density,
                controlLayout.placement(ControlLayoutV2.Element.D_PAD).scale());
        RectF vertical = new RectF(d.centerX() - arm / 2f, d.top,
                d.centerX() + arm / 2f, d.bottom);
        RectF horizontal = new RectF(d.left, d.centerY() - arm / 2f,
                d.right, d.centerY() + arm / 2f);
        configurePaint(false, false);
        if (visual.owned()) {
            stroke.setColor(withAlpha(COLOR_CORAL, pressedAlpha()));
            stroke.setStrokeWidth(3f * density);
        }
        canvas.drawRoundRect(vertical, 8f * density, 8f * density, fill);
        canvas.drawRoundRect(horizontal, 8f * density, 8f * density, fill);
        canvas.drawRoundRect(vertical, 8f * density, 8f * density, stroke);
        canvas.drawRoundRect(horizontal, 8f * density, 8f * density, stroke);
        int directionBits = visual.directionBits();
        drawDirectionHighlight(canvas, GamepadHitMap.Control.UP, InputBits.UP, directionBits);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.DOWN, InputBits.DOWN, directionBits);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.LEFT, InputBits.LEFT, directionBits);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.RIGHT, InputBits.RIGHT, directionBits);
        fill.setColor(withAlpha(0xFF121316, idleAlpha()));
        canvas.drawCircle(d.centerX(), d.centerY(), 8f * density, fill);
    }

    private void drawJoystick(Canvas canvas) {
        GamepadInputState.JoystickVisual visual = touchState.joystickVisual();
        float radius = hitMap.joystickRadius();
        configurePaint(false, false);
        if (visual.owned()) {
            stroke.setColor(withAlpha(COLOR_CORAL, pressedAlpha()));
            stroke.setStrokeWidth(3f * density);
        }
        canvas.drawCircle(visual.centerX(), visual.centerY(), radius, fill);
        canvas.drawCircle(visual.centerX(), visual.centerY(), radius, stroke);
        if (visual.saturated()) {
            stroke.setStyle(Paint.Style.STROKE);
            stroke.setStrokeWidth(4f * density);
            stroke.setColor(withAlpha(COLOR_CORAL, Math.min(220, pressedAlpha())));
            canvas.drawCircle(visual.centerX(), visual.centerY(),
                    radius + 4f * density, stroke);
        }
        boolean activated = visual.activated();
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(withAlpha(activated ? COLOR_PRESSED : COLOR_IDLE,
                activated ? pressedAlpha() : idleAlpha()));
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth((visual.owned() ? 3f : 2f) * density);
        stroke.setColor(withAlpha(visual.owned() ? COLOR_CORAL : COLOR_OUTLINE,
                visual.owned() ? pressedAlpha() : idleAlpha()));
        float knobX = visual.knobX();
        float knobY = visual.knobY();
        if (hitMap.fixedJoystickMode() && !visual.owned() && joystickReturn.active()) {
            JoystickReturnAnimation.Sample sample = joystickReturn.sample(SystemClock.uptimeMillis());
            knobX = sample.x();
            knobY = sample.y();
            if (joystickReturn.active()) postInvalidateOnAnimation();
        }
        float knobRadius = radius * .4375f;
        canvas.drawCircle(knobX, knobY, knobRadius, fill);
        canvas.drawCircle(knobX, knobY, knobRadius, stroke);
    }

    private void drawDirectionHighlight(Canvas canvas, GamepadHitMap.Control control,
                                        int bit, int directionBits) {
        if ((directionBits & bit) == 0) return;
        GamepadHitMap.Bounds b = hitMap.target(control).bounds();
        RectF bounds = new RectF(b.left, b.top, b.right, b.bottom);
        fill.setColor(withAlpha(COLOR_PRESSED, pressedAlpha()));
        canvas.drawRoundRect(bounds, 8f * density, 8f * density, fill);
    }

    private void drawTarget(Canvas canvas, GamepadHitMap.Target target, String text) {
        int bit = bitFor(target.control());
        boolean pressed = (buttons & bit) != 0;
        boolean accent = target.control() == GamepadHitMap.Control.A;
        configurePaint(pressed, accent);
        GamepadHitMap.Bounds b = target.bounds();
        RectF bounds = new RectF(b.left, b.top, b.right, b.bottom);
        if (target.shape() == GamepadHitMap.Shape.PILL) {
            float visualH = 28f * density;
            bounds = new RectF(bounds.left, bounds.centerY() - visualH / 2f,
                    bounds.right, bounds.centerY() + visualH / 2f);
        } else if (pressed) {
            bounds.inset(2f * density, 2f * density);
        }
        if (target.shape() == GamepadHitMap.Shape.CIRCLE) {
            canvas.drawCircle(bounds.centerX(), bounds.centerY(), bounds.width() / 2f, fill);
            canvas.drawCircle(bounds.centerX(), bounds.centerY(), bounds.width() / 2f, stroke);
        } else {
            float radius = target.shape() == GamepadHitMap.Shape.PILL
                    ? bounds.height() / 2f : 12f * density;
            canvas.drawRoundRect(bounds, radius, radius, fill);
            canvas.drawRoundRect(bounds, radius, radius, stroke);
        }
        label.setColor(withAlpha(pressed ? 0xFF121316 : Color.WHITE,
                Math.round(controlLayout.opacity() * 255f)));
        label.setTextSize((target.shape() == GamepadHitMap.Shape.PILL ? 11f : 22f) * density);
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        label.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics metrics = label.getFontMetrics();
        canvas.drawText(text, bounds.centerX(), bounds.centerY() - (metrics.ascent + metrics.descent) / 2f, label);
    }

    private void configurePaint(boolean pressed, boolean accent) {
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(withAlpha(pressed ? COLOR_PRESSED : COLOR_IDLE,
                pressed ? pressedAlpha() : idleAlpha()));
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth((accent ? 3f : 2f) * density);
        stroke.setColor(withAlpha(accent ? COLOR_CORAL : COLOR_OUTLINE,
                Math.round(controlLayout.opacity() * 255f)));
    }
    private int idleAlpha() { return Math.round(controlLayout.opacity() * 255f); }
    private int pressedAlpha() { return Math.round(Math.max(.88f, controlLayout.opacity()) * 255f); }
    private static int withAlpha(int color, int alpha) {
        return (color & 0x00FFFFFF) | (Math.max(0, Math.min(255, alpha)) << 24);
    }

    @Override public boolean onTouchEvent(MotionEvent event) {
        if (hitMap == null || touchState == null) return false;
        lastInputEventElapsedNs = event.getEventTime() * 1_000_000L;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            cancelTouchSession(true);
            return true;
        }
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        boolean buttonFeedbackRequested = false;
        boolean directionTerminated = false;
        if (action == MotionEvent.ACTION_DOWN) cancelTouchSession(false);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            GamepadHitMap.Control button = hitMap.buttonHit(
                    event.getX(index), event.getY(index));
            if (action == MotionEvent.ACTION_POINTER_DOWN) {
                updateCurrentPointerPositions(event, index);
            }
            boolean accepted = touchState.down(
                    id, event.getX(index), event.getY(index), event.getEventTime());
            if (accepted) {
                disallowParentInterception();
                if (touchState.joystickVisual().owned()) joystickReturn.cancel();
            }
            if (accepted && button != GamepadHitMap.Control.NONE) {
                haptics.feedback(button);
                buttonFeedbackRequested = true;
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int history = 0; history < event.getHistorySize(); history++) {
                long eventTime = event.getHistoricalEventTime(history);
                for (int pointer = 0; pointer < event.getPointerCount(); pointer++) {
                    touchState.move(event.getPointerId(pointer),
                            event.getHistoricalX(pointer, history),
                            event.getHistoricalY(pointer, history), eventTime);
                }
            }
            for (int i = 0; i < event.getPointerCount(); i++) {
                touchState.move(event.getPointerId(i), event.getX(i), event.getY(i), event.getEventTime());
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            boolean canceledPointer = isCanceledPointerUp(event);
            if (canceledPointer) {
                updateCurrentPointerPositions(event, index);
                GamepadInputState.JoystickVisual beforeCancel = touchState.joystickVisual();
                touchState.cancel(id);
                directionTerminated = beforeCancel.owned()
                        && !touchState.joystickVisual().owned();
                joystickReturn.cancel();
            } else {
                updateCurrentPointerPositions(event, -1);
                GamepadInputState.JoystickVisual beforeRelease = touchState.joystickVisual();
                GamepadInputState.Release release = touchState.up(id, event.getEventTime());
                GamepadInputState.JoystickVisual afterRelease = touchState.joystickVisual();
                directionTerminated = beforeRelease.owned() && !afterRelease.owned();
                if (hitMap.fixedJoystickMode() && beforeRelease.owned()
                        && !afterRelease.owned()) {
                    joystickReturn.start(beforeRelease.centerX(), beforeRelease.centerY(),
                            beforeRelease.knobX(), beforeRelease.knobY(), event.getEventTime());
                }
                long remaining = MinimumTap.remainingMillis(
                        event.getEventTime() - release.heldMillis(),
                        event.getEventTime(), MIN_FRAME_MS);
                int pulseable = release.bits() & TAP_PULSE_BITS;
                if (remaining > 0L && pulseable != 0) pulse(pulseable, remaining);
            }
            if (action == MotionEvent.ACTION_UP) releaseParentInterception();
            if (action == MotionEvent.ACTION_UP && !canceledPointer) performClick();
        }
        long directionRevision = touchState.directionFeedbackRevision();
        if (directionTerminated || buttonFeedbackRequested) {
            directionFeedback.consume(directionRevision);
        } else if (directionFeedback.shouldEmit(directionRevision, event.getEventTime())) {
            haptics.feedbackDirectionTick();
        }
        recompute();
        updateSystemGestureExclusion();
        return true;
    }

    private void updateCurrentPointerPositions(MotionEvent event, int excludedIndex) {
        for (int pointer = 0; pointer < event.getPointerCount(); pointer++) {
            if (pointer == excludedIndex) continue;
            touchState.move(event.getPointerId(pointer), event.getX(pointer), event.getY(pointer),
                    event.getEventTime());
        }
    }

    private static boolean isCanceledPointerUp(MotionEvent event) {
        return Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU
                && (event.getFlags() & MotionEvent.FLAG_CANCELED) != 0;
    }

    private void cancelTouchSession(boolean publish) {
        joystickReturn.cancel();
        if (touchState != null) {
            touchState.cancelAll();
            directionFeedback.consume(touchState.directionFeedbackRevision());
        }
        synchronized (inputStateLock) {
            for (int index = 0; index < pulseVersions.length; index++) {
                pulseVersions[index]++;
            }
            pulseBits = 0;
        }
        releaseParentInterception();
        if (publish) recompute();
        updateSystemGestureExclusion();
        invalidate();
    }

    private void disallowParentInterception() {
        if (parentInterceptionDisallowed) return;
        ViewParent parent = getParent();
        if (parent == null) return;
        parent.requestDisallowInterceptTouchEvent(true);
        parentInterceptionDisallowed = true;
    }

    private void releaseParentInterception() {
        if (!parentInterceptionDisallowed) return;
        ViewParent parent = getParent();
        if (parent != null) parent.requestDisallowInterceptTouchEvent(false);
        parentInterceptionDisallowed = false;
    }

    private void updateSystemGestureExclusion() {
        if (Build.VERSION.SDK_INT < Build.VERSION_CODES.Q) return;
        if (hitMap == null || touchState == null
                || getWidth() <= 1 || getHeight() <= 0) {
            setSystemGestureExclusionRects(Collections.emptyList());
            return;
        }
        GamepadInputState.JoystickVisual visual = touchState.joystickVisual();
        float padding = GESTURE_EXCLUSION_PADDING_DP * density;
        float halfWidth = hitMap.joystickMode()
                ? hitMap.joystickRadius() + padding
                : hitMap.dpadBounds().width() / 2f + padding;
        float halfHeight = hitMap.joystickMode()
                ? hitMap.joystickRadius() + padding
                : hitMap.dpadBounds().height() / 2f + padding;
        int safeTop = clamp(insetTop, 0, getHeight());
        int safeBottom = clamp(getHeight()
                - Math.max(insetBottom, mandatoryGestureInsetBottom), safeTop, getHeight());
        int availableHeight = safeBottom - safeTop;
        int desiredHeight = Math.min(Math.round(MAX_GESTURE_EXCLUSION_HEIGHT_DP * density),
                (int) Math.ceil(halfHeight * 2f));
        int exclusionHeight = Math.min(availableHeight, desiredHeight);
        if (exclusionHeight <= 0) {
            setSystemGestureExclusionRects(Collections.emptyList());
            return;
        }
        int top = clamp(Math.round(visual.centerY()) - exclusionHeight / 2,
                safeTop, safeBottom - exclusionHeight);
        int bottom = top + exclusionHeight;
        Rect exclusion;
        if (visual.centerX() <= getWidth() / 2f) {
            int right = clamp((int) Math.ceil(visual.centerX() + halfWidth),
                    1, getWidth() - 1);
            exclusion = new Rect(0, top, right, bottom);
        } else {
            int left = clamp((int) Math.floor(visual.centerX() - halfWidth),
                    1, getWidth() - 1);
            exclusion = new Rect(left, top, getWidth(), bottom);
        }
        setSystemGestureExclusionRects(Collections.singletonList(exclusion));
    }

    private static int clamp(int value, int minimum, int maximum) {
        return Math.max(minimum, Math.min(maximum, value));
    }

    private void pulse(int bits, long millis) {
        synchronized (inputStateLock) {
            pulseBits |= bits;
            for (int remaining = bits; remaining != 0; remaining &= remaining - 1) {
                int bit = Integer.lowestOneBit(remaining);
                int index = Integer.numberOfTrailingZeros(bit);
                int version = ++pulseVersions[index];
                TAP_RELEASES.schedule(() -> expirePulse(bit, index, version),
                        millis, TimeUnit.MILLISECONDS);
            }
        }
    }

    private void expirePulse(int bit, int index, int version) {
        synchronized (inputStateLock) {
            if (pulseVersions[index] != version) return;
            pulseBits &= ~bit;
            recomputeLocked(System.nanoTime());
        }
        postInvalidate();
    }

    @Override public boolean onKeyDown(int keyCode, KeyEvent event) {
        int bit = keyBit(keyCode);
        if (bit == 0) return super.onKeyDown(keyCode, event);
        if (event.getRepeatCount() == 0) {
            keyboardBits |= bit;
            publishKeyboard();
        }
        return true;
    }
    @Override public boolean onKeyUp(int keyCode, KeyEvent event) {
        int bit = keyBit(keyCode);
        if (bit == 0) return super.onKeyUp(keyCode, event);
        keyboardBits &= ~bit;
        publishKeyboard();
        return true;
    }
    private void publishKeyboard() {
        if (inputRouter != null) inputRouter.setMask(InputRouter.Source.KEYBOARD, keyboardBits,
                System.nanoTime());
    }
    private static int keyBit(int key) {
        switch (key) {
            case KeyEvent.KEYCODE_DPAD_UP: return InputBits.UP;
            case KeyEvent.KEYCODE_DPAD_DOWN: return InputBits.DOWN;
            case KeyEvent.KEYCODE_DPAD_LEFT: return InputBits.LEFT;
            case KeyEvent.KEYCODE_DPAD_RIGHT: return InputBits.RIGHT;
            case KeyEvent.KEYCODE_BUTTON_A: case KeyEvent.KEYCODE_Z: return InputBits.A;
            case KeyEvent.KEYCODE_BUTTON_B: case KeyEvent.KEYCODE_X: return InputBits.B;
            case KeyEvent.KEYCODE_BUTTON_SELECT: return InputBits.SELECT;
            case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_ENTER: return InputBits.START;
            default: return 0;
        }
    }

    @Override public boolean performClick() { super.performClick(); return true; }
    @Override public void onWindowFocusChanged(boolean focus) { super.onWindowFocusChanged(focus); if (!focus) reset(); }
    @Override protected void onDetachedFromWindow() { reset(); super.onDetachedFromWindow(); }

    private void recompute() {
        int currentTouchBits = touchState == null ? 0 : touchState.mask();
        synchronized (inputStateLock) {
            touchBits = currentTouchBits;
            recomputeLocked(lastInputEventElapsedNs > 0L
                    ? lastInputEventElapsedNs : System.nanoTime());
        }
        invalidate();
    }

    private void recomputeLocked(long stateChangedElapsedNs) {
        int next = pulseBits | touchBits;
        if (next != buttons) {
            buttons = next;
            publishButtonsLocked(stateChangedElapsedNs);
        }
    }

    private void publishButtonsLocked(long stateChangedElapsedNs) {
        if (inputRouter != null) inputRouter.setMask(InputRouter.Source.TOUCH, buttons,
                stateChangedElapsedNs);
        if (listener != null) listener.onButtons(buttons);
    }
    private static int bitFor(GamepadHitMap.Control control) {
        switch (control) {
            case UP: return InputBits.UP;
            case DOWN: return InputBits.DOWN;
            case LEFT: return InputBits.LEFT;
            case RIGHT: return InputBits.RIGHT;
            case B: return InputBits.B;
            case A: return InputBits.A;
            case SELECT: return InputBits.SELECT;
            case START: return InputBits.START;
            default: return 0;
        }
    }

    private final class GamepadAccessibilityHelper extends ExploreByTouchHelper {
        final GamepadHitMap.Control[] order = {GamepadHitMap.Control.UP,
                GamepadHitMap.Control.DOWN, GamepadHitMap.Control.LEFT, GamepadHitMap.Control.RIGHT,
                GamepadHitMap.Control.A, GamepadHitMap.Control.B,
                GamepadHitMap.Control.SELECT, GamepadHitMap.Control.START};
        GamepadAccessibilityHelper(View host) { super(host); }
        @Override protected int getVirtualViewAt(float x, float y) {
            if (hitMap == null) return INVALID_ID;
            GamepadHitMap.Control hit = hitMap.hit(x, y);
            for (int i=0;i<order.length;i++) if (order[i] == hit) return i;
            return INVALID_ID;
        }
        @Override protected void getVisibleVirtualViews(List<Integer> ids) {
            for (int i=0;i<order.length;i++) ids.add(i);
        }
        @Override protected void onPopulateNodeForVirtualView(int id, AccessibilityNodeInfoCompat node) {
            GamepadHitMap.Control control = order[id];
            node.setClassName("android.widget.Button");
            node.setContentDescription(controlName(control));
            node.setClickable(true);
            node.addAction(AccessibilityNodeInfoCompat.ACTION_CLICK);
            if (hitMap != null) {
                GamepadHitMap.Bounds b=hitMap.target(control).bounds();
                node.setBoundsInParent(new Rect(Math.round(b.left),Math.round(b.top),Math.round(b.right),Math.round(b.bottom)));
            } else node.setBoundsInParent(new Rect(0,0,1,1));
        }
        @Override protected boolean onPerformActionForVirtualView(int id, int action, Bundle args) {
            if (action != AccessibilityNodeInfoCompat.ACTION_CLICK) return false;
            GamepadHitMap.Control control = order[id];
            int bit = bitFor(control);
            if ((bit & DIRECTION_BITS) != 0) {
                haptics.feedbackDirectionTick();
            } else {
                haptics.feedback(control);
            }
            pulse(bit, MIN_FRAME_MS);
            recompute();
            invalidateVirtualView(id);
            return true;
        }
        String controlName(GamepadHitMap.Control control) {
            switch(control) {
                case UP:return getContext().getString(R.string.control_up);
                case DOWN:return getContext().getString(R.string.control_down);
                case LEFT:return getContext().getString(R.string.control_left);
                case RIGHT:return getContext().getString(R.string.control_right);
                case A:return getContext().getString(R.string.control_a);
                case B:return getContext().getString(R.string.control_b);
                case SELECT:return getContext().getString(R.string.control_select);
                case START:return getContext().getString(R.string.control_start);
                default:return control.name();
            }
        }
    }
}
