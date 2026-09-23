package com.flynes.emu;

import android.app.Application;

import com.flynes.emu.catalog.android.AndroidCatalogRuntime;
import com.flynes.emu.settings.ControlLayoutRepository;
import com.flynes.emu.settings.SettingsRepository;

/** Process-scoped owner for the catalog and exact-launch pipeline. */
public final class FlyNesApplication extends Application {
    private AndroidCatalogRuntime catalogRuntime;
    private AndroidGameLaunchService gameLaunchService;
    private NearbyAvailability<NearbySessionOwner> nearbyAvailability;
    private NearbySession nearbySession;
    private NearbyMvpOwner nearbyMvpOwner;

    @Override public void onCreate() {
        super.onCreate();
        catalogRuntime = new AndroidCatalogRuntime(this);
        catalogRuntime.start();
        gameLaunchService = new AndroidGameLaunchService(catalogRuntime);
        nearbyAvailability = new NearbyAvailability<>(NearbyAvailability.fromIllegalState(
                () -> NearbySessionOwner.create(catalogRuntime), "nearby_blocked_session_read"));
        nearbyMvpOwner = new NearbyMvpOwner();
    }

    public AndroidCatalogRuntime catalogRuntime() { return catalogRuntime; }
    public AndroidGameLaunchService gameLaunchService() { return gameLaunchService; }

    /** Creates the process-scoped V2 owner on first nearby entry. Catalog never calls this. */
    public NearbyAvailability.Status ensureNearby() {
        NearbyAvailability.Status status = nearbyAvailability.ensure();
        if (status.ready() && nearbySession == null) {
            nearbySession = NearbySession.attach(nearbyAvailability.ownerOrNull());
        }
        return status;
    }

    public NearbySession nearbySession() {
        ensureNearby();
        return nearbySession != null ? nearbySession : NearbySession.unavailable();
    }

    public NearbySessionOwner nearbySessionOwner() {
        return nearbyAvailability == null ? null : nearbyAvailability.ownerOrNull();
    }

    public NearbyMvpOwner nearbyMvpOwner() { return nearbyMvpOwner; }

    public NearbyAvailability.Status nearbyStatus() {
        if (nearbyAvailability == null) {
            return NearbyAvailability.Status.unavailable("nearby_blocked_session_read");
        }
        if (nearbyAvailability.ready()) return NearbyAvailability.Status.ok();
        return NearbyAvailability.Status.unavailable(nearbyAvailability.reasonKey());
    }
    public SettingsRepository settingsRepository() {
        return catalogRuntime == null ? null : catalogRuntime.settingsRepository();
    }

    public ControlLayoutRepository.Backend controlLayoutBackend() {
        return catalogRuntime == null ? null : catalogRuntime.controlLayoutBackend();
    }
}
