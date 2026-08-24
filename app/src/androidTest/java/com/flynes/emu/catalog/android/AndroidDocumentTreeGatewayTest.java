package com.flynes.emu.catalog.android;

import static org.junit.Assert.assertEquals;

import android.content.Context;

import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.catalog.RomSource;
import com.flynes.emu.catalog.source.SourceEnumerator;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AndroidDocumentTreeGatewayTest {
    @Test
    public void nonTreeLocatorBecomesFatalWithoutProviderWriteAccess() {
        Context context = ApplicationProvider.getApplicationContext();
        RomSource source = new RomSource(
                "source", RomSource.Type.SAF_TREE, "content://provider/not-a-tree",
                RomSource.PermissionState.GRANTED);
        SourceEnumerator.Result result = new SourceEnumerator(16, 20_000).enumerate(
                source, new AndroidDocumentTreeGateway(
                        context.getContentResolver(), source.uri()));
        assertEquals(SourceEnumerator.Completeness.FATAL, result.completeness());
        assertEquals(0, result.candidateCount());
    }
}
