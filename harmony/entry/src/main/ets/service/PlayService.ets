import nativePlay from 'libentry.so';
import { image } from '@kit.ImageKit';
import { audio } from '@kit.AudioKit';
import { common } from '@kit.AbilityKit';

export const NES_BTN_A = 0x01;
export const NES_BTN_B = 0x02;
export const NES_BTN_SELECT = 0x04;
export const NES_BTN_START = 0x08;
export const NES_BTN_UP = 0x10;
export const NES_BTN_DOWN = 0x20;
export const NES_BTN_LEFT = 0x40;
export const NES_BTN_RIGHT = 0x80;

export function mapsToBuiltinFromBelow(canonicalId: string): boolean {
  const id = canonicalId.trim().toLowerCase();
  return id === 'builtin' || id.indexOf('from_below') >= 0 || id.indexOf('from-below') >= 0;
}

export class PlayService {
  private buttons: number = 0;
  private renderer: audio.AudioRenderer | undefined = undefined;
  private pendingPcm: ArrayBuffer[] = [];
  private pendingOffset: number = 0;
  private outputPaused: boolean = false;
  private readonly onWriteData = (data: ArrayBuffer): audio.AudioDataCallbackResult => {
    this.fillAudioBuffer(data);
    return audio.AudioDataCallbackResult.VALID;
  };

  async open(context: common.UIAbilityContext, canonicalId: string): Promise<void> {
    if (!mapsToBuiltinFromBelow(canonicalId)) {
      throw new Error('This game could not be opened.');
    }
    let rom: Uint8Array;
    try {
      rom = context.resourceManager.getRawFileContentSync('from_below.nes');
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'Failed to read ROM rawfile';
      throw new Error(detail);
    }
    nativePlay.playOpen(rom);
    try {
      this.renderer = await audio.createAudioRenderer({
        streamInfo: {
          samplingRate: audio.AudioSamplingRate.SAMPLE_RATE_48000,
          channels: audio.AudioChannel.CHANNEL_1,
          sampleFormat: audio.AudioSampleFormat.SAMPLE_FORMAT_S16LE,
          encodingType: audio.AudioEncodingType.ENCODING_TYPE_RAW
        },
        rendererInfo: {
          usage: audio.StreamUsage.STREAM_USAGE_GAME,
          rendererFlags: 0
        }
      });
      this.renderer.on('writeData', this.onWriteData);
      await this.renderer.start();
    } catch (error) {
      this.renderer = undefined;
      const detail = error instanceof Error ? error.message : 'audio start failed';
      console.error(`FlyNES audio disabled: ${detail}`);
    }
  }

  setButtons(mask: number): void {
    this.buttons = mask;
    nativePlay.playSetButtons(this.buttons);
  }

  stepFrame() {
    const result = nativePlay.playStep();
    if (!this.outputPaused && result.pcm.byteLength > 0) {
      this.pendingPcm.push(result.pcm);
    }
    return result;
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
    nativePlay.playSaveCheckpoint();
  }

  pauseOutput(): void {
    this.outputPaused = true;
    this.pendingPcm = [];
    this.pendingOffset = 0;
    const renderer = this.renderer;
    if (renderer !== undefined) {
      renderer.pause().catch((error: Object) => {
        const detail = error instanceof Error ? error.message : 'audio pause failed';
        console.error(`FlyNES audio pause failed: ${detail}`);
      });
    }
  }

  resumeOutput(): void {
    this.outputPaused = false;
    const renderer = this.renderer;
    if (renderer !== undefined) {
      renderer.start().catch((error: Object) => {
        const detail = error instanceof Error ? error.message : 'audio resume failed';
        console.error(`FlyNES audio resume failed: ${detail}`);
      });
    }
  }

  close(): void {
    try {
      nativePlay.playClose();
    } finally {
      const renderer = this.renderer;
      this.renderer = undefined;
      this.pendingPcm = [];
      this.pendingOffset = 0;
      this.outputPaused = false;
      this.buttons = 0;
      if (renderer !== undefined) {
        try {
          renderer.off('writeData', this.onWriteData);
        } catch (error) {
          const detail = error instanceof Error ? error.message : 'audio off failed';
          console.error(`FlyNES audio detach failed: ${detail}`);
        }
        renderer.stop().then(() => renderer.release()).catch(() => {
          renderer.release();
        });
      }
    }
  }

  private fillAudioBuffer(data: ArrayBuffer): void {
    const dest = new Uint8Array(data);
    if (this.outputPaused) {
      dest.fill(0);
      return;
    }
    let written = 0;
    while (written < dest.length && this.pendingPcm.length > 0) {
      const source = new Uint8Array(this.pendingPcm[0]);
      const available = source.length - this.pendingOffset;
      const copy = Math.min(available, dest.length - written);
      dest.set(source.subarray(this.pendingOffset, this.pendingOffset + copy), written);
      written += copy;
      this.pendingOffset += copy;
      if (this.pendingOffset >= source.length) {
        this.pendingPcm.shift();
        this.pendingOffset = 0;
      }
    }
    if (written < dest.length) {
      dest.fill(0, written);
    }
  }
}
