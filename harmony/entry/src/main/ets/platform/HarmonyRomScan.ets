import { fileIo } from '@kit.CoreFileKit';
import { http } from '@kit.NetworkKit';
import nativeApp, { ScanJobFileDto, ScanJobStatusDto } from 'libentry.so';
import {
  encodeSideloadFileName,
  isDebugSideloadFixture,
  shouldDownloadSideloadFile
} from './DebugSideload';

class ListFolderOptions {
  recursion: boolean = true;
  listNum: number = 0;
}

const SCAN_FULL: number = 1;
const SCAN_PARTIAL: number = 2;
const SCAN_PHASE_COMPLETED: number = 3;
const SCAN_PHASE_CANCELLED: number = 4;
const SCAN_PHASE_FAILED: number = 5;

/** Lists a sandbox path or persistable folder URI and indexes catalog candidates. */
export class HarmonyRomScan {
  private activeJobId: number = 0;

  async scanDirectory(uuidHex: string, uri: string, sourceScope: number,
    onProgress?: (status: ScanJobStatusDto) => void): Promise<void> {
    let names: string[] = [];
    try {
      const options = new ListFolderOptions();
      // Flat managed/sideload folders only — recursion can yield absolute-looking paths
      // that fail shared relative-path validation (FLY_RESULT_INVALID_ARGUMENT).
      options.recursion = false;
      names = await fileIo.listFile(uri, options);
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'list folder failed';
      throw new Error(detail);
    }
    let completeness = SCAN_FULL;
    const candidates: ScanJobFileDto[] = [];
    const opened: fileIo.File[] = [];
    try {
      for (let index = 0; index < names.length; index++) {
        const relative = this.normalizeRelative(names[index]);
        if (relative.length === 0 || !this.isCatalogCandidate(relative)) {
          continue;
        }
        const childUri = `${uri}/${relative}`;
        let file: fileIo.File | undefined = undefined;
        try {
          file = await fileIo.open(childUri, fileIo.OpenMode.READ_ONLY);
          const fd = Number(file.fd);
          const stat = await fileIo.stat(fd);
          const size = Number(stat.size);
          candidates.push({
            relativePath: relative,
            displayName: this.fileName(relative),
            borrowedFd: fd,
            declaredSize: size
          });
          opened.push(file);
          file = undefined;
        } catch (error) {
          completeness = SCAN_PARTIAL;
          const detail = error instanceof Error ? error.message : 'scan file failed';
          console.error(`FlyNES scan file failed (${relative}): ${detail}`);
        } finally {
          if (file !== undefined) {
            await fileIo.close(file);
          }
        }
      }
      this.activeJobId = nativeApp.scanJobStart(uuidHex, sourceScope, candidates, completeness);
    } finally {
      for (let index = 0; index < opened.length; index++) {
        await fileIo.close(opened[index]);
      }
    }

    for (;;) {
      const status: ScanJobStatusDto = nativeApp.scanJobStatus(this.activeJobId);
      if (onProgress !== undefined) {
        onProgress(status);
      }
      if (status.phase === SCAN_PHASE_COMPLETED) {
        console.info(`FlyNES scan commit indexed=${status.resultCount} completeness=${completeness}`);
        this.activeJobId = 0;
        return;
      }
      if (status.phase === SCAN_PHASE_CANCELLED) {
        this.activeJobId = 0;
        throw new Error('scan cancelled');
      }
      if (status.phase === SCAN_PHASE_FAILED) {
        this.activeJobId = 0;
        throw new Error(status.error.length > 0 ? status.error : 'scan failed');
      }
      await this.delay(50);
    }
  }

  cancelActiveScan(): boolean {
    if (this.activeJobId <= 0) {
      return false;
    }
    return nativeApp.scanJobCancel(this.activeJobId);
  }

  private async delay(milliseconds: number): Promise<void> {
    await new Promise<void>((resolve) => {
      setTimeout(() => resolve(), milliseconds);
    });
  }

