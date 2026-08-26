package com.flynes.emu.video.status;

import com.flynes.emu.video.power.TemporalTransition;
import com.flynes.emu.video.power.ThermalBand;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.FallbackReason;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.quality.VideoConfigurationKey;

import java.util.Collections;
import java.util.HashSet;
import java.util.Set;

/** Render-thread metrics collector. Configuration identity has no split setters. */
public final class VideoStatusAccumulator {
    private long windowStartedAtElapsedMs;
    private SourceTiming sourceTiming = SourceTiming.UNKNOWN;
    private float sourceNominalFps;
    private long coreFrames;
    private long textureUploads;
    private long synthesisSlots;
    private long motionWarpedSlots;
    private long bufferSubmissions;
    private final Set<Long> copiedSourceSequences = new HashSet<>();
    private long surfaceEpoch;
    private long displayRequestGeneration;
    private String activeConfigurationId;
    private VideoConfigurationKey activeConfigurationKey;
    private PhysicalRefreshPolicy requestedPolicy = PhysicalRefreshPolicy.FOLLOW_SYSTEM;
    private DisplayModeCapability requestedMode;
    private DisplayModeCapability activeMode;
    private RuntimeTemporalState runtimeTemporalState = RuntimeTemporalState.IMMEDIATE_NATIVE;
    private int videoDelayFrames;
    private float audioDelayMs;
    private int videoQueueDepth;
    private int audioQueueDepthSamples;
    private TemporalTransition temporalTransition;
    private long cadenceAdjustments;
    private float safetyFallbackRatio;
    private ThermalBand thermalBand = ThermalBand.NONE;

    public synchronized void beginWindow(long startedAtElapsedRealtimeMs,
                                         SourceTiming sourceTiming,
                                         float sourceNominalFps) {
        this.windowStartedAtElapsedMs = startedAtElapsedRealtimeMs;
        this.sourceTiming = sourceTiming;
        this.sourceNominalFps = sourceNominalFps;
        coreFrames = textureUploads = synthesisSlots = motionWarpedSlots = bufferSubmissions = 0L;
        copiedSourceSequences.clear();
    }

    public synchronized void onCoreFrameProduced(long sequence) { coreFrames++; }
    public synchronized void onSourceFrameCopied(long sequence) {
        copiedSourceSequences.add(sequence);
    }
    public synchronized void onTextureUploaded() { textureUploads++; }
    public synchronized void onSynthesisSlotSubmitted(boolean motionWarped) {
        synthesisSlots++;
        if (motionWarped) motionWarpedSlots++;
    }
    public synchronized void onBufferSubmitted() { bufferSubmissions++; }
    public synchronized void onCadenceAdjusted() { cadenceAdjustments++; }
    public synchronized void onSafetyFallbackRatio(float ratio) { safetyFallbackRatio = ratio; }
    public synchronized void onQueueDepths(int videoDepth, int audioDepthSamples) {
        videoQueueDepth = videoDepth;
        audioQueueDepthSamples = audioDepthSamples;
    }

    public synchronized void onDisplayState(PhysicalRefreshPolicy policy,
                                            DisplayModeCapability requested,
                                            DisplayModeCapability systemReportedActive) {
        requestedPolicy = policy;
        requestedMode = requested;
        activeMode = systemReportedActive;
    }

    public synchronized void onThermalBand(ThermalBand band) { thermalBand = band; }

    public synchronized void publishStableConfiguration(
            long newSurfaceEpoch, long newDisplayRequestGeneration,
            String configurationId, VideoConfigurationKey configurationKey,
            RuntimeTemporalState temporalState, int delayFrames, float delayMs) {
        if (configurationId == null || configurationKey == null) {
            throw new IllegalArgumentException("stable configuration identity is required");
        }
        surfaceEpoch = newSurfaceEpoch;
        displayRequestGeneration = newDisplayRequestGeneration;
        activeConfigurationId = configurationId;
        activeConfigurationKey = configurationKey;
        runtimeTemporalState = temporalState;
        videoDelayFrames = delayFrames;
        audioDelayMs = delayMs;
        temporalTransition = null;
    }

    public synchronized void publishTransition(long newSurfaceEpoch,
                                               long newDisplayRequestGeneration,
                                               RuntimeTemporalState temporalState,
                                               int delayFrames, float delayMs) {
        surfaceEpoch = newSurfaceEpoch;
        displayRequestGeneration = newDisplayRequestGeneration;
        activeConfigurationId = null;
        activeConfigurationKey = null;
        runtimeTemporalState = temporalState;
        videoDelayFrames = delayFrames;
        audioDelayMs = delayMs;
        temporalTransition = null;
    }

    public synchronized VideoRuntimeStatus snapshot(long nowElapsedRealtimeMs) {
        float seconds = Math.max(0L, nowElapsedRealtimeMs - windowStartedAtElapsedMs) / 1000.0f;
        float divisor = seconds > 0.0f ? seconds : Float.POSITIVE_INFINITY;
        long copied = copiedSourceSequences.size();
        return new VideoRuntimeStatus(surfaceEpoch, displayRequestGeneration,
                activeConfigurationId, activeConfigurationKey, sourceTiming, sourceNominalFps,
                coreFrames / divisor, copied / divisor, textureUploads / divisor,
                Math.max(0L, coreFrames - copied), requestedPolicy, requestedMode, activeMode,
                synthesisSlots / divisor, motionWarpedSlots / divisor,
                bufferSubmissions / divisor, safetyFallbackRatio, runtimeTemporalState,
                videoDelayFrames, audioDelayMs, videoQueueDepth, audioQueueDepthSamples,
                temporalTransition, cadenceAdjustments, thermalBand, StatusFreshness.FRESH,
                nowElapsedRealtimeMs, Collections.<FallbackReason>emptyList());
    }
}
