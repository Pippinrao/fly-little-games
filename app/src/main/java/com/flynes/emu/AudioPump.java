package com.flynes.emu;

import java.nio.ByteBuffer;

/** Executes one emulated frame and drains all of its PCM bytes to the sink. */
public final class AudioPump {
    public interface Core {
        int runOneFrame();
        ByteBuffer audioBuffer();
    }

    public interface Sink {
        int write(ByteBuffer data, int bytes);
    }

    private static final int ERROR_INVALID_AUDIO = -4;
    private static final int ERROR_WRITE_STALLED = -203;

    private final Core core;
    private final Sink sink;

    public AudioPump(Core core, Sink sink) {
        this.core = core;
        this.sink = sink;
    }

    /** @return samples produced, zero for idle, or a negative core/sink error. */
    public int pumpOnce() {
        int samples = core.runOneFrame();
        if (samples <= 0) return samples;

        ByteBuffer audio = core.audioBuffer().duplicate();
        long byteCount = (long) samples * 2L;
        if (byteCount > audio.capacity()) return ERROR_INVALID_AUDIO;
        audio.clear();
        audio.limit((int) byteCount);

        while (audio.hasRemaining()) {
            int start = audio.position();
            int written = sink.write(audio, audio.remaining());
            if (written < 0) return written;
            if (written == 0 || written > audio.limit() - start) return ERROR_WRITE_STALLED;
            int expectedPosition = start + written;
            if (audio.position() != expectedPosition) audio.position(expectedPosition);
        }
        return samples;
    }
}
