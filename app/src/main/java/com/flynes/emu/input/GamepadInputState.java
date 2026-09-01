package com.flynes.emu.input;

import java.util.HashMap;
import java.util.Map;

/** Pure pointer-id based state machine. Android events are adapted by GamepadView. */
public final class GamepadInputState {
    private static final int NO_POINTER = -1;

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

    public static final class JoystickVisual {
        private final boolean active;
        private final float centerX;
        private final float centerY;
        private final float knobX;
        private final float knobY;

        JoystickVisual(boolean active, float centerX, float centerY,
                       float knobX, float knobY) {
            this.active = active;
            this.centerX = centerX;
            this.centerY = centerY;
            this.knobX = knobX;
            this.knobY = knobY;
        }

        public boolean active() { return active; }
        public float centerX() { return centerX; }
        public float centerY() { return centerY; }
        public float knobX() { return knobX; }
        public float knobY() { return knobY; }
    }

    private static final class Pointer {
        final long downTime;
        final boolean dpad;
        final boolean joystick;
        int bits;
        long sequence;
        float x;
        float y;
        float effectiveX;
        float effectiveY;
        float centerX;
        float centerY;
        float knobX;
        float knobY;

        Pointer(long downTime, boolean dpad, boolean joystick, int bits, long sequence) {
            this.downTime = downTime;
            this.dpad = dpad;
            this.joystick = joystick;
            this.bits = bits;
            this.sequence = sequence;
        }

        static Pointer joystick(long downTime, float x, float y, long sequence) {
            Pointer pointer = new Pointer(downTime, true, true, 0, sequence);
            pointer.x = x;
            pointer.y = y;
            return pointer;
        }
    }

    private GamepadHitMap map;
    private final Map<Integer, Pointer> pointers = new HashMap<>();
    private long sequence;
    private int mask;
    private int joystickPointerId = NO_POINTER;

    public GamepadInputState(GamepadHitMap map) {
        if (map == null) throw new IllegalArgumentException("map");
        this.map = map;
    }

    public boolean down(int pointerId, float x, float y, long eventTime) {
        remove(pointerId);
        if (!map.joystickMode()) {
            putDpadIfHit(pointerId, x, y, eventTime);
            if (!pointers.containsKey(pointerId)) {
                GamepadHitMap.Control button = map.buttonHit(x, y);
                if (button != GamepadHitMap.Control.NONE) {
                    putButton(pointerId, x, y, eventTime, bitsFor(button));
                }
            }
        } else {
            GamepadHitMap.Control button = map.buttonHit(x, y);
            if (button != GamepadHitMap.Control.NONE) {
                putButton(pointerId, x, y, eventTime, bitsFor(button));
            } else if (map.canStartJoystick(x, y) && joystickPointerId == NO_POINTER) {
                Pointer pointer = Pointer.joystick(eventTime, x, y, ++sequence);
                pointer.centerX = map.clampJoystickCenterX(x);
                pointer.centerY = map.clampJoystickCenterY(y);
                pointer.effectiveX = pointer.centerX;
                pointer.effectiveY = pointer.centerY;
                pointers.put(pointerId, pointer);
                joystickPointerId = pointerId;
                updateJoystick(pointer);
            }
        }
        recompute();
        return pointers.containsKey(pointerId);
    }

