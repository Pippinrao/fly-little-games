package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.util.LinkedHashMap;
import java.util.Map;

public final class AndroidRetryableMigrationLogTest {
    @Test
    public void failedAttemptKeepsOldDataEligibleUntilOneSuccess() {
        Map<String, String> backing = new LinkedHashMap<>();
        AndroidRetryableMigrationLog log = new AndroidRetryableMigrationLog(
                backing::get, backing::put);

        log.record(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA, false, "scan conflict");
        log.record(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4, false, "apply failed");

        assertFalse(log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertFalse(log.succeeded(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4));
        assertTrue(log.shouldRetry(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertTrue(log.shouldRetry(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4));
        assertEquals(2, log.attempts().size());
        assertEquals("scan conflict", log.attempts().get(0).detail());

        log.record(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA, true, "persisted flycat01");
        assertTrue(log.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertFalse(log.shouldRetry(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertTrue(log.shouldRetry(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4));

        AndroidRetryableMigrationLog reloaded = new AndroidRetryableMigrationLog(
                backing::get, backing::put);
        assertTrue(reloaded.succeeded(AndroidRetryableMigrationLog.Kind.CATALOG_FNCA));
        assertFalse(reloaded.succeeded(AndroidRetryableMigrationLog.Kind.SETTINGS_SCHEMA4));
        assertEquals(3, reloaded.attempts().size());
    }
}
