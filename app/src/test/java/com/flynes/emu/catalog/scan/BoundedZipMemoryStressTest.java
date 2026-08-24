package com.flynes.emu.catalog.scan;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;

public final class BoundedZipMemoryStressTest {
    @Test(timeout = 30_000)
    public void scannerAndLoaderFitDefaultLimitsIn64MiBFork() throws Exception {
        String executable = new File(
                System.getProperty("java.home"), "bin" + File.separator + "java").getPath();
        Process process = new ProcessBuilder(
                executable,
                "-Xmx64m",
                "-cp",
                System.getProperty("java.class.path"),
                BoundedZipMemoryStressMain.class.getName())
                .redirectErrorStream(true)
                .start();
        ByteArrayOutputStream output = new ByteArrayOutputStream();
        try (InputStream input = process.getInputStream()) {
            byte[] buffer = new byte[4096];
            int count;
            while ((count = input.read(buffer)) >= 0) {
                output.write(buffer, 0, count);
            }
        }
        assertTrue("stress child timed out", process.waitFor(20, TimeUnit.SECONDS));
        String text = new String(output.toByteArray(), StandardCharsets.UTF_8);
        assertEquals(text, 0, process.exitValue());
        assertTrue(text, text.contains("STRESS_OK"));
    }
}
