package com.flynes.emu.input;

/**
 * Single-owner direction gesture state shared by D-pad and joystick modes.
 * Capture geometry is checked once; owned moves are intentionally unbounded.
 */
final class DirectionSession {
    private static final int NO_POINTER = -1;
    private static final double FULL_TURN = Math.PI * 2.0;
    private static final double SECTOR_ANGLE = Math.PI / 4.0;
    private static final double HALF_SECTOR = Math.PI / 8.0;
    private static final double HYSTERESIS = Math.toRadians(7.5);

    static final class Snapshot {
        final boolean owned;
        final boolean activated;
        final boolean saturated;
        final float centerX;
        final float centerY;
        final float knobX;
        final float knobY;
        final int directionBits;

        Snapshot(boolean owned, boolean activated, boolean saturated,
                 float centerX, float centerY, float knobX, float knobY,
                 int directionBits) {
            this.owned = owned;
            this.activated = activated;
            this.saturated = saturated;
            this.centerX = centerX;
            this.centerY = centerY;
            this.knobX = knobX;
            this.knobY = knobY;
            this.directionBits = directionBits;
        }
    }

    private GamepadHitMap map;
    private int ownerPointerId = NO_POINTER;
    private long downTime;
    private float rawX;
    private float rawY;
    private float effectiveX;
    private float effectiveY;
    private float centerX;
    private float centerY;
    private float knobX;
    private float knobY;
    private boolean activated;
    private boolean saturated;
    private int stableSector = -1;
    private int directionBits;
    private long feedbackRevision;

    DirectionSession(GamepadHitMap map) {
        if (map == null) throw new IllegalArgumentException("map");
        this.map = map;
    }

    boolean capture(int pointerId, float x, float y, long eventTime) {
        if (owned() || !map.canStartDirection(x, y)) return false;
        ownerPointerId = pointerId;
        downTime = eventTime;
        rawX = x;
        rawY = y;
        activated = false;
        stableSector = -1;
        directionBits = 0;

        if (map.followingJoystickMode()) {
            centerX = map.clampJoystickCenterX(x);
            centerY = map.clampJoystickCenterY(y);
            effectiveX = centerX;
            effectiveY = centerY;
        } else {
            GamepadHitMap.Bounds base = map.dpadBounds();
            centerX = base.centerX();
            centerY = base.centerY();
            effectiveX = x;
            effectiveY = y;
        }
        refreshVisual();
        updateDirection();
        return true;
    }

    void move(int pointerId, float x, float y) {
        if (!owns(pointerId)) return;
        float deltaX = x - rawX;
        float deltaY = y - rawY;
        rawX = x;
        rawY = y;
        effectiveX += deltaX;
        effectiveY += deltaY;
        if (map.followingJoystickMode() && (deltaX != 0f || deltaY != 0f)) {
            followBase();
        }
        refreshVisual();
        updateDirection();
    }

    void reconfigure(GamepadHitMap replacement) {
        if (replacement == null) throw new IllegalArgumentException("map");
        if (map.directionMode() != replacement.directionMode()) {
            map = replacement;
            clearOwner();
            return;
        }
        if (!owned()) {
            map = replacement;
            return;
        }

        float previousCenterX = centerX;
        float previousCenterY = centerY;
        map = replacement;
        if (map.followingJoystickMode()) {
            centerX = map.clampJoystickCenterX(previousCenterX);
            centerY = map.clampJoystickCenterY(previousCenterY);
        } else {
            GamepadHitMap.Bounds base = map.dpadBounds();
            centerX = base.centerX();
            centerY = base.centerY();
        }
        effectiveX += centerX - previousCenterX;
        effectiveY += centerY - previousCenterY;
        refreshVisual();
    }

    void cancel(int pointerId) {
        if (owns(pointerId)) clearOwner();
    }

    void cancelAll() {
        clearOwner();
    }

    boolean owns(int pointerId) {
        return ownerPointerId == pointerId;
    }

    boolean owned() {
        return ownerPointerId != NO_POINTER;
    }

    int bits() {
        return directionBits;
    }

    long downTime() {
        return downTime;
    }

    long feedbackRevision() {
        return feedbackRevision;
    }

    Snapshot snapshot() {
        if (owned()) {
            return new Snapshot(true, activated, saturated, centerX, centerY,
                    knobX, knobY, directionBits);
        }
        GamepadHitMap.Bounds base = map.dpadBounds();
        return new Snapshot(false, false, false, base.centerX(), base.centerY(),
                base.centerX(), base.centerY(), 0);
    }

