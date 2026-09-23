package com.flynes.emu;

import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import com.flynes.emu.catalog.android.AndroidCatalogRuntime;
import com.flynes.emu.catalog.GameCatalogEntry;
import com.flynes.emu.catalog.GameVariant;
import com.flynes.emu.launch.ExactRomLoader;
import com.flynes.emu.launch.LaunchCoordinator;
import com.flynes.emu.launch.LaunchResult;

import java.util.concurrent.Future;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.function.Supplier;

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
        submit(() -> coordinator().launch(variantId), callback);
    }

    /** Resolves a cached UI selection only after the current native catalog is ready. */
    public void launchCanonical(String canonicalId, Callback callback) {
        submit(() -> launchAfterNativeReady(
                runtime.nativeReady(), canonicalId,
                this::preferredLiveVariantId,
                variantId -> coordinator().launch(variantId)), callback);
    }

    private void submit(Supplier<LaunchResult> operation, Callback callback) {
        executor.execute(() -> {
            LaunchResult completed;
            try {
                completed = operation.get();
            } catch (RuntimeException fatal) {
                // This executor runs with a bare execute(), so anything that escapes here would
                // terminate the process rather than reach the callback.
                Log.e(TAG, "unexpected launch failure", fatal);
                completed = LaunchResult.unexpectedFailure(fatal.getMessage());
            }
            LaunchResult result = completed;
            if (!result.sessionCommitted()) {
                Log.e(TAG, "launch failed: code=" + result.code() + ", message=" + result.message());
            }
            main.post(() -> callback.onComplete(result));
        });
    }

    private LaunchCoordinator coordinator() {
        return new LaunchCoordinator(
                runtime.gameCatalog(),
                new ExactRomLoader(runtime.streamOpener()),
                (request, bytes) -> {
                    var title = runtime.gameCatalog().canonicalEntries().stream()
                            .filter(entry -> entry.canonicalGame().id().equals(request.canonicalGameId()))
                            .map(GameCatalogEntry::canonicalGame)
                            .findFirst().orElse(null);
                    PendingGameLaunch.stage(request, bytes, title);
                },
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
    }

    private String preferredLiveVariantId(String canonicalId) {
        for (GameCatalogEntry entry : runtime.gameCatalog().canonicalEntries()) {
            if (!entry.canonicalGame().id().equals(canonicalId)) continue;
            for (GameVariant variant : entry.variants()) {
                if (variant.isLaunchable()) return variant.variantId();
            }
            return null;
        }
        return null;
    }

    @FunctionalInterface interface VariantResolver {
        String resolve(String canonicalId);
    }

    @FunctionalInterface interface VariantLauncher {
        LaunchResult launch(String variantId);
    }

    static LaunchResult launchAfterNativeReady(
            Future<?> nativeReady,
            String canonicalId,
            VariantResolver resolver,
            VariantLauncher launcher) {
        try {
            nativeReady.get();
            String variantId = resolver.resolve(canonicalId);
            if (variantId == null) {
                return LaunchResult.catalogChanged(
                        "cached game is absent or unavailable in the live catalog");
            }
            return launcher.launch(variantId);
        } catch (InterruptedException failure) {
            Thread.currentThread().interrupt();
            return LaunchResult.unexpectedFailure("launch interrupted while catalog was loading");
        } catch (Exception failure) {
            return LaunchResult.unexpectedFailure(failure.getMessage());
        }
    }
}
