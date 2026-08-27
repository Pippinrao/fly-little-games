package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Queue;
import java.util.concurrent.Executor;

public final class FrameDispatchExecutorTest {
    @Test public void nativePolicyCoalescesToLatestAndReportsSkippedSequences() {
        QueuedExecutor executor = new QueuedExecutor();
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = new FrameDispatchExecutor(executor, delivered::add);

        dispatch.offer(10L);
        dispatch.offer(11L);
        dispatch.offer(12L);
        executor.runNext();

        assertEquals(Arrays.asList(12L), delivered);
        assertEquals(2L, dispatch.skippedSequences());
    }

    @Test public void closeDropsPendingWorkAndOldOrDuplicateSequences() {
        QueuedExecutor executor = new QueuedExecutor();
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = new FrameDispatchExecutor(executor, delivered::add);
        dispatch.offer(3L);
        dispatch.close();
        executor.runNext();
        dispatch.offer(4L);
        assertEquals(0, delivered.size());
    }

    @Test public void motionPolicyPreservesEveryAdjacentSequenceInOrder() {
        QueuedExecutor executor = new QueuedExecutor();
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = FrameDispatchExecutor.motionLossless(
                executor, delivered::add, 4);

        dispatch.offer(20L);
        dispatch.offer(21L);
        dispatch.offer(22L);
        while (!executor.tasks.isEmpty()) executor.runNext();

        assertEquals(Arrays.asList(20L, 21L, 22L), delivered);
        assertFalse(dispatch.failed());
        assertEquals(0L, dispatch.skippedSequences());
    }

    @Test public void motionPolicyFailsClosedOnSequenceGap() {
        QueuedExecutor executor = new QueuedExecutor();
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = FrameDispatchExecutor.motionLossless(
                executor, delivered::add, 4);
        dispatch.offer(5L);
        dispatch.offer(7L);
        while (!executor.tasks.isEmpty()) executor.runNext();

        assertTrue(dispatch.failed());
        assertEquals(FrameDispatchExecutor.Failure.SOURCE_SEQUENCE_GAP, dispatch.failure());
        assertEquals(0, delivered.size());
    }

    @Test public void motionPolicyFailsClosedBeforeOverwritingBoundedRing() {
        QueuedExecutor executor = new QueuedExecutor();
        FrameDispatchExecutor dispatch = FrameDispatchExecutor.motionLossless(
                executor, sequence -> {}, 2);
        dispatch.offer(1L);
        dispatch.offer(2L);
        dispatch.offer(3L);

        assertTrue(dispatch.failed());
        assertEquals(FrameDispatchExecutor.Failure.STAGING_OVERFLOW, dispatch.failure());
        while (!executor.tasks.isEmpty()) executor.runNext();
        assertEquals(0, dispatch.queuedSequences());
    }

    @Test public void productionMotionCaptureCompletesBeforeOfferReturns() {
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = FrameDispatchExecutor.motionCaptureSynchronous(
                delivered::add, 3);

        dispatch.offer(41L);
        assertEquals(Arrays.asList(41L), delivered);
        assertEquals(0, dispatch.queuedSequences());
        dispatch.offer(42L);
        assertEquals(Arrays.asList(41L, 42L), delivered);
        assertFalse(dispatch.failed());
    }

    @Test public void productionMotionFailureNotifiesAndCanBeReprimed() {
        List<FrameDispatchExecutor.Failure> failures = new ArrayList<>();
        List<Long> delivered = new ArrayList<>();
        FrameDispatchExecutor dispatch = FrameDispatchExecutor.motionCaptureSynchronous(
                delivered::add, 3, failures::add);
        dispatch.offer(1L);
        dispatch.offer(3L);
        assertEquals(Arrays.asList(FrameDispatchExecutor.Failure.SOURCE_SEQUENCE_GAP), failures);
        dispatch.resetMotion();
        dispatch.offer(10L);
        dispatch.offer(11L);
        assertEquals(Arrays.asList(1L, 10L, 11L), delivered);
        assertFalse(dispatch.failed());
    }

    private static final class QueuedExecutor implements Executor {
        final Queue<Runnable> tasks = new ArrayDeque<>();
        @Override public void execute(Runnable command) { tasks.add(command); }
        void runNext() { tasks.remove().run(); }
    }
}
