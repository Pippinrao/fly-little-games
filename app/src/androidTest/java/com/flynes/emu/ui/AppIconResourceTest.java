package com.flynes.emu.ui;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertNotNull;

import android.content.Context;
import android.content.pm.ApplicationInfo;

import androidx.core.content.ContextCompat;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AppIconResourceTest {
    @Test
    public void manifestUsesFlynesLauncherIcons() throws Exception {
        Context context = ApplicationProvider.getApplicationContext();
        ApplicationInfo info = context.getPackageManager().getApplicationInfo(
                context.getPackageName(), 0);

        assertEquals(R.mipmap.ic_launcher, info.icon);
        assertNotNull(ContextCompat.getDrawable(context, R.mipmap.ic_launcher));
        assertNotNull(ContextCompat.getDrawable(context, R.mipmap.ic_launcher_round));
    }
}
