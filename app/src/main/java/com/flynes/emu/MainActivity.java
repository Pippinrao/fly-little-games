package com.flynes.emu;

import android.app.AlertDialog;
import android.content.Intent;
import android.os.Bundle;
import android.graphics.drawable.GradientDrawable;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Choreographer;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.ViewGroup;
import android.view.WindowManager;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;

import com.flynes.emu.input.InputRouter;
import com.flynes.emu.data.RomIdentity;
import com.flynes.emu.data.RomInfo;
import com.flynes.emu.save.SaveRecord;
import com.flynes.emu.save.SaveRepository;
import com.flynes.emu.save.LegacySaveMigrator;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.session.EmulationSession;
import com.flynes.emu.session.SessionResult;
import com.flynes.emu.session.SessionState;
import com.flynes.emu.video.DisplayModeController;
import com.flynes.emu.video.ViewportLayout;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;

/**
 * Stage-0 vertical slice: load the bundled homebrew ROM, render via
 * Choreographer blits, play audio via the audio-master-clock thread, accept
 * touch input, and auto-save on pause.
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "FlyNES";
    private static final String ROM_ASSET = "roms/from_below.nes";
    private static final int AUDIO_SAMPLE_RATE = 48000;
    private static final int REQ_LIBRARY = 1001;

    private final NesCore core = new NesCore();
    private final EmulationSession session = new EmulationSession(core);
    private EmuView view;
    private GamepadView gamepad;
    private InputRouter inputRouter;
    private AlertDialog pauseDialog;
    private AudioThread audio;
    private SaveRepository saves;
    private SettingsRepository settings;
    private AppSettings appSettings;
    private FrameLayout root;
    private int scale = 2;
    private boolean rendering = false;
    private RomIdentity currentRomIdentity;

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
        saves = new SaveRepository(this);
        settings = new SettingsRepository(new SharedPreferencesSettingsStore(this));
        appSettings = settings.load();
        Log.i(TAG, "legacy autosave migration=" + LegacySaveMigrator.migrate(this, saves));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        view = new EmuView(this);
        view.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                DisplayModeController.ApplyResult result = DisplayModeController.apply(
                        MainActivity.this, holder.getSurface(),
                        appSettings.refreshMode(), 60.0988f);
                Log.i(TAG, "display refresh request=" + result);
            }

            @Override public void surfaceChanged(SurfaceHolder holder, int format,
                                                 int width, int height) { }

            @Override public void surfaceDestroyed(SurfaceHolder holder) { }
        });
        gamepad = new GamepadView(this);
        applyHapticSettings();
        gamepad.setId(R.id.gamepad);
        inputRouter = new InputRouter(buttons -> {
            Log.d(TAG, "input=0x" + Integer.toHexString(buttons));
            session.setInput(buttons);
        }, this::handleAppAction);
        gamepad.setInputRouter(inputRouter);

        root = new FrameLayout(this);
        // Game surface: fixed 4:3 view sized to fit the screen and centered —
        // a MATCH_PARENT surface would stretch the 1024x960 (hq4x) buffer
        // non-uniformly on wide screens (the "stretched picture" complaint).
        DisplayMetrics dm = getResources().getDisplayMetrics();
        ViewportLayout.Size viewport = viewportSize(dm.widthPixels, dm.heightPixels, 0, 0);
        root.addView(view, new FrameLayout.LayoutParams(
                viewport.width(), viewport.height(), Gravity.CENTER));
        root.setOnApplyWindowInsetsListener((container, insets) -> {
            updateViewport(insets.getSystemWindowInsetLeft(),
                    insets.getSystemWindowInsetRight());
            return insets;
        });

        // Gamepad overlay sits above the game surface and owns all touch input.
        root.addView(gamepad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));

        ImageButton pauseButton = createPauseButton();
        int pauseSize = Math.round(48 * getResources().getDisplayMetrics().density);
        int pauseMargin = Math.round(16 * getResources().getDisplayMetrics().density);
        FrameLayout.LayoutParams pauseParams = new FrameLayout.LayoutParams(pauseSize, pauseSize,
                Gravity.TOP | Gravity.END);
        pauseParams.setMargins(pauseMargin, pauseMargin, pauseMargin, pauseMargin);
        root.addView(pauseButton, pauseParams);
        pauseButton.setOnApplyWindowInsetsListener((button, insets) -> {
            FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) button.getLayoutParams();
            params.setMargins(pauseMargin,
                    pauseMargin + insets.getSystemWindowInsetTop(),
                    pauseMargin + insets.getSystemWindowInsetRight(),
                    pauseMargin + insets.getSystemWindowInsetBottom());
            button.setLayoutParams(params);
            return insets;
        });
        pauseButton.requestApplyInsets();

        setContentView(root);
        root.requestApplyInsets();

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

        PendingGameLaunch.Payload pending = PendingGameLaunch.consume();
        byte[] rom = pending == null ? readAsset(ROM_ASSET) : pending.bytes();
        if (rom == null) {
            toastAndFinish(getString(R.string.missing_builtin_game));
            return;
        }
        // rc < 0 = failure; 0/positive = success (warnings are positive).
        if (startPlaying(rom) < 0) {
            toastAndFinish(getString(R.string.load_game_failed));
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
            Toast.makeText(this, R.string.switch_game_failed, Toast.LENGTH_LONG).show();
            return;
        }
        try {
            session.stop().get();
        } catch (Exception e) {
            Log.e(TAG, "session stop before ROM switch failed", e);
            return;
        }
        if (!core.create()) return;
        byte[] db = readAsset("NstDatabase.xml");
        if (db != null) core.loadDatabase(db);
        if (startPlaying(rom) < 0) {
            Toast.makeText(this, R.string.load_game_failed, Toast.LENGTH_LONG).show();
            return;
        }
        // onResume() (which follows immediately) starts a fresh AudioThread and
        // rendering; startPlaying() only loads the ROM and records its hash.
    }

    @Override
    protected void onResume() {
        super.onResume();
        appSettings = settings.load();
        applyHapticSettings();
        applyRuntimeVideoSettings();
        // Guard against double-start: a timed-out pause join can leave the
        // previous thread still running inside the native core; starting a
        // second AudioThread on the same core would race nes_run_frames.
        // Only start fresh when the previous thread is confirmed dead.
        if (!core.isCreated() || (audio != null && audio.isAlive())) return;

        if (session.state() == SessionState.PAUSED) {
            session.resume();
        }

        // Restore only from the active ROM's core-backed SHA-1 directory.
        if (appSettings.autosaveEnabled() && currentRomIdentity != null) {
            try {
                SaveRecord record = saves.readAutosave(currentRomIdentity).orElse(null);
                if (record != null && record.state().length > 0) {
                    int rc = core.loadState(record.state());
                    Log.i(TAG, "autosave restore rc=" + rc + " rom=" + currentRomIdentity.sha1());
                }
            } catch (IOException e) {
                Log.e(TAG, "read per-ROM autosave failed", e);
            }
        }

        audio = new AudioThread(core, appSettings.audioEnabled());
        audio.start();
        startRendering();
    }

    @Override
    protected void onPause() {
        super.onPause();
        if (session.state() == SessionState.RUNNING) session.pause();
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

        byte[] state = appSettings.autosaveEnabled() ? core.saveState() : null;
        if (state != null && state.length > 0 && currentRomIdentity != null) {
            try {
                saves.writeAutosave(currentRomIdentity, state, System.currentTimeMillis());
                Log.i(TAG, "per-ROM autosave written: " + state.length
                        + " bytes rom=" + currentRomIdentity.sha1());
            } catch (IOException e) {
                Log.e(TAG, "write per-ROM autosave failed", e);
            }
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
        try {
            session.stop().get();
        } catch (Exception e) {
            Log.e(TAG, "session stop failed", e);
            core.destroy();
        }
        session.closeExecutor();
    }

    // ------------------------------------------------------------------
    // Pause menu (opened only by the independent App control)
    // ------------------------------------------------------------------

    private ImageButton createPauseButton() {
        ImageButton button = new ImageButton(this);
        button.setId(R.id.pause_button);
        button.setImageResource(R.drawable.ic_pause);
        button.setContentDescription(getString(R.string.open_pause));
        button.setPadding(dp(12), dp(12), dp(12), dp(12));
        GradientDrawable background = new GradientDrawable();
        background.setShape(GradientDrawable.OVAL);
        background.setColor(0xC8323A4A);
        background.setStroke(dp(1), 0xD0AAB6CB);
        button.setBackground(background);
        button.setOnClickListener(v -> inputRouter.dispatch(InputRouter.AppAction.OPEN_PAUSE));
        return button;
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }

    private void handleAppAction(InputRouter.AppAction action) {
        switch (action) {
            case OPEN_PAUSE:
                showPauseMenu();
                break;
            case CLOSE_PAUSE:
                resumeFromPauseMenu();
                break;
            case OPEN_LIBRARY:
                startActivity(new Intent(this, HomeActivity.class));
                break;
            case OPEN_SETTINGS:
                startActivity(new Intent(this, SettingsActivity.class));
                break;
        }
    }

    private void showPauseMenu() {
        if (isFinishing() || gamepad == null
                || (pauseDialog != null && pauseDialog.isShowing())) return;
        inputRouter.cancelAll();
        if (session.state() == SessionState.RUNNING) session.pause();
        stopRendering();
        stopAudioThread();
        pauseDialog = new AlertDialog.Builder(this)
                .setTitle(R.string.pause_title)
                .setItems(new String[]{getString(R.string.continue_game),
                        getString(R.string.game_library),
                        getString(R.string.settings),
                        getString(R.string.license_information),
                        getString(R.string.cancel)}, (d, which) -> {
                    switch (which) {
                        case 0:
                            d.dismiss();
                            resumeFromPauseMenu();
                            break;
                        case 1:
                            d.dismiss();
                            handleAppAction(InputRouter.AppAction.OPEN_LIBRARY);
                            break;
                        case 2:
                            d.dismiss();
                            handleAppAction(InputRouter.AppAction.OPEN_SETTINGS);
                            break;
                        case 3:
                            d.dismiss();
                            startActivity(new Intent(this, LicensesActivity.class));
                            break;
                        default:
                            d.dismiss();
                            break;
                    }
                })
                .setOnCancelListener(d -> resumeFromPauseMenu())
                .create();
        pauseDialog.setOnDismissListener(d -> pauseDialog = null);
        pauseDialog.show();
    }

    private void applyHapticSettings() {
        if (gamepad != null) {
            gamepad.setControlSettings(appSettings == null ? settings.load() : appSettings);
        }
    }

    private void resumeFromPauseMenu() {
        if (isFinishing()) return;
        if (session.state() == SessionState.PAUSED) session.resume();
        if (audio == null || !audio.isAlive()) {
            audio = new AudioThread(core, appSettings.audioEnabled());
            audio.start();
        }
        startRendering();
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
        SessionResult loadResult;
        try {
            loadResult = session.load(rom).get();
        } catch (Exception e) {
            Log.e(TAG, "session load failed", e);
            return -1;
        }
        if (!loadResult.isSuccess()) {
            Log.e(TAG, "loadRom failed, rc=" + loadResult.code());
            return loadResult.code();
        }
        RomInfo info = core.romInfo();
        if (info == null) {
            Log.e(TAG, "core returned no ROM identity");
            return -1;
        }
        currentRomIdentity = info.identity();
        scale = computeScale();
        // HQ4X after load: Machine::Load/Power can rebuild renderer state, so
        // the filter must be (re)applied once the ROM is in place.
        core.setVideoFilter(nativeFilter(appSettings.filterMode()));
        try {
            SessionResult startResult = session.start().get();
            if (!startResult.isSuccess()) return startResult.code();
        } catch (Exception e) {
            Log.e(TAG, "session start failed", e);
            return -1;
        }
        Log.i(TAG, "ROM loaded, render scale=" + scale + "x");
        return 0;
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

    // ------------------------------------------------------------------
    // Rendering
    // ------------------------------------------------------------------

    private void applyRuntimeVideoSettings() {
        if (!core.isCreated()) return;
        core.setVideoFilter(nativeFilter(appSettings.filterMode()));
        updateViewport(0, gamepad == null ? 0 : gamepad.getRootWindowInsets() == null
                ? 0 : gamepad.getRootWindowInsets().getSystemWindowInsetRight());
        Surface surface = view.getHolder().getSurface();
        if (surface != null && surface.isValid()) {
            DisplayModeController.ApplyResult result = DisplayModeController.apply(
                    this, surface, appSettings.refreshMode(), 60.0988f);
            Log.i(TAG, "display refresh update=" + result);
        }
    }

    private void updateViewport(int insetLeft, int insetRight) {
        if (view == null || appSettings == null) return;
        DisplayMetrics metrics = getResources().getDisplayMetrics();
        ViewportLayout.Size viewport = viewportSize(metrics.widthPixels, metrics.heightPixels,
                insetLeft, insetRight);
        FrameLayout.LayoutParams params = (FrameLayout.LayoutParams) view.getLayoutParams();
        if (params == null) return;
        params.width = viewport.width();
        params.height = viewport.height();
        params.gravity = Gravity.CENTER;
        view.setLayoutParams(params);
    }

    private ViewportLayout.Size viewportSize(int width, int height, int insetLeft, int insetRight) {
        boolean filtered = appSettings.filterMode() == FilterMode.HQ4X;
        return ViewportLayout.compute(width, height, insetLeft, insetRight,
                appSettings.aspectMode(), filtered ? 1024 : 256, filtered ? 960 : 240);
    }

    private static int nativeFilter(FilterMode mode) {
        return mode == FilterMode.HQ4X ? NesCore.FILTER_HQ4X : NesCore.FILTER_NONE;
    }

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

    private void toastAndFinish(String message) {
        Toast.makeText(this, message, Toast.LENGTH_LONG).show();
        finish();
    }
}
