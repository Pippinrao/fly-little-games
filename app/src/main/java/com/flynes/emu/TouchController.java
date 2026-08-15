package com.flynes.emu;

import android.os.Handler;
import android.os.Looper;
import android.view.MotionEvent;
import android.view.View;

/**
 * Phase-0 virtual pad, mapping touches to the NES button bitmask:
 *
 *   left half  = d-pad zones (top=UP, middle-left=LEFT, middle-right=RIGHT, bottom=DOWN)
 *   right half = B (top) / A (bottom)
 *   long-press (500 ms) on the d-pad = SELECT, on the right half = START
 *
 * Single-pointer for now (the plan's phase-0 simplification); the current
 * mask is reported through {@link Listener#onButtons(int)}.
 */
public class TouchController implements View.OnTouchListener {

    // NES button bitmask (matches core/include/nes/nes.h NES_BTN_*).
    public static final int A = 0x01;
    public static final int B = 0x02;
    public static final int SELECT = 0x04;
    public static final int START = 0x08;
    public static final int UP = 0x10;
    public static final int DOWN = 0x20;
    public static final int LEFT = 0x40;
    public static final int RIGHT = 0x80;

    private static final long LONG_PRESS_MS = 500;

    public interface Listener {
        void onButtons(int buttons);
    }

    private final Handler handler = new Handler(Looper.getMainLooper());
    private Listener listener;
    private int buttons = 0;
    private boolean longPressActive = false;
    private boolean inDPadZone = false;

    public void setListener(Listener l) {
        this.listener = l;
    }

    /** Current button bitmask (also delivered via {@link Listener#onButtons(int)}). */
    public int buttons() {
        return buttons;
    }

    private final Runnable longPressRunnable = new Runnable() {
        @Override public void run() {
            longPressActive = true;
            buttons |= inDPadZone ? SELECT : START;
            notifyListener();
        }
    };

    @Override
    public boolean onTouch(View v, MotionEvent e) {
        switch (e.getActionMasked()) {
            case MotionEvent.ACTION_DOWN:
                longPressActive = false;
                inDPadZone = e.getX() < v.getWidth() * 0.5f;
                handler.removeCallbacksAndMessages(null);
                handler.postDelayed(longPressRunnable, LONG_PRESS_MS);
                updateButtons(e, v);
                break;
            case MotionEvent.ACTION_MOVE:
                updateButtons(e, v);
                break;
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_CANCEL:
            default:
                handler.removeCallbacksAndMessages(null);
                longPressActive = false;
                buttons = 0;
                notifyListener();
                break;
        }
        return true;
    }

    private void updateButtons(MotionEvent e, View v) {
        int w = v.getWidth();
        int h = v.getHeight();
        int b = 0;
        float x = e.getX();
        float y = e.getY();

        if (x < w * 0.5f) {
            inDPadZone = true;
            if (y < h * 0.33f) {
                b |= UP;
            } else if (y < h * 0.66f) {
                b |= (x < w * 0.25f ? LEFT : RIGHT);
            } else {
                b |= DOWN;
            }
        } else {
            inDPadZone = false;
            b |= (y < h * 0.5f ? B : A);
        }

        buttons = b;
        if (longPressActive) {
            buttons |= inDPadZone ? SELECT : START;
        }
        notifyListener();
    }

    private void notifyListener() {
        if (listener != null) listener.onButtons(buttons);
    }
}
