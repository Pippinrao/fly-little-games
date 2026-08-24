package com.flynes.emu.input;

import java.util.HashMap;
import java.util.Map;

/** Pure pointer-id based state machine. Android events are adapted by GamepadView. */
public final class GamepadInputState {
    public static final class Release {
        private final int bits;
        private final long downTime;
        private final long upTime;
        Release(int bits, long downTime, long upTime) {
            this.bits = bits;
            this.downTime = downTime;
            this.upTime = upTime;
        }
        public int bits() { return bits; }
        public long heldMillis() { return Math.max(0L, upTime - downTime); }
    }

    private static final class Pointer {
        final long downTime;
        final boolean dpad;
        int bits;
        long sequence;
        Pointer(long downTime, boolean dpad, int bits, long sequence) {
            this.downTime = downTime;
            this.dpad = dpad;
            this.bits = bits;
            this.sequence = sequence;
        }
    }

    private final GamepadHitMap map;
    private final Map<Integer, Pointer> pointers = new HashMap<>();
    private long sequence;
    private int mask;

    public GamepadInputState(GamepadHitMap map) { this.map = map; }

    public void down(int pointerId, float x, float y, long eventTime) {
        pointers.remove(pointerId);
        int direction = map.directionBits(x, y, 0);
        boolean dpad = direction != 0;
        int bits = dpad ? direction : bitsFor(map.hit(x, y));
        if (bits != 0) pointers.put(pointerId, new Pointer(eventTime, dpad, bits, ++sequence));
        recompute();
    }

    public void move(int pointerId, float x, float y, long eventTime) {
        Pointer pointer = pointers.get(pointerId);
        if (pointer == null) return;
        if (pointer.dpad) {
            pointer.bits = map.directionBits(x, y, pointer.bits);
        } else {
            int currentBits = bitsFor(map.hit(x, y));
            if ((pointer.bits & (InputBits.A | InputBits.B)) != 0
                    && (currentBits & (InputBits.A | InputBits.B)) != 0) {
                pointer.bits |= currentBits;
            } else if (currentBits == 0) {
                pointer.bits = 0;
            }
        }
        pointer.sequence = ++sequence;
        if (pointer.bits == 0) pointers.remove(pointerId);
        recompute();
    }

    public Release up(int pointerId, long eventTime) {
        Pointer pointer = pointers.remove(pointerId);
        recompute();
        return pointer == null ? new Release(0, eventTime, eventTime)
                : new Release(pointer.bits, pointer.downTime, eventTime);
    }

    public void cancelAll() { pointers.clear(); mask = 0; }
    public int mask() { return mask; }
    public int activePointerCount() { return pointers.size(); }

    public boolean hasConsistentOwnership() {
        if ((mask & InputBits.UP) != 0 && (mask & InputBits.DOWN) != 0) return false;
        if ((mask & InputBits.LEFT) != 0 && (mask & InputBits.RIGHT) != 0) return false;
        for (Pointer pointer : pointers.values()) if (pointer.bits == 0) return false;
        return true;
    }

    private void recompute() {
        int result = 0;
        long up = Long.MIN_VALUE, down = Long.MIN_VALUE, left = Long.MIN_VALUE, right = Long.MIN_VALUE;
        for (Pointer pointer : pointers.values()) {
            result |= pointer.bits & ~(InputBits.UP | InputBits.DOWN | InputBits.LEFT | InputBits.RIGHT);
            if ((pointer.bits & InputBits.UP) != 0 && pointer.sequence > up) up = pointer.sequence;
            if ((pointer.bits & InputBits.DOWN) != 0 && pointer.sequence > down) down = pointer.sequence;
            if ((pointer.bits & InputBits.LEFT) != 0 && pointer.sequence > left) left = pointer.sequence;
            if ((pointer.bits & InputBits.RIGHT) != 0 && pointer.sequence > right) right = pointer.sequence;
        }
        if (up != Long.MIN_VALUE || down != Long.MIN_VALUE) result |= up > down ? InputBits.UP : InputBits.DOWN;
        if (left != Long.MIN_VALUE || right != Long.MIN_VALUE) result |= left > right ? InputBits.LEFT : InputBits.RIGHT;
        mask = result;
    }

    private static int bitsFor(GamepadHitMap.Control control) {
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
