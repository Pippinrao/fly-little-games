package com.flynes.emu.video;

import java.util.Arrays;

/** Pad state captured at the native core's actual read boundary. */
public final class NativeInputSample {
    private final long generation;
    private final int[] padBits;
    private final long nativeMonotonicNs;

    public NativeInputSample(long generation, int[] padBits, long nativeMonotonicNs) {
        if (generation < 0L || padBits == null || padBits.length != 4
                || nativeMonotonicNs < 0L) throw new IllegalArgumentException("invalid input sample");
        this.generation = generation;
        this.padBits = Arrays.copyOf(padBits, padBits.length);
        this.nativeMonotonicNs = nativeMonotonicNs;
    }
    public long generation() { return generation; }
    public int padBits(int port) { return padBits[port]; }
    public long nativeMonotonicNs() { return nativeMonotonicNs; }
}
