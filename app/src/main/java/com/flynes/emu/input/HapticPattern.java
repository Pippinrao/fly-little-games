package com.flynes.emu.input;

import java.util.Arrays;

public final class HapticPattern {
    public static final HapticPattern NONE = new HapticPattern(new long[0], new int[0]);

    private final long[] timings;
    private final int[] amplitudes;

    private HapticPattern(long[] timings, int[] amplitudes) {
        this.timings = timings.clone();
        this.amplitudes = amplitudes.clone();
    }

    public static HapticPattern forControl(GamepadHitMap.Control control,
                                           HapticLevel level,
                                           boolean distinguishAB) {
        if (level == HapticLevel.OFF || control == GamepadHitMap.Control.NONE) {
            return NONE;
        }
        int amplitude = amplitude(level);
        long duration = duration(level);
        if (control == GamepadHitMap.Control.B && !distinguishAB) {
            control = GamepadHitMap.Control.A;
        }
        switch (control) {
            case UP: return pulse(amplitude, duration);
            case DOWN: return pulse(amplitude, duration + 2);
            case LEFT: return cadence(amplitude, 3, 7, duration);
            case RIGHT: return cadence(amplitude, duration, 7, 3);
            case A: return pulse(amplitude, duration + 1);
            case B: return cadence(amplitude, Math.max(3, duration / 2), 9,
                    Math.max(3, duration / 2));
            case SELECT: return new HapticPattern(
                    new long[]{0, 3, 8, 3, 8, 3},
                    new int[]{0, amplitude, 0, amplitude, 0, amplitude});
            case START: return cadence(amplitude, duration + 4, 14, 4);
            case PAUSE: return pulse(amplitude, duration + 8);
            default: return NONE;
        }
    }

    private static HapticPattern pulse(int amplitude, long duration) {
        return new HapticPattern(new long[]{0, duration}, new int[]{0, amplitude});
    }

    private static HapticPattern cadence(int amplitude, long first, long gap, long second) {
        return new HapticPattern(new long[]{0, first, gap, second},
                new int[]{0, amplitude, 0, amplitude});
    }

    private static int amplitude(HapticLevel level) {
        switch (level) {
            case LIGHT: return 72;
            case STANDARD: return 140;
            case STRONG: return 220;
            default: return 0;
        }
    }

    private static long duration(HapticLevel level) {
        switch (level) {
            case LIGHT: return 8;
            case STANDARD: return 12;
            case STRONG: return 18;
            default: return 0;
        }
    }

    public long[] timings() {
        return timings.clone();
    }

    public int[] amplitudes() {
        return amplitudes.clone();
    }

    public boolean isNone() {
        return timings.length == 0;
    }

    @Override
    public boolean equals(Object other) {
        if (this == other) return true;
        if (!(other instanceof HapticPattern)) return false;
        HapticPattern pattern = (HapticPattern) other;
        return Arrays.equals(timings, pattern.timings)
                && Arrays.equals(amplitudes, pattern.amplitudes);
    }

    @Override
    public int hashCode() {
        return 31 * Arrays.hashCode(timings) + Arrays.hashCode(amplitudes);
    }
}