  private normalizeRelative(raw: string): string {
    let value = raw.replace(/\\/g, '/').trim();
    while (value.startsWith('./')) {
      value = value.substring(2);
    }
    if (value.startsWith('/')) {
      const parts = value.split('/');
      value = parts.length > 0 ? parts[parts.length - 1] : '';
    }
    if (value.indexOf('/') >= 0) {
      value = this.fileName(value);
    }
    return value;
  }

  directoryExists(path: string): boolean {
    try {
      fileIo.statSync(path);
      return true;
    } catch (error) {
      return false;
    }
  }

  importSideload(srcDir: string, destDir: string): number {
    try {
      fileIo.mkdirSync(destDir, true);
    } catch (error) {
      if (!this.directoryExists(destDir)) {
        const detail = error instanceof Error ? error.message : 'mkdir failed';
        throw new Error(detail);
      }
    }
    const options = new ListFolderOptions();
    const names = fileIo.listFileSync(srcDir, options);
    let copied = 0;
    for (let index = 0; index < names.length; index++) {
      const relative = names[index].replace(/\\/g, '/');
      if (!this.isCatalogCandidate(relative)) {
        continue;
      }
      const fromPath = `${srcDir}/${relative}`;
      const toPath = `${destDir}/${this.fileName(relative)}`;
      try {
        fileIo.copyFileSync(fromPath, toPath);
        copied += 1;
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'copy failed';
        console.error(`FlyNES sideload copy failed: ${detail}`);
      }
    }
    return copied;
  }

  /**
   * Copies picker-selected document URIs into the managed roms sandbox.
   * Phone devices lack reliable FolderSelection; FILE picks are the supported add path.
   */
  importDocumentUris(uris: string[], destDir: string): number {
    try {
      fileIo.mkdirSync(destDir, true);
    } catch (error) {
      if (!this.directoryExists(destDir)) {
        const detail = error instanceof Error ? error.message : 'mkdir failed';
        throw new Error(detail);
      }
    }
    let copied = 0;
    for (let index = 0; index < uris.length; index++) {
      const uri = uris[index];
      const name = this.displayNameFromUri(uri);
      if (!this.isCatalogCandidate(name)) {
        console.error(`FlyNES skip non-catalog URI: ${name}`);
        continue;
      }
      const toPath = `${destDir}/${name}`;
      try {
        fileIo.copyFileSync(uri, toPath);
        copied += 1;
        console.info(`FlyNES imported document ${name}`);
      } catch (error) {
        // Some URI schemes reject copyFileSync — fall back to open/read/write.
        try {
          this.copyUriViaFd(uri, toPath);
          copied += 1;
          console.info(`FlyNES imported document via fd ${name}`);
        } catch (inner) {
          const detail = inner instanceof Error ? inner.message :
            (error instanceof Error ? error.message : 'import failed');
          console.error(`FlyNES document import failed (${name}): ${detail}`);
        }
      }
    }
    return copied;
  }

