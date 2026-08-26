package com.flynes.emu.video.status;

import com.flynes.emu.video.power.TemporalTransition;
import com.flynes.emu.video.power.ThermalBand;
import com.flynes.emu.video.quality.DisplayModeCapability;
import com.flynes.emu.video.quality.FallbackReason;
import com.flynes.emu.video.quality.PhysicalRefreshPolicy;
import com.flynes.emu.video.quality.RuntimeTemporalState;
import com.flynes.emu.video.quality.SourceTiming;
import com.flynes.emu.video.quality.VideoConfigurationKey;

import java.util.ArrayList;
import java.util.Collections;
import java.util.List;

public final class VideoRuntimeStatus {
    private final long surfaceEpoch;
    private final long displayRequestGeneration;
    private final String activeConfigurationId;
    private final VideoConfigurationKey activeConfigurationKey;
    private final SourceTiming sourceTiming;
    private final float sourceNominalFps;
    private final float coreProducedFps;
    private final float copiedUniqueSourceFps;
    private final float textureUploadFps;
    private final long sourceFramesNotCopied;
    private final PhysicalRefreshPolicy requestedPolicy;
    private final DisplayModeCapability requestedMode;
    private final DisplayModeCapability systemReportedActiveMode;
    private final float synthesizedSlotSubmitFps;
    private final float motionWarpedSlotFps;
    private final float appBufferSubmitFps;
    private final float safetyFallbackRatio;
    private final RuntimeTemporalState runtimeTemporalState;
    private final int videoDelayFrames;
    private final float audioDelayMs;
    private final int videoQueueDepth;
    private final int audioQueueDepthSamples;
    private final TemporalTransition temporalTransition;
    private final long cadenceAdjustmentCount;
    private final ThermalBand thermalBand;
    private final StatusFreshness freshness;
    private final long capturedAtElapsedRealtimeMs;
    private final List<FallbackReason> fallbacks;

    public VideoRuntimeStatus(long surfaceEpoch, long displayRequestGeneration,
                              String activeConfigurationId,
                              VideoConfigurationKey activeConfigurationKey,
                              SourceTiming sourceTiming, float sourceNominalFps,
                              float coreProducedFps, float copiedUniqueSourceFps,
                              float textureUploadFps, long sourceFramesNotCopied,
                              PhysicalRefreshPolicy requestedPolicy,
                              DisplayModeCapability requestedMode,
                              DisplayModeCapability systemReportedActiveMode,
                              float synthesizedSlotSubmitFps, float motionWarpedSlotFps,
                              float appBufferSubmitFps, float safetyFallbackRatio,
                              RuntimeTemporalState runtimeTemporalState,
                              int videoDelayFrames, float audioDelayMs,
                              int videoQueueDepth, int audioQueueDepthSamples,
                              TemporalTransition temporalTransition,
                              long cadenceAdjustmentCount, ThermalBand thermalBand,
                              StatusFreshness freshness, long capturedAtElapsedRealtimeMs,
                              List<FallbackReason> fallbacks) {
        if ((activeConfigurationId == null) != (activeConfigurationKey == null)) {
            throw new IllegalArgumentException("active configuration identity must be atomic");
        }
        this.surfaceEpoch = surfaceEpoch;
        this.displayRequestGeneration = displayRequestGeneration;
        this.activeConfigurationId = activeConfigurationId;
        this.activeConfigurationKey = activeConfigurationKey;
        this.sourceTiming = sourceTiming;
        this.sourceNominalFps = sourceNominalFps;
        this.coreProducedFps = coreProducedFps;
        this.copiedUniqueSourceFps = copiedUniqueSourceFps;
        this.textureUploadFps = textureUploadFps;
        this.sourceFramesNotCopied = sourceFramesNotCopied;
        this.requestedPolicy = requestedPolicy;
        this.requestedMode = requestedMode;
        this.systemReportedActiveMode = systemReportedActiveMode;
        this.synthesizedSlotSubmitFps = synthesizedSlotSubmitFps;
        this.motionWarpedSlotFps = motionWarpedSlotFps;
        this.appBufferSubmitFps = appBufferSubmitFps;
        this.safetyFallbackRatio = safetyFallbackRatio;
        this.runtimeTemporalState = runtimeTemporalState;
        this.videoDelayFrames = videoDelayFrames;
        this.audioDelayMs = audioDelayMs;
        this.videoQueueDepth = videoQueueDepth;
        this.audioQueueDepthSamples = audioQueueDepthSamples;
        this.temporalTransition = temporalTransition;
        this.cadenceAdjustmentCount = cadenceAdjustmentCount;
        this.thermalBand = thermalBand;
        this.freshness = freshness;
        this.capturedAtElapsedRealtimeMs = capturedAtElapsedRealtimeMs;
        this.fallbacks = Collections.unmodifiableList(new ArrayList<>(fallbacks));
    }

    public long surfaceEpoch() { return surfaceEpoch; }
    public long displayRequestGeneration() { return displayRequestGeneration; }
    public String activeConfigurationId() { return activeConfigurationId; }
    public VideoConfigurationKey activeConfigurationKey() { return activeConfigurationKey; }
    public SourceTiming sourceTiming() { return sourceTiming; }
    public float sourceNominalFps() { return sourceNominalFps; }
    public float coreProducedFps() { return coreProducedFps; }
    public float copiedUniqueSourceFps() { return copiedUniqueSourceFps; }
    public float textureUploadFps() { return textureUploadFps; }
    public long sourceFramesNotCopied() { return sourceFramesNotCopied; }
    public PhysicalRefreshPolicy requestedPolicy() { return requestedPolicy; }
    public DisplayModeCapability requestedMode() { return requestedMode; }
    public DisplayModeCapability systemReportedActiveMode() { return systemReportedActiveMode; }
    public float synthesizedSlotSubmitFps() { return synthesizedSlotSubmitFps; }
    public float motionWarpedSlotFps() { return motionWarpedSlotFps; }
    public float appBufferSubmitFps() { return appBufferSubmitFps; }
    public float safetyFallbackRatio() { return safetyFallbackRatio; }
    public RuntimeTemporalState runtimeTemporalState() { return runtimeTemporalState; }
    public int videoDelayFrames() { return videoDelayFrames; }
    public float audioDelayMs() { return audioDelayMs; }
    public int videoQueueDepth() { return videoQueueDepth; }
    public int audioQueueDepthSamples() { return audioQueueDepthSamples; }
    public TemporalTransition temporalTransition() { return temporalTransition; }
    public long cadenceAdjustmentCount() { return cadenceAdjustmentCount; }
    public ThermalBand thermalBand() { return thermalBand; }
    public StatusFreshness freshness() { return freshness; }
    public long capturedAtElapsedRealtimeMs() { return capturedAtElapsedRealtimeMs; }
    public List<FallbackReason> fallbacks() { return fallbacks; }

    public StatusFreshness freshnessAt(long nowElapsedRealtimeMs, long ttlMs) {
        return StatusFreshness.at(capturedAtElapsedRealtimeMs, nowElapsedRealtimeMs, ttlMs);
    }
}
