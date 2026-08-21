package com.flynes.emu.ui;

import static org.junit.Assert.assertTrue;

import android.content.Context;

import androidx.core.content.ContextCompat;
import androidx.core.graphics.ColorUtils;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class ThemeContrastTest {
    private final Context context = ApplicationProvider.getApplicationContext();

    @Test
    public void primaryTextMeetsWcagAaOnEveryDarkSurface() {
        int text = color(R.color.fly_on_surface);
        assertContrastAtLeast(text, color(R.color.fly_background), 4.5);
        assertContrastAtLeast(text, color(R.color.fly_surface), 4.5);
        assertContrastAtLeast(text, color(R.color.fly_surface_variant), 4.5);
    }

    @Test
    public void mutedTextAndAccentRemainLegible() {
        int surface = color(R.color.fly_surface);
        assertContrastAtLeast(color(R.color.fly_on_surface_muted), surface, 4.5);
        assertContrastAtLeast(color(R.color.fly_primary), surface, 3.0);
    }

    @Test
    public void primaryButtonLabelMeetsWcagAa() {
        assertContrastAtLeast(color(R.color.fly_on_primary), color(R.color.fly_primary), 4.5);
    }

    private int color(int resource) {
        return ContextCompat.getColor(context, resource);
    }

    private static void assertContrastAtLeast(int foreground, int background, double minimum) {
        double ratio = ColorUtils.calculateContrast(foreground, background);
        assertTrue("contrast " + ratio + " must be >= " + minimum, ratio >= minimum);
    }
}