    public void move(int pointerId, float x, float y, long eventTime) {
        Pointer pointer = pointers.get(pointerId);
        if (pointer == null) return;
        float deltaX = x - pointer.x;
        float deltaY = y - pointer.y;
        pointer.x = x;
        pointer.y = y;
        if (pointer.joystick) {
            pointer.effectiveX += deltaX;
            pointer.effectiveY += deltaY;
            updateJoystick(pointer);
        } else if (pointer.dpad) {
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
        if (pointer.bits == 0 && !pointer.dpad) remove(pointerId);
        recompute();
    }

    public Release up(int pointerId, long eventTime) {
        Pointer pointer = pointers.get(pointerId);
        remove(pointerId);
        recompute();
        return pointer == null ? new Release(0, eventTime, eventTime)
                : new Release(pointer.bits, pointer.downTime, eventTime);
    }

    public void cancelAll() {
        pointers.clear();
        joystickPointerId = NO_POINTER;
        mask = 0;
    }

    public void reconfigure(GamepadHitMap replacement) {
        if (replacement == null) throw new IllegalArgumentException("map");
        if (map.joystickMode() != replacement.joystickMode()) {
            cancelAll();
            map = replacement;
            return;
        }
        map = replacement;
        Pointer joystick = pointers.get(joystickPointerId);
        if (joystick != null && joystick.joystick) {
            float centerX = map.clampJoystickCenterX(joystick.centerX);
            float centerY = map.clampJoystickCenterY(joystick.centerY);
            joystick.effectiveX += centerX - joystick.centerX;
            joystick.effectiveY += centerY - joystick.centerY;
            joystick.centerX = centerX;
            joystick.centerY = centerY;
            updateJoystick(joystick);
        } else {
            joystickPointerId = NO_POINTER;
        }
        recompute();
    }

    public JoystickVisual joystickVisual() {
        Pointer pointer = pointers.get(joystickPointerId);
        if (pointer != null && pointer.joystick) {
            return new JoystickVisual(true, pointer.centerX, pointer.centerY,
                    pointer.knobX, pointer.knobY);
        }
        GamepadHitMap.Bounds base = map.dpadBounds();
        return new JoystickVisual(false, base.centerX(), base.centerY(),
                base.centerX(), base.centerY());
    }

    public int mask() { return mask; }
    public int activePointerCount() { return pointers.size(); }

    public boolean hasConsistentOwnership() {
        if (hasOpposingDirections(mask)) return false;
        int joystickCount = 0;
        for (Map.Entry<Integer, Pointer> entry : pointers.entrySet()) {
            Pointer pointer = entry.getValue();
            if (hasOpposingDirections(pointer.bits)) return false;
            if (pointer.bits == 0 && !pointer.dpad) return false;
            if (!pointer.joystick) continue;
            joystickCount++;
            if (!pointer.dpad || !map.joystickMode()
                    || entry.getKey() != joystickPointerId) return false;
            float dx = pointer.knobX - pointer.centerX;
            float dy = pointer.knobY - pointer.centerY;
            float knobDistance = (float) Math.hypot(dx, dy);
            if (Float.isNaN(knobDistance) || Float.isInfinite(knobDistance)
                    || knobDistance > map.joystickTravelRadius() + .01f) return false;
        }
        return joystickCount <= 1
                && ((joystickCount == 0 && joystickPointerId == NO_POINTER)
                || (joystickCount == 1 && joystickPointerId != NO_POINTER));
    }

    private void remove(int pointerId) {
        Pointer removed = pointers.remove(pointerId);
        if (removed != null && removed.joystick) joystickPointerId = NO_POINTER;
    }

    private void putButton(int pointerId, float x, float y, long eventTime, int bits) {
        Pointer pointer = new Pointer(eventTime, false, false, bits, ++sequence);
        pointer.x = x;
        pointer.y = y;
        pointers.put(pointerId, pointer);
    }

    private void putDpadIfHit(int pointerId, float x, float y, long eventTime) {
        int bits = map.directionBits(x, y, 0);
        if (bits == 0) return;
        Pointer pointer = new Pointer(eventTime, true, false, bits, ++sequence);
        pointer.x = x;
        pointer.y = y;
        pointers.put(pointerId, pointer);
    }

    private void updateJoystick(Pointer pointer) {
        pointer.centerX = map.clampJoystickCenterX(pointer.centerX);
        pointer.centerY = map.clampJoystickCenterY(pointer.centerY);
        float dx = pointer.effectiveX - pointer.centerX;
        float dy = pointer.effectiveY - pointer.centerY;
        float distance = (float) Math.hypot(dx, dy);
        float travel = map.joystickTravelRadius();
        if (distance > travel && distance > 0f) {
            float follow = (distance - travel) / distance;
            pointer.centerX = map.clampJoystickCenterX(pointer.centerX + dx * follow);
            pointer.centerY = map.clampJoystickCenterY(pointer.centerY + dy * follow);
            dx = pointer.effectiveX - pointer.centerX;
            dy = pointer.effectiveY - pointer.centerY;
            distance = (float) Math.hypot(dx, dy);
        }
        float scale = distance > travel && distance > 0f ? travel / distance : 1f;
        pointer.knobX = pointer.centerX + dx * scale;
        pointer.knobY = pointer.centerY + dy * scale;
        pointer.bits = map.joystickDirectionBits(
                pointer.centerX, pointer.centerY,
                pointer.effectiveX, pointer.effectiveY, pointer.bits);
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

    private static boolean hasOpposingDirections(int bits) {
        return ((bits & InputBits.UP) != 0 && (bits & InputBits.DOWN) != 0)
                || ((bits & InputBits.LEFT) != 0 && (bits & InputBits.RIGHT) != 0);
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
