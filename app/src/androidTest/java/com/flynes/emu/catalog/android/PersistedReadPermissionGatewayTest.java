package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;

import android.content.Intent;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class PersistedReadPermissionGatewayTest {
    @Test
    public void pickerRequestsOnlyReadPersistablePrefix() {
        Intent intent = PersistedReadPermissionGateway.pickerIntent();
        assertEquals(Intent.ACTION_OPEN_DOCUMENT_TREE, intent.getAction());
        assertEquals(Intent.FLAG_GRANT_READ_URI_PERMISSION
                        | Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION
                        | Intent.FLAG_GRANT_PREFIX_URI_PERMISSION,
                intent.getFlags());
        assertEquals(0, intent.getFlags() & Intent.FLAG_GRANT_WRITE_URI_PERMISSION);
    }
}
