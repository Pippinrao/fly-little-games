package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.HapticController;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.InputBits;
import com.flynes.emu.input.InputRouter;
import com.flynes.emu.input.MinimumTap;

/** Virtual NES controls with non-overlapping hit regions and source-aware input. */
public class GamepadView extends View {
    public static final int A = InputBits.A;
    public static final int B = InputBits.B;
    public static final int SELECT = InputBits.SELECT;
    public static final int START = InputBits.START;
    public static final int UP = InputBits.UP;
    public static final int DOWN = InputBits.DOWN;
    public static final int LEFT = InputBits.LEFT;
    public static final int RIGHT = InputBits.RIGHT;

    /** Transitional compatibility listener; app actions no longer originate here. */
    public interface Listener {
        void onButtons(int buttons);
        default void onPauseMenu() { }
    }

    private static final int PULSE_MS = 50;
    private static final int COLOR_IDLE = 0x70323A4A;
    private static final int COLOR_PRESSED = 0xE0F2F5FA;
    private static final int COLOR_OUTLINE = 0xD0AAB6CB;
    private static final int COLOR_ACCENT = 0xFFF0B45A;

    private final float density;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final SparseArray<Pointer> pointers = new SparseArray<>(5);
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final HapticController haptics;

    private GamepadHitMap hitMap;
    private InputRouter inputRouter;
    private Listener listener;
    private int buttons;
    private int pulseBits;
    private float joyKnobX;
    private float joyKnobY;
    private int insetLeft;
    private int insetTop;
    private int insetRight;
    private int insetBottom;

    private static final class Pointer {
        final GamepadHitMap.Control control;
        final long downTimeMillis;
        float x;
        float y;

        Pointer(GamepadHitMap.Control control, float x, float y, long downTimeMillis) {
            this.control = control;
            this.x = x;
            this.y = y;
            this.downTimeMillis = downTimeMillis;
        }
    }

