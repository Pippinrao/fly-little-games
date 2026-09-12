import { fileIo } from '@kit.CoreFileKit';
import { image } from '@kit.ImageKit';

const COVERS_DIR = 'covers/v1';

function stringToAsciiBuffer(input: string): ArrayBuffer {
  const out = new ArrayBuffer(input.length);
  const view = new Uint8Array(out);
  for (let index = 0; index < input.length; index++) {
    view[index] = input.charCodeAt(index) & 0xff;
  }
  return out;
}

/**
 * Deterministic, non-reversible filename key for a canonical id.
 * Mirrors Android's opaque-sha256 policy: names reveal neither ROM titles
 * nor device locators. Uses two independent 64-bit FNV-1a variants; the
 * canonical id itself is app-private (not shared across apps).
 */
function coverKey(canonicalId: string): string {
  const bytes = stringToAsciiBuffer(canonicalId);
  const view = new Uint8Array(bytes);
  let h1a = 0xcbf29ce4;
  let h1b = 0x84222325;
  let h2a = 0x811c9dc5;
  let h2b = 0x01000193;
  for (let index = 0; index < view.length; index++) {
    const b = view[index];
    h1a = Math.imul(h1a ^ b, 0x01000193) >>> 0;
    h1b = Math.imul(h1b ^ b, 0x85ebca6b) >>> 0;
    h2a = Math.imul(h2a ^ (b + 0x9e), 0xc2b2ae35) >>> 0;
    h2b = Math.imul(h2b ^ (b + 0x27), 0x165667b1) >>> 0;
  }
  const hex = (v: number): string => v.toString(16).padStart(8, '0');
  return `${hex(h1a)}${hex(h1b)}${hex(h2a)}${hex(h2b)}`;
}

/**
 * Cross-platform cover store mirroring Android AndroidCoverRepository:
 * PNG captures stored under covers/v1, keyed by sha256(canonicalId).
 * Filenames reveal neither ROM titles nor device locators.
 */
export class CoverStore {
  private readonly memory = new Map<string, image.PixelMap>();
  private readonly root: string;

  constructor(filesDir: string) {
    this.root = `${filesDir}/${COVERS_DIR}`;
  }

  private pathFor(canonicalId: string): string {
    return `${this.root}/${coverKey(canonicalId)}.png`;
  }

  exists(canonicalId: string): boolean {
    return fileIo.accessSync(this.pathFor(canonicalId));
  }

  /** Saves an RGB565 NES frame (256x240) as the canonical cover art. */
  async storeFromRgb565(canonicalId: string, rgb565: ArrayBuffer): Promise<boolean> {
    try {
      if (!fileIo.accessSync(this.root)) {
        fileIo.mkdirSync(this.root, true);
      }
      const pixelMap = image.createPixelMapSync(rgb565, {
        size: { width: 256, height: 240 },
        srcPixelFormat: image.PixelMapFormat.RGB_565,
        pixelFormat: image.PixelMapFormat.RGB_565,
        editable: false
      });
      const packer = image.createImagePacker();
      try {
        const packed: ArrayBuffer =
          await packer.packToData(pixelMap, { format: 'image/png', quality: 100 });
        const path = this.pathFor(canonicalId);
        const file = fileIo.openSync(
          path, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
        try {
          fileIo.writeSync(file.fd, packed);
        } finally {
          fileIo.closeSync(file.fd);
        }
        const previous = this.memory.get(canonicalId);
        if (previous !== undefined && previous !== pixelMap) {
          previous.release();
        }
        this.memory.set(canonicalId, pixelMap);
        return true;
      } finally {
        packer.release();
      }
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'cover store failed';
      console.error(`FlyNES cover store failed: ${detail}`);
      return false;
    }
  }

  async load(canonicalId: string): Promise<image.PixelMap | undefined> {
    const cached = this.memory.get(canonicalId);
    if (cached !== undefined) {
      return cached;
    }
    const path = this.pathFor(canonicalId);
    try {
      if (!fileIo.accessSync(path)) {
        return undefined;
      }
      const source = image.createImageSource(path);
      const decoded: image.PixelMap = await source.createPixelMap({ editable: false });
      await source.release();
      this.memory.set(canonicalId, decoded);
      return decoded;
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'cover load failed';
      console.error(`FlyNES cover load failed: ${detail}`);
      return undefined;
    }
  }
}
