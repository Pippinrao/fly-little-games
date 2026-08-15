package com.flynes.emu;

import android.media.AudioFormat;
import android.media.AudioManager;
import android.media.AudioTrack;
import android.os.Process;

import java.nio.ByteBuffer;

/**
 * Audio-master-clock loop: the emulator never runs ahead of the audio sink.
 *
 * Each iteration runs 2 NES frames (~1600 samples at 48 kHz) into the core's
 * direct buffer, then does a BLOCKING AudioTrack.write(). The write blocks
 * until the audio HAL consumes enough buffer space, which paces emulation in
 * real time — no sleep, no busy-wait.
 */
public class AudioThread extends Thread {

    private static final int SAMPLE_RATE = 48000;

    private final NesCore core;
    private volatile boolean running = false;
    private AudioTrack track;

    public AudioThread(NesCore core) {
        super("FlyNES-Audio");
        this.core = core;
    }

    /** Requests the loop to stop and unblocks any in-flight blocking write. */
    public void stopLoop() {
        running = false;
        AudioTrack t = track;
        if (t != null) {
            t.pause();
            t.flush(); // discards pending data -> unblocks write()
        }
    }

    @Override
    public void run() {
        Process.setThreadPriority(Process.THREAD_PRIORITY_URGENT_AUDIO);

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
            running = false;
            return;
        }

        ByteBuffer audio = core.audioBuffer();
        track.play();
        running = true;

        while (running) {
            int samples = core.runFrames(2);
            if (samples > 0) {
                audio.position(0);
                audio.limit(samples * 2);
                // Blocking write paces the emulation loop to real-time audio.
                track.write(audio, samples * 2, AudioTrack.WRITE_BLOCKING);
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
