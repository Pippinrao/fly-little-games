package com.flynes.emu.video.audio;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

/** One-video-frame timestamped PCM delay with bounded, explicit transition removal. */
public final class TemporalAudioDelay {
    public interface Sink { int write(ByteBuffer data, int requestedBytes); }

    public static final class WriteResult {
        private final int bytesWritten;
        private final int errorCode;
        private final boolean insertedSilence;
        WriteResult(int bytesWritten, int errorCode, boolean insertedSilence) {
            this.bytesWritten = bytesWritten;
            this.errorCode = errorCode;
            this.insertedSilence = insertedSilence;
        }
        public int bytesWritten() { return bytesWritten; }
        public int errorCode() { return errorCode; }
        public boolean insertedSilence() { return insertedSilence; }
    }
    public record OutputSpan(long contentSequence, int startFrameOffset, int frameCount,
                             int boundaryUncertaintyFrames) { }

    private static final int ERROR_INVALID_PCM = -401;
    private static final int ERROR_WRITE_STALLED = -402;
    private final int channels;
    private final int delaySamples;
    private final int crossfadeSamples;
    private static final class Segment {
        final long timestampNs;
        final short[] samples;
        int offset;
        Segment(long timestampNs, short[] samples) {
            this.timestampNs = timestampNs;
            this.samples = samples;
        }
    }
    private final ArrayDeque<Segment> blocks = new ArrayDeque<>();
    private int queuedSampleCount;
    private short[] lastOutputTail;
    private boolean delayed = true;
    private boolean crossfadePending;
    private long newestTimestampNs = Long.MIN_VALUE;
    private long lastOutputTimestampNs = Long.MIN_VALUE;
    private long underrunFrames;
    private List<OutputSpan> lastOutputSpans = Collections.emptyList();

    public TemporalAudioDelay(int sampleRate, int channels, double sourceHz) {
        if (sampleRate <= 0 || channels <= 0 || !Double.isFinite(sourceHz) || sourceHz <= 0.0)
            throw new IllegalArgumentException("invalid PCM timing");
        this.channels = channels;
        this.delaySamples = Math.max(channels,
                (int) Math.round(sampleRate / sourceHz) * channels);
        this.crossfadeSamples = Math.max(channels,
                (int) Math.round(sampleRate * 0.008) * channels);
        this.lastOutputTail = new short[crossfadeSamples];
    }

    public synchronized WriteResult process(ByteBuffer pcm, int bytes, long timestampNs,
                                            Sink sink) {
        if (pcm == null || sink == null || bytes < 0 || bytes > pcm.remaining()
                || (bytes & 1) != 0 || (bytes / 2) % channels != 0 || timestampNs < 0L) {
            return new WriteResult(0, ERROR_INVALID_PCM, false);
        }
        ByteBuffer source = pcm.duplicate().order(ByteOrder.LITTLE_ENDIAN);
        source.limit(source.position() + bytes);
        short[] captured = new short[bytes / 2];
        for (int i = 0; i < captured.length; ++i) captured[i] = source.getShort();
        blocks.addLast(new Segment(timestampNs, captured));
        queuedSampleCount += captured.length;
        newestTimestampNs = timestampNs;

        final int requestedSamples = bytes / 2;
        final int available = delayed ? Math.max(0, queuedSampleCount - delaySamples)
                                      : queuedSampleCount;
        final int readable = Math.min(requestedSamples, available);
        final boolean applyCrossfade = crossfadePending;
        final List<OutputSpan> outputSpans = snapshotOutputSpans(readable, applyCrossfade);
        final long outputTimestampNs = readable > 0 && blocks.peekFirst() != null
                ? blocks.peekFirst().timestampNs : Long.MIN_VALUE;
        short[] outputSamples = new short[requestedSamples];
        peekSamples(outputSamples, readable);
        final boolean insertedSilence = readable < requestedSamples;
        if (insertedSilence) ++underrunFrames;
        if (applyCrossfade) {
            applyEqualPowerCrossfade(outputSamples);
        }

        ByteBuffer output = ByteBuffer.allocateDirect(bytes).order(ByteOrder.LITTLE_ENDIAN);
        for (short sample : outputSamples) output.putShort(sample);
        output.flip();
        int total = 0;
        int committedSourceSamples = 0;
        while (output.hasRemaining()) {
            int before = output.position();
            int written = sink.write(output, output.remaining());
            if (written < 0) return new WriteResult(total, written, insertedSilence);
            if (written == 0 || written > output.limit() - before)
                return new WriteResult(total, ERROR_WRITE_STALLED, insertedSilence);
            int expected = before + written;
            if (output.position() != expected) output.position(expected);
            total += written;
            int shouldCommit = Math.min(readable, total / 2);
            while (committedSourceSamples < shouldCommit) {
                popSample();
                ++committedSourceSamples;
            }
        }
        if (applyCrossfade) crossfadePending = false;
        rememberTail(outputSamples);
        lastOutputTimestampNs = outputTimestampNs;
        lastOutputSpans = outputSpans;
        return new WriteResult(total, 0, insertedSilence);
    }