    boolean isConsistent() {
        if (!owned()) {
            return directionBits == 0 && !activated && stableSector == -1;
        }
        if (activated != (directionBits != 0)) return false;
        if (activated && bitsForSector(stableSector) != directionBits) return false;
        if (hasOpposingDirections(directionBits)) return false;
        float distance = (float) Math.hypot(knobX - centerX, knobY - centerY);
        return Float.isFinite(distance)
                && distance <= map.joystickTravelRadius() + .01f;
    }

    private void followBase() {
        centerX = map.clampJoystickCenterX(centerX);
        centerY = map.clampJoystickCenterY(centerY);
        float dx = effectiveX - centerX;
        float dy = effectiveY - centerY;
        float distance = (float) Math.hypot(dx, dy);
        float travel = map.joystickTravelRadius();
        if (distance <= travel || distance == 0f) return;
        float follow = (distance - travel) / distance;
        centerX = map.clampJoystickCenterX(centerX + dx * follow);
        centerY = map.clampJoystickCenterY(centerY + dy * follow);
    }

    private void refreshVisual() {
        float dx = effectiveX - centerX;
        float dy = effectiveY - centerY;
        float distance = (float) Math.hypot(dx, dy);
        float travel = map.joystickTravelRadius();
        saturated = distance > travel;
        float scale = saturated && distance > 0f ? travel / distance : 1f;
        knobX = centerX + dx * scale;
        knobY = centerY + dy * scale;
    }

    private void updateDirection() {
        float dx = effectiveX - centerX;
        float dy = effectiveY - centerY;
        float distance = (float) Math.hypot(dx, dy);
        float activationDistance = map.deadZone() * map.joystickRadius();
        if (!activated) {
            if (distance < activationDistance) return;
            activated = true;
            stableSector = sectorFor(dx, dy);
            directionBits = bitsForSector(stableSector);
            feedbackRevision++;
            return;
        }

        if (distance < activationDistance) return;
        int candidate = sectorFor(dx, dy);
        if (candidate == stableSector || insideCurrentSectorHysteresis(dx, dy)) return;
        stableSector = candidate;
        directionBits = bitsForSector(stableSector);
        feedbackRevision++;
    }

    private boolean insideCurrentSectorHysteresis(float dx, float dy) {
        double angle = normalizedAngle(dx, dy);
        double currentCenter = stableSector * SECTOR_ANGLE;
        double delta = angle - currentCenter;
        if (delta <= -Math.PI) delta += FULL_TURN;
        else if (delta > Math.PI) delta -= FULL_TURN;
        return Math.abs(delta) <= HALF_SECTOR + HYSTERESIS;
    }

    private void clearOwner() {
        ownerPointerId = NO_POINTER;
        downTime = 0L;
        rawX = 0f;
        rawY = 0f;
        effectiveX = 0f;
        effectiveY = 0f;
        centerX = 0f;
        centerY = 0f;
        knobX = 0f;
        knobY = 0f;
        activated = false;
        saturated = false;
        stableSector = -1;
        directionBits = 0;
    }

    private static int sectorFor(float dx, float dy) {
        double angle = normalizedAngle(dx, dy);
        return ((int) Math.floor((angle + HALF_SECTOR) / SECTOR_ANGLE)) & 7;
    }

    private static double normalizedAngle(float dx, float dy) {
        double angle = Math.atan2(dy, dx);
        return angle < 0.0 ? angle + FULL_TURN : angle;
    }

    private static int bitsForSector(int sector) {
        switch (sector) {
            case 0: return InputBits.RIGHT;
            case 1: return InputBits.RIGHT | InputBits.DOWN;
            case 2: return InputBits.DOWN;
            case 3: return InputBits.LEFT | InputBits.DOWN;
            case 4: return InputBits.LEFT;
            case 5: return InputBits.LEFT | InputBits.UP;
            case 6: return InputBits.UP;
            case 7: return InputBits.RIGHT | InputBits.UP;
            default: return 0;
        }
    }

    private static boolean hasOpposingDirections(int bits) {
        return ((bits & InputBits.UP) != 0 && (bits & InputBits.DOWN) != 0)
                || ((bits & InputBits.LEFT) != 0 && (bits & InputBits.RIGHT) != 0);
    }
}
