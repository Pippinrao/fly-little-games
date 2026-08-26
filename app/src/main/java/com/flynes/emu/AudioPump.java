package com.flynes.emu;

import java.nio.ByteBuffer;

import com.flynes.emu.video.FrameAvailableSignal;
import com.flynes.emu.video.FrameStepResult;

/** Executes one emulated frame and drains all of its PCM bytes to the sink. */
public final class AudioPump {
    public interface Core {
        FrameStepResult runFrameStep();
        ByteBuffer audioBuffer();
    }

    public interface Sink {
        int write(ByteBuffer data, int bytes);
    }

    private static final int ERROR_INVALID_AUDIO = -4;
    private static final int ERROR_WRITE_STALLED = -203;

    private final Core core;
    private final Sink sink;
    private final FrameAvailableSignal frameAvailable;

    public AudioPump(Core core, Sink sink) {
        this(core, sink, new FrameAvailableSignal());
    }

    public AudioPump(Core core, Sink sink, FrameAvailableSignal frameAvailable) {
        this.core = core;
        this.sink = sink;
        this.frameAvailable = frameAvailable;
    }

    public FrameStepResult pumpOnce() {
        FrameStepResult step = core.runFrameStep();
        if (step.failed()) return step;
        if (step.framesRun() > 0) frameAvailable.signal(step.sequence());
        int samples = step.audioSamples();
        if (samples <= 0) return step;

        ByteBuffer audio = core.audioBuffer().duplicate();
        long byteCount = (long) samples * 2L;
        if (byteCount > audio.capacity()) return FrameStepResult.error(ERROR_INVALID_AUDIO);
        audio.clear();
        audio.limit((int) byteCount);

        while (audio.hasRemaining()) {
            int start = audio.position();
            int written = sink.write(audio, audio.remaining());
            if (written < 0) return FrameStepResult.error(written);
            if (written == 0 || written > audio.limit() - start)
                return FrameStepResult.error(ERROR_WRITE_STALLED);
            int expectedPosition = start + written;
            if (audio.position() != expectedPosition) audio.position(expectedPosition);
        }
        return step;
    }
}
