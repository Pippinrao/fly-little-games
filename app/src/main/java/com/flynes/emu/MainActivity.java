package com.flynes.emu;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.Surface;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
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
public class MainActivity extends Activity {

    private static final String TAG = "FlyNES";
    private static final String ROM_ASSET = "roms/from_below.nes";
    private static final String AUTOSAVE_NAME = "autosave.nst";
    private static final int AUDIO_SAMPLE_RATE = 48000;
    private static final int REQ_LIBRARY = 1001;
    /** Content hash of the ROM an autosave was saved from; compared against
     *  {@link #currentRomHash} before restoring, so a state saved from one ROM
     *  is never applied onto a different one. */
    private static final String PREFS_NAME = "main";
    private static final String KEY_AUTOSAVE_ROM_HASH = "autosave_rom_hash";

    private final NesCore core = new NesCore();
    private EmuView view;
    private GamepadView gamepad;
    private AudioThread audio;
    private int scale = 2;
    private boolean rendering = false;
    /** Content hash of the ROM loaded by {@link #startPlaying}; compared
     *  against the persisted hash of the last autosave before restoring it. */
    private String currentRomHash;

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
        gamepad = new GamepadView(this);
        gamepad.setListener(new GamepadView.Listener() {
            @Override
            public void onButtons(int buttons) {
                // Debug aid: the adb-injection verification asserts these lines.
                Log.d(TAG, "input=0x" + Integer.toHexString(buttons));
                core.setInput(buttons);
            }

            @Override
            public void onPauseMenu() {
                showPauseMenu();
            }
        });

        FrameLayout root = new FrameLayout(this);
        // Game surface: fixed 4:3 view sized to fit the screen and centered —
        // a MATCH_PARENT surface would stretch the 1024x960 (hq4x) buffer
        // non-uniformly on wide screens (the "stretched picture" complaint).
        DisplayMetrics dm = getResources().getDisplayMetrics();
        float aspect = 1024f / 960f; // core framebuffer ratio under hq4x
        int vh = dm.heightPixels;
        int vw = Math.min(dm.widthPixels, Math.round(vh * aspect));
        root.addView(view, new FrameLayout.LayoutParams(vw, vh, Gravity.CENTER));

