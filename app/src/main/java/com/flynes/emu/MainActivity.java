package com.flynes.emu;

import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.graphics.drawable.GradientDrawable;
import android.hardware.display.DisplayManager;
import android.util.DisplayMetrics;
import android.util.Log;
import android.view.Gravity;
import android.view.Surface;
import android.view.SurfaceHolder;
import android.view.ViewGroup;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.Toast;

import androidx.appcompat.app.AppCompatActivity;
import androidx.activity.OnBackPressedCallback;
import androidx.core.view.ViewCompat;

import com.flynes.emu.input.InputRouter;
import com.flynes.emu.input.HapticController;
import com.flynes.emu.input.GamepadHitMap;
import com.flynes.emu.cover.AndroidCoverRepository;
import com.flynes.emu.cover.CoverCaptureCoordinator;
import com.flynes.emu.data.RomIdentity;
import com.flynes.emu.data.RomInfo;
import com.flynes.emu.save.SaveRecord;
import com.flynes.emu.save.SaveRepository;
import com.flynes.emu.save.LegacySaveMigrator;
import com.flynes.emu.settings.AppSettings;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.settings.SharedPreferencesSettingsStore;
import com.flynes.emu.session.EmulationSession;
import com.flynes.emu.session.SessionResult;
import com.flynes.emu.session.SessionState;
import com.flynes.emu.video.DisplayModeController;
import com.flynes.emu.video.FramePublisher;
import com.flynes.emu.video.GlFrameView;
import com.flynes.emu.video.NativeFrameSource;
import com.flynes.emu.video.ViewportLayout;
import com.flynes.emu.video.quality.LegacyVideoRuntimeAdapter;
import com.flynes.emu.video.platform.AndroidDisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayObservation;
import com.flynes.emu.video.quality.FallbackReason;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.status.DisplayStatusMonitor;
import com.flynes.emu.video.status.VideoStatusAccumulator;
import com.flynes.emu.video.status.VideoStatusRepository;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;

/**
 * Stage-0 vertical slice: load the bundled homebrew ROM, render via
 * sequenced OpenGL presentation, audio-master-clock playback, touch input,
 * touch input, and auto-save on pause.
 */
public class MainActivity extends AppCompatActivity {

    private static final String TAG = "FlyNES";
    private static final String ROM_ASSET = "roms/from_below.nes";
    private static final int AUDIO_SAMPLE_RATE = 48000;
    private static final int REQ_LIBRARY = 1001;

