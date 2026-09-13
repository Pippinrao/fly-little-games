import nativePlay, { PlayStepResult } from 'libentry.so';
import { image } from '@kit.ImageKit';
import { common } from '@kit.AbilityKit';
import { fileIo } from '@kit.CoreFileKit';
import { HarmonySourceMap } from '../platform/HarmonySourceMap';
import { BuiltinGames } from './BuiltinGames';
import { CheckpointStore } from './CheckpointStore';

export const NES_BTN_A = 0x01;
export const NES_BTN_B = 0x02;
export const NES_BTN_SELECT = 0x04;
export const NES_BTN_START = 0x08;
export const NES_BTN_UP = 0x10;
export const NES_BTN_DOWN = 0x20;
export const NES_BTN_LEFT = 0x40;
export const NES_BTN_RIGHT = 0x80;

export let lastRomOpenError: string = '';

export function rememberRomOpenError(message: string): void {
  lastRomOpenError = message;
}

export function takeRomOpenError(): string {
  const value = lastRomOpenError;
  lastRomOpenError = '';
  return value;
}

let cachedBuiltinGames: BuiltinGames | undefined = undefined;

/**
 * The bundled games, loaded once per process from the shared manifest. A missing
 * or broken manifest yields an empty list rather than taking playback down.
 * Synchronous so catalog projection can stay on the UI thread.
 */
export function builtinGamesSync(context: common.UIAbilityContext): BuiltinGames {
  if (cachedBuiltinGames === undefined) {
    try {
      cachedBuiltinGames = BuiltinGames.loadSync(context);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'builtin manifest unreadable';
      console.error(`FlyNES builtin manifest failed: ${detail}`);
      cachedBuiltinGames = BuiltinGames.empty();
    }
  }
  return cachedBuiltinGames;
}

export class RomLaunchLocator {
  canonicalId: string = '';
  sourceUuidHex: string = '';
  sourceRelativePath: string = '';
  packageFormat: number = 0;
  zipEntryName: string = '';
}

export class PlayService {
  private buttons: number = 0;
  private audioMuted: boolean = false;
  private outputPaused: boolean = false;
  private opened: boolean = false;
  private checkpointStore: CheckpointStore | undefined = undefined;
  private checkpointId: string = '';

  async open(context: common.UIAbilityContext, locator: RomLaunchLocator,
    restoreCheckpoint: boolean = true): Promise<void> {
    lastRomOpenError = '';
    let rom: Uint8Array | ArrayBuffer;
    const games = builtinGamesSync(context);
    const bundled = games.byCanonicalId(locator.canonicalId);
    if (bundled !== undefined) {
      try {
        rom = context.resourceManager.getRawFileContentSync(bundled.assetPath());
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'Failed to read ROM rawfile';
        throw new Error(detail);
      }
    } else {
      rom = await this.loadUserRom(context, locator);
    }
    nativePlay.playOpen(rom);
    this.opened = true;
    this.checkpointStore = new CheckpointStore(context.filesDir);
    this.checkpointId = locator.canonicalId;
    if (restoreCheckpoint && this.checkpointId.length > 0) {
      try {
        const checkpoint = this.checkpointStore.read(this.checkpointId);
        if (checkpoint !== undefined) {
          nativePlay.playLoadCheckpoint(checkpoint);
          console.info(`FlyNES autosave restored for ${this.checkpointId}`);
        }
      } catch (error) {
        this.checkpointStore.quarantine(this.checkpointId);
        const detail = error instanceof Error ? error.message : 'autosave restore failed';
        console.error(`FlyNES corrupt autosave quarantined: ${detail}`);
      }
    }
    nativePlay.playSetAudioMuted(this.audioMuted);
  }

  setButtons(mask: number): void {
    this.buttons = mask;
    nativePlay.playSetButtons(this.buttons);
  }

  /** Settings parity: audio.enabled=0 mutes output while the game keeps running. */
  setAudioMuted(muted: boolean): void {
    this.audioMuted = muted;
    if (this.opened) {
      nativePlay.playSetAudioMuted(muted);
    }
  }

  stepFrame(): PlayStepResult {
    return nativePlay.playStep();
  }

  createFrame(rgb565: ArrayBuffer): image.PixelMap {
    return image.createPixelMapSync(rgb565, {
      size: { width: 256, height: 240 },
      srcPixelFormat: image.PixelMapFormat.RGB_565,
      pixelFormat: image.PixelMapFormat.RGB_565,
      editable: false
    });
  }

  saveCheckpoint(): void {
    if (!this.opened || this.checkpointStore === undefined || this.checkpointId.length === 0) {
      throw new Error('play session is not open');
    }
    this.checkpointStore.write(this.checkpointId, nativePlay.playSaveCheckpoint());
  }

  pauseOutput(): void {
    this.outputPaused = true;
    nativePlay.playSetPaused(true);
  }

  resumeOutput(): void {
    this.outputPaused = false;
    nativePlay.playSetPaused(false);
  }

  close(): void {
    try {
      if (this.opened) {
        nativePlay.playClose();
      }
    } finally {
      this.opened = false;
      this.checkpointStore = undefined;
      this.checkpointId = '';
      this.outputPaused = false;
      this.buttons = 0;
    }
  }

  private async loadUserRom(context: common.UIAbilityContext,
    locator: RomLaunchLocator): Promise<ArrayBuffer> {
    if (locator.sourceUuidHex.length !== 32 || locator.sourceRelativePath.length === 0) {
      throw new Error('This game could not be opened.');
    }
    const sourceMap = new HarmonySourceMap();
    await sourceMap.load(context);
    const base = sourceMap.get(locator.sourceUuidHex);
    if (base.length === 0) {
      throw new Error('This game could not be opened.');
    }
    const childUri = `${base}/${locator.sourceRelativePath}`;
    let file: fileIo.File | undefined = undefined;
    try {
      file = fileIo.openSync(childUri, fileIo.OpenMode.READ_ONLY);
      const stat = fileIo.statSync(file.fd);
      const physical = new ArrayBuffer(stat.size);
      fileIo.readSync(file.fd, physical);
      return nativePlay.playDecodePackage(physical, locator.packageFormat, locator.zipEntryName);
    } finally {
      if (file !== undefined) {
        fileIo.closeSync(file.fd);
      }
    }
  }

}
