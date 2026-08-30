package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.graphics.SurfaceTexture;
import android.view.Surface;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.FramePublisher;
import com.flynes.emu.video.NativePresenterStats;
import com.flynes.emu.video.NativeVideoPresenter;
import com.flynes.emu.video.PublishedFrame;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;

@RunWith(AndroidJUnit4.class)
public final class MotionShadowPresenterTest {
    static { System.loadLibrary("nescore"); }

    @Test public void shadowComputesAdjacentPairsWithoutPresentingMidpoints() throws Exception {
        SequencedSource source = new SequencedSource();
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(source), ApplicationProvider.getApplicationContext());
        SurfaceTexture texture = new SurfaceTexture(0);
        texture.setDefaultBufferSize(256, 240);
        Surface surface = new Surface(texture);
        try {
            assertTrue(presenter.surfaceCreated(surface, 1L));
            await(presenter, stats -> stats.pacingOwner()
                    == NativePresenterStats.PACING_OWNER_NATIVE);
            presenter.setActive(false);
            long holdId = presenter.exitMotion(1L, false);
            assertTrue(holdId > 0L);
            await(presenter, stats -> stats.lastTransitionId() == holdId
                    && stats.temporalState() == NativePresenterStats.TEMPORAL_BUFFERED_HOLD);

            long shadowId = presenter.beginMotionShadowForTesting(1L, 60.0988f);
            assertTrue(shadowId > holdId);
            await(presenter, stats -> stats.lastTransitionId() == shadowId
                    && stats.temporalState() == NativePresenterStats.TEMPORAL_PRIMING_SHADOW);
            presenter.setActive(true);

            for (long sequence = 1L; sequence <= 4L; sequence++) {
                source.sequence = sequence;
                presenter.onFrameAvailable(sequence);
            }
            NativePresenterStats stats = await(presenter, value -> value.shadowPairCount() >= 3L);
            assertEquals(0L, stats.interpolatedSlots());
            assertTrue(stats.submittedFrames() >= 3L);
            assertTrue(stats.shadowUnsafePpmSum() >= 0L);
            assertTrue(stats.shadowPeakUnsafePpm() >= 0L);
            if (stats.shadowGpuTimingStatus() == NativePresenterStats.GPU_TIMING_VALID) {
                assertTrue(stats.shadowLastGpuDurationNs() > 0L);
            } else {
                assertEquals(-1L, stats.shadowLastGpuDurationNs());
            }
        } finally {
            presenter.setActive(false);
            presenter.surfaceDestroyed(1L);
            presenter.close();
            surface.release();
            texture.release();
        }
    }

    @Test public void queuedHoldCanUpgradeToDrainWithExactTransitionGeneration()
            throws Exception {
        SequencedSource source = new SequencedSource();
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(source), ApplicationProvider.getApplicationContext());
        SurfaceTexture texture = new SurfaceTexture(0);
        Surface surface = new Surface(texture);
        try {
            assertTrue(presenter.surfaceCreated(surface, 1L));
            await(presenter, stats -> stats.pacingOwner()
                    == NativePresenterStats.PACING_OWNER_NATIVE
                    && stats.surfaceEpoch() == 1L && stats.lastTransitionId() > 0L);
            presenter.setActive(false);
            long initialHoldId = presenter.exitMotion(1L, false);
            await(presenter, stats -> stats.lastTransitionId() == initialHoldId
                    && stats.temporalState() == NativePresenterStats.TEMPORAL_BUFFERED_HOLD);
            long shadowId = presenter.beginMotionShadowForTesting(1L, 60.0988f);
            await(presenter, stats -> stats.lastTransitionId() == shadowId
                    && stats.temporalState() == NativePresenterStats.TEMPORAL_PRIMING_SHADOW);
            long holdId = presenter.exitMotion(1L, false);
            long drainId = presenter.exitMotion(1L, true);
            assertTrue(holdId > 0L);
            assertTrue(drainId >= holdId);
            NativePresenterStats drained = await(presenter,
                    stats -> stats.lastTransitionId() == drainId
                            && stats.temporalState()
                            == NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE);
            assertEquals(NativePresenterStats.PACING_OWNER_NATIVE, drained.pacingOwner());
        } finally {
            presenter.surfaceDestroyed(1L);
            presenter.close();
            surface.release();
            texture.release();
        }
    }

    @Test public void shadowSurfaceLossRejectsOldEpochAndRecreatesNativeOwner()
            throws Exception {
        SequencedSource source = new SequencedSource();
        NativeVideoPresenter presenter = new NativeVideoPresenter(
                new FramePublisher(source), ApplicationProvider.getApplicationContext());
        SurfaceTexture firstTexture = new SurfaceTexture(0);
        Surface first = new Surface(firstTexture);
        SurfaceTexture secondTexture = new SurfaceTexture(0);
        Surface second = new Surface(secondTexture);
        try {
            assertTrue(presenter.surfaceCreated(first, 1L));
            await(presenter, stats -> stats.pacingOwner()
                    == NativePresenterStats.PACING_OWNER_NATIVE
                    && stats.surfaceEpoch() == 1L && stats.lastTransitionId() > 0L);
            presenter.setActive(false);
            long holdId = presenter.exitMotion(1L, false);
            assertTrue(holdId > 0L);
            await(presenter, stats -> stats.lastTransitionId() == holdId);
            long shadowId = presenter.beginMotionShadowForTesting(1L, 60.0988f);
            await(presenter, stats -> stats.lastTransitionId() == shadowId
                    && stats.temporalState() == NativePresenterStats.TEMPORAL_PRIMING_SHADOW);

            assertTrue(presenter.surfaceDestroyed(1L));
            NativePresenterStats suspended = presenter.stats();
            assertEquals(NativePresenterStats.PACING_OWNER_NONE, suspended.pacingOwner());
            assertEquals(NativePresenterStats.TEMPORAL_SURFACE_SUSPENDED_HOLD,
                    suspended.temporalState());
            assertTrue(!presenter.surfaceCreated(second, 1L));
            assertTrue(presenter.surfaceCreated(second, 2L));
            NativePresenterStats recreated = await(presenter,
                    stats -> stats.surfaceEpoch() == 2L
                            && stats.pacingOwner() == NativePresenterStats.PACING_OWNER_NATIVE);
            assertEquals(NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE,
                    recreated.temporalState());
        } finally {
            presenter.setActive(false);
            presenter.surfaceDestroyed(2L);
            presenter.close();
            first.release();
            second.release();
            firstTexture.release();
            secondTexture.release();
        }
    }

    private static NativePresenterStats await(NativeVideoPresenter presenter,
                                               Condition condition) throws Exception {
        long deadline = System.currentTimeMillis() + 5_000L;
        NativePresenterStats value = presenter.stats();
        while (!condition.matches(value) && System.currentTimeMillis() < deadline) {
            Thread.sleep(10L);
            value = presenter.stats();
        }
        assertTrue("condition not reached; stats=" + value, condition.matches(value));
        return value;
    }

    private interface Condition { boolean matches(NativePresenterStats stats); }

    private static final class SequencedSource implements FramePublisher.Source {
        volatile long sequence;

        @Override public PublishedFrame copyLatest() {
            ByteBuffer pixels = ByteBuffer.allocateDirect(256 * 240 * 2)
                    .order(ByteOrder.nativeOrder());
            for (int i = 0; i < 256 * 240; i++) {
                int x = i % 256;
                int moving = (int) (sequence % 16L);
                pixels.putShort((short) (x >= 40 + moving && x < 48 + moving
                        ? 0xffff : 0x0000));
            }
            pixels.flip();
            return new PublishedFrame(sequence, 256, 240, 512,
                    PublishedFrame.Format.RGB565, pixels, true);
        }
    }
}
