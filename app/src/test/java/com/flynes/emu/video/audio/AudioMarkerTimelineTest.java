package com.flynes.emu.video.audio;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.List;

public final class AudioMarkerTimelineTest {
    @Test public void markerUsesSegmentStartInsteadOfOneFrameBlockEnd() {
        AudioMarkerTimeline timeline = new AudioMarkerTimeline(48_000);
        timeline.recordBlock(10_000L,
                List.of(new TemporalAudioDelay.OutputSpan(44L, 0, 800, 0)));

        AudioMarkerTimeline.Match match = timeline.match(10_000L, 2_000_000_000L)
                .orElseThrow();
        assertEquals(44L, match.contentSequence());
        assertEquals(2_000_000_000L, match.presentationTimeNs());
        assertEquals(20_834L, match.boundaryUncertaintyNs());
    }

    @Test public void crossingBlockKeepsDistinctSequenceStartMarkers() {
        AudioMarkerTimeline timeline = new AudioMarkerTimeline(1_000);
        timeline.recordBlock(1_000L, List.of(
                new TemporalAudioDelay.OutputSpan(10L, 0, 8, 0),
                new TemporalAudioDelay.OutputSpan(11L, 8, 2, 0)));

        AudioMarkerTimeline.Match match = timeline.match(1_009L, 5_000_000_000L)
                .orElseThrow();
        assertEquals(11L, match.contentSequence());
        assertEquals(4_999_000_000L, match.presentationTimeNs());
        assertTrue(match.boundaryUncertaintyNs() >= 1_000_000L);
    }
}
