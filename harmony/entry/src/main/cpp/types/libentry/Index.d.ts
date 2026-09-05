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

export const catalogSmoke: (dataRoot: string, cacheRoot: string) => CatalogSmokeResult;
export const gameCenterFilter: (rows: GameCenterRow[], category: string, query: string) => GameCenterRow[];
export const controlLayoutRecommended: () => string;
export const controlLayoutDecodeOrRecommended: (value: string) => string;
export const hitMapFromLayout: (width: number, height: number, density: number,
  insetL: number, insetR: number, insetT: number, insetB: number, layoutUtf8: string,
  directionMode: number, deadZone: number) => HitMapDto;
export const pauseCommands: () => string[];
export const playOpen: (rom: Uint8Array | ArrayBuffer) => void;
export const playSetButtons: (buttons: number) => void;
export const playStep: () => PlayStepResult;
export const playSaveCheckpoint: () => ArrayBuffer;
export const playLoadCheckpoint: (checkpoint: Uint8Array | ArrayBuffer) => void;
export const playClose: () => void;
