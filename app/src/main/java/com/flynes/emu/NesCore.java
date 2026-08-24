package com.flynes.emu;

import java.nio.ByteBuffer;
import java.util.Arrays;

import com.flynes.emu.session.CoreFacade;
import com.flynes.emu.data.RomIdentity;
import com.flynes.emu.data.RomInfo;
import com.flynes.emu.video.NativeFrameSource;

/**
 * JNI wrapper around the FlyNES C ABI core (libnescore.so).
 *
 * One instance owns one emulator context. All native methods are static;
 * the context is carried as a jlong handle.
 */
public final class NesCore implements CoreFacade, NativeFrameSource.Bridge {
    static {
        System.loadLibrary("nescore");
    }

    /**
     * In-process ROM handoff from {@link GameLibraryActivity} back to
     * MainActivity: a byte[] cannot be passed through an Intent extra, so the
     * library drops the loaded ROM here and MainActivity consumes it in
     * onActivityResult, then sets it back to null. volatile so the handoff is
     * visible across threads.
     */
    public static volatile byte[] sPendingRom;

    // Direct buffer used as the native audio sink for runFrames.
    // 128 KiB = 65536 int16 samples = ~82 frames of NTSC audio at 48 kHz;
    // far more than one native frame needs (~800 samples at 48 kHz).
    private static final int AUDIO_BUFFER_BYTES = 128 * 1024;
    private static final int SAVE_STATE_BUFFER_BYTES = 4 * 1024 * 1024;

    private long handle;
    private final ByteBuffer audioBuffer = ByteBuffer.allocateDirect(AUDIO_BUFFER_BYTES);

    private static native long nativeCreate();
    private static native void nativeDestroy(long h);
    private static native int nativeLoadRom(long h, byte[] rom, byte[] patch);
    private static native String[] nativeRomInfoStrings(long h);
    private static native int[] nativeRomInfoNumbers(long h);
    private static native int nativeLoadDatabase(long h, byte[] xml);
    private static native int nativeRunFrames(long h, int maxFrames, ByteBuffer audio, int capSamples);
    private static native long nativeCopyVideoFrame(long h, ByteBuffer destination, int[] metadata);
    private static native void nativeSetInput(long h, int buttons);
    private static native void nativeSetAudioFormat(long h, int rate, int stereo);
    private static native void nativeSetVideoFilter(long h, int filter);
    private static native int nativeSaveState(long h, byte[] out);
    private static native int nativeLoadState(long h, byte[] in);

    /** @return true if a native context is now live. */
    @Override public boolean create() {
        if (handle != 0) return true;
        handle = nativeCreate();
        return handle != 0;
    }

    @Override public void destroy() {
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

    @Override public int loadRom(byte[] rom) {
        return loadRom(rom, null);
    }

    public RomInfo romInfo() {
        if (handle == 0) return null;
        String[] text = nativeRomInfoStrings(handle);
        int[] values = nativeRomInfoNumbers(handle);
        if (text == null || text.length != 6 || values == null || values.length != 13) {
            return null;
        }
        return new RomInfo(new RomIdentity(text[4]), text[0], text[1], text[2], text[3],
                values[0], values[1], values[2], values[3], values[4], values[5],
                values[6] != 0, values[7], values[8], values[9], values[10] != 0,
                values[11] != 0, text[5], values[12]);
    }

    /**
     * Loads the bundled NstDatabase.xml into the core so ROM loading can
     * resolve profiles. Must be called before {@link #loadRom}; reloading is
     * idempotent.
     *
     * @return 0/positive on success (positive = warnings), negative on failure.
     */
    public int loadDatabase(byte[] xml) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeLoadDatabase(handle, xml);
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

    @Override public int runOneFrame() {
        return runFrames(1);
    }

    @Override public long copyVideoFrame(ByteBuffer destination, int[] metadata) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeCopyVideoFrame(handle, destination, metadata);
    }

    @Override public void setInput(int buttons) {
        if (handle != 0) nativeSetInput(handle, buttons);
    }

    /** @param stereo 0 = mono (the only mode the core supports in phase 0). */
    public void setAudioFormat(int rate, int stereo) {
        if (handle != 0) nativeSetAudioFormat(handle, rate, stereo);
    }

    /** Video filter constants (match core/include/nes/nes.h nes_video_filter). */
    public static final int FILTER_NONE = 0;
    public static final int FILTER_HQ2X = 2;
    public static final int FILTER_HQ3X = 3;
    public static final int FILTER_HQ4X = 4;

    /**
     * Sets the core video filter (scales the framebuffer 1x/2x/3x/4x).
     *
     * @param filter one of {@link #FILTER_NONE} / {@link #FILTER_HQ2X} /
     *               {@link #FILTER_HQ3X} / {@link #FILTER_HQ4X}.
     */
    public void setVideoFilter(int filter) {
        if (handle != 0) nativeSetVideoFilter(handle, filter);
    }

    /**
     * @return the saved state bytes, or null on failure (e.g. not ready).
     */
    @Override public byte[] saveState() {
        if (handle == 0) return null;
        byte[] out = new byte[SAVE_STATE_BUFFER_BYTES];
        int written = nativeSaveState(handle, out);
        if (written <= 0) return null;
        return Arrays.copyOf(out, written);
    }

    /** @return 0/positive on success, negative on failure. */
    @Override public int loadState(byte[] in) {
        if (handle == 0) return -3; // NES_ERR_NOT_READY
        return nativeLoadState(handle, in);
    }

    /** The direct audio buffer filled by the most recent runFrames call. */
    ByteBuffer audioBuffer() {
        return audioBuffer;
    }
}
