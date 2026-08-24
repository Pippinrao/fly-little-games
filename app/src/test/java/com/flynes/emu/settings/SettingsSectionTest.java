package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.Arrays;

public final class SettingsSectionTest {
    @Test public void frozenMasterDetailOrderMapsEveryCategoryToOneRoot() {
        assertEquals(Arrays.asList(
                SettingsSection.DISPLAY, SettingsSection.CONTROLS, SettingsSection.AUDIO,
                SettingsSection.GAME_LANGUAGE, SettingsSection.ABOUT),
                Arrays.asList(SettingsSection.values()));
        for (SettingsSection section : SettingsSection.values()) {
            assertFalse(section.rootKey().isEmpty());
            assertEquals(section, SettingsSection.fromRootKey(section.rootKey()));
        }
    }

    @Test public void displayStatusNeverCallsRepeatedFramesNativeGameFrames() {
        DisplayStatusText status = new DisplayStatusText(120f, 119.88f, "");
        String text = status.asText();
        assertTrue(text.contains("120"));
        assertTrue(text.contains("119.88"));
        assertFalse(text.toLowerCase().contains("game fps"));
        assertFalse(text.toLowerCase().contains("native frames"));
    }

    @Test public void displayFallbackIsStableAndExplicit() {
        DisplayStatusText status = new DisplayStatusText(120f, 60f, "MODE_UNAVAILABLE");
        assertEquals("Requested 120 Hz · Actual 60 Hz · Fallback MODE_UNAVAILABLE", status.asText());
    }
}
