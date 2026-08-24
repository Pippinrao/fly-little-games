package com.flynes.emu;

import android.app.Application;

import com.flynes.emu.catalog.android.AndroidCatalogRuntime;

/** Process-scoped owner for the catalog and exact-launch pipeline. */
public final class FlyNesApplication extends Application {
    private AndroidCatalogRuntime catalogRuntime;
    private AndroidGameLaunchService gameLaunchService;

    @Override public void onCreate() {
        super.onCreate();
        catalogRuntime = new AndroidCatalogRuntime(this);
        gameLaunchService = new AndroidGameLaunchService(catalogRuntime);
    }

    public AndroidCatalogRuntime catalogRuntime() { return catalogRuntime; }
    public AndroidGameLaunchService gameLaunchService() { return gameLaunchService; }
}
