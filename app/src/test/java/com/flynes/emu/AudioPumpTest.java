package com.flynes.emu;

import static org.junit.Assert.assertEquals;

import java.nio.ByteBuffer;

import com.flynes.emu.video.FrameAvailableSignal;
import com.flynes.emu.video.FrameStepResult;
import com.flynes.emu.video.quality.SourceTiming;

import org.junit.Test;

public final class AudioPumpTest {
    @Test
    public void advancesExactlyOneFrameAndCompletesPartialWrites() {
        FakeCore core = new FakeCore(4);
        FakeSink sink = new FakeSink(3);
        FrameAvailableSignal signal = new FrameAvailableSignal();
        long[] signaled = {-1L};
        signal.addListener(sequence -> signaled[0] = sequence);
        AudioPump pump = new AudioPump(core, sink, signal);

        assertEquals(4, pump.pumpOnce().audioSamples());
        assertEquals(1, core.calls);
        assertEquals(8, sink.totalBytes);
        assertEquals(2, sink.calls);
        assertEquals(1L, signaled[0]);
    }

    @Test
    public void propagatesCoreAndSinkFailuresWithoutSpinning() {
        FakeCore failedCore = new FakeCore(-3);
        assertEquals(-3, new AudioPump(failedCore, new FakeSink(8)).pumpOnce().errorCode());

        FakeCore core = new FakeCore(4);
        assertEquals(-6, new AudioPump(core, (data, bytes) -> -6).pumpOnce().errorCode());
        assertEquals(1, core.calls);
    }

    private static final class FakeCore implements AudioPump.Core {
        final int samples;
        final ByteBuffer buffer = ByteBuffer.allocateDirect(32);
        int calls;

        FakeCore(int samples) { this.samples = samples; }

        @Override public FrameStepResult runFrameStep() {
            calls++;
            return samples < 0 ? FrameStepResult.error(samples)
                    : new FrameStepResult(1, samples, calls, SourceTiming.NTSC_60_0988);
        }

        @Override public ByteBuffer audioBuffer() { return buffer; }
    }

    private static final class FakeSink implements AudioPump.Sink {
        final int firstWriteBytes;
        int calls;
        int totalBytes;

        FakeSink(int firstWriteBytes) { this.firstWriteBytes = firstWriteBytes; }

        @Override public int write(ByteBuffer data, int bytes) {
            calls++;
            int written = calls == 1 ? Math.min(firstWriteBytes, bytes) : bytes;
            data.position(data.position() + written);
            totalBytes += written;
            return written;
        }
    }
}
