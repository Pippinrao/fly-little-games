package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.View;
import android.view.WindowInsets;

import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.input.GamepadInputState;
import com.flynes.emu.input.ControlLayoutV2;
import com.flynes.emu.input.HapticController;
import com.flynes.emu.input.HapticLevel;
import com.flynes.emu.input.InputBits;
import com.flynes.emu.input.InputRouter;
import com.flynes.emu.input.MinimumTap;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.ControlLayoutRepository;

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

    private final float density;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final HapticController haptics;
    private AppSettings controlSettings = AppSettings.defaults();
    private ControlLayoutV2 controlLayout;
    private GamepadHitMap hitMap;
    private GamepadInputState touchState;
    private InputRouter inputRouter;
    private Listener listener;
    private int buttons;
    private int pulseBits;
    private int keyboardBits;
    private int insetLeft, insetTop, insetRight, insetBottom;

    public GamepadView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        haptics = new HapticController(this);
        controlLayout = new ControlLayoutRepository(context).load();
        setWillNotDraw(false);
        setFocusable(true);
        setClickable(true);
        setContentDescription(context.getString(R.string.virtual_game_controls));
    }

    public void setListener(Listener listener) { this.listener = listener; }
    public void setInputRouter(InputRouter inputRouter) { this.inputRouter = inputRouter; publishButtons(); }
    public void setHapticPreferences(HapticLevel level, boolean distinguishAB) {
        haptics.configure(level, distinguishAB);
    }
    public void setControlSettings(AppSettings settings) {
        controlSettings = settings == null ? AppSettings.defaults() : settings;
        controlLayout = new ControlLayoutRepository(getContext()).load();
        haptics.configure(controlSettings.hapticLevel(), controlSettings.distinctABHaptics());
        rebuildHitMap();
    }
    public int buttons() { return buttons; }
    public GamepadHitMap hitMapForTest() { return hitMap; }

    public void reset() {
        handler.removeCallbacksAndMessages(null);
        if (touchState != null) touchState.cancelAll();
        pulseBits = 0;
        keyboardBits = 0;
        if (buttons != 0) { buttons = 0; publishButtons(); }
        else if (inputRouter != null) inputRouter.cancel(InputRouter.Source.TOUCH);
        if (inputRouter != null) inputRouter.cancel(InputRouter.Source.KEYBOARD);
        invalidate();
    }

    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        rebuildHitMap();
    }

    @Override public WindowInsets onApplyWindowInsets(WindowInsets insets) {
        insetLeft = insets.getSystemWindowInsetLeft();
        insetTop = insets.getSystemWindowInsetTop();
        insetRight = insets.getSystemWindowInsetRight();
        insetBottom = insets.getSystemWindowInsetBottom();
        rebuildHitMap();
        return insets;
    }

    private void rebuildHitMap() {
        if (getWidth() <= 0 || getHeight() <= 0) return;
        hitMap = GamepadHitMap.fromLayout(getWidth(), getHeight(), density,
                insetLeft, insetRight, insetTop, insetBottom, controlLayout);
        touchState = new GamepadInputState(hitMap);
        recompute();
    }

    @Override protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        if (hitMap == null) return;
        drawDpad(canvas);
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.B), "B");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.A), "A");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.SELECT), "SELECT");
        drawTarget(canvas, hitMap.target(GamepadHitMap.Control.START), "START");
    }

    private void drawDpad(Canvas canvas) {
        GamepadHitMap.Bounds d = hitMap.dpadBounds();
        float arm = 48f * density;
        RectF vertical = new RectF(d.centerX() - arm / 2f, d.top,
                d.centerX() + arm / 2f, d.bottom);
        RectF horizontal = new RectF(d.left, d.centerY() - arm / 2f,
                d.right, d.centerY() + arm / 2f);
        configurePaint(false, false);
        canvas.drawRoundRect(vertical, 8f * density, 8f * density, fill);
        canvas.drawRoundRect(horizontal, 8f * density, 8f * density, fill);
        canvas.drawRoundRect(vertical, 8f * density, 8f * density, stroke);
        canvas.drawRoundRect(horizontal, 8f * density, 8f * density, stroke);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.UP, InputBits.UP);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.DOWN, InputBits.DOWN);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.LEFT, InputBits.LEFT);
        drawDirectionHighlight(canvas, GamepadHitMap.Control.RIGHT, InputBits.RIGHT);
        fill.setColor(withAlpha(0xFF121316, idleAlpha()));
        canvas.drawCircle(d.centerX(), d.centerY(), 8f * density, fill);
    }

    private void drawDirectionHighlight(Canvas canvas, GamepadHitMap.Control control, int bit) {
        if ((buttons & bit) == 0) return;
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
        int action = event.getActionMasked();
        if (action == MotionEvent.ACTION_CANCEL) { reset(); return true; }
        int index = event.getActionIndex();
        int id = event.getPointerId(index);
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            GamepadHitMap.Control control = hitMap.hit(event.getX(index), event.getY(index));
            touchState.down(id, event.getX(index), event.getY(index), event.getEventTime());
            if (control == GamepadHitMap.Control.A || control == GamepadHitMap.Control.B
                    || control == GamepadHitMap.Control.SELECT || control == GamepadHitMap.Control.START) {
                haptics.feedback(control);
            }
        } else if (action == MotionEvent.ACTION_MOVE) {
            for (int i = 0; i < event.getPointerCount(); i++) {
                touchState.move(event.getPointerId(i), event.getX(i), event.getY(i), event.getEventTime());
            }
        } else if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP) {
            GamepadInputState.Release release = touchState.up(id, event.getEventTime());
            long remaining = MinimumTap.remainingMillis(event.getEventTime() - release.heldMillis(),
                    event.getEventTime(), MIN_FRAME_MS);
            if (remaining > 0L && release.bits() != 0) pulse(release.bits(), remaining);
            performClick();
        }
        recompute();
        return true;
    }

    private void pulse(int bits, long millis) {
        pulseBits |= bits;
        handler.postDelayed(() -> { pulseBits &= ~bits; recompute(); }, millis);
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
        if (inputRouter != null) inputRouter.setMask(InputRouter.Source.KEYBOARD, keyboardBits);
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
        int next = pulseBits | (touchState == null ? 0 : touchState.mask());
        if (next != buttons) { buttons = next; publishButtons(); }
        invalidate();
    }
    private void publishButtons() {
        if (inputRouter != null) inputRouter.setMask(InputRouter.Source.TOUCH, buttons);
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
}
