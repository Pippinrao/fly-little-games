package com.flynes.emu;

/** Pure, deterministic fixed-joystick render return. Logical input is not involved. */
final class JoystickReturnAnimation {
    private static final long DURATION_MS = 80L;

    static final class Sample {
        private final float x;
        private final float y;

        Sample(float x, float y) {
            this.x = x;
            this.y = y;
        }

        float x() { return x; }
        float y() { return y; }
    }

    private boolean active;
    private long startTimeMs;
    private float centerX;
    private float centerY;
    private float startX;
    private float startY;

    boolean start(float centerX, float centerY, float knobX, float knobY, long eventTimeMs) {
        this.centerX = centerX;
        this.centerY = centerY;
        startX = knobX;
        startY = knobY;
        startTimeMs = eventTimeMs;
        active = knobX != centerX || knobY != centerY;
        return active;
    }

    Sample sample(long eventTimeMs) {
        if (!active) return new Sample(centerX, centerY);
        float progress = Math.max(0f,
                Math.min(1f, (eventTimeMs - startTimeMs) / (float) DURATION_MS));
        float x = startX + (centerX - startX) * progress;
        float y = startY + (centerY - startY) * progress;
        if (progress >= 1f) active = false;
        return new Sample(x, y);
    }

    boolean active() { return active; }

    void cancel() { active = false; }
}