    /** Removes the buffered ~one-frame block; the next output masks only its boundary. */
    public synchronized int removeOneFrameDelay() {
        if (queuedSampleCount < delaySamples) return 0;
        int removed = delaySamples;
        for (int i = 0; i < removed; ++i) popSample();
        delayed = false;
        crossfadePending = true;
        return removed;
    }

    public synchronized void enableOneFrameDelay() { delayed = true; }

    /** Atomic paused rollback used before any delayed audio has been released. */
    public synchronized void disableImmediately() {
        delayed = false;
        crossfadePending = false;
    }

    public synchronized void flush() {
        blocks.clear();
        queuedSampleCount = 0;
        newestTimestampNs = Long.MIN_VALUE;
        lastOutputTimestampNs = Long.MIN_VALUE;
        lastOutputSpans = Collections.emptyList();
        crossfadePending = false;
        lastOutputTail = new short[crossfadeSamples];
    }

    private void applyEqualPowerCrossfade(short[] output) {
        int count = Math.min(Math.min(crossfadeSamples, output.length), lastOutputTail.length);
        if (count <= 1) return;
        int oldStart = lastOutputTail.length - count;
        for (int i = 0; i < count; ++i) {
            double progress = (double) i / (double) (count - 1);
            double mixed = lastOutputTail[oldStart + i] * Math.cos(progress * Math.PI * 0.5)
                    + output[i] * Math.sin(progress * Math.PI * 0.5);
            output[i] = (short) Math.max(Short.MIN_VALUE,
                    Math.min(Short.MAX_VALUE, Math.round(mixed)));
        }
    }

    private void rememberTail(short[] output) {
        short[] tail = new short[crossfadeSamples];
        int count = Math.min(output.length, crossfadeSamples);
        if (count > 0)
            System.arraycopy(output, output.length - count, tail, crossfadeSamples - count, count);
        lastOutputTail = tail;
    }

    private short popSample() {
        Segment block = blocks.peekFirst();
        if (block == null) throw new IllegalStateException("PCM accounting underflow");
        short value = block.samples[block.offset++];
        --queuedSampleCount;
        if (block.offset == block.samples.length) blocks.removeFirst();
        return value;
    }

    private void peekSamples(short[] destination, int count) {
        int written = 0;
        for (Segment block : blocks) {
            int available = block.samples.length - block.offset;
            int copy = Math.min(count - written, available);
            if (copy > 0) {
                System.arraycopy(block.samples, block.offset, destination, written, copy);
                written += copy;
            }
            if (written == count) return;
        }
        if (written != count) throw new IllegalStateException("PCM accounting underflow");
    }

    private List<OutputSpan> snapshotOutputSpans(int readableSamples,
                                                 boolean applyCrossfade) {
        if (readableSamples <= 0) return Collections.emptyList();
        ArrayList<OutputSpan> result = new ArrayList<>();
        int outputSamples = 0;
        for (Segment block : blocks) {
            int available = block.samples.length - block.offset;
            int copied = Math.min(readableSamples - outputSamples, available);
            if (copied > 0) {
                result.add(new OutputSpan(block.timestampNs, outputSamples / channels,
                        copied / channels, applyCrossfade && outputSamples == 0
                                ? crossfadeSamples / channels : 0));
                outputSamples += copied;
            }
            if (outputSamples == readableSamples) break;
        }
        if (outputSamples != readableSamples) {
            throw new IllegalStateException("PCM span accounting underflow");
        }
        return Collections.unmodifiableList(result);
    }

    public synchronized int queuedSamples() { return queuedSampleCount; }
    public synchronized long oldestTimestampNs() {
        Segment block = blocks.peekFirst();
        return block == null ? Long.MIN_VALUE : block.timestampNs;
    }
    public synchronized int crossfadeSamples() { return crossfadeSamples / channels; }
    public synchronized long newestTimestampNs() { return newestTimestampNs; }
    public synchronized long lastOutputTimestampNs() { return lastOutputTimestampNs; }
    public synchronized List<OutputSpan> lastOutputSpans() { return lastOutputSpans; }
    public synchronized long underrunFrames() { return underrunFrames; }
}
