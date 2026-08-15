package com.flynes.emu;

import android.view.Surface;

import java.nio.ByteBuffer;
import java.util.Arrays;

/**
 * JNI wrapper around the FlyNES C ABI core (libnescore.so).
 *
 * One instance owns one emulator context. All native methods are static;
 * the context is carried as a jlong handle.
 */
public final class NesCore {
    static {
        System.loadLibrary("nescore");
    }

    // Direct buffer used as the native audio sink for runFrames.
    // 128 KiB = 65536 int16 samples = ~82 frames of NTSC audio at 48 kHz;
    // far more than a single runFrames(2) call needs (~1600 samples).
    private static final int AUDIO_BUFFER_BYTES = 128 * 1024;
    private static final int SAVE_STATE_BUFFER_BYTES = 4 * 1024 * 1024;

    private long handle;
    private final ByteBuffer audioBuffer = ByteBuffer.allocateDirect(AUDIO_BUFFER_BYTES);

    private static native long nativeCreate();
    private static native void nativeDestroy(long h);
    private static native int nativeLoadRom(long h, byte[] rom, byte[] patch);
    private static native int nativeRunFrames(long h, int maxFrames, ByteBuffer audio, int capSamples);
    private static native void nativeSetInput(long h, int buttons);
    private static native void nativeSetAudioFormat(long h, int rate, int stereo);
    private static native int nativeSaveState(long h, byte[] out);
    private static native int nativeLoadState(long h, byte[] in);
    private static native void nativeBlit(long h, Surface surface, int scale);

    /** @return true if a native context is now live. */
    public boolean create() {
        if (handle != 0) return true;
        handle = nativeCreate();
        return handle != 0;
    }

    public void destroy() {
        if (handle != 0) {
            nativeDestroy(handle);
            handle = 0;
        }
    }

    public boolean isCreated() {
        return handle != 0;
    }

    /**
     * @return 0/positive on success (positive = warnings), negative on failure.
     *         A non-null {@code patch} is rejected (not implemented in the core).
     */
    public int loadRom(byte[] rom, byte[] patch) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeLoadRom(handle, rom, patch);
    }

    /**
     * Runs up to {@code n} frames on the audio-master clock and writes the
     * produced audio into the internal direct buffer.
     *
     * @return number of int16 samples written (0/positive), or a negative error.
     */
    public int runFrames(int n) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeRunFrames(handle, n, audioBuffer, audioBuffer.capacity() / 2);
    }

    public void setInput(int buttons) {
        if (handle != 0) nativeSetInput(handle, buttons);
    }

    /** @param stereo 0 = mono (the only mode the core supports in phase 0). */
    public void setAudioFormat(int rate, int stereo) {
        if (handle != 0) nativeSetAudioFormat(handle, rate, stereo);
    }

    /**
     * @return the saved state bytes, or null on failure (e.g. not ready).
     */
    public byte[] saveState() {
        if (handle == 0) return null;
        byte[] out = new byte[SAVE_STATE_BUFFER_BYTES];
        int written = nativeSaveState(handle, out);
        if (written <= 0) return null;
        return Arrays.copyOf(out, written);
    }

    /** @return 0/positive on success, negative on failure. */
    public int loadState(byte[] in) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeLoadState(handle, in);
    }

    /** Blits the latest framebuffer to {@code surface}, scaled by {@code scale}. */
    public void blit(Surface surface, int scale) {
        if (handle != 0) nativeBlit(handle, surface, scale);
    }

    /** The direct audio buffer filled by the most recent runFrames call. */
    ByteBuffer audioBuffer() {
        return audioBuffer;
    }
}
