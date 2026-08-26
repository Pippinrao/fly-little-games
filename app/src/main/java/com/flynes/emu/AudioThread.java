package com.flynes.emu;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;

import java.nio.ByteBuffer;
import java.util.concurrent.atomic.AtomicBoolean;

import com.flynes.emu.video.FrameAvailableSignal;
import com.flynes.emu.video.FrameStepResult;

/**
 * Audio-master-clock loop: the emulator never runs ahead of the audio sink.
 *
 * Each iteration runs exactly one NES frame into the core's direct buffer,
 * then drains it with BLOCKING AudioTrack.write(). The write blocks
 * until the audio HAL consumes enough buffer space, which paces emulation in
 * real time — no sleep, no busy-wait.
 */
public class AudioThread extends Thread {

    private static final int SAMPLE_RATE = 48000;

    /** Consecutive runFrames() returns of &lt;= 0 samples before the loop gives up. */
    private static final int MAX_IDLE_ITERATIONS = 60;

    private final NesCore core;
    private final boolean audible;
    private final FrameAvailableSignal frameAvailable;
    // AtomicBoolean (not a plain volatile flag): run() CLAIMS the loop with
    // compareAndSet(false, true) so a stopLoop() issued before the thread
    // actually starts can never be overwritten by a later `running = true`.
    private final AtomicBoolean running = new AtomicBoolean(false);
    private volatile AudioTrack track;

    public AudioThread(NesCore core) {
        this(core, true, new FrameAvailableSignal());
    }

    public AudioThread(NesCore core, boolean audible) {
        this(core, audible, new FrameAvailableSignal());
    }

    public AudioThread(NesCore core, boolean audible, FrameAvailableSignal frameAvailable) {
        super("FlyNES-Audio");
        this.core = core;
        this.audible = audible;
        this.frameAvailable = frameAvailable;
    }

    /**
     * Requests the loop to stop and unblocks any in-flight blocking write.
     * Safe from any thread, idempotent (double-call safe), and safe to call
     * before the track exists or after it has been released.
     */
    public void stopLoop() {
        running.set(false);
        AudioTrack t = track;
        if (t != null) {
            try {
                t.pause();
                t.flush(); // discards pending data -> unblocks write()
            } catch (IllegalStateException ignored) {
                // Track not initialized, never started, or already released —
                // nothing to pause/flush; the flag alone still stops the loop.
            }
        }
    }

    @Override
    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);

        // Claim the loop atomically BEFORE doing any work. The old code set
        // `running = true` only after AudioTrack construction (a few ms), so a
        // stopLoop() landing in that window had its `running = false` overwritten
        // and the thread looped forever — use-after-free once MainActivity
        // destroyed the native core. If a stop was already requested before we
        // started, bail out without creating a track at all.
        if (!running.compareAndSet(false, true))
            return;

        int minBytes = AudioTrack.getMinBufferSize(
                SAMPLE_RATE, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT);
        if (minBytes <= 0) {
            minBytes = SAMPLE_RATE * 2; // fallback (~1 s) if the HAL is uncooperative
        }
        // ~4 frames of 48000/60 Hz ≈ 3200 samples ≈ 6.4 KB, but never below the HAL minimum.
        int bufferBytes = Math.max(minBytes, 4 * SAMPLE_RATE * 2 / 60);

        track = new AudioTrack(AudioManager.STREAM_MUSIC, SAMPLE_RATE,
                AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT,
                bufferBytes, AudioTrack.MODE_STREAM);
        if (track.getState() != AudioTrack.STATE_INITIALIZED) {
            track.release();
            track = null;
            running.set(false);
            return;
        }

        // A stop may have arrived while the track was being built — honor it
        // instead of starting playback (belt-and-braces on top of the flag claim).
        if (!running.get()) {
            track.release();
            track = null;
            return;
        }

        if (!audible) track.setVolume(0f);
        track.play();

        AudioPump pump = new AudioPump(new AudioPump.Core() {
            @Override public FrameStepResult runFrameStep() { return core.runFrameStep(); }
            @Override public ByteBuffer audioBuffer() { return core.audioBuffer(); }
        }, (data, bytes) -> track.write(data, bytes, AudioTrack.WRITE_BLOCKING), frameAvailable);

        int idleIterations = 0;
        while (running.get()) {
            FrameStepResult step = pump.pumpOnce();
            if (step.audioSamples() > 0) {
                idleIterations = 0;
            } else if (step.failed()) {
                break;
            } else if (++idleIterations >= MAX_IDLE_ITERATIONS) {
                // Persistent no-output (e.g. ROM not loaded / dead core): stop
                // spinning so we don't peg a core. The lifecycle tolerates a
                // dead audio thread (join() returns immediately).
                break;
            }
        }

        try {
            track.stop();
        } catch (IllegalStateException ignored) {
            // already stopped/paused — nothing to do
        }
        track.release();
        track = null;
    }
}