  private copyUriViaFd(uri: string, toPath: string): void {
    const src = fileIo.openSync(uri, fileIo.OpenMode.READ_ONLY);
    try {
      const stat = fileIo.statSync(src.fd);
      const size = Number(stat.size);
      const buffer = new ArrayBuffer(size > 0 ? size : 1);
      if (size > 0) {
        fileIo.readSync(src.fd, buffer);
      }
      const dest = fileIo.openSync(
        toPath, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
      try {
        if (size > 0) {
          fileIo.writeSync(dest.fd, buffer);
        }
      } finally {
        fileIo.closeSync(dest.fd);
      }
    } finally {
      fileIo.closeSync(src.fd);
    }
  }

  private displayNameFromUri(uri: string): string {
    let value = uri.replace(/\\/g, '/');
    const query = value.indexOf('?');
    if (query >= 0) {
      value = value.substring(0, query);
    }
    const slash = value.lastIndexOf('/');
    if (slash >= 0) {
      value = value.substring(slash + 1);
    }
    try {
      value = decodeURIComponent(value);
    } catch (error) {
      // keep raw
    }
    return value;
  }

  /**
   * Downloads sideload ROMs over HTTP. Accepts several candidate base URLs
   * (emulator host alias, LAN debug host, device loopback tunnel); the first
   * URL that serves flynes-sideload.txt wins. URL-encodes non-ASCII names.
   */
  async fetchHttpSideload(baseUrls: string[], destDir: string): Promise<number> {
    let baseUrl = '';
    let listBody = '';
    const probeClient = http.createHttp();
    try {
      for (let index = 0; index < baseUrls.length && baseUrl.length === 0; index++) {
        const candidate = baseUrls[index];
        try {
          const probe = await probeClient.request(`${candidate}/flynes-sideload.txt`, {
            method: http.RequestMethod.GET,
            expectDataType: http.HttpDataType.STRING,
            connectTimeout: 1500,
            readTimeout: 5000
          });
          if (probe.responseCode === 200) {
            baseUrl = candidate;
            listBody = `${probe.result}`;
          }
        } catch (error) {
          // unreachable host — try next candidate
        }
      }
    } finally {
      probeClient.destroy();
    }
    if (baseUrl.length === 0 || listBody.length === 0) {
      return 0;
    }
    const client = http.createHttp();
    try {
      try {
        fileIo.mkdirSync(destDir, true);
      } catch (error) {
        if (!this.directoryExists(destDir)) {
          const detail = error instanceof Error ? error.message : 'mkdir failed';
          throw new Error(detail);
        }
      }
      const names = listBody.replace(/\r/g, '').split('\n');
      let copied = 0;
      for (let index = 0; index < names.length; index++) {
        const name = names[index].trim();
        if (!this.isCatalogCandidate(name) || name.indexOf('/') >= 0 || name.indexOf('\\') >= 0) {
          continue;
        }
        const destPath = `${destDir}/${name}`;
        // Resume support: skip files already downloaded in a previous pass so
        // app relaunches do not restart the whole batch.
        if (!shouldDownloadSideloadFile(fileIo.accessSync(destPath))) {
          continue;
        }
        const encoded = encodeSideloadFileName(name);
        const fileResp = await client.request(`${baseUrl}/${encoded}`, {
          method: http.RequestMethod.GET,
          expectDataType: http.HttpDataType.ARRAY_BUFFER,
          connectTimeout: 2000,
          readTimeout: 20000
        });
        if (fileResp.responseCode !== 200) {
          continue;
        }
        const bytes = fileResp.result as ArrayBuffer;
        if (bytes.byteLength < 16) {
          continue;
        }
        const file = fileIo.openSync(
          destPath, fileIo.OpenMode.READ_WRITE | fileIo.OpenMode.CREATE | fileIo.OpenMode.TRUNC);
        try {
          fileIo.writeSync(file.fd, bytes);
        } finally {
          fileIo.closeSync(file.fd);
        }
        copied += 1;
        if (copied % 50 === 0) {
          console.info(`FlyNES http sideload progress: ${copied}`);
        }
      }
      return copied;
    } finally {
      client.destroy();
    }
  }

  /** Removes only the named emulator TDD fixtures; user-imported ROMs are never matched. */
  async removeDebugSideloadFixtures(destDir: string): Promise<number> {
    const options = new ListFolderOptions();
    options.recursion = false;
    const names = await fileIo.listFile(destDir, options);
    let removed = 0;
    for (let index = 0; index < names.length; index++) {
      const name = this.normalizeRelative(names[index]);
      if (!isDebugSideloadFixture(name)) {
        continue;
      }
      await fileIo.unlink(`${destDir}/${name}`);
      removed += 1;
    }
    return removed;
  }

  private isCatalogCandidate(relative: string): boolean {
    const lower = relative.toLowerCase();
    return lower.endsWith('.nes') || lower.endsWith('.zip') || lower.endsWith('.unf') ||
      lower.endsWith('.unif');
  }

  private fileName(relative: string): string {
    const parts = relative.split('/');
    return parts.length > 0 ? parts[parts.length - 1] : relative;
  }
}
