package com.flynes.emu.video.audio;

import java.util.ArrayDeque;
import java.util.List;
import java.util.Optional;

/** Maps real AudioTrack playback positions to the start of each emulated PCM segment. */
public final class AudioMarkerTimeline {
    public record Match(long contentSequence, long presentationTimeNs,
                        long boundaryUncertaintyNs) { }
    private record Marker(long contentSequence, long startFramePosition,
                          int boundaryUncertaintyFrames) { }

    private final int sampleRate;
    private final ArrayDeque<Marker> markers = new ArrayDeque<>();

    public AudioMarkerTimeline(int sampleRate) {
        if (sampleRate <= 0) throw new IllegalArgumentException("positive sample rate required");
        this.sampleRate = sampleRate;
    }

    public void recordBlock(long blockStartFramePosition,
                            List<TemporalAudioDelay.OutputSpan> spans) {
        if (blockStartFramePosition < 0L || spans == null) {
            throw new IllegalArgumentException("valid block and spans required");
        }
        for (TemporalAudioDelay.OutputSpan span : spans) {
            if (span == null || span.contentSequence() < 0L || span.startFrameOffset() < 0
                    || span.frameCount() <= 0) continue;
            long start = Math.addExact(blockStartFramePosition, span.startFrameOffset());
            markers.addLast(new Marker(span.contentSequence(), start,
                    Math.max(1, span.boundaryUncertaintyFrames())));
        }
        while (markers.size() > 256) markers.removeFirst();
    }

    public Optional<Match> match(long playbackFramePosition, long playbackTimestampNs) {
        if (playbackFramePosition < 0L || playbackTimestampNs < 0L) return Optional.empty();
        Marker played = null;
        while (!markers.isEmpty()
                && markers.peekFirst().startFramePosition() <= playbackFramePosition) {
            played = markers.removeFirst();
        }
        if (played == null) return Optional.empty();
        try {
            long framesAfterStart = Math.subtractExact(playbackFramePosition,
                    played.startFramePosition());
            long elapsedNs = Math.multiplyExact(framesAfterStart, 1_000_000_000L) / sampleRate;
            long uncertaintyNs = (Math.multiplyExact(
                    played.boundaryUncertaintyFrames(), 1_000_000_000L)
                    + sampleRate - 1L) / sampleRate;
            return Optional.of(new Match(played.contentSequence(),
                    Math.subtractExact(playbackTimestampNs, elapsedNs),
                    uncertaintyNs));
        } catch (ArithmeticException overflow) {
            return Optional.empty();
        }
    }
}
