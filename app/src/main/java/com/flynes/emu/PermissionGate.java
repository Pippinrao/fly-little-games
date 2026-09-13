package com.flynes.emu;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;

import androidx.annotation.NonNull;
import androidx.core.app.ActivityCompat;
import androidx.core.content.ContextCompat;

/**
 * Permission gates for the nearby pages (design 2026-09-13 §4.2): permissions
 * are requested per stage and per use - the camera only when the user opens
 * 扫码加入, never at page entry, and a denial never loops the system prompt.
 */
public final class PermissionGate {
    /** Single camera request code for the nearby pages. */
    public static final int REQUEST_CAMERA = 0x4E42;  // "NB"

    private PermissionGate() { }

    public static boolean hasCamera(@NonNull Activity activity) {
        return ContextCompat.checkSelfPermission(activity, Manifest.permission.CAMERA)
                == PackageManager.PERMISSION_GRANTED;
    }

    public interface Outcome {
        void onDone(boolean denied);
    }

    /** Only calls the system prompt through a {@link CameraPermissionHost}. */
    public static void requestCamera(@NonNull Activity activity, @NonNull Outcome outcome) {
        if (hasCamera(activity)) {
            outcome.onDone(false);
            return;
        }
        if (activity instanceof CameraPermissionHost) {
            ((CameraPermissionHost) activity).requestCamera(outcome);
        }
    }

    /** Activities that want a denial callback implement this. */
    public interface CameraPermissionHost {
        void requestCamera(Outcome outcome);
    }
}
