package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

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

    private static final class QueuedExecutor implements Executor {
        final Queue<Runnable> tasks = new ArrayDeque<>();
        @Override public void execute(Runnable command) { tasks.add(command); }
        void runNext() { tasks.remove().run(); }
    }
}