    private final NesCore core = new NesCore();
    private final EmulationSession session = new EmulationSession(core);
    private GlFrameView view;
    private FramePublisher framePublisher;
    private GamepadView gamepad;
    private ImageButton pauseButton;
    private HapticController pauseHaptics;
    private InputRouter inputRouter;
    private FrameLayout pauseLayer;
    private AudioThread audio;
    private SaveRepository saves;
    private SettingsRepository settings;
    private AppSettings appSettings;
    private FrameLayout root;
    private boolean rendering = false;
    private RomIdentity currentRomIdentity;
    private final ExecutorService coverExecutor = Executors.newSingleThreadExecutor(runnable -> {
        Thread thread = new Thread(runnable, "flynes-cover-capture");
        thread.setDaemon(true);
        return thread;
    });
    private CoverCaptureCoordinator coverCapture;
    private final VideoStatusAccumulator videoStatus = new VideoStatusAccumulator();
    private final Handler statusHandler = new Handler(Looper.getMainLooper());
    private final ScheduledExecutorService displayPollExecutor =
            Executors.newSingleThreadScheduledExecutor(runnable -> daemonThread(
                    runnable, "flynes-display-poll"));
    private final ScheduledExecutorService displaySafetyExecutor =
            Executors.newSingleThreadScheduledExecutor(runnable -> daemonThread(
                    runnable, "flynes-display-safety"));
    private DisplayStatusMonitor displayMonitor;
    private DisplayManager displayManager;
    private boolean displayListenerRegistered;
    private volatile long surfaceEpoch;
    private volatile DisplayObservation lastDisplayObservation;
    private volatile RuntimeTemporalState requestedRuntimeTemporalState =
            RuntimeTemporalState.IMMEDIATE_NATIVE;
    private final Runnable publishVideoStatus = new Runnable() {
        @Override public void run() {
            if (!rendering) return;
            VideoStatusRepository.process().publish(videoStatus.snapshot(
                    SystemClock.elapsedRealtime()));
            statusHandler.postDelayed(this, 500L);
        }
    };
    private final DisplayManager.DisplayListener displayListener =
            new DisplayManager.DisplayListener() {
        @Override public void onDisplayAdded(int displayId) { }
        @Override public void onDisplayChanged(int displayId) {
            if (getWindowManager().getDefaultDisplay().getDisplayId() == displayId
                    && displayMonitor != null) displayMonitor.onDisplayChanged();
        }
        @Override public void onDisplayRemoved(int displayId) {
            if (displayMonitor != null) displayMonitor.invalidate();
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

        displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        displayMonitor = new DisplayStatusMonitor(new AndroidDisplayPlatformFacade(
                getWindowManager().getDefaultDisplay()), SystemClock::elapsedRealtime,
                scheduler(displayPollExecutor), scheduler(displaySafetyExecutor),
                new DisplayStatusMonitor.Listener() {
                    @Override public void onObservation(DisplayObservation observation) {
                        lastDisplayObservation = observation;
                        videoStatus.onDisplayState(observation.requestedPolicy(),
                                observation.requestedMode(),
                                observation.systemReportedActiveMode());
                        videoStatus.publishTransition(surfaceEpoch,
                                observation.requestGeneration(), requestedRuntimeTemporalState,
                                0, 0f);
                    }
                    @Override public void onUnknown(long generation, long observedAtElapsedMs) {
                        DisplayObservation previous = lastDisplayObservation;
                        videoStatus.onDisplayState(previous == null
                                        ? com.flynes.emu.video.quality.PhysicalRefreshPolicy.FOLLOW_SYSTEM
                                        : previous.requestedPolicy(),
                                previous == null ? null : previous.requestedMode(), null);
                        videoStatus.onFallback(FallbackReason.DISPLAY_OBSERVATION_STALE);
                    }
                    @Override public void onMotionLeaseExpired(long epoch, long generation) {
                        videoStatus.publishTransition(epoch, generation,
                                RuntimeTemporalState.FALLBACK, 0, 0f);
                        videoStatus.onFallback(FallbackReason.DISPLAY_OBSERVATION_STALE);
                    }
                    @Override public void onPersistentPolicyMismatch(long generation) {
                        videoStatus.onFallback(FallbackReason.SYSTEM_OR_DEVICE_POLICY);
                    }
                });

        framePublisher = new FramePublisher(new NativeFrameSource(core, 4 * 1024 * 1024));
        framePublisher.addObserver(frame -> videoStatus.onSourceFrameCopied(frame.sequence()));
        view = new GlFrameView(this, framePublisher, videoStatus);
        view.setId(R.id.game_surface);
        view.getHolder().addCallback(new SurfaceHolder.Callback() {
            @Override public void surfaceCreated(SurfaceHolder holder) {
                surfaceEpoch = Math.addExact(surfaceEpoch, 1L);
                DisplayModeController.ApplyResult result = requestDisplay(holder.getSurface());
                Log.i(TAG, "display refresh request=" + result);
            }

            @Override public void surfaceChanged(SurfaceHolder holder, int format,
                                                 int width, int height) { }

            @Override public void surfaceDestroyed(SurfaceHolder holder) {
                surfaceEpoch = Math.addExact(surfaceEpoch, 1L);
                if (displayMonitor != null) displayMonitor.invalidate();
            }
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
        root.setBackgroundColor(0xFF121316);
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

        pauseButton = createPauseButton();
        pauseHaptics = new HapticController(pauseButton);
        pauseHaptics.configure(appSettings.hapticLevel(), appSettings.distinctABHaptics());
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
        getOnBackPressedDispatcher().addCallback(this, new OnBackPressedCallback(true) {
            @Override public void handleOnBackPressed() {
                if (pauseLayer != null) resumeFromPauseMenu();
                else showPauseMenu();
            }
        });

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
        String coverGameId = pending == null
                ? "builtin:from-below" : pending.request().canonicalGameId();
        coverCapture = new CoverCaptureCoordinator(coverGameId,
                new AndroidCoverRepository(this), coverExecutor);
        framePublisher.addObserver(coverCapture);
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
        registerDisplayListener();
        if (pauseLayer == null) {
            gamepad.setVisibility(View.VISIBLE);
            pauseButton.setVisibility(View.VISIBLE);
        }
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
        unregisterDisplayListener();
        if (displayMonitor != null) displayMonitor.invalidate();
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
        unregisterDisplayListener();
        statusHandler.removeCallbacks(publishVideoStatus);
        if (displayMonitor != null) displayMonitor.close();
        displayPollExecutor.shutdownNow();
        displaySafetyExecutor.shutdownNow();
        if (coverCapture != null) framePublisher.removeObserver(coverCapture);
        coverExecutor.shutdownNow();
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
        background.setColor(0xB81B1D22);
        background.setStroke(dp(2), 0xFFFF6B5E);
        button.setBackground(background);
        button.setOnClickListener(v -> {
            if (pauseHaptics != null) pauseHaptics.feedback(GamepadHitMap.Control.PAUSE);
            inputRouter.dispatch(InputRouter.AppAction.OPEN_PAUSE);
        });
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
        if (isFinishing() || gamepad == null || pauseLayer != null) return;
        inputRouter.cancelAll();
        gamepad.reset();
        if (session.state() == SessionState.RUNNING) session.pause();
        stopRendering();
        stopAudioForPauseAsync();
        gamepad.setVisibility(View.INVISIBLE);
        pauseButton.setVisibility(View.INVISIBLE);
        pauseLayer = createPauseDrawer();
        root.addView(pauseLayer, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
        pauseLayer.requestApplyInsets();
        View scrim = pauseLayer.findViewById(R.id.pause_scrim);
        View drawer = pauseLayer.findViewById(R.id.pause_drawer);
        scrim.setAlpha(0f);
        drawer.setTranslationX(drawer.getLayoutParams().width);
        scrim.animate().alpha(1f).setDuration(180L).start();
        drawer.animate().translationX(0f).setDuration(220L).start();
    }

    private FrameLayout createPauseDrawer() {
        FrameLayout layer = new FrameLayout(this);
        layer.setId(R.id.pause_layer);
        layer.setImportantForAccessibility(View.IMPORTANT_FOR_ACCESSIBILITY_YES);

        View scrim = new View(this);
        scrim.setId(R.id.pause_scrim);
        scrim.setContentDescription(getString(R.string.continue_game));
        scrim.setBackgroundColor(0x8A000000);
        scrim.setOnClickListener(v -> resumeFromPauseMenu());
        layer.addView(scrim, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));

        LinearLayout drawer = new LinearLayout(this);
        drawer.setId(R.id.pause_drawer);
        drawer.setOrientation(LinearLayout.VERTICAL);
        drawer.setGravity(Gravity.CENTER_VERTICAL);
        android.view.WindowInsets currentInsets = root.getRootWindowInsets();
        int currentRightInset = currentInsets == null ? 0
                : currentInsets.getSystemWindowInsetRight();
        drawer.setPadding(dp(24), dp(24), dp(24) + currentRightInset, dp(24));
        drawer.setOnApplyWindowInsetsListener((content, insets) -> {
            content.setPadding(dp(24), dp(24),
                    dp(24) + insets.getSystemWindowInsetRight(), dp(24));
            return insets;
        });
        GradientDrawable surface = new GradientDrawable();
        surface.setColor(0xFF1B1D22);
        surface.setStroke(dp(1), 0xFF34373F);
        drawer.setBackground(surface);
        drawer.setContentDescription(getString(R.string.pause_title));

        TextView eyebrow = new TextView(this);
        eyebrow.setText(R.string.pause_eyebrow);
        eyebrow.setTextColor(0xFFFF6B5E);
        eyebrow.setTextSize(12);
        eyebrow.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        drawer.addView(eyebrow, matchWrap(dp(8)));
        TextView title = new TextView(this);
        title.setId(R.id.pause_game_title);
        title.setText(R.string.builtin_game_name);
        title.setTextColor(0xFFF4EFE6);
        title.setTextSize(28);
        title.setTypeface(android.graphics.Typeface.DEFAULT_BOLD);
        ViewCompat.setAccessibilityHeading(title, true);
        drawer.addView(title, matchWrap(dp(24)));

        Button resume = drawerButton(R.id.pause_continue, R.string.continue_game, true);
        resume.setOnClickListener(v -> resumeFromPauseMenu());
        drawer.addView(resume, matchHeight(dp(52), dp(12)));
        Button center = drawerButton(R.id.pause_game_center, R.string.game_center_title, false);
        center.setOnClickListener(v -> closePauseForNavigation(HomeActivity.class));
        drawer.addView(center, matchHeight(dp(48), dp(8)));
        Button settingsButton = drawerButton(R.id.pause_settings, R.string.settings, false);
        settingsButton.setOnClickListener(v -> closePauseForNavigation(SettingsActivity.class));
        drawer.addView(settingsButton, matchHeight(dp(48), 0));

        int screenWidth = getResources().getDisplayMetrics().widthPixels;
        int drawerWidth = Math.min(dp(360), Math.max(dp(280), Math.round(screenWidth * .38f)));
        FrameLayout.LayoutParams drawerParams = new FrameLayout.LayoutParams(
                drawerWidth, ViewGroup.LayoutParams.MATCH_PARENT, Gravity.END);
        layer.addView(drawer, drawerParams);
        drawer.requestApplyInsets();
        return layer;
    }

    private Button drawerButton(int id, int text, boolean primary) {
        Button button = new Button(this);
        button.setId(id);
        button.setText(text);
        button.setTextSize(15);
        button.setAllCaps(false);
        button.setTextColor(primary ? 0xFF121316 : 0xFFF4EFE6);
        GradientDrawable background = new GradientDrawable();
        background.setCornerRadius(dp(14));
        background.setColor(primary ? 0xFFFF6B5E : 0xFF25282F);
        if (!primary) background.setStroke(dp(1), 0xFF555962);
        button.setBackground(background);
        return button;
    }

    private LinearLayout.LayoutParams matchWrap(int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
        params.bottomMargin = bottom;
        return params;
    }

    private LinearLayout.LayoutParams matchHeight(int height, int bottom) {
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, height);
        params.bottomMargin = bottom;
        return params;
    }

    private void closePauseForNavigation(Class<?> destination) {
        removePauseLayer();
        startActivity(new Intent(this, destination));
    }

    private void removePauseLayer() {
        if (pauseLayer == null) return;
        root.removeView(pauseLayer);
        pauseLayer = null;
    }

    private void applyHapticSettings() {
        if (gamepad != null) {
            gamepad.setControlSettings(appSettings == null ? settings.load() : appSettings);
        }
        if (pauseHaptics != null) {
            AppSettings active = appSettings == null ? settings.load() : appSettings;
            pauseHaptics.configure(active.hapticLevel(), active.distinctABHaptics());
        }
    }

    private void resumeFromPauseMenu() {
        if (isFinishing()) return;
        removePauseLayer();
        inputRouter.cancelAll();
        gamepad.setVisibility(View.VISIBLE);
        pauseButton.setVisibility(View.VISIBLE);
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
        framePublisher.reset();
        // Scaling and reconstruction now belong to the GPU presenter; the core
        // always publishes its native 256x240 frame.
        core.setVideoFilter(NesCore.FILTER_NONE);
        view.setFilterMode(runtimeVideo().rendererFilter());
        try {
            SessionResult startResult = session.start().get();
            if (!startResult.isSuccess()) return startResult.code();
        } catch (Exception e) {
            Log.e(TAG, "session start failed", e);
            return -1;
        }
        Log.i(TAG, "ROM loaded, sequenced GPU presenter ready");
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
        core.setVideoFilter(NesCore.FILTER_NONE);
        LegacyVideoRuntimeAdapter video = runtimeVideo();
        view.setFilterMode(video.rendererFilter());
        updateViewport(0, gamepad == null ? 0 : gamepad.getRootWindowInsets() == null
                ? 0 : gamepad.getRootWindowInsets().getSystemWindowInsetRight());
        Surface surface = view.getHolder().getSurface();
        if (surface != null && surface.isValid()) {
            DisplayModeController.ApplyResult result = requestDisplay(surface);
            Log.i(TAG, "display refresh update=" + result);
        }
    }

    private DisplayModeController.ApplyResult requestDisplay(Surface surface) {
        LegacyVideoRuntimeAdapter video = runtimeVideo();
        requestedRuntimeTemporalState = video.temporalState();
        boolean motionRequested = video.requestedTemporalMode()
                == com.flynes.emu.video.quality.TemporalMode.MOTION_INTERPOLATION;
        return video.followsSystemRefresh()
                ? DisplayModeController.followSystem(this, surface, 60.0988f,
                        displayMonitor, surfaceEpoch, motionRequested)
                : DisplayModeController.apply(this, surface, video.displayRefresh(), 60.0988f,
                        displayMonitor, surfaceEpoch, motionRequested);
    }

    private LegacyVideoRuntimeAdapter runtimeVideo() {
        return LegacyVideoRuntimeAdapter.project(appSettings.videoPreferences());
    }

    /** Stops the audio master without delaying the first drawer frame. */
    private void stopAudioForPauseAsync() {
        AudioThread stopping = audio;
        if (stopping == null) return;
        stopping.stopLoop();
        Thread joiner = new Thread(() -> {
            boolean dead = false;
            try {
                stopping.join(5000L);
                dead = !stopping.isAlive();
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
            }
            final boolean stopped = dead;
            runOnUiThread(() -> {
                if (audio != stopping || !stopped) return;
                audio = null;
                if (!isFinishing() && session.state() == SessionState.RUNNING) {
                    audio = new AudioThread(core, appSettings.audioEnabled());
                    audio.start();
                }
            });
        }, "FlyNES-pause-audio-stop");
        joiner.start();
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
        return ViewportLayout.compute(width, height, insetLeft, insetRight,
                appSettings.aspectMode(), 256, 240);
    }

    private void startRendering() {
        if (!rendering) {
            rendering = true;
            RomInfo info = core.romInfo();
            SourceTiming timing = info != null && !info.ntsc()
                    ? SourceTiming.PAL_50 : SourceTiming.NTSC_60_0988;
            videoStatus.beginWindow(SystemClock.elapsedRealtime(), timing,
                    timing == SourceTiming.PAL_50 ? 50f : 60.0988f);
            statusHandler.removeCallbacks(publishVideoStatus);
            statusHandler.post(publishVideoStatus);
            view.onResume();
        }
    }

    private void stopRendering() {
        if (rendering) {
            rendering = false;
            statusHandler.removeCallbacks(publishVideoStatus);
            VideoStatusRepository.process().publish(videoStatus.snapshot(
                    SystemClock.elapsedRealtime()));
            view.onPause();
        }
    }

    private void registerDisplayListener() {
        if (displayListenerRegistered || displayManager == null) return;
        displayManager.registerDisplayListener(displayListener, statusHandler);
        displayListenerRegistered = true;
    }

    private void unregisterDisplayListener() {
        if (!displayListenerRegistered || displayManager == null) return;
        displayManager.unregisterDisplayListener(displayListener);
        displayListenerRegistered = false;
    }

    private static DisplayStatusMonitor.Scheduler scheduler(
            ScheduledExecutorService executor) {
        return (runnable, delayMs) -> {
            ScheduledFuture<?> future = executor.schedule(runnable, delayMs,
                    TimeUnit.MILLISECONDS);
            return () -> future.cancel(false);
        };
    }

    private static Thread daemonThread(Runnable runnable, String name) {
        Thread thread = new Thread(runnable, name);
        thread.setDaemon(true);
        return thread;
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
