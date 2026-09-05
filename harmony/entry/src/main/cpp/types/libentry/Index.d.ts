export interface CatalogSmokeResult {
  generation: string;
  count: string;
}

export interface GameCenterRow {
  canonicalId: string; titleEn: string; titleZhHans: string;
  builtin: boolean; favorite: boolean; lastPlayedSequence: number;
  originalFilename: string;
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

export const catalogSmoke: (dataRoot: string, cacheRoot: string) => CatalogSmokeResult;
export const catalogSnapshot: () => GameCenterRow[];
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
export const scanBegin: (sourceUuidHex: string, sourceScope: number) => void;
export const scanAddFile: (relativePath: string, displayName: string, borrowedFd: number,
  declaredSize: number) => ScanFileResultDto;
export const scanCommit: (completeness: number) => void;
export const scanAbort: () => void;
export const playOpen: (rom: Uint8Array | ArrayBuffer) => void;
export const playSetButtons: (buttons: number) => void;
export const playStep: () => PlayStepResult;
export const playSaveCheckpoint: () => ArrayBuffer;
export const playClose: () => void;
