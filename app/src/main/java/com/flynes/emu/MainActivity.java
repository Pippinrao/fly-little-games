package com.flynes.emu;

import android.app.Activity;
import android.os.Bundle;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Choreographer;
import android.view.Surface;
import android.view.WindowManager;
import android.widget.Toast;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Stage-0 vertical slice: load the bundled homebrew ROM, render via
 * Choreographer blits, play audio via the audio-master-clock thread, accept
 * touch input, and auto-save on pause.
 */
public class MainActivity extends Activity implements TouchController.Listener {

    private static final String TAG = "FlyNES";
    private static final String ROM_ASSET = "roms/from_below.nes";
    private static final String AUTOSAVE_NAME = "autosave.nst";
    private static final int AUDIO_SAMPLE_RATE = 48000;

    private final NesCore core = new NesCore();
    private EmuView view;
    private AudioThread audio;
    private int scale = 2;
    private boolean rendering = false;

    private final Choreographer.FrameCallback frameCallback = new Choreographer.FrameCallback() {
        @Override
        public void doFrame(long frameTimeNanos) {
            if (rendering) {
                Surface s = view.getHolder().getSurface();
                if (s != null && s.isValid()) {
                    core.blit(s, scale);
                }
                Choreographer.getInstance().postFrameCallback(this);
            }
        }
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        view = new EmuView(this);
        TouchController touch = new TouchController();
        touch.setListener(this);
        view.setOnTouchListener(touch);
        setContentView(view);

        if (!core.create()) {
            toastAndFinish("Failed to create emulator core");
            return;
        }
        core.setAudioFormat(AUDIO_SAMPLE_RATE, 0);

        byte[] rom = readAsset(ROM_ASSET);
        if (rom == null) {
            toastAndFinish("Missing ROM asset: " + ROM_ASSET);
            return;
        }
        // rc < 0 = failure; 0/positive = success (warnings are positive).
        int rc = core.loadRom(rom, null);
        if (rc < 0) {
            Log.e(TAG, "loadRom failed, rc=" + rc);
            toastAndFinish("Failed to load ROM (rc=" + rc + ")");
            return;
        }
        scale = computeScale();
        Log.i(TAG, "ROM loaded (rc=" + rc + "), render scale=" + scale + "x");
    }

    @Override
    protected void onResume() {
        super.onResume();
        if (!core.isCreated() || audio != null) return;

        // Restore the previous session BEFORE powering frames.
        byte[] state = readAutosave();
        if (state != null && state.length > 0) {
            int rc = core.loadState(state);
            Log.i(TAG, "autosave restore rc=" + rc);
        }

        audio = new AudioThread(core);
        audio.start();
        startRendering();
    }

    @Override
    protected void onPause() {
        super.onPause();
        stopRendering();

        // Stop the audio-master clock first so the core is quiescent, then snapshot.
        if (audio != null) {
            audio.stopLoop();
            try {
                audio.join(500);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            audio = null;
        }

        byte[] state = core.saveState();
        if (state != null && state.length > 0) {
            writeAutosave(state);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopRendering();
        core.destroy();
    }

    @Override
    public void onButtons(int buttons) {
        core.setInput(buttons);
    }

    // ------------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------------

    private void startRendering() {
        if (!rendering) {
            rendering = true;
            Choreographer.getInstance().postFrameCallback(frameCallback);
        }
    }

    private void stopRendering() {
        rendering = false;
        Choreographer.getInstance().removeFrameCallback(frameCallback);
    }

    private int computeScale() {
        DisplayMetrics dm = getResources().getDisplayMetrics();
        int s = Math.min(dm.widthPixels / 256, dm.heightPixels / 240);
        return Math.max(1, s);
    }

    // ------------------------------------------------------------------
    // Assets / autosave
    // ------------------------------------------------------------------

    private byte[] readAsset(String path) {
        try (InputStream in = getAssets().open(path);
             ByteArrayOutputStream out = new ByteArrayOutputStream()) {
            byte[] buf = new byte[8192];
            int n;
            while ((n = in.read(buf)) > 0) {
                out.write(buf, 0, n);
            }
            return out.toByteArray();
        } catch (IOException e) {
            Log.e(TAG, "readAsset failed: " + path, e);
            return null;
        }
    }

    private File autosaveFile() {
        return new File(getFilesDir(), AUTOSAVE_NAME);
    }

    private byte[] readAutosave() {
        File f = autosaveFile();
        if (!f.exists()) return null;
        long len = f.length();
        if (len <= 0 || len > Integer.MAX_VALUE) return null;
        byte[] data = new byte[(int) len];
        try (FileInputStream in = new FileInputStream(f)) {
            int off = 0;
            int n;
            while (off < data.length && (n = in.read(data, off, data.length - off)) > 0) {
                off += n;
            }
            return off == data.length ? data : null;
        } catch (IOException e) {
            Log.e(TAG, "read autosave failed", e);
            return null;
        }
    }

    private void writeAutosave(byte[] data) {
        try (FileOutputStream out = new FileOutputStream(autosaveFile())) {
            out.write(data);
            Log.i(TAG, "autosave written: " + data.length + " bytes");
        } catch (IOException e) {
            Log.e(TAG, "write autosave failed", e);
        }
    }

    private void toastAndFinish(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
        finish();
    }
}
