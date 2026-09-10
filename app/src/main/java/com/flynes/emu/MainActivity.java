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
import com.flynes.emu.settings.SettingsAccess;
import com.flynes.emu.settings.SettingsRepository;
import com.flynes.emu.session.EmulationSession;
import com.flynes.emu.session.SessionResult;
import com.flynes.emu.session.SessionState;
import com.flynes.emu.video.DisplayModeController;
import com.flynes.emu.video.FramePublisher;
import com.flynes.emu.video.FrameAvailableSignal;
import com.flynes.emu.video.FrameDispatchExecutor;
import com.flynes.emu.video.ClockDomainCalibrator;
import com.flynes.emu.video.audio.AvSyncMonitor;
import com.flynes.emu.video.audio.DisplayLeaseWatchdog;
import com.flynes.emu.video.audio.MotionShadowEvidence;
import com.flynes.emu.video.audio.SurfaceRecoveryIntentPolicy;
import com.flynes.emu.video.audio.TemporalAudioDelay;
import com.flynes.emu.video.audio.TemporalTransitionController;
import com.flynes.emu.video.InputLatencyTracker;
import com.flynes.emu.video.NativeInputSample;
import com.flynes.emu.video.GameSurfaceView;
import com.flynes.emu.video.NativePresenterStats;
import com.flynes.emu.video.NativeFrameSource;
import com.flynes.emu.video.RefreshMode;
import com.flynes.emu.video.ViewportLayout;
import com.flynes.emu.video.quality.LegacyVideoRuntimeAdapter;
import com.flynes.emu.video.quality.BuildAlgorithmAvailability;
import com.flynes.emu.video.quality.BundledAlgorithmAvailability;
import com.flynes.emu.video.quality.DisplayCapabilities;
import com.flynes.emu.video.quality.DisplayQualityResolver;
import com.flynes.emu.video.quality.EffectiveVideoConfig;
import com.flynes.emu.video.quality.GlCapabilities;
import com.flynes.emu.video.quality.PresenterFailureMapper;
import com.flynes.emu.video.quality.RuntimeConstraints;
import com.flynes.emu.video.quality.RuntimeFailure;
import com.flynes.emu.video.platform.AndroidDisplayPlatformFacade;
import com.flynes.emu.video.quality.DisplayObservation;
import com.flynes.emu.video.quality.FallbackReason;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.status.DisplayStatusMonitor;
import com.flynes.emu.video.status.DisplayCapabilitiesReader;
import com.flynes.emu.video.status.GlCapabilityProbe;
import com.flynes.emu.video.status.VideoStatusAccumulator;
import com.flynes.emu.video.status.VideoStatusRepository;
import com.flynes.emu.video.power.ThermalBand;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicLong;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;

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
    private GameSurfaceView view;
    private FramePublisher framePublisher;
    private final FrameAvailableSignal frameAvailable = new FrameAvailableSignal();
    private final ExecutorService frameDispatchThread = Executors.newSingleThreadExecutor(
            runnable -> daemonThread(runnable, "flynes-frame-dispatch"));
    private FrameDispatchExecutor frameDispatch;
    private FrameDispatchExecutor motionFrameDispatch;
    private volatile boolean motionCaptureActive;
    private final ClockDomainCalibrator clockCalibrator = new ClockDomainCalibrator();
    private final AvSyncMonitor avSyncMonitor = new AvSyncMonitor(clockCalibrator);
    private final TemporalAudioDelay temporalAudioDelay =
            new TemporalAudioDelay(AUDIO_SAMPLE_RATE, 1, 60.0988);
    private final TemporalTransitionController temporalTransitionController =
            new TemporalTransitionController();
    private final InputLatencyTracker inputLatencyTracker =
            new InputLatencyTracker(clockCalibrator);
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
    private final ExecutorService displayLifecycleExecutor = Executors.newSingleThreadExecutor(
            runnable -> daemonThread(runnable, "flynes-display-lifecycle"));
    private final ExecutorService qualityProbeExecutor = Executors.newSingleThreadExecutor(
            runnable -> daemonThread(runnable, "flynes-quality-probe"));
    private final GlCapabilityProbe glCapabilityProbe =
            new GlCapabilityProbe(new GlCapabilityProbe.AndroidBackend());
    private final DisplayQualityResolver displayQualityResolver = new DisplayQualityResolver();
    private final BuildAlgorithmAvailability bundledAlgorithms =
            BundledAlgorithmAvailability.current();
    private final Set<RuntimeFailure> presenterRuntimeFailures =
            ConcurrentHashMap.newKeySet();
    private volatile GlCapabilities runtimeGlCapabilities = GlCapabilities.unknown();
    private volatile SourceTiming runtimeSourceTiming = SourceTiming.NTSC_60_0988;
    private DisplayStatusMonitor displayMonitor;
    private DisplayManager displayManager;
    private boolean displayListenerRegistered;
    private volatile long surfaceEpoch;
    private NativePresenterStats lastPresenterStats = NativePresenterStats.EMPTY;
    private volatile DisplayObservation lastDisplayObservation;
    private volatile RuntimeTemporalState requestedRuntimeTemporalState =
            RuntimeTemporalState.IMMEDIATE_NATIVE;
    private final AtomicBoolean motionTransitionInFlight = new AtomicBoolean();
    private volatile boolean motionRuntimeActive;
    private volatile boolean motionShadowActive;
    private volatile long shadowRuntimeFailureBaseline;
    private volatile boolean surfaceRecoveryPending;
    private volatile boolean surfaceRecoveryWasRunning;
    private volatile long motionDisplayGeneration;
    private DisplayLeaseWatchdog displayLeaseWatchdog;
    private long appliedDisplayEpoch = -1L;
    private RefreshMode appliedRefreshMode;
    private boolean appliedFollowSystem;
    private int appliedSourceMilliHz;
    private final AtomicLong displayRequestGeneration = new AtomicLong();
    private final AtomicLong displayFallbackRetryToken = new AtomicLong();
    private boolean displayModeRejectedForSession;
    private long lastLoggedDisplayRequestGeneration = -1L;
    private final Runnable publishVideoStatus = new Runnable() {
        @Override public void run() {
            if (!rendering) return;
            collectNativePresenterStats();
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
        settings = SettingsAccess.repository(this);
        appSettings = settings.load();
        Log.i(TAG, "legacy autosave migration=" + LegacySaveMigrator.migrate(this, saves));
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        displayManager = (DisplayManager) getSystemService(DISPLAY_SERVICE);
        temporalAudioDelay.disableImmediately();
        displayLeaseWatchdog = new DisplayLeaseWatchdog(
                TimeUnit.MILLISECONDS.toNanos(DisplayStatusMonitor.MOTION_LEASE_MS),
                (callback, delayNs) -> {
                    ScheduledFuture<?> future = displaySafetyExecutor.schedule(
                            callback, delayNs, TimeUnit.NANOSECONDS);
                    return () -> future.cancel(false);
                }, token -> scheduleMotionExit(
                        TemporalTransitionController.ExitReason.LEASE_EXPIRED, false));
        displayMonitor = new DisplayStatusMonitor(new AndroidDisplayPlatformFacade(
                getWindowManager().getDefaultDisplay()), SystemClock::elapsedRealtime,
                scheduler(displayPollExecutor), scheduler(displaySafetyExecutor),
                new DisplayStatusMonitor.Listener() {
                    @Override public void onObservation(DisplayObservation observation) {
                        lastDisplayObservation = observation;
                        if (observation.requestGeneration()
                                != lastLoggedDisplayRequestGeneration
                                && observation.requestedMode() != null) {
                            lastLoggedDisplayRequestGeneration = observation.requestGeneration();
                            com.flynes.emu.video.quality.DisplayModeCapability requested =
                                    observation.requestedMode();
                            Log.i(TAG, "EVIDENCE_DISPLAY_REQUEST generation="
                                    + observation.requestGeneration() + " policy="
                                    + observation.requestedPolicy().name() + " modeId="
                                    + requested.modeId() + " width=" + requested.width()
                                    + " height=" + requested.height() + " refreshMilliHz="
                                    + requested.refreshMilliHz());
                        }
                        videoStatus.onDisplayState(observation.requestedPolicy(),
                                observation.requestedMode(),
                                observation.systemReportedActiveMode());
                        EffectiveVideoConfig effective = resolveEffectiveVideoConfig(observation);
                        requestedRuntimeTemporalState = effective.runtimeTemporalState();
                        handleMotionObservation(effective, observation);
                        if (effective.resolvedConfigurationId() != null
                                && effective.resolvedConfigurationKey() != null) {
                            videoStatus.publishStableConfiguration(surfaceEpoch,
                                    observation.requestGeneration(),
                                    effective.resolvedConfigurationId(),
                                    effective.resolvedConfigurationKey(),
                                    effective.runtimeTemporalState(),
                                    effective.videoDelayFrames(), effective.audioDelayMs());
                        } else {
                            videoStatus.publishTransition(surfaceEpoch,
                                    observation.requestGeneration(),
                                    effective.runtimeTemporalState(),
                                    effective.videoDelayFrames(), effective.audioDelayMs());
                        }
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
                        scheduleMotionExit(
                                TemporalTransitionController.ExitReason.LEASE_EXPIRED, false);
                    }
                    @Override public void onPersistentPolicyMismatch(long generation) {
                        videoStatus.onFallback(FallbackReason.SYSTEM_OR_DEVICE_POLICY);
                        runOnUiThread(() -> handlePersistentDisplayMismatch(generation));
                    }
                });

        qualityProbeExecutor.execute(() -> {
            GlCapabilityProbe.Snapshot snapshot = glCapabilityProbe.probe();
            runtimeGlCapabilities = snapshot.capabilities();
            runOnUiThread(() -> {
                if (!isFinishing() && !isDestroyed() && core.isCreated()) {
                    applyRuntimeVideoSettings();
                }
            });
        });

        framePublisher = new FramePublisher(new NativeFrameSource(core, 4 * 1024 * 1024));
        framePublisher.addObserver(frame -> videoStatus.onSourceFrameCopied(frame.sequence()));
        view = new GameSurfaceView(this, framePublisher, new GameSurfaceView.Listener() {
            @Override public void onSurfaceAvailable(Surface surface, long epoch) {
                surfaceEpoch = epoch;
                DisplayModeController.ApplyResult result = requestDisplay();
                Log.i(TAG, "display refresh request=" + result + " epoch=" + epoch);
            }

            @Override public void onSurfaceLost(long epoch) {
                motionCaptureActive = false;
                motionRuntimeActive = false;
                motionShadowActive = false;
                if (displayLeaseWatchdog != null) displayLeaseWatchdog.clear();
                clearDisplayRequest(epoch);
                if (surfaceEpoch == epoch && displayMonitor != null) displayMonitor.invalidate();
            }

            @Override public void onSurfaceDestroying(long epoch) {
                if ((!motionRuntimeActive && !motionShadowActive) || surfaceEpoch != epoch) return;
                surfaceRecoveryPending = true;
                surfaceRecoveryWasRunning = session.state() == SessionState.RUNNING;
                motionCaptureActive = false;
                displayLeaseWatchdog.clear();
                requestedRuntimeTemporalState = RuntimeTemporalState.SURFACE_SUSPENDED_HOLD;
                temporalTransitionController.enterHold(System.nanoTime());
                if (inputRouter != null) inputRouter.cancelAll();
                if (gamepad != null) gamepad.reset();
                // This callback completes before the native ANativeWindow is
                // destroyed. Prove the audio-master/core loop quiescent first;
                // otherwise a late captured frame could target the dead epoch.
                if (!stopAudioThread()) {
                    Log.e(TAG, "surface recovery could not quiesce audio/core");
                    surfaceRecoveryWasRunning = false;
                }
                if (session.state() == SessionState.RUNNING) {
                    try { session.pause().get(2, TimeUnit.SECONDS); }
                    catch (Exception failure) {
                        Log.e(TAG, "surface recovery pause failed", failure);
                        surfaceRecoveryWasRunning = false;
                    }
                }
            }
        });
        frameDispatch = new FrameDispatchExecutor(frameDispatchThread, view::onFrameAvailable);
        motionFrameDispatch = FrameDispatchExecutor.motionCaptureSynchronous(
                view::onFrameAvailable, 3, failure -> scheduleMotionExit(
                        failure == FrameDispatchExecutor.Failure.SOURCE_SEQUENCE_GAP
                                ? TemporalTransitionController.ExitReason.SOURCE_SEQUENCE_GAP
                                : TemporalTransitionController.ExitReason.STAGING_OVERFLOW,
                        false));
        frameAvailable.addListener(sequence -> {
            videoStatus.onCoreFrameProduced(sequence);
            (motionCaptureActive ? motionFrameDispatch : frameDispatch).offer(sequence);
            NativeInputSample sample = core.lastInputSample();
            inputLatencyTracker.onCoreSample(sample).ifPresent(latencyNs ->
                    Log.d(TAG, "touch-to-core-ns=" + latencyNs));
        });
        view.setId(R.id.game_surface);
        gamepad = new GamepadView(this);
        applyHapticSettings();
        gamepad.setId(R.id.gamepad);
        inputRouter = InputRouter.timestamped((buttons, eventElapsedNs) -> {
            Log.d(TAG, "input=0x" + Integer.toHexString(buttons));
            session.setInput(buttons, generation ->
                    inputLatencyTracker.onTouchGeneration(generation, eventElapsedNs));
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
        if (surfaceRecoveryPending) return;
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

        audio = createAudioThread();
        audio.start();
        startRendering();
    }

    @Override
    protected void onPause() {
        super.onPause();
        unregisterDisplayListener();
        if (session.state() == SessionState.RUNNING) session.pause();
        stopRendering();
        gamepad.reset();

        // Stop the audio-master clock first so the core is quiescent, then snapshot.
        boolean audioStopped = stopAudioThread();
        if (motionRuntimeActive || motionShadowActive) {
            motionCaptureActive = false;
            displayLeaseWatchdog.clear();
            long exitId = view.exitMotion(true);
            if (exitId >= 0L && awaitTransition(exitId,
                    NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE, 2_000L)) {
                motionRuntimeActive = false;
                motionShadowActive = false;
                requestedRuntimeTemporalState = RuntimeTemporalState.IMMEDIATE_NATIVE;
                if (audioStopped && temporalAudioDelay.removeOneFrameDelay() == 0) {
                    temporalAudioDelay.flush();
                    temporalAudioDelay.disableImmediately();
                }
            }
        }
        clearDisplayRequest(surfaceEpoch);
        if (displayMonitor != null) displayMonitor.invalidate();
        if (!audioStopped) {
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
        if (frameDispatch != null) frameDispatch.close();
        if (motionFrameDispatch != null) motionFrameDispatch.close();
        frameDispatchThread.shutdownNow();
        displayPollExecutor.shutdownNow();
        displaySafetyExecutor.shutdownNow();
        displayLifecycleExecutor.shutdownNow();
        qualityProbeExecutor.shutdownNow();
        if (coverCapture != null) framePublisher.removeObserver(coverCapture);
        coverExecutor.shutdownNow();
        stopRendering();
        if (view != null) view.release();
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
        beginPauseDisplayClear(surfaceEpoch);
        if (displayMonitor != null) displayMonitor.invalidate();
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
            audio = createAudioThread();
            audio.start();
        }
        startRendering();
        scheduleDisplayReapplyAfterPause();
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
        runtimeSourceTiming = info.ntsc() ? SourceTiming.NTSC_60_0988 : SourceTiming.PAL_50;
        framePublisher.reset();
        view.resetSequence();
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
    private AudioThread createAudioThread() {
        return new AudioThread(core, appSettings.audioEnabled(), frameAvailable,
                temporalAudioDelay, avSyncMonitor,
                sequence -> view == null ? -1L : view.actualRealPresentationNs(sequence));
    }

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
            DisplayModeController.ApplyResult result = requestDisplay();
            Log.i(TAG, "display refresh update=" + result);
        }
        calibrateClockDomain();
    }

    private DisplayModeController.ApplyResult requestDisplay() {
        if (pauseLayer != null) return DisplayModeController.ApplyResult.FAILED;
        LegacyVideoRuntimeAdapter video = runtimeVideo();
        boolean certificationHookAllowed = (getApplicationInfo().flags
                & android.content.pm.ApplicationInfo.FLAG_DEBUGGABLE) != 0
                && getIntent() != null;
        String certificationMode = certificationHookAllowed ? getIntent().getStringExtra(
                "com.flynes.emu.extra.CERTIFY_NATIVE_TIME_MODE") : null;
        boolean certifyNative120 = "120".equals(certificationMode);
        boolean certifyNative60 = "60".equals(certificationMode);
        boolean followsSystem = !(certifyNative120 || certifyNative60)
                && video.followsSystemRefresh();
        RefreshMode requestedRefresh = certifyNative120 ? RefreshMode.HZ_120
                : certifyNative60 ? RefreshMode.HZ_60 : video.displayRefresh();
        if (displayModeRejectedForSession && !followsSystem) {
            requestedRefresh = RefreshMode.HZ_60;
        }
        requestedRuntimeTemporalState = video.temporalState();
        boolean motionRequested = video.requestedTemporalMode()
                == com.flynes.emu.video.quality.TemporalMode.MOTION_INTERPOLATION;
        float sourceFps = runtimeSourceTiming == SourceTiming.PAL_50 ? 50f : 60.0988f;
        int sourceMilliHz = Math.round(sourceFps * 1_000f);
        Log.i(TAG, "EVIDENCE_SOURCE_TIMING=" + runtimeSourceTiming.name()
                + " sourceMilliHz=" + sourceMilliHz);
        if (appliedDisplayEpoch == surfaceEpoch
                && appliedFollowSystem == followsSystem
                && appliedRefreshMode == requestedRefresh
                && appliedSourceMilliHz == sourceMilliHz) {
            return appliedFollowSystem ? DisplayModeController.ApplyResult.FALLBACK_AUTO
                    : DisplayModeController.ApplyResult.APPLIED;
        }
        AndroidDisplayPlatformFacade platform = new AndroidDisplayPlatformFacade(
                getWindow(), getWindowManager().getDefaultDisplay());
        displayRequestGeneration.incrementAndGet();
        if (followsSystem) {
            DisplayModeController.ApplyResult result = DisplayModeController.followSystem(
                    platform, view, surfaceEpoch);
            if (result != DisplayModeController.ApplyResult.FAILED) {
                appliedDisplayEpoch = surfaceEpoch;
                appliedRefreshMode = null;
                appliedFollowSystem = true;
                appliedSourceMilliHz = sourceMilliHz;
                if (displayMonitor != null) displayMonitor.request(surfaceEpoch,
                        com.flynes.emu.video.quality.PhysicalRefreshPolicy.FOLLOW_SYSTEM,
                        null, motionRequested);
            }
            return result;
        }
        DisplayModeController.ApplyResult result = DisplayModeController.apply(
                platform, view, requestedRefresh, sourceFps,
                surfaceEpoch, displayMonitor, motionRequested);
        if (result == DisplayModeController.ApplyResult.APPLIED) {
            appliedDisplayEpoch = surfaceEpoch;
            appliedRefreshMode = requestedRefresh;
            appliedFollowSystem = false;
            appliedSourceMilliHz = sourceMilliHz;
        }
        return result;
    }

    private void clearDisplayRequest(long epoch) {
        if (view == null) return;
        displayFallbackRetryToken.incrementAndGet();
        long generation = displayRequestGeneration.incrementAndGet();
        appliedDisplayEpoch = -1L;
        appliedRefreshMode = null;
        appliedSourceMilliHz = 0;
        scheduleNativeClearUntilConfirmed(view, epoch, generation, 0);
    }

    private void beginPauseDisplayClear(long epoch) {
        if (view == null) return;
        displayFallbackRetryToken.incrementAndGet();
        long generation = displayRequestGeneration.incrementAndGet();
        appliedDisplayEpoch = -1L;
        appliedRefreshMode = null;
        appliedSourceMilliHz = 0;
        scheduleNativeClearUntilConfirmed(view, epoch, generation, 0);
    }

    private void scheduleNativeClearUntilConfirmed(GameSurfaceView target, long epoch,
                                                   long generation, int attempt) {
        displayLifecycleExecutor.execute(() -> {
            if (generation != displayRequestGeneration.get()) return;
            boolean cleared = target.clearFrameRate(epoch);
            if (cleared) {
                runOnUiThread(() -> {
                    if (generation != displayRequestGeneration.get() || isDestroyed()) return;
                    new AndroidDisplayPlatformFacade(getWindow(),
                            getWindowManager().getDefaultDisplay())
                            .setPreferredDisplayModeId(0);
                });
                return;
            }
            if (attempt >= 11 || generation != displayRequestGeneration.get()) {
                Log.w(TAG, "display vote clear remains unconfirmed after bounded retries");
                return;
            }
            try {
                Thread.sleep(250L);
            } catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                return;
            }
            scheduleNativeClearUntilConfirmed(target, epoch, generation, attempt + 1);
        });
    }

    private void handlePersistentDisplayMismatch(long generation) {
        if (displayMonitor == null || pauseLayer != null || isFinishing()) return;
        DisplayStatusMonitor.Snapshot snapshot = displayMonitor.snapshotAt(
                SystemClock.elapsedRealtime());
        DisplayObservation observation = snapshot.observation();
        if (observation == null || observation.requestGeneration() != generation) return;
        displayModeRejectedForSession = true;
        appliedDisplayEpoch = -1L;
        long retryToken = displayFallbackRetryToken.incrementAndGet();
        requestDisplayFallback(retryToken, surfaceEpoch, 0);
    }

    private void requestDisplayFallback(long retryToken, long expectedSurfaceEpoch,
                                        int attempt) {
        if (retryToken != displayFallbackRetryToken.get() || pauseLayer != null
                || isFinishing() || isDestroyed() || surfaceEpoch != expectedSurfaceEpoch) return;
        DisplayModeController.ApplyResult result = requestDisplay();
        Log.w(TAG, "persistent display-mode mismatch; ordered 60 Hz fallback=" + result
                + " attempt=" + attempt);
        if (result != DisplayModeController.ApplyResult.FAILED || attempt >= 11) return;
        statusHandler.postDelayed(() -> requestDisplayFallback(retryToken,
                expectedSurfaceEpoch, attempt + 1), 250L);
    }

    private void scheduleDisplayReapplyAfterPause() {
        long generation = displayRequestGeneration.incrementAndGet();
        displayLifecycleExecutor.execute(() -> runOnUiThread(() -> {
            if (generation != displayRequestGeneration.get() || pauseLayer != null
                    || isFinishing()) return;
            Surface surface = view.getHolder().getSurface();
            if (surface != null && surface.isValid()) requestDisplay();
        }));
    }

    private void handleMotionObservation(EffectiveVideoConfig effective,
                                         DisplayObservation observation) {
        if (effective == null || observation == null || view == null
                || observation.requestGeneration() <= 0L) return;
        final boolean wantsMotion = effective.effectiveTemporal()
                == com.flynes.emu.video.quality.TemporalMode.MOTION_INTERPOLATION;
        final boolean compatibleMotionDisplay = wantsMotion
                && observation.stableForMs() >= 3_000L
                && observation.systemReportedActiveMode() != null
                && observation.systemReportedActiveMode().refreshMilliHz() >= 119_000
                && observation.systemReportedActiveMode().refreshMilliHz() <= 121_000;
        final boolean qualifiedPrime = compatibleMotionDisplay
                && effective.runtimeTemporalState() == RuntimeTemporalState.PRIMING;
        if (motionRuntimeActive) {
            if (!wantsMotion || observation.requestGeneration() != motionDisplayGeneration) {
                scheduleMotionExit(TemporalTransitionController.ExitReason.DISPLAY_MISMATCH,
                        false);
                return;
            }
            long nowNs = System.nanoTime();
            DisplayLeaseWatchdog.Token token = displayLeaseWatchdog.arm(
                    surfaceEpoch, observation.requestGeneration(), nowNs);
            if (!view.updateMotionLease(observation.requestGeneration(), token.deadlineNs())) {
                scheduleMotionExit(TemporalTransitionController.ExitReason.LEASE_EXPIRED, false);
            }
        } else if (motionShadowActive) {
            boolean leaseFresh = compatibleMotionDisplay
                    && observation.requestGeneration() == motionDisplayGeneration;
            if (leaseFresh) {
                displayLeaseWatchdog.arm(surfaceEpoch, observation.requestGeneration(),
                        System.nanoTime());
            }
            NativePresenterStats stats = view.presenterStats();
            MotionShadowEvidence evidence = MotionShadowEvidence.from(stats,
                    shadowRuntimeFailureBaseline, leaseFresh, 5_000_000L);
            if (!evidence.sequencesContinuous() || !leaseFresh) {
                scheduleMotionExit(!leaseFresh
                                ? TemporalTransitionController.ExitReason.LEASE_EXPIRED
                                : TemporalTransitionController.ExitReason.SOURCE_SEQUENCE_GAP,
                        false);
                return;
            }
            if (evidence.adjacentPairCount() < 120
                    && evidence.peakArtifactRatio() <= 0.25) return;
            RuntimeTemporalState next = temporalTransitionController.observeShadow(
                    System.nanoTime(), evidence.adjacentPairCount(), evidence.artifactRatio(),
                    evidence.peakArtifactRatio(), evidence.sequencesContinuous(),
                    evidence.gpuBudgetPass(), evidence.displayLeaseFresh());
            if (next == RuntimeTemporalState.MOTION_COMPENSATING) {
                scheduleMotionEnter(observation);
            } else if (next == RuntimeTemporalState.BUFFERED_NATIVE_HOLD) {
                scheduleMotionExit(evidence.peakArtifactRatio() > 0.25
                                || evidence.artifactRatio() >= 0.10
                                ? TemporalTransitionController.ExitReason.ARTIFACT_RATIO
                                : TemporalTransitionController.ExitReason.MOTION_SHADER_FAILURE,
                        false);
            }
        } else if (surfaceRecoveryPending && compatibleMotionDisplay) {
            scheduleMotionEnter(observation);
        } else if (qualifiedPrime) {
            if (requestedRuntimeTemporalState
                    != RuntimeTemporalState.BUFFERED_NATIVE_HOLD) {
                scheduleMotionEnter(observation);
            }
        } else if (compatibleMotionDisplay
                && temporalTransitionController.beginShadowIfCooldownComplete(System.nanoTime())
                        == RuntimeTemporalState.PRIMING_SHADOW) {
            scheduleMotionShadow(observation);
        } else if (surfaceRecoveryPending && observation.stableForMs() >= 3_000L) {
            scheduleSurfaceRecoveryFallback();
        }
    }

    private void scheduleMotionShadow(DisplayObservation observation) {
        if (!motionTransitionInFlight.compareAndSet(false, true)) return;
        final long expectedEpoch = surfaceEpoch;
        final long expectedGeneration = observation.requestGeneration();
        displayLifecycleExecutor.execute(() -> {
            boolean wasRunning = session.state() == SessionState.RUNNING;
            boolean safePacingOwner = false;
            try {
                if (expectedEpoch <= 0L || expectedEpoch != surfaceEpoch
                        || !motionObservationStillQualified(observation)) {
                    throw new IllegalStateException("Shadow display lease changed before pause");
                }
                if (wasRunning) session.pause().get(2, TimeUnit.SECONDS);
                if (!stopAudioThread()) return;
                view.onPause();
                view.resetSequence();
                motionFrameDispatch.resetMotion();
                long transitionId = view.beginMotionShadow(
                        runtimeSourceTiming == SourceTiming.PAL_50 ? 50f : 60.0988f);
                safePacingOwner = transitionId > 0L && awaitTransition(transitionId,
                        NativePresenterStats.TEMPORAL_PRIMING_SHADOW, 2_000L);
                if (!safePacingOwner) {
                    throw new IllegalStateException("Shadow owner was not published");
                }
                NativePresenterStats stats = view.presenterStats();
                shadowRuntimeFailureBaseline = stats.runtimeFailureCount();
                motionDisplayGeneration = expectedGeneration;
                motionShadowActive = true;
                motionCaptureActive = true;
                requestedRuntimeTemporalState = RuntimeTemporalState.PRIMING_SHADOW;
                DisplayLeaseWatchdog.Token token = displayLeaseWatchdog.arm(
                        expectedEpoch, expectedGeneration, System.nanoTime());
                if (token.deadlineNs() <= System.nanoTime()) {
                    throw new IllegalStateException("Shadow display lease expired");
                }
            } catch (Exception failure) {
                Log.e(TAG, "paused Motion Shadow transaction failed", failure);
                motionCaptureActive = false;
                motionShadowActive = false;
                temporalTransitionController.enterHold(System.nanoTime());
                NativePresenterStats stats = view.presenterStats();
                if (stats.temporalState() == NativePresenterStats.TEMPORAL_PRIMING_SHADOW) {
                    long exitId = view.exitMotion(false);
                    safePacingOwner = exitId > 0L && awaitTransition(exitId,
                            NativePresenterStats.TEMPORAL_BUFFERED_HOLD, 2_000L);
                } else {
                    safePacingOwner = stats.pacingOwner()
                            == NativePresenterStats.PACING_OWNER_NATIVE;
                }
                requestedRuntimeTemporalState = safePacingOwner
                        ? RuntimeTemporalState.BUFFERED_NATIVE_HOLD
                        : RuntimeTemporalState.FALLBACK;
            } finally {
                if (safePacingOwner) view.onResume();
                if (safePacingOwner && wasRunning && session.state() == SessionState.PAUSED) {
                    try { session.resume().get(2, TimeUnit.SECONDS); }
                    catch (Exception failure) {
                        Log.e(TAG, "resume after Motion Shadow transaction failed", failure);
                    }
                }
                if (safePacingOwner && wasRunning
                        && (audio == null || !audio.isAlive()) && core.isCreated()) {
                    audio = createAudioThread();
                    audio.start();
                }
                motionTransitionInFlight.set(false);
            }
        });
    }

    private void scheduleMotionEnter(DisplayObservation observation) {
        if (!motionTransitionInFlight.compareAndSet(false, true)) return;
        final long expectedEpoch = surfaceEpoch;
        final long expectedGeneration = observation.requestGeneration();
        final float displayHz = observation.systemReportedActiveMode().refreshMilliHz() / 1000f;
        final boolean enteringFromShadow = motionShadowActive;
        displayLifecycleExecutor.execute(() -> {
            final boolean recoveringSurface = surfaceRecoveryPending;
            boolean wasRunning = session.state() == SessionState.RUNNING;
            boolean shouldResume = wasRunning
                    || (surfaceRecoveryPending && surfaceRecoveryWasRunning);
            boolean configured = false;
            boolean safePacingOwner = false;
            boolean recoveryCompleted = false;
            try {
                if (expectedEpoch <= 0L || expectedEpoch != surfaceEpoch
                        || !motionObservationStillQualified(observation)) return;
                if (wasRunning) session.pause().get(2, TimeUnit.SECONDS);
                runOnUiThread(() -> {
                    inputRouter.cancelAll();
                    gamepad.reset();
                });
                if (!stopAudioThread()) return;
                view.onPause();
                temporalAudioDelay.flush();
                temporalAudioDelay.enableOneFrameDelay();
                motionFrameDispatch.resetMotion();
                if (expectedEpoch != surfaceEpoch
                        || !motionObservationStillQualified(observation)) {
                    throw new IllegalStateException("Motion display lease changed during pause");
                }
                long nowNs = System.nanoTime();
                DisplayLeaseWatchdog.Token token = displayLeaseWatchdog.arm(
                        expectedEpoch, expectedGeneration, nowNs);
                configured = view.configureMotionForTesting(expectedGeneration, displayHz,
                        runtimeSourceTiming == SourceTiming.PAL_50 ? 50f : 60.0988f,
                        token.deadlineNs(), false);
                if (configured) {
                    if (!motionObservationStillQualified(observation)) {
                        throw new IllegalStateException(
                                "Motion display generation changed during native enter");
                    }
                    safePacingOwner = awaitPacingOwner(
                            NativePresenterStats.PACING_OWNER_MOTION, 1_000L);
                    if (!safePacingOwner) {
                        throw new IllegalStateException("Motion owner was not published");
                    }
                    motionDisplayGeneration = expectedGeneration;
                    motionCaptureActive = true;
                    motionRuntimeActive = true;
                    motionShadowActive = false;
                    requestedRuntimeTemporalState = RuntimeTemporalState.MOTION_COMPENSATING;
                    surfaceRecoveryPending = false;
                    surfaceRecoveryWasRunning = false;
                    recoveryCompleted = true;
                } else {
                    safePacingOwner = awaitPacingOwner(
                            NativePresenterStats.PACING_OWNER_NATIVE, 1_500L);
                    displayLeaseWatchdog.clear();
                    motionCaptureActive = false;
                    motionRuntimeActive = false;
                    motionShadowActive = false;
                    if (enteringFromShadow && safePacingOwner) {
                        temporalTransitionController.enterHold(System.nanoTime());
                        requestedRuntimeTemporalState = RuntimeTemporalState.BUFFERED_NATIVE_HOLD;
                    } else {
                        temporalAudioDelay.flush();
                        temporalAudioDelay.disableImmediately();
                        requestedRuntimeTemporalState = safePacingOwner
                                ? RuntimeTemporalState.IMMEDIATE_NATIVE
                                : RuntimeTemporalState.FALLBACK;
                    }
                    if (!recoveringSurface || SurfaceRecoveryIntentPolicy.completes(
                            true, safePacingOwner, expectedEpoch, surfaceEpoch)) {
                        surfaceRecoveryPending = false;
                        recoveryCompleted = recoveringSurface;
                    }
                }
            } catch (Exception failure) {
                Log.e(TAG, "paused Motion enter transaction failed", failure);
                displayLeaseWatchdog.clear();
                motionCaptureActive = false;
                motionRuntimeActive = false;
                motionShadowActive = false;
                NativePresenterStats failedStats = view.presenterStats();
                if (failedStats.pacingOwner() == NativePresenterStats.PACING_OWNER_MOTION
                        && expectedEpoch == surfaceEpoch) {
                    boolean drainFailure = recoveringSurface || !enteringFromShadow;
                    long exitId = view.exitMotion(drainFailure);
                    safePacingOwner = exitId >= 0L && awaitTransition(exitId,
                            drainFailure ? NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE
                                    : NativePresenterStats.TEMPORAL_BUFFERED_HOLD,
                            2_000L);
                } else {
                    safePacingOwner = failedStats.pacingOwner()
                            == NativePresenterStats.PACING_OWNER_NATIVE;
                }
                if (enteringFromShadow && safePacingOwner) {
                    temporalTransitionController.enterHold(System.nanoTime());
                    requestedRuntimeTemporalState = RuntimeTemporalState.BUFFERED_NATIVE_HOLD;
                } else {
                    temporalAudioDelay.flush();
                    temporalAudioDelay.disableImmediately();
                    requestedRuntimeTemporalState = safePacingOwner
                            ? RuntimeTemporalState.IMMEDIATE_NATIVE
                            : RuntimeTemporalState.FALLBACK;
                }
                if (!recoveringSurface || SurfaceRecoveryIntentPolicy.completes(
                        true, safePacingOwner, expectedEpoch, surfaceEpoch)) {
                    surfaceRecoveryPending = false;
                    recoveryCompleted = recoveringSurface;
                }
            } finally {
                if (safePacingOwner) view.onResume();
                if (safePacingOwner && shouldResume
                        && session.state() == SessionState.PAUSED) {
                    try { session.resume().get(2, TimeUnit.SECONDS); }
                    catch (Exception failure) {
                        Log.e(TAG, "resume after Motion transaction failed", failure);
                    }
                }
                if (safePacingOwner && shouldResume
                        && (audio == null || !audio.isAlive()) && core.isCreated()) {
                    audio = createAudioThread();
                    audio.start();
                }
                if (recoveryCompleted) surfaceRecoveryWasRunning = false;
                motionTransitionInFlight.set(false);
            }
        });
    }

    private void scheduleSurfaceRecoveryFallback() {
        if (!surfaceRecoveryPending
                || !motionTransitionInFlight.compareAndSet(false, true)) return;
        displayLifecycleExecutor.execute(() -> {
            boolean shouldResume = surfaceRecoveryWasRunning;
            try {
                temporalAudioDelay.flush();
                temporalAudioDelay.disableImmediately();
                motionCaptureActive = false;
                motionRuntimeActive = false;
                motionShadowActive = false;
                requestedRuntimeTemporalState = RuntimeTemporalState.IMMEDIATE_NATIVE;
                view.resetSequence();
                motionFrameDispatch.resetMotion();
                view.onResume();
                if (shouldResume && session.state() == SessionState.PAUSED) {
                    session.resume().get(2, TimeUnit.SECONDS);
                }
                if (shouldResume && (audio == null || !audio.isAlive()) && core.isCreated()) {
                    audio = createAudioThread();
                    audio.start();
                }
            } catch (Exception failure) {
                Log.e(TAG, "new-surface fallback recovery failed", failure);
                requestedRuntimeTemporalState = RuntimeTemporalState.FALLBACK;
            } finally {
                surfaceRecoveryPending = false;
                surfaceRecoveryWasRunning = false;
                motionTransitionInFlight.set(false);
            }
        });
    }

    private boolean motionObservationStillQualified(DisplayObservation expected) {
        DisplayObservation current = lastDisplayObservation;
        if (current == null || current.requestGeneration() != expected.requestGeneration()
                || current.stableForMs() < 3_000L
                || current.systemReportedActiveMode() == null
                || expected.systemReportedActiveMode() == null) return false;
        com.flynes.emu.video.quality.DisplayModeCapability a =
                current.systemReportedActiveMode();
        com.flynes.emu.video.quality.DisplayModeCapability b =
                expected.systemReportedActiveMode();
        return a.modeId() == b.modeId() && a.width() == b.width() && a.height() == b.height()
                && a.refreshMilliHz() == b.refreshMilliHz()
                && a.refreshMilliHz() >= 119_000 && a.refreshMilliHz() <= 121_000;
    }

    private boolean awaitPacingOwner(int expectedOwner, long timeoutMs) {
        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        do {
            if (view.presenterStats().pacingOwner() == expectedOwner) return true;
            try { Thread.sleep(10L); }
            catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                return false;
            }
        } while (SystemClock.elapsedRealtime() < deadline);
        return view.presenterStats().pacingOwner() == expectedOwner;
    }

    private boolean awaitTransition(long transitionId, int expectedState, long timeoutMs) {
        long deadline = SystemClock.elapsedRealtime() + timeoutMs;
        do {
            NativePresenterStats stats = view.presenterStats();
            if (stats.lastTransitionId() > transitionId) return false;
            if (stats.lastTransitionId() == transitionId
                    && stats.temporalState() == expectedState
                    && stats.pacingOwner() == NativePresenterStats.PACING_OWNER_NATIVE) {
                return true;
            }
            try { Thread.sleep(10L); }
            catch (InterruptedException interrupted) {
                Thread.currentThread().interrupt();
                return false;
            }
        } while (SystemClock.elapsedRealtime() < deadline);
        return false;
    }

    private void scheduleMotionExit(TemporalTransitionController.ExitReason reason,
                                    boolean forceDrain) {
        if ((!motionRuntimeActive && !motionShadowActive)
                || !motionTransitionInFlight.compareAndSet(false, true)) return;
        final boolean exitingShadow = motionShadowActive;
        displayLifecycleExecutor.execute(() -> {
            boolean wasRunning = session.state() == SessionState.RUNNING;
            boolean safePacingOwner = false;
            try {
                TemporalTransitionController.Decision decision =
                        TemporalTransitionController.onExit(
                                exitingShadow ? RuntimeTemporalState.PRIMING_SHADOW
                                        : RuntimeTemporalState.MOTION_COMPENSATING,
                                reason, false);
                boolean drain = forceDrain
                        || decision.target() == RuntimeTemporalState.DRAINING;
                if (wasRunning) session.pause().get(2, TimeUnit.SECONDS);
                if (!stopAudioThread()) return;
                view.onPause();
                long exitId = view.exitMotion(drain);
                safePacingOwner = exitId >= 0L && awaitTransition(exitId,
                        drain ? NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE
                                : NativePresenterStats.TEMPORAL_BUFFERED_HOLD,
                        2_000L);
                displayLeaseWatchdog.clear();
                motionCaptureActive = false;
                motionRuntimeActive = false;
                motionShadowActive = false;
                requestedRuntimeTemporalState = safePacingOwner ? decision.target()
                        : RuntimeTemporalState.FALLBACK;
                if (drain) {
                    if (temporalAudioDelay.removeOneFrameDelay() == 0) {
                        temporalAudioDelay.flush();
                        temporalAudioDelay.disableImmediately();
                    }
                } else {
                    temporalTransitionController.enterHold(System.nanoTime());
                }
            } catch (Exception failure) {
                Log.e(TAG, "Motion exit transaction failed", failure);
                requestedRuntimeTemporalState = RuntimeTemporalState.FALLBACK;
            } finally {
                if (safePacingOwner) view.onResume();
                if (safePacingOwner && wasRunning && session.state() == SessionState.PAUSED) {
                    try { session.resume().get(2, TimeUnit.SECONDS); }
                    catch (Exception failure) {
                        Log.e(TAG, "resume after Motion exit failed", failure);
                    }
                }
                if (safePacingOwner && wasRunning
                        && (audio == null || !audio.isAlive()) && core.isCreated()) {
                    audio = createAudioThread();
                    audio.start();
                }
                motionTransitionInFlight.set(false);
            }
        });
    }

    private LegacyVideoRuntimeAdapter runtimeVideo() {
        return LegacyVideoRuntimeAdapter.project(
                resolveEffectiveVideoConfig(lastDisplayObservation));
    }

    private EffectiveVideoConfig resolveEffectiveVideoConfig(
            DisplayObservation observation) {
        AndroidDisplayPlatformFacade platform = new AndroidDisplayPlatformFacade(
                getWindowManager().getDefaultDisplay());
        DisplayCapabilities capabilities = new DisplayCapabilitiesReader().read(
                platform, runtimeGlCapabilities);
        RuntimeConstraints constraints = new RuntimeConstraints(runtimeSourceTiming,
                observation, false, 100, 0f, ThermalBand.NONE,
                appSettings.videoPreferences().adaptiveProtection(),
                presenterRuntimeFailures, requestedRuntimeTemporalState);
        EffectiveVideoConfig effective = displayQualityResolver.resolve(
                appSettings.videoPreferences(), appSettings.aspectMode(), capabilities,
                bundledAlgorithms, constraints, SystemClock.elapsedRealtime());
        for (FallbackReason fallback : effective.fallbacks()) videoStatus.onFallback(fallback);
        return effective;
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
                if (motionRuntimeActive || motionShadowActive) {
                    motionCaptureActive = false;
                    displayLeaseWatchdog.clear();
                    long exitId = view.exitMotion(true);
                    if (exitId >= 0L && awaitTransition(exitId,
                            NativePresenterStats.TEMPORAL_IMMEDIATE_NATIVE, 2_000L)) {
                        motionRuntimeActive = false;
                        motionShadowActive = false;
                        requestedRuntimeTemporalState = RuntimeTemporalState.IMMEDIATE_NATIVE;
                        if (temporalAudioDelay.removeOneFrameDelay() == 0) {
                            temporalAudioDelay.flush();
                            temporalAudioDelay.disableImmediately();
                        }
                    }
                }
                if (!isFinishing() && session.state() == SessionState.RUNNING) {
                    audio = createAudioThread();
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
            lastPresenterStats = view.presenterStats();
            statusHandler.removeCallbacks(publishVideoStatus);
            statusHandler.post(publishVideoStatus);
            view.onResume();
        }
    }

    private void calibrateClockDomain() {
        for (int attempt = 0; attempt < 5; attempt++) {
            long before = System.nanoTime();
            long nativeNow = core.nativeMonotonicTimeNs();
            long after = System.nanoTime();
            clockCalibrator.addPairedSample(before, nativeNow, after);
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

    private void collectNativePresenterStats() {
        NativePresenterStats current = view.presenterStats();
        AvSyncMonitor.Sample avSample = avSyncMonitor.latestFresh(
                System.nanoTime(), 1_000_000_000L);
        videoStatus.onAvSync(avSample.valid(), avSample.skewNs(), avSample.uncertaintyNs());
        long uploaded = Math.max(0L,
                current.uploadedFrames() - lastPresenterStats.uploadedFrames());
        long submitted = Math.max(0L,
                current.submittedFrames() - lastPresenterStats.submittedFrames());
        videoStatus.onNativePresentationCounts(uploaded, submitted);
        videoStatus.onNativeMotionCounts(
                Math.max(0L, current.interpolatedSlots()
                        - lastPresenterStats.interpolatedSlots()),
                Math.max(0L, current.warpedSlots() - lastPresenterStats.warpedSlots()),
                Math.max(0L, current.heldSlots() - lastPresenterStats.heldSlots()),
                Math.max(0L, current.cadenceAdjustments()
                        - lastPresenterStats.cadenceAdjustments()),
                current.motionQueueDepth());
        if (current.runtimeFailureCount() > lastPresenterStats.runtimeFailureCount()) {
            videoStatus.onFallback(FallbackReason.RUNTIME_FAILURE);
            if (motionRuntimeActive || motionShadowActive) {
                TemporalTransitionController.ExitReason exitReason = switch (
                        current.runtimeFailureCode()) {
                    case 101 -> TemporalTransitionController.ExitReason.SOURCE_SEQUENCE_GAP;
                    case 102 -> TemporalTransitionController.ExitReason.STAGING_OVERFLOW;
                    default -> TemporalTransitionController.ExitReason.MOTION_SHADER_FAILURE;
                };
                scheduleMotionExit(exitReason, false);
            }
            RuntimeFailure failure = PresenterFailureMapper.fromNativeCode(
                    current.runtimeFailureCode());
            if (failure != null && presenterRuntimeFailures.add(failure)) {
                // The presenter has already emitted emergency Nearest for the failed frame.
                // Re-resolve once so every following frame uses a complete qualified config.
                applyRuntimeVideoSettings();
            }
        }
        lastPresenterStats = current;
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
