package com.flynes.emu;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import com.flynes.emu.video.FramePublisher;
import com.flynes.emu.video.NativeVideoPresenter;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.assertEquals;

/** Exercises worker startup/shutdown without a Surface or optional graphics capabilities. */
@RunWith(AndroidJUnit4.class)
public final class NativePresenterInitializationTest {
    @Test public void repeatedConstructionNeverExposesUninitializedWorkerState() {
        System.loadLibrary("nescore");
        for (int i = 0; i < 2000; ++i) {
            try (var presenter = new NativeVideoPresenter(new FramePublisher(() -> null))) {
                assertEquals(0L, presenter.activeEpoch());
            }
        }
    }
}
