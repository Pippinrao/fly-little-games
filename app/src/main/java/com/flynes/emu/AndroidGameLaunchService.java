package com.flynes.emu;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.flynes.emu.catalog.android.AndroidCatalogRuntime;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchCoordinator;
import com.flynes.emu.launch.LaunchResult;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/** The only Android UI entry into exact catalog launch. */
public final class AndroidGameLaunchService {
    private static final String TAG = "FlyNesLaunch";
    public interface Callback { void onComplete(LaunchResult result); }

    private final AndroidCatalogRuntime runtime;
    private final ExecutorService executor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "flynes-launch");
        thread.setDaemon(true);
        return thread;
    });
    private final Handler main = new Handler(Looper.getMainLooper());

    AndroidGameLaunchService(AndroidCatalogRuntime runtime) {
        this.runtime = runtime;
    }

    public void launch(String variantId, Callback callback) {
        executor.execute(() -> {
            LaunchCoordinator coordinator = new LaunchCoordinator(
                    runtime.gameCatalog(),
                    new ExactRomLoader(runtime.streamOpener()),
                    PendingGameLaunch::stage,
                    request -> {
                        try {
                            if (!runtime.recordSuccessfulLaunch(request.canonicalGameId()).get()) {
                                throw new IllegalStateException("catalog history target disappeared");
                            }
                        } catch (InterruptedException failure) {
                            Thread.currentThread().interrupt();
                            throw new com.flynes.emu.launch.LaunchHistory.HistoryException(
                                    "could not persist launch history", failure);
                        } catch (Exception failure) {
                            throw new com.flynes.emu.launch.LaunchHistory.HistoryException(
                                    "could not persist launch history", failure);
                        }
                    });
            LaunchResult result = coordinator.launch(variantId);
            if (!result.sessionCommitted()) {
                Log.e(TAG, "launch failed: code=" + result.code() + ", message=" + result.message());
            }
            main.post(() -> callback.onComplete(result));
        });
    }
}
