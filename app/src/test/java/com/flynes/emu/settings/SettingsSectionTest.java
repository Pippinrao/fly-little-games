package com.flynes.emu.settings;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;

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
}
