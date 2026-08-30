package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Virtual gamepad overlay for FlyNES (mobile-game style): dynamic joystick in
 * the left half, A/B action buttons at bottom-right, START/SELECT at top-right.
 *
 * Every control is drawn with Canvas (no XML drawables) and every pointer is
 * tracked independently (SparseArray keyed by pointerId), so the left thumb
 * can steer while the right thumb presses A/B at the same time. The resulting
 * NES button bitmask is reported via {@link Listener#onButtons(int)} whenever
 * it changes; a START press additionally fires {@link Listener#onPauseMenu()}.
 */
public class GamepadView extends View {

    // NES button bitmask (matches core/include/nes/nes.h NES_BTN_*).
    public static final int A = 0x01;
    public static final int B = 0x02;
    public static final int SELECT = 0x04;
    public static final int START = 0x08;
    public static final int UP = 0x10;
    public static final int DOWN = 0x20;
    public static final int LEFT = 0x40;
    public static final int RIGHT = 0x80;

    /** Input + UI events; all callbacks arrive on the UI thread. */
    public interface Listener {
        /** Bitmask of currently-held NES buttons (directions + A/B + SELECT/START). */
        void onButtons(int buttons);
        /** A START press just happened; the host should show its pause menu. */
        void onPauseMenu();
    }

    private static final int PULSE_MS = 50;
    private static final int ROLE_NONE = 0;
    private static final int ROLE_JOY = 1;
    private static final int ROLE_B = 2;
    private static final int ROLE_A = 3;
    private static final int ROLE_START = 4;
    private static final int ROLE_SELECT = 5;

    private final float density;
    private final Handler handler = new Handler(Looper.getMainLooper());

    // Control sizes in px (dp * density, computed on size change).
    private float joyBaseR, joyKnobR, bR, aR, startW, startH, selR, gap;
    // Control centers in px.
    private float joyCX, joyCY, bCX, bCY, aCX, aCY, startCX, startCY, selCX, selCY;

    private final SparseArray<Pointer> pointers = new SparseArray<>(4);
    private int buttons = 0;
    private Listener listener;

    private static final class Pointer {
        int role = ROLE_NONE;
        float x, y;         // current position in view coords
        float joyCX, joyCY; // dynamic joystick center while this pointer owns it
        float knobX, knobY; // joystick knob offset from base center
    }

    public GamepadView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        setWillNotDraw(false);
    }

    public void setListener(Listener l) {
        this.listener = l;
    }

    /** Current NES button bitmask (also delivered via {@link Listener#onButtons}). */
    public int buttons() {
        return buttons;
    }

    /** Releases every held control and cancels pending pulses (pause/resume). */
    public void reset() {
        handler.removeCallbacksAndMessages(null);
        pointers.clear();
        if (buttons != 0) {
            buttons = 0;
            notifyButtons();
        }
        invalidate();
    }

    // ------------------------------------------------------------------ layout

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float dp = density;
        joyBaseR = 56 * dp;
        joyKnobR = 28 * dp;
        bR = 40 * dp;
        aR = 32 * dp;
        startW = 56 * dp;
        startH = 24 * dp;
        selR = 18 * dp;
        gap = 8 * dp;

        float margin = 24 * dp;
        float topMargin = 16 * dp;

        joyCX = margin + joyBaseR;
        joyCY = h - margin - joyBaseR;

        bCX = w - margin - bR;
        bCY = h - margin - bR;

        float dist = bR + aR + gap;
        double rad45 = Math.toRadians(45);
        aCX = bCX - (float) (dist * Math.cos(rad45));
        aCY = bCY - (float) (dist * Math.sin(rad45));

        startCX = w - margin - startW / 2f;
        startCY = topMargin + startH / 2f;

        selCX = startCX - startW / 2f - selR - gap;
        selCY = startCY;
    }

    // ------------------------------------------------------------------ drawing

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);

        Pointer joy = findRole(ROLE_JOY);
        Pointer b = findRole(ROLE_B);
        Pointer a = findRole(ROLE_A);
        Pointer start = findRole(ROLE_START);
        Pointer sel = findRole(ROLE_SELECT);

        // Joystick: fixed resting anchor; its base follows the first left-side touch.
        float baseX = joyCX, baseY = joyCY;
        float knobX = baseX, knobY = baseY;
        if (joy != null) {
            baseX = joy.joyCX;
            baseY = joy.joyCY;
            knobX = baseX + joy.knobX;
            knobY = baseY + joy.knobY;
        }
        drawCircle(c, baseX, baseY, joyBaseR, false, null);
        drawCircle(c, knobX, knobY, joyKnobR, true, null);

        // Action buttons.
        drawCircle(c, bCX, bCY, bR, b != null, "B");
        drawCircle(c, aCX, aCY, aR, a != null, "A");

        // Function buttons.
        drawRounded(c, startCX - startW / 2f, startCY - startH / 2f, startW, startH,
                start != null, "START");
        drawCircle(c, selCX, selCY, selR, sel != null, "SEL");
    }

    private void drawCircle(Canvas c, float cx, float cy, float r, boolean pressed, String text) {
        float scale = pressed ? 1.1f : 1f;
        float rr = r * scale;
        fill.setColor(pressed ? 0xAAFFFFFF : 0x55FFFFFF);
        fill.setStyle(Paint.Style.FILL);
        stroke.setColor(0xCCFFFFFF);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f * density);
        c.drawCircle(cx, cy, rr, fill);
        c.drawCircle(cx, cy, rr, stroke);
        if (text != null) {
            label.setColor(Color.WHITE);
            label.setTextSize((pressed ? 1.1f : 1f) * r * 0.8f);
            label.setTextAlign(Paint.Align.CENTER);
            Paint.FontMetrics fm = label.getFontMetrics();
            c.drawText(text, cx, cy - (fm.ascent + fm.descent) / 2f, label);
        }
    }

    private void drawRounded(Canvas c, float left, float top, float w, float h,
                             boolean pressed, String text) {
        float scale = pressed ? 1.08f : 1f;
        float ww = w * scale, hh = h * scale;
        float cx = left + w / 2f, cy = top + h / 2f;
        RectF rect = new RectF(cx - ww / 2f, cy - hh / 2f, cx + ww / 2f, cy + hh / 2f);
        fill.setColor(pressed ? 0xAAFFFFFF : 0x55FFFFFF);
        fill.setStyle(Paint.Style.FILL);
        stroke.setColor(0xCCFFFFFF);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f * density);
        c.drawRoundRect(rect, hh / 2f, hh / 2f, fill);
        c.drawRoundRect(rect, hh / 2f, hh / 2f, stroke);
        label.setColor(Color.WHITE);
        label.setTextSize((pressed ? 1.08f : 1f) * h * 0.5f);
        label.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics fm = label.getFontMetrics();
        c.drawText(text, cx, cy - (fm.ascent + fm.descent) / 2f, label);
    }

    private Pointer findRole(int role) {
        for (int i = 0; i < pointers.size(); i++) {
            Pointer p = pointers.valueAt(i);
            if (p.role == role) return p;
        }
        return null;
    }

    // ------------------------------------------------------------------ input

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = e.getX(index);
                float y = e.getY(index);
                int role = hitTest(x, y);
                if (role != ROLE_NONE && findRole(role) != null) {
                    role = ROLE_NONE; // control already held by another finger
                }
                Pointer p = new Pointer();
                p.role = role;
                p.x = x;
                p.y = y;
                if (role == ROLE_JOY) {
                    p.joyCX = clampJoyCenterX(x);
                    p.joyCY = clampJoyCenterY(y);
                    clampKnob(p);
                }
                pointers.put(id, p);
                if (role == ROLE_START) {
                    pulse(START);
                    if (listener != null) listener.onPauseMenu();
                } else if (role == ROLE_SELECT) {
                    pulse(SELECT);
                }
                recompute();
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    Pointer p = pointers.get(e.getPointerId(i));
                    if (p == null) continue;
                    p.x = e.getX(i);
                    p.y = e.getY(i);
                    if (p.role == ROLE_JOY) {
                        clampKnob(p);
                    }
                }
                recompute();
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP: {
                pointers.remove(id);
                recompute();
                break;
            }
            case MotionEvent.ACTION_CANCEL:
                pointers.clear();
                recompute();
                break;
        }
        return true;
    }

    /** Assigns a control role to a touch point (action zone checked first). */
    private int hitTest(float x, float y) {
        float pad = 8 * density;
        if (dist2(x, y, bCX, bCY) <= bR * bR * 1.6f) return ROLE_B;
        if (dist2(x, y, aCX, aCY) <= aR * aR * 1.6f) return ROLE_A;
        if (x >= startCX - startW / 2f - pad && x <= startCX + startW / 2f + pad
                && y >= startCY - startH / 2f - pad && y <= startCY + startH / 2f + pad) {
            return ROLE_START;
        }
        if (dist2(x, y, selCX, selCY) <= selR * selR * 1.6f) return ROLE_SELECT;
        if (isJoystickStart(x, getWidth())) return ROLE_JOY;
        return ROLE_NONE;
    }

    /** The left half is the joystick activation zone; other controls win hit testing first. */
    static boolean isJoystickStart(float x, float viewWidth) {
        return x >= 0f && x < viewWidth * 0.5f;
    }

    private static float dist2(float x1, float y1, float x2, float y2) {
        float dx = x1 - x2, dy = y1 - y2;
        return dx * dx + dy * dy;
    }

    /** Clamps the knob offset to the travel limit (saturation: full direction). */
    private void clampKnob(Pointer p) {
        float max = joyBaseR - joyKnobR * 0.5f;
        float[] center = centerAfterFollow(p.joyCX, p.joyCY, p.x, p.y, max);
        p.joyCX = clampJoyCenterX(center[0]);
        p.joyCY = clampJoyCenterY(center[1]);
        float dx = p.x - p.joyCX, dy = p.y - p.joyCY;
        float dist = (float) Math.hypot(dx, dy);
        if (dist <= max || dist == 0f) {
            p.knobX = dx;
            p.knobY = dy;
        } else {
            float s = max / dist;
            p.knobX = dx * s;
            p.knobY = dy * s;
        }
    }

    private float clampJoyCenterX(float x) {
        float min = joyBaseR;
        float max = Math.max(min, getWidth() * 0.5f - joyKnobR);
        return Math.max(min, Math.min(max, x));
    }

    private float clampJoyCenterY(float y) {
        float min = joyBaseR;
        float max = Math.max(min, getHeight() - joyBaseR);
        return Math.max(min, Math.min(max, y));
    }

    /** Moves a dynamic joystick center only by the portion beyond its travel limit. */
    static float[] centerAfterFollow(float centerX, float centerY, float fingerX, float fingerY,
                                     float maxTravel) {
        float dx = fingerX - centerX;
        float dy = fingerY - centerY;
        float distance = (float) Math.hypot(dx, dy);
        if (distance <= maxTravel || distance == 0f) {
            return new float[] {centerX, centerY};
        }
        float follow = (distance - maxTravel) / distance;
        return new float[] {centerX + dx * follow, centerY + dy * follow};
    }

    /** START/SELECT fire a short pulse (down then auto-release). */
    private void pulse(int mask) {
        buttons |= mask;
        notifyButtons();
        handler.postDelayed(() -> {
            buttons &= ~mask;
            notifyButtons();
        }, PULSE_MS);
    }

    /** Recomputes the held-bitmask from live pointers and repaints. */
    private void recompute() {
        int b = 0;
        Pointer joy = findRole(ROLE_JOY);
        if (joy != null) {
            b |= joystickBits(joy.knobX, joy.knobY, joyBaseR);
        }
        if (findRole(ROLE_B) != null) b |= B;
        if (findRole(ROLE_A) != null) b |= A;
        b |= buttons & (START | SELECT); // pulse bits survive recompute
        if (b != buttons) {
            buttons = b;
            notifyButtons();
        }
        invalidate();
    }

    /**
     * Maps a joystick knob offset (px) to NES direction bits.
     * 8 sectors; diagonals set both keys; dead zone below 15% of base radius.
     */
    static int joystickBits(float knobX, float knobY, float baseRadius) {
        float dist = (float) Math.hypot(knobX, knobY);
        if (dist < 0.15f * baseRadius) return 0;
        double deg = Math.toDegrees(Math.atan2(knobY, knobX)); // 0=right, 90=down
        if (deg < -157.5 || deg >= 157.5) return LEFT;
        if (deg < -112.5) return UP | LEFT;
        if (deg < -67.5) return UP;
        if (deg < -22.5) return UP | RIGHT;
        if (deg < 22.5) return RIGHT;
        if (deg < 67.5) return DOWN | RIGHT;
        if (deg < 112.5) return DOWN;
        if (deg < 157.5) return DOWN | LEFT;
        return LEFT;
    }

    private void notifyButtons() {
        if (listener != null) listener.onButtons(buttons);
    }
}
