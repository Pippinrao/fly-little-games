package com.flynes.emu.video.audio;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayList;
import java.util.List;

public final class TemporalAudioDelayTest {
    @Test public void oneFrameDelayWritesSilenceThenPreviousFrameWithPartialWrites() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        CapturingSink sink = new CapturingSink(6);
        assertEquals(20, delay.process(pcm(1, 10), 20, 100L, sink).bytesWritten());
        assertArrayEquals(constantSamples((short) 0, 10), sink.takeSamples());
        assertEquals(1, delay.underrunFrames());

        assertEquals(20, delay.process(pcm(101, 10), 20, 200L, sink).bytesWritten());
        assertArrayEquals(samples(1, 10), sink.takeSamples());
        assertTrue(sink.calls >= 4);
        assertEquals(200L, delay.newestTimestampNs());
        assertEquals(200L, delay.oldestTimestampNs());
    }

    @Test public void flushDropsTimestampedPcmAndRestartsWithSilence() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        CapturingSink sink = new CapturingSink(64);
        delay.process(pcm(1, 10), 20, 100L, sink);
        sink.takeSamples();
        assertEquals(10, delay.queuedSamples());
        assertEquals(100L, delay.oldestTimestampNs());
        delay.flush();
        assertEquals(0, delay.queuedSamples());
        assertEquals(Long.MIN_VALUE, delay.newestTimestampNs());
        delay.process(pcm(50, 10), 20, 300L, sink);
        assertArrayEquals(constantSamples((short) 0, 10), sink.takeSamples());
    }

    @Test public void removalDropsOneFrameAndAppliesEightMillisecondEqualPowerCrossfade() {
        TemporalAudioDelay delay = new TemporalAudioDelay(1000, 1, 100.0);
        CapturingSink sink = new CapturingSink(128);
        delay.process(constantPcm((short) 1000, 10), 20, 100L, sink); // silence
        sink.takeSamples();
        delay.process(constantPcm((short) 2000, 10), 20, 200L, sink); // outputs 1000
        assertArrayEquals(constantSamples((short) 1000, 10), sink.takeSamples());

        assertEquals(10, delay.removeOneFrameDelay());
        delay.process(constantPcm((short) 3000, 10), 20, 300L, sink);
        short[] transition = sink.takeSamples();
        assertEquals(1000, transition[0]);
        assertTrue(transition[7] >= 2950 && transition[7] <= 3050);
        assertEquals(3000, transition[9]);
        assertEquals(8, delay.crossfadeSamples());
        assertEquals(8, delay.lastOutputSpans().get(0).boundaryUncertaintyFrames());
        assertEquals(0, delay.queuedSamples());
    }

    @Test public void sinkFailureStopsWithoutSpinning() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        TemporalAudioDelay.WriteResult result = delay.process(
                pcm(1, 10), 20, 1L, (data, bytes) -> -9);
        assertEquals(-9, result.errorCode());
    }

    @Test public void partialWriteFailureCommitsOnlyPlayedPcm() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        CapturingSink initial = new CapturingSink(64);
        delay.process(pcm(1, 10), 20, 100L, initial);
        initial.takeSamples();

        int[] calls = {0};
        TemporalAudioDelay.WriteResult failed = delay.process(pcm(101, 10), 20, 200L,
                (data, bytes) -> {
                    if (calls[0]++ == 0) {
                        data.position(data.position() + 4);
                        return 4;
                    }
                    return -9;
                });
        assertEquals(4, failed.bytesWritten());
        assertEquals(-9, failed.errorCode());
        assertEquals(18, delay.queuedSamples());

        CapturingSink recovery = new CapturingSink(64);
        delay.process(pcm(201, 10), 20, 300L, recovery);
        assertArrayEquals(samples(3, 8), java.util.Arrays.copyOfRange(
                recovery.takeSamples(), 0, 8));
    }

    @Test public void incompleteDelayRemovalIsRejectedWithoutChangingMode() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        assertEquals(0, delay.removeOneFrameDelay());
        CapturingSink sink = new CapturingSink(64);
        delay.process(pcm(1, 5), 10, 100L, sink);
        assertArrayEquals(constantSamples((short) 0, 5), sink.takeSamples());
        assertEquals(5, delay.queuedSamples());
    }

    @Test public void reportsEveryRealContentSpanAndExcludesInsertedSilence() {
        TemporalAudioDelay delay = new TemporalAudioDelay(600, 1, 60.0);
        CapturingSink sink = new CapturingSink(256);
        delay.process(pcm(1, 6), 12, 100L, sink);
        assertTrue(delay.lastOutputSpans().isEmpty());
        delay.process(pcm(20, 10), 20, 200L, sink);
        assertEquals(List.of(new TemporalAudioDelay.OutputSpan(100L, 0, 6, 0)),
                delay.lastOutputSpans());
        delay.process(pcm(40, 15), 30, 300L, sink);
        assertEquals(List.of(new TemporalAudioDelay.OutputSpan(200L, 0, 10, 0),
                        new TemporalAudioDelay.OutputSpan(300L, 10, 5, 0)),
                delay.lastOutputSpans());
    }

    private static ByteBuffer pcm(int first, int count) {
        ByteBuffer data = ByteBuffer.allocateDirect(count * 2).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < count; ++i) data.putShort((short) (first + i));
        data.flip();
        return data;
    }

    private static ByteBuffer constantPcm(short value, int count) {
        ByteBuffer data = ByteBuffer.allocateDirect(count * 2).order(ByteOrder.LITTLE_ENDIAN);
        for (int i = 0; i < count; ++i) data.putShort(value);
        data.flip();
        return data;
    }

    private static short[] samples(int first, int count) {
        short[] values = new short[count];
        for (int i = 0; i < count; ++i) values[i] = (short) (first + i);
        return values;
    }

    private static short[] constantSamples(short value, int count) {
        short[] values = new short[count];
        java.util.Arrays.fill(values, value);
        return values;
    }

    private static final class CapturingSink implements TemporalAudioDelay.Sink {
        final int maxWrite;
        final List<Byte> bytes = new ArrayList<>();
        int calls;
        CapturingSink(int maxWrite) { this.maxWrite = maxWrite; }
        @Override public int write(ByteBuffer data, int requestedBytes) {
            calls++;
            int count = Math.min(maxWrite, requestedBytes);
            for (int i = 0; i < count; ++i) bytes.add(data.get());
            return count;
        }
        short[] takeSamples() {
            byte[] raw = new byte[bytes.size()];
            for (int i = 0; i < raw.length; ++i) raw[i] = bytes.get(i);
            bytes.clear();
            ByteBuffer data = ByteBuffer.wrap(raw).order(ByteOrder.LITTLE_ENDIAN);
            short[] result = new short[raw.length / 2];
            for (int i = 0; i < result.length; ++i) result[i] = data.getShort();
            return result;
        }
    }
}
