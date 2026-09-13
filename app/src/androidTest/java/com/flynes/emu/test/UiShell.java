package com.flynes.emu.test;

import android.app.UiAutomation;
import android.os.ParcelFileDescriptor;

import androidx.test.platform.app.InstrumentationRegistry;

import java.io.FileInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;

/**
 * One UiAutomation shell pipe shared by instrumented tests.
 *
 * The framework can reclaim the connection between instrumented runs, and both
 * a half-drained pipe and a reconnect-per-call wedge the registration, so the
 * helper drains to EOF, closes the descriptor, and recovers exactly once from
 * a dead connection instead of retrying indefinitely.
 */
public final class UiShell {
    private static volatile UiAutomation automation;

    private UiShell() {}

    public static String run(String command) {
        try {
            return drain(current().executeShellCommand(command));
        } catch (IllegalStateException | IOException wedged) {
            // fall through to the single bounded recovery
        }
        automation = null;
        UiAutomation recovered = InstrumentationRegistry.getInstrumentation()
                .getUiAutomation(UiAutomation.FLAG_DONT_SUPPRESS_ACCESSIBILITY_SERVICES);
        try {
            String output = drain(recovered.executeShellCommand(command));
            automation = recovered;
            return output;
        } catch (IOException failure) {
            throw new IllegalStateException("UiAutomation shell recovery failed: " + command, failure);
        }
    }

    private static UiAutomation current() {
        UiAutomation instance = automation;
        if (instance == null) {
            synchronized (UiShell.class) {
                if (automation == null) {
                    automation = InstrumentationRegistry.getInstrumentation().getUiAutomation();
                }
                instance = automation;
            }
        }
        return instance;
    }

    private static String drain(ParcelFileDescriptor descriptor) throws IOException {
        try (ParcelFileDescriptor owned = descriptor;
             FileInputStream input = new FileInputStream(owned.getFileDescriptor())) {
            StringBuilder output = new StringBuilder();
            byte[] buffer = new byte[4096];
            int read;
            while ((read = input.read(buffer)) != -1) {
                output.append(new String(buffer, 0, read, StandardCharsets.UTF_8));
            }
            return output.toString();
        }
    }
}