    public GamepadView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        haptics = new HapticController(this);
        setWillNotDraw(false);
        setFocusable(true);
        setClickable(true);
    }

    public void setListener(Listener listener) {
        this.listener = listener;
    }

    public void setInputRouter(InputRouter inputRouter) {
        this.inputRouter = inputRouter;
        publishButtons();
    }

    public void setHapticPreferences(HapticLevel level, boolean distinguishAB) {
        haptics.configure(level, distinguishAB);
    }

    public int buttons() {
        return buttons;
    }

    /** Production lifecycle operation used on pause, focus loss, and detach. */
    public void reset() {
        handler.removeCallbacksAndMessages(null);
        pointers.clear();
        pulseBits = 0;
        joyKnobX = 0;
        joyKnobY = 0;
        if (buttons != 0) {
            buttons = 0;
            publishButtons();
        } else if (inputRouter != null) {
            inputRouter.cancel(InputRouter.Source.TOUCH);
        }
        invalidate();
    }

    @Override
    protected void onSizeChanged(int width, int height, int oldWidth, int oldHeight) {
        super.onSizeChanged(width, height, oldWidth, oldHeight);
        rebuildHitMap();
    }

    @Override
    public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        rebuildHitMap();
        return insets;
    }

    private void rebuildHitMap() {
        if (getWidth() > 0 && getHeight() > 0) {
            hitMap = GamepadHitMap.standard(getWidth(), getHeight(), density,
                    insetLeft, insetRight, insetTop, insetBottom);
            invalidate();
        }
    }

    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (hitMap == null) return;
        for (GamepadHitMap.Circle circle : hitMap.circles()) {
            boolean pressed = isHeld(circle.control());
            if (circle.control() == GamepadHitMap.Control.JOY) {
                drawCircle(canvas, circle.cx(), circle.cy(), circle.radius(), pressed, null, false);
                drawCircle(canvas, circle.cx() + joyKnobX, circle.cy() + joyKnobY,
                        28f * density, pressed, null, true);
            } else {
                drawCircle(canvas, circle.cx(), circle.cy(), circle.radius(), pressed,
                        labelFor(circle.control()), circle.control() == GamepadHitMap.Control.A);
            }
        }
    }

    private void drawCircle(Canvas canvas, float cx, float cy, float radius,
                            boolean pressed, String text, boolean accent) {
        float renderedRadius = radius * (pressed ? 0.94f : 1f);
        fill.setStyle(Paint.Style.FILL);
        fill.setColor(pressed ? COLOR_PRESSED : COLOR_IDLE);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth((accent ? 3f : 2f) * density);
        stroke.setColor(accent ? COLOR_ACCENT : COLOR_OUTLINE);
        canvas.drawCircle(cx, cy, renderedRadius, fill);
        canvas.drawCircle(cx, cy, renderedRadius, stroke);
        if (text == null) return;
        label.setColor(pressed ? 0xFF17202D : Color.WHITE);
        label.setTextSize(Math.min(radius * 0.72f, 20f * density));
        label.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        label.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics metrics = label.getFontMetrics();
        canvas.drawText(text, cx, cy - (metrics.ascent + metrics.descent) / 2f, label);
    }

    private String labelFor(GamepadHitMap.Control control) {
        switch (control) {
            case A: return "A";
            case B: return "B";
            case START: return "START";
            case SELECT: return "SELECT";
            default: return null;
        }
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        if (hitMap == null) return false;
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) {
            reset();
            return true;
        }

        int actionIndex = event.getActionIndex();
        int pointerId = event.getPointerId(actionIndex);
        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN:
                handleDown(pointerId, event.getX(actionIndex), event.getY(actionIndex),
                        event.getEventTime());
                break;
            case MotionEvent.ACTION_MOVE:
                for (int i = 0; i < event.getPointerCount(); i++) {
                    Pointer pointer = pointers.get(event.getPointerId(i));
                    if (pointer == null) continue;
                    pointer.x = event.getX(i);
                    pointer.y = event.getY(i);
                    if (pointer.control == GamepadHitMap.Control.JOY) updateJoystick(pointer);
                }
                recompute();
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
                Pointer removed = pointers.get(pointerId);
                pointers.remove(pointerId);
                if (removed != null && removed.control == GamepadHitMap.Control.JOY) {
                    joyKnobX = 0;
                    joyKnobY = 0;
                }
                if (removed != null && (removed.control == GamepadHitMap.Control.A
                        || removed.control == GamepadHitMap.Control.B)) {
                    int bit = removed.control == GamepadHitMap.Control.A
                            ? InputBits.A : InputBits.B;
                    long remaining = MinimumTap.remainingMillis(removed.downTimeMillis,
                            event.getEventTime(), PULSE_MS);
                    if (remaining > 0L) pulse(bit, remaining);
                }
                recompute();
                performClick();
                break;
            default:
                break;
        }
        return true;
    }

    private void handleDown(int pointerId, float x, float y, long eventTimeMillis) {
        GamepadHitMap.Control control = hitMap.hit(x, y);
        if (control != GamepadHitMap.Control.NONE && isHeld(control)) {
            control = GamepadHitMap.Control.NONE;
        }
        Pointer pointer = new Pointer(control, x, y, eventTimeMillis);
        pointers.put(pointerId, pointer);
        if (control == GamepadHitMap.Control.JOY) {
            updateJoystick(pointer);
        } else if (control == GamepadHitMap.Control.START) {
            haptics.feedback(control);
            pulse(InputBits.START);
        } else if (control == GamepadHitMap.Control.SELECT) {
            haptics.feedback(control);
            pulse(InputBits.SELECT);
        } else if (control == GamepadHitMap.Control.A || control == GamepadHitMap.Control.B) {
            haptics.feedback(control);
        }
        recompute();
    }

    @Override
    public boolean performClick() {
        super.performClick();
        return true;
    }

    @Override
    public void onWindowFocusChanged(boolean hasWindowFocus) {
        super.onWindowFocusChanged(hasWindowFocus);
        if (!hasWindowFocus) reset();
    }

    @Override
    protected void onDetachedFromWindow() {
        reset();
        super.onDetachedFromWindow();
    }

    private boolean isHeld(GamepadHitMap.Control control) {
        for (int i = 0; i < pointers.size(); i++) {
            if (pointers.valueAt(i).control == control) return true;
        }
        return false;
    }

    private void updateJoystick(Pointer pointer) {
        GamepadHitMap.Circle joy = hitMap.circle(GamepadHitMap.Control.JOY);
        float dx = pointer.x - joy.cx();
        float dy = pointer.y - joy.cy();
        float distance = (float) Math.hypot(dx, dy);
        float max = joy.radius() - 14f * density;
        if (distance > max && distance > 0f) {
            float scale = max / distance;
            dx *= scale;
            dy *= scale;
        }
        joyKnobX = dx;
        joyKnobY = dy;
    }

    private void pulse(int bit) {
        pulse(bit, PULSE_MS);
    }

    private void pulse(int bit, long durationMillis) {
        pulseBits |= bit;
        handler.postDelayed(() -> {
            pulseBits &= ~bit;
            recompute();
        }, durationMillis);
    }

    private void recompute() {
        int next = pulseBits;
        for (int i = 0; i < pointers.size(); i++) {
            GamepadHitMap.Control control = pointers.valueAt(i).control;
            if (control == GamepadHitMap.Control.A) next |= InputBits.A;
            if (control == GamepadHitMap.Control.B) next |= InputBits.B;
        }
        if (isHeld(GamepadHitMap.Control.JOY)) {
            next |= joystickBits(joyKnobX, joyKnobY,
                    hitMap.circle(GamepadHitMap.Control.JOY).radius());
        }
        if (next != buttons) {
            buttons = next;
            publishButtons();
        }
        invalidate();
    }

    static int joystickBits(float knobX, float knobY, float baseRadius) {
        float distance = (float) Math.hypot(knobX, knobY);
        if (distance < 0.15f * baseRadius) return 0;
        double degrees = Math.toDegrees(Math.atan2(knobY, knobX));
        if (degrees < -157.5 || degrees >= 157.5) return InputBits.LEFT;
        if (degrees < -112.5) return InputBits.UP | InputBits.LEFT;
        if (degrees < -67.5) return InputBits.UP;
        if (degrees < -22.5) return InputBits.UP | InputBits.RIGHT;
        if (degrees < 22.5) return InputBits.RIGHT;
        if (degrees < 67.5) return InputBits.DOWN | InputBits.RIGHT;
        if (degrees < 112.5) return InputBits.DOWN;
        if (degrees < 157.5) return InputBits.DOWN | InputBits.LEFT;
        return InputBits.LEFT;
    }

    private void publishButtons() {
        if (inputRouter != null) {
            inputRouter.setMask(InputRouter.Source.TOUCH, buttons);
        }
        if (listener != null) {
            listener.onButtons(buttons);
        }
    }
}