        // Gamepad overlay sits above the game surface and owns all touch input.
        root.addView(gamepad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        setContentView(root);

        if (!core.create()) {
            toastAndFinish("Failed to create emulator core");
            return;
        }
        core.setAudioFormat(AUDIO_SAMPLE_RATE, 0);

        // Load the bundled NstDatabase.xml before any ROM so profiles resolve.
        // A failure must not block the game: the database only refines rom_info.
        byte[] db = readAsset("NstDatabase.xml");
        if (db != null && core.loadDatabase(db) < 0) {
            Log.w(TAG, "database load failed");
        }

        byte[] rom = readAsset(ROM_ASSET);
        if (rom == null) {
            toastAndFinish("Missing ROM asset: " + ROM_ASSET);
            return;
        }
        // rc < 0 = failure; 0/positive = success (warnings are positive).
        if (startPlaying(rom) < 0) {
            toastAndFinish("Failed to load ROM");
            return;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != REQ_LIBRARY || resultCode != RESULT_OK) return;

        byte[] rom = NesCore.sPendingRom;
        NesCore.sPendingRom = null; // consumed either way — nothing to relaunch without it
        if (rom == null) return;

        // Stop the current game's audio-master clock before touching the core.
        if (!stopAudioThread()) {
            // The old thread is still inside the native core; loading a new ROM
            // now would race nes_run_frames. Keep the old game, leak at process
            // death (same policy as onPause/onDestroy).
            Log.w(TAG, "audio thread still alive; not switching ROM");
            Toast.makeText(this, "无法切换游戏: 音频线程仍在运行", Toast.LENGTH_LONG).show();
            return;
        }
        if (!core.isCreated()) return;
        if (startPlaying(rom) < 0) {
            Toast.makeText(this, "无法加载游戏", Toast.LENGTH_LONG).show();
            return;
        }
        // onResume() (which follows immediately) starts a fresh AudioThread and
        // rendering; startPlaying() only loads the ROM and records its hash.
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Guard against double-start: a timed-out pause join can leave the
        // previous thread still running inside the native core; starting a
        // second AudioThread on the same core would race nes_run_frames.
        // Only start fresh when the previous thread is confirmed dead.
        if (!core.isCreated() || (audio != null && audio.isAlive())) return;

        // Restore the previous session BEFORE powering frames — but only when
        // the autosave was saved from the ROM that is currently loaded. A state
        // saved from a different ROM (e.g. a library pick replaced the asset)
        // must never be applied onto another game.
        byte[] state = readAutosave();
        String savedRomHash = getSharedPreferences(PREFS_NAME, MODE_PRIVATE)
                .getString(KEY_AUTOSAVE_ROM_HASH, null);
        if (state != null && state.length > 0
                && currentRomHash != null && currentRomHash.equals(savedRomHash)) {
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
        gamepad.reset();

        // Stop the audio-master clock first so the core is quiescent, then snapshot.
        if (!stopAudioThread()) {
            // Safety valve: the thread is STILL inside the native core after the
            // cap. Never save/destroy while it runs — skip the autosave, keep the
            // `audio` reference so onResume()/onDestroy() can see the live thread,
            // and let the native core leak at process death rather than crash with
            // a use-after-free.
            Log.w(TAG, "audio thread still alive after 5 s; skipping autosave, core kept alive");
            return;
        }

        byte[] state = core.saveState();
        if (state != null && state.length > 0) {
            // Remember which ROM this state belongs to, so a later resume never
            // restores it onto a different ROM (see the guard in onResume).
            getSharedPreferences(PREFS_NAME, MODE_PRIVATE).edit()
                    .putString(KEY_AUTOSAVE_ROM_HASH, currentRomHash)
                    .apply();
            writeAutosave(state);
        }
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        stopRendering();
        gamepad.reset();
        // Never destroy the native core while the audio thread might still be
        // inside nes_run_frames (use-after-free). If the pause-time join cap
        // was exceeded the thread is still referenced and alive — leak the
        // core at process death instead of crashing.
        if (audio != null && audio.isAlive()) {
            Log.w(TAG, "audio thread still alive; leaking native core instead of destroying");
            return;
        }
        core.destroy();
    }

    // ------------------------------------------------------------------
    // Pause menu (opened by the gamepad START key)
    // ------------------------------------------------------------------

    private void showPauseMenu() {
        if (isFinishing() || gamepad == null) return;
        new AlertDialog.Builder(this)
                .setTitle("FlyNES")
                .setItems(new String[]{"继续游戏", "游戏库", "许可信息", "取消"}, (d, which) -> {
                    switch (which) {
                        case 0:
                            d.dismiss();
                            pressStart();
                            break;
                        case 1:
                            d.dismiss();
                            startActivityForResult(new Intent(this, GameLibraryActivity.class), REQ_LIBRARY);
                            break;
                        case 2:
                            d.dismiss();
                            startActivity(new Intent(this, LicensesActivity.class));
                            break;
                        default:
                            d.dismiss();
                            break;
                    }
                })
                .setOnCancelListener(d -> {
                    // Back / outside-tap dismiss: stay paused in-game, user's choice.
                })
                .show();
    }

    /** Sends one START pulse to resume from the NES game's own pause state. */
    private void pressStart() {
        core.setInput(GamepadView.START);
        new Handler(Looper.getMainLooper()).postDelayed(
                () -> core.setInput(gamepad.buttons()), 50);
    }

    // ------------------------------------------------------------------
    // ROM loading / audio lifecycle
    // ------------------------------------------------------------------

    /**
     * Loads ROM bytes into the emulator core and prepares for play. Used by
     * both the onCreate assets path and the library handoff (onActivityResult).
     * Does NOT start the AudioThread or the renderer: onResume() always follows
     * this call and does that, guarded against double-start.
     *
     * @return 0/positive rc on success, negative on failure.
     */
    private int startPlaying(byte[] rom) {
        if (!core.isCreated()) return -3; // NES_ERR_NOT_READY
        int rc = core.loadRom(rom, null);
        if (rc < 0) {
            Log.e(TAG, "loadRom failed, rc=" + rc);
            return rc;
        }
        currentRomHash = romHash(rom);
        scale = computeScale();
        // HQ4X after load: Machine::Load/Power can rebuild renderer state, so
        // the filter must be (re)applied once the ROM is in place.
        core.setVideoFilter(NesCore.FILTER_HQ4X);
        Log.i(TAG, "ROM loaded (rc=" + rc + "), render scale=" + scale + "x");
        return rc;
    }

    /**
     * Stops and joins the audio thread, bounded by a 5 s cap (a blocking
     * AudioTrack.write() can stall the loop; never proceed to saveState() /
     * loadRom() / destroy() while nes_run_frames might still be running).
     *
     * @return true when the thread is confirmed dead afterwards; on false the
     *         thread is still referenced and alive — treat the core as unsafe
     *         to touch and let it leak at process death.
     */
    private boolean stopAudioThread() {
        if (audio == null) return true;
        audio.stopLoop();
        long deadline = System.currentTimeMillis() + 5000;
        boolean dead = false;
        try {
            while (audio.isAlive() && System.currentTimeMillis() < deadline) {
                audio.join(50);
            }
            dead = !audio.isAlive();
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        if (dead) audio = null;
        return dead;
    }

    /** Cheap content hash identifying a ROM; used to pair autosaves with ROMs. */
    private static String romHash(byte[] rom) {
        java.util.zip.Adler32 a = new java.util.zip.Adler32();
        a.update(rom);
        return Long.toHexString(a.getValue());
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
        // The core framebuffer is already filter-scaled (hq4x = 1024x960);
        // blit 1:1 and let the SurfaceView geometry fit the window.
        return 1;
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
