export interface CatalogSmokeResult {
  generation: string;
  count: string;
}

export interface CatalogUserState {
  favorite: boolean;
  lastPlayedSequence: number;
}

export interface GameCenterRow {
  canonicalId: string; titleEn: string; titleZhHans: string;
  builtin: boolean; favorite: boolean; lastPlayedSequence: number;
  originalFilename: string; sourceUuidHex: string; sourceRelativePath: string;
  packageFormat: number;
}

export interface HitMapDto {
  dpadLeft: number;
  dpadTop: number;
  dpadRight: number;
  dpadBottom: number;
  controls: Array<{
    control: string;
    centerX: number;
    centerY: number;
    width: number;
    height: number;
    shape: string;
  }>;
  directionMode: number;
  deadZone: number;
  joystickRadius: number;
  joystickTravelRadius: number;
}

export interface PlayStepResult {
  frameIndex: number;
  width: number;
  height: number;
  format: number;
  bytesWritten: number;
  pcmSampleCount: number;
  appliedButtons: number;
  rgb565: ArrayBuffer;
  pcm: ArrayBuffer;
}

export interface SettingsDto {
  aspectMode: number;
  videoQualityPreset: number;
  customRefreshPolicy: number;
  customTemporalMode: number;
  customSpatialMode: number;
  customPostEffect: number;
  adaptiveProtection: number;
  layoutPreset: number;
  directionMode: number;
  buttonScale: number;
  verticalOffset: number;
  controlOpacity: number;
  joystickScale: number;
  deadZone: number;
  hapticLevel: number;
  distinctAbHaptics: number;
  audioEnabled: number;
  audioFocusPolicy: number;
  autosaveEnabled: number;
  localeTag: string;
  lastPlayedId: string;
}

export interface SourceStatusDto {
  uuidHex: string;
  sourceScope: number;
  lastCompleteness: number;
  freshness: number;
}

export interface ScanFileResultDto {
  outcome: number;
  reason: number;
  variantCount: number;
}

export interface ScanJobFileDto {
  relativePath: string;
  displayName: string;
  borrowedFd: number;
  declaredSize: number;
}

export interface ScanJobStatusDto {
  id: number;
  phase: number;
  processedFiles: number;
  resultCount: number;
  cancelRequested: boolean;
  error: string;
}

export interface RenderStatusDto {
  componentBound: boolean;
  surfaceReady: boolean;
  nativeReady: boolean;
  fallbackActive: boolean;
  timingValid: boolean;
  displayRequestAccepted: boolean;
  displayObservationValid: boolean;
  motionQualified: boolean;
  protectionObservationValid: boolean;
  thermalLimited: boolean;
  lowBattery: boolean;
  surfaceGeneration: number;
  sourceFrames: number;
  uploadedFrames: number;
  presentedFrames: number;
  presentFailures: number;
  requestedSpatial: number;
  effectiveSpatial: number;
  requestedPost: number;
  vsyncPeriodNanos: number;
  gpuTimingValid: boolean;
  gpuTimeNanos: number;
  gpuTimeMaxNanos: number;
  gpuTimingSamples: number;
  requestedRefreshHz: number;
  actualRefreshMilliHz: number;
  effectiveRefreshHz: number;
  displayRequestGeneration: number;
  motionSourceSlots: number;
  motionSynthesizedSlots: number;
  motionHoldSlots: number;
  motionAdjacentPairs: number;
  fallbackReason: string;
  displayFallbackReason: string;
  temporalState: string;
  temporalFallbackReason: string;
}

export interface PlayRuntimeStatusDto {
  running: boolean;
  paused: boolean;
  audioReady: boolean;
  audioStarted: boolean;
  audioTimestampValid: boolean;
  sourceFrames: number;
  audioUnderflows: number;
  audioPostFallbackUnderflows: number;
  audioLockMisses: number;
  audioShortReads: number;
  audioPrimingCallbacks: number;
  audioDroppedSamples: number;
  audioProducedSamples: number;
  audioConsumedSamples: number;
  audioQueuedSamples: number;
  audioHighWaterSamples: number;
  audioCallbackFrames: number;
  audioSampleRate: number;
  audioChannelCount: number;
  audioLastCallbackBytes: number;
  audioCallbackCount: number;
  audioFastPath: boolean;
  audioFallbackReason: string;
  audioDelaySamples: number;
  audioTemporalState: string;
  audioFramePosition: number;
  audioTimestampNanos: number;
  sourceFps: number;
  sourceStandard: string;
  error: string;
}

export const catalogSmoke: (dataRoot: string, cacheRoot: string) => CatalogSmokeResult;
export const catalogSnapshot: () => GameCenterRow[];
export const catalogFavoriteSet: (canonicalId: string, favorite: boolean) => void;
export const catalogMarkPlayed: (canonicalId: string) => void;
export const catalogUserStateGet: (canonicalId: string) => CatalogUserState;
export const gameCenterFilter: (rows: GameCenterRow[], category: string, query: string) => GameCenterRow[];
export const controlLayoutRecommended: () => string;
export const controlLayoutDecodeOrRecommended: (value: string) => string;
export const controlLayoutGet: () => string;
export const controlLayoutApply: (utf8: string) => string;
export const hitMapFromLayout: (width: number, height: number, density: number,
  insetL: number, insetR: number, insetT: number, insetB: number, layoutUtf8: string,
  directionMode: number, deadZone: number) => HitMapDto;
export const pauseCommands: () => string[];
export const appOpen: (dataRoot: string, cacheRoot: string) => void;
export const appClose: () => void;
export const settingsGet: () => SettingsDto;
export const settingsApply: (snapshot: SettingsDto) => SettingsDto;
export const sourceStatusList: () => SourceStatusDto[];
export const sourceRemove: (sourceUuidHex: string) => void;
export const scanBegin: (sourceUuidHex: string, sourceScope: number) => void;
export const scanAddFile: (relativePath: string, displayName: string, borrowedFd: number,
  declaredSize: number) => ScanFileResultDto;
export const scanCommit: (completeness: number) => void;
export const scanAbort: () => void;
export const scanJobStart: (sourceUuidHex: string, sourceScope: number,
  files: ScanJobFileDto[], completeness: number) => number;
export const scanJobStatus: (jobId: number) => ScanJobStatusDto;
export const scanJobCancel: (jobId: number) => boolean;
export const playOpen: (rom: Uint8Array | ArrayBuffer) => void;
export const playDecodePackage: (rom: Uint8Array | ArrayBuffer, packageFormat: number,
  zipEntryName: string) => ArrayBuffer;
export const playSetButtons: (buttons: number) => void;
export const playStep: () => PlayStepResult;
export const playSetPaused: (paused: boolean) => void;
export const playSetAudioMuted: (muted: boolean) => void;
export const playRuntimeStatus: () => PlayRuntimeStatusDto;
export const renderConfigure: (refresh: number, temporal: number, spatial: number,
  post: number, adaptiveProtection: boolean) => void;
export const renderSetPaused: (paused: boolean) => void;
export const renderSetProtection: (thermalLimited: boolean, lowBattery: boolean,
  observationValid: boolean) => void;
export const renderStatus: () => RenderStatusDto;
export const playSaveCheckpoint: () => ArrayBuffer;
export const playLoadCheckpoint: (checkpoint: Uint8Array | ArrayBuffer) => void;
export const playClose: () => void;
