package com.flynes.emu.input;

import java.util.HashMap;
import java.util.Map;

/** Pure pointer-id based state machine. Android events are adapted by GamepadView. */
public final class GamepadInputState {
    private static final int DIRECTION_BITS = InputBits.UP | InputBits.DOWN
            | InputBits.LEFT | InputBits.RIGHT;
    private static final int BUTTON_BITS = InputBits.A | InputBits.B
            | InputBits.SELECT | InputBits.START;

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

    /** Immutable direction visual state. The historic name is retained for source compatibility. */
    public static final class JoystickVisual {
        private final boolean owned;
        private final boolean activated;
        private final boolean saturated;
        private final float centerX;
        private final float centerY;
        private final float knobX;
        private final float knobY;
        private final int directionBits;

        JoystickVisual(DirectionSession.Snapshot snapshot) {
            owned = snapshot.owned;
            activated = snapshot.activated;
            saturated = snapshot.saturated;
            centerX = snapshot.centerX;
            centerY = snapshot.centerY;
            knobX = snapshot.knobX;
            knobY = snapshot.knobY;
            directionBits = snapshot.directionBits;
        }

        /** Compatibility alias: active means that a pointer owns the direction gesture. */
        public boolean active() { return owned; }
        public boolean owned() { return owned; }
        public boolean activated() { return activated; }
        public boolean saturated() { return saturated; }
        public float centerX() { return centerX; }
        public float centerY() { return centerY; }
        public float knobX() { return knobX; }
        public float knobY() { return knobY; }
        public int directionBits() { return directionBits; }
    }

    private static final class ButtonPointer {
        final long downTime;
        int bits;
        float x;
        float y;

        ButtonPointer(long downTime, int bits, float x, float y) {
            this.downTime = downTime;
            this.bits = bits;
            this.x = x;
            this.y = y;
        }
    }

    private GamepadHitMap map;
    private final DirectionSession direction;
    private final Map<Integer, ButtonPointer> buttonPointers = new HashMap<>();
    private int mask;

    public GamepadInputState(GamepadHitMap map) {
        if (map == null) throw new IllegalArgumentException("map");
        this.map = map;
        direction = new DirectionSession(map);
    }

    public boolean down(int pointerId, float x, float y, long eventTime) {
        remove(pointerId);
        if (!putButtonIfHit(pointerId, x, y, eventTime)) {
            direction.capture(pointerId, x, y, eventTime);
        }
        recompute();
        return direction.owns(pointerId) || buttonPointers.containsKey(pointerId);
    }

    public void move(int pointerId, float x, float y, long eventTime) {
        if (direction.owns(pointerId)) {
            direction.move(pointerId, x, y);
            recompute();
            return;
        }

        ButtonPointer pointer = buttonPointers.get(pointerId);
        if (pointer == null) return;
        pointer.x = x;
        pointer.y = y;
        int currentBits = bitsFor(map.buttonHit(x, y));
        if ((pointer.bits & (InputBits.A | InputBits.B)) != 0
                && (currentBits & (InputBits.A | InputBits.B)) != 0) {
            pointer.bits |= currentBits;
        } else if (currentBits == 0) {
            buttonPointers.remove(pointerId);
        }
        recompute();
    }

    public Release up(int pointerId, long eventTime) {
        if (direction.owns(pointerId)) {
            Release release = new Release(direction.bits(), direction.downTime(), eventTime);
            direction.cancel(pointerId);
            recompute();
            return release;
        }
        ButtonPointer pointer = buttonPointers.remove(pointerId);
        recompute();
        return pointer == null ? new Release(0, eventTime, eventTime)
                : new Release(pointer.bits, pointer.downTime, eventTime);
    }

    /** Cancels one pointer without producing a tap-release result. */
    public void cancel(int pointerId) {
        remove(pointerId);
        recompute();
    }

    public void cancelAll() {
        buttonPointers.clear();
        direction.cancelAll();
        mask = 0;
    }

    public void reconfigure(GamepadHitMap replacement) {
        if (replacement == null) throw new IllegalArgumentException("map");
        map = replacement;
        direction.reconfigure(replacement);
        recompute();
    }

    public JoystickVisual joystickVisual() {
        return new JoystickVisual(direction.snapshot());
    }

    public int mask() { return mask; }

    public int activePointerCount() {
        return buttonPointers.size() + (direction.owned() ? 1 : 0);
    }

    public long directionFeedbackRevision() {
        return direction.feedbackRevision();
    }

    public boolean hasConsistentOwnership() {
        if (hasOpposingDirections(mask) || !direction.isConsistent()) return false;
        for (Map.Entry<Integer, ButtonPointer> entry : buttonPointers.entrySet()) {
            ButtonPointer pointer = entry.getValue();
            if (direction.owns(entry.getKey()) || pointer.bits == 0
                    || (pointer.bits & ~BUTTON_BITS) != 0) return false;
        }
        return (mask & DIRECTION_BITS) == direction.bits();
    }

    private boolean putButtonIfHit(int pointerId, float x, float y, long eventTime) {
        int bits = bitsFor(map.buttonHit(x, y));
        if (bits == 0) return false;
        buttonPointers.put(pointerId, new ButtonPointer(eventTime, bits, x, y));
        return true;
    }

    private void remove(int pointerId) {
        direction.cancel(pointerId);
        buttonPointers.remove(pointerId);
    }

    private void recompute() {
        int result = direction.bits();
        for (ButtonPointer pointer : buttonPointers.values()) result |= pointer.bits;
        mask = result;
    }

    private static boolean hasOpposingDirections(int bits) {
        return ((bits & InputBits.UP) != 0 && (bits & InputBits.DOWN) != 0)
                || ((bits & InputBits.LEFT) != 0 && (bits & InputBits.RIGHT) != 0);
    }

    private static int bitsFor(GamepadHitMap.Control control) {
        switch (control) {
            case B: return InputBits.B;
            case A: return InputBits.A;
            case SELECT: return InputBits.SELECT;
            case START: return InputBits.START;
            default: return 0;
        }
    }
}
