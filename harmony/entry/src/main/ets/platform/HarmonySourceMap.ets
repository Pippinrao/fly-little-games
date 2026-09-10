import { common } from '@kit.AbilityKit';
import { preferences } from '@kit.ArkData';
import { util } from '@kit.ArkTS';

const STORE_NAME: string = 'flynes_source_uuids';
const MAP_KEY: string = 'source_map_v1';

class StoredSource {
  uuid: string = '';
  uri: string = '';
}

class StoredMapPayload {
  builtin: string = '';
  sources: StoredSource[] = [];
}

/** UUID hex → persistable FilePicker URI. Lives in app preferences only. */
export class HarmonySourceMap {
  private store: preferences.Preferences | undefined = undefined;
  private builtinUuidHex: string = '';
  private readonly uuidHexes: string[] = [];
  private readonly urisByUuid: Map<string, string> = new Map();

  async load(context: common.UIAbilityContext): Promise<void> {
    this.store = await preferences.getPreferences(context, STORE_NAME);
    const raw = await this.store.get(MAP_KEY, '') as string;
    this.uuidHexes.splice(0, this.uuidHexes.length);
    this.urisByUuid.clear();
    this.builtinUuidHex = '';
    if (raw.length === 0) {
      this.builtinUuidHex = this.newUuidHex();
      await this.flush();
      return;
    }
    try {
      const parsed = JSON.parse(raw) as StoredMapPayload;
      if (parsed.builtin.length === 32) {
        this.builtinUuidHex = parsed.builtin;
      }
      for (let index = 0; index < parsed.sources.length; index++) {
        const row = parsed.sources[index];
        if (row.uuid.length === 32 && row.uri.length > 0 && !this.isAllZero(row.uuid)) {
          this.uuidHexes.push(row.uuid);
          this.urisByUuid.set(row.uuid, row.uri);
        }
      }
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'source map parse failed';
      console.error(`FlyNES source map parse failed: ${detail}`);
    }
    if (this.builtinUuidHex.length !== 32) {
      this.builtinUuidHex = this.newUuidHex();
      await this.flush();
    }
  }

  builtinUuid(): string {
    return this.builtinUuidHex;
  }

  put(uuidHex: string, persistableUri: string): void {
    const hex = this.requireAssigned(uuidHex);
    const uri = persistableUri.trim();
    if (uri.length === 0) {
      throw new Error('persistable URI is empty');
    }
    if (this.urisByUuid.get(hex) === undefined) {
      this.uuidHexes.push(hex);
    }
    this.urisByUuid.set(hex, uri);
  }

  get(uuidHex: string): string {
    const uri = this.urisByUuid.get(this.requireHex(uuidHex));
    return uri === undefined ? '' : uri;
  }

  remove(uuidHex: string): void {
    const hex = this.requireHex(uuidHex);
    this.urisByUuid.delete(hex);
    for (let index = this.uuidHexes.length - 1; index >= 0; index--) {
      if (this.uuidHexes[index] === hex) {
        this.uuidHexes.splice(index, 1);
      }
    }
  }

  uuids(): string[] {
    return this.uuidHexes.slice();
  }

  newUuidHex(): string {
    return this.normalizeUuidHex(util.generateRandomUUID(false));
  }

  async flush(): Promise<void> {
    if (this.store === undefined) {
      return;
    }
    const sources: StoredSource[] = [];
    for (let index = 0; index < this.uuidHexes.length; index++) {
      const uuidHex = this.uuidHexes[index];
      const uri = this.urisByUuid.get(uuidHex);
      if (uri !== undefined && uri.length > 0) {
        const row = new StoredSource();
        row.uuid = uuidHex;
        row.uri = uri;
        sources.push(row);
      }
    }
    const payload = new StoredMapPayload();
    payload.builtin = this.builtinUuidHex;
    payload.sources = sources;
    await this.store.put(MAP_KEY, JSON.stringify(payload));
    await this.store.flush();
  }

  private requireHex(uuidHex: string): string {
    return this.normalizeUuidHex(uuidHex.trim());
  }

  private normalizeUuidHex(raw: string): string {
    let hex = '';
    const lower = raw.toLowerCase();
    for (let index = 0; index < lower.length; index++) {
      const ch = lower.charAt(index);
      if ((ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f')) {
        hex += ch;
      }
    }
    if (hex.length !== 32) {
      throw new Error('uuid hex must be 32 digits');
    }
    return hex;
  }

  private requireAssigned(uuidHex: string): string {
    const hex = this.requireHex(uuidHex);
    if (this.isAllZero(hex)) {
      throw new Error('uuid must not be all zero');
    }
    return hex;
  }

  private isAllZero(uuidHex: string): boolean {
    for (let index = 0; index < uuidHex.length; index++) {
      if (uuidHex.charAt(index) !== '0') {
        return false;
      }
    }
    return true;
  }
}
