package com.flynes.emu.video;

import static org.junit.Assert.assertEquals;

import com.flynes.emu.video.platform.DisplayPlatformFacade;

import org.junit.Test;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

public final class DisplayRequestLifecycleTest {
    @Test public void fixed120ClearsOldVoteThenSelectsModeThenQueuesNativeSourceVote() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);

        DisplayModeController.ApplyResult result = DisplayModeController.apply(
                platform, target, RefreshMode.HZ_120, 60.0988f, 9L);

        assertEquals(DisplayModeController.ApplyResult.APPLIED, result);
        assertEquals(Arrays.asList("native.clear:9", "window.mode:3",
                "native.request:9:60099"), commands);
    }

    @Test public void followSystemAndPauseClearNativeBeforeClearingWindowPreference() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);

        DisplayModeController.followSystem(platform, target, 8L);
        DisplayModeController.clear(platform, target, 8L);

        assertEquals(Arrays.asList("native.clear:8", "window.mode:0",
                "native.clear:8", "window.mode:0"), commands);
    }

    @Test public void palUsesCoreConfirmed50FpsAndNeverSelectsWrongResolution() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);

        DisplayModeController.apply(platform, target, RefreshMode.HZ_120, 50f, 4L);

        assertEquals(Arrays.asList("native.clear:4", "window.mode:3",
                "native.request:4:50000"), commands);
    }

    @Test public void failedNativeClearLeavesExistingWindowModeUntouched() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);
        target.clearSucceeds = false;

        assertEquals(DisplayModeController.ApplyResult.FAILED,
                DisplayModeController.apply(platform, target,
                        RefreshMode.HZ_120, 60.0988f, 9L));
        assertEquals(Arrays.asList("native.clear:9"), commands);

        commands.clear();
        assertEquals(DisplayModeController.ApplyResult.FAILED,
                DisplayModeController.followSystem(platform, target, 9L));
        assertEquals(Arrays.asList("native.clear:9"), commands);
    }

    @Test public void failedNativeVoteWithConfirmedClearFallsBackToSystem() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);
        target.requestSucceeds = false;

        assertEquals(DisplayModeController.ApplyResult.FALLBACK_AUTO,
                DisplayModeController.apply(platform, target,
                        RefreshMode.HZ_120, 60.0988f, 9L));
        assertEquals(Arrays.asList("native.clear:9", "window.mode:3",
                "native.request:9:60099", "native.clear:9", "window.mode:0"), commands);
    }

    @Test public void failedRollbackClearRetainsWindowModeUntilNativeIsConfirmedClear() {
        List<String> commands = new ArrayList<>();
        FakePlatform platform = new FakePlatform(commands);
        FakeFrameRateTarget target = new FakeFrameRateTarget(commands);
        target.requestSucceeds = false;
        target.secondClearSucceeds = false;

        assertEquals(DisplayModeController.ApplyResult.FAILED,
                DisplayModeController.apply(platform, target,
                        RefreshMode.HZ_120, 60.0988f, 11L));
        assertEquals(Arrays.asList("native.clear:11", "window.mode:3",
                "native.request:11:60099", "native.clear:11"), commands);
    }

    private static final class FakePlatform implements DisplayPlatformFacade {
        private final List<String> commands;
        FakePlatform(List<String> commands) { this.commands = commands; }
        @Override public Mode currentMode() { return new Mode(1, 2340, 1080, 60f); }
        @Override public List<Mode> supportedModes() {
            return Arrays.asList(new Mode(1, 2340, 1080, 60f),
                    new Mode(2, 1920, 1080, 120f),
                    new Mode(3, 2340, 1080, 120f));
        }
        @Override public void setPreferredDisplayModeId(int modeId) {
            commands.add("window.mode:" + modeId);
        }
    }

    private static final class FakeFrameRateTarget implements FrameRateRequestTarget {
        private final List<String> commands;
        private boolean clearSucceeds = true;
        private boolean requestSucceeds = true;
        private boolean secondClearSucceeds = true;
        private int clearCalls;
        FakeFrameRateTarget(List<String> commands) { this.commands = commands; }
        @Override public boolean requestFrameRate(long epoch, float sourceFps) {
            commands.add("native.request:" + epoch + ':' + Math.round(sourceFps * 1_000f));
            return requestSucceeds;
        }
        @Override public boolean clearFrameRate(long epoch) {
            commands.add("native.clear:" + epoch);
            clearCalls++;
            return clearSucceeds && (clearCalls != 2 || secondClearSucceeds);
        }
    }
}
