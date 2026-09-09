import { fileIo } from '@kit.CoreFileKit';

export function checkpointKey(canonicalId: string): string {
  const normalized = canonicalId.trim().toLowerCase();
  if (/^[a-z0-9_-]{1,96}$/.test(normalized)) {
    return normalized;
  }
  let hash = 2166136261;
  for (let index = 0; index < canonicalId.length; index++) {
    hash ^= canonicalId.charCodeAt(index);
    hash = Math.imul(hash, 16777619) >>> 0;
  }
  return `rom-${canonicalId.length}-${hash.toString(16).padStart(8, '0')}`;
}

/** Per-ROM autosave store with fsync + rename commit. */
export class CheckpointStore {
  private readonly root: string;

  constructor(filesDir: string) {
    this.root = `${filesDir}/saves`;
  }

  read(canonicalId: string): ArrayBuffer | undefined {
    const path = this.pathFor(canonicalId);
    if (!fileIo.accessSync(path)) {
      return undefined;
    }
    const file = fileIo.openSync(path, fileIo.OpenMode.READ_ONLY);
    try {
      const size = fileIo.statSync(file.fd).size;
      if (size <= 0 || size > 16 * 1024 * 1024) {
        throw new Error('autosave size is invalid');
      }
      const bytes = new ArrayBuffer(size);
      const read = fileIo.readSync(file.fd, bytes);
      if (read !== size) {
        throw new Error('autosave was truncated');
      }
      return bytes;
    } finally {
      fileIo.closeSync(file.fd);
    }
  }

  write(canonicalId: string, checkpoint: ArrayBuffer): void {
    const directory = this.directoryFor(canonicalId);
    if (!fileIo.accessSync(directory)) {
      fileIo.mkdirSync(directory, true);
    }
    const target = `${directory}/autosave.nst`;
    const temporary = `${target}.tmp`;
    const file = fileIo.openSync(
      temporary, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
    try {
      const written = fileIo.writeSync(file.fd, checkpoint);
      if (written !== checkpoint.byteLength) {
        throw new Error('autosave write was incomplete');
      }
      fileIo.fsyncSync(file.fd);
    } finally {
      fileIo.closeSync(file.fd);
    }
    fileIo.renameSync(temporary, target);
  }

  quarantine(canonicalId: string): void {
    const path = this.pathFor(canonicalId);
    if (!fileIo.accessSync(path)) {
      return;
    }
    fileIo.renameSync(path, `${path}.corrupt-${Date.now()}`);
  }

  private directoryFor(canonicalId: string): string {
    return `${this.root}/${checkpointKey(canonicalId)}`;
  }

  private pathFor(canonicalId: string): string {
    return `${this.directoryFor(canonicalId)}/autosave.nst`;
  }
}
