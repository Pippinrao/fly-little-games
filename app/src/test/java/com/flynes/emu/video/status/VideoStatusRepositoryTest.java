package com.flynes.emu.video.status;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNull;

import com.flynes.emu.video.power.ThermalBand;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;

import org.junit.Test;

import java.util.Collections;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicReference;

public final class VideoStatusRepositoryTest {
    @Test public void observerGetsCurrentAndFutureValuesUntilClosed() {
        VideoStatusRepository repository = new VideoStatusRepository();
        VideoRuntimeStatus first = status(10L);
        VideoRuntimeStatus second = status(20L);
        repository.publish(first);
        AtomicReference<VideoRuntimeStatus> observed = new AtomicReference<>();
        AtomicInteger count = new AtomicInteger();

        VideoStatusRepository.Subscription subscription = repository.observe(Runnable::run,
                value -> { observed.set(value); count.incrementAndGet(); });
        assertEquals(first, observed.get());
        repository.publish(second);
        assertEquals(second, observed.get());
        assertEquals(2, count.get());

        subscription.close();
        repository.publish(first);
        assertEquals(2, count.get());
    }

    @Test public void repositoryIsInProcessAndStartsWithoutInventedRuntimeState() {
        assertNull(new VideoStatusRepository().current());
    }

    private static VideoRuntimeStatus status(long capturedAt) {
        return new VideoRuntimeStatus(1L, 1L, null, null, SourceTiming.NTSC_60_0988,
                60.0988f, 0f, 0f, 0f, 0L, PhysicalRefreshPolicy.FOLLOW_SYSTEM,
                null, null, 0f, 0f, 0f, 0f, RuntimeTemporalState.IMMEDIATE_NATIVE,
                0, 0f, 0, 0, null, 0L, ThermalBand.NONE, StatusFreshness.FRESH,
                capturedAt, Collections.emptyList());
    }
}
