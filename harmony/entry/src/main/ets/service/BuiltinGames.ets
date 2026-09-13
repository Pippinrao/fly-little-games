import { common } from '@kit.AbilityKit';
import { util } from '@kit.ArkTS';

/** One license block as declared by the shared manifest. */
interface BuiltinLicensePayload {
  spdx: string;
  file: string;
  sourceUrl: string;
  sourceRevision: string;
  sourceOffer?: string;
}

/** One bundled game as declared by the shared manifest. */
interface BuiltinGamePayload {
  canonicalId: string;
  assetFilename: string;
  titleEn: string;
  titleZhHans: string;
  mapper: number;
  romSha256: string;
  credit: string;
  sortOrder: number;
  license: BuiltinLicensePayload;
}

interface BuiltinGamesPayload {
  schemaVersion: number;
  games: BuiltinGamePayload[];
}

/** A bundled homebrew game, as described by content/assets/builtin-games.json. */
export class BuiltinGame {
  readonly canonicalId: string;
  readonly assetFilename: string;
  readonly titleEn: string;
  readonly titleZhHans: string;
  readonly credit: string;
  readonly licenseFile: string;
  readonly licenseSpdx: string;
  readonly licenseSourceUrl: string;
  readonly mapper: number;
  readonly sortOrder: number;

  constructor(canonicalId: string, assetFilename: string, titleEn: string, titleZhHans: string,
    credit: string, licenseFile: string, licenseSpdx: string, licenseSourceUrl: string,
    mapper: number, sortOrder: number) {
    this.canonicalId = canonicalId;
    this.assetFilename = assetFilename;
    this.titleEn = titleEn;
    this.titleZhHans = titleZhHans;
    this.credit = credit;
    this.licenseFile = licenseFile;
    this.licenseSpdx = licenseSpdx;
    this.licenseSourceUrl = licenseSourceUrl;
    this.mapper = mapper;
    this.sortOrder = sortOrder;
  }

  /** Rawfile path of this game's ROM. */
  assetPath(): string {
    return BuiltinGames.ASSET_DIR + this.assetFilename;
  }
}

/**
 * The bundled homebrew games, read from the one shared manifest.
 *
 * Nothing in the HarmonyOS source may name a bundled game: adding one is a
 * manifest edit plus an asset, and this list picks it up.
 */
export class BuiltinGames {
  static readonly ASSET_NAME: string = 'builtin-games.json';
  static readonly ASSET_DIR: string = 'roms/';
  private static readonly SUPPORTED_SCHEMA: number = 1;

  private readonly entries: BuiltinGame[] = [];

  /** No bundled games: used when the manifest cannot be read. */
  static empty(): BuiltinGames {
    return new BuiltinGames();
  }

  /** Reads the manifest from the module's rawfile directory. */
  static async load(context: common.UIAbilityContext): Promise<BuiltinGames> {
    const bytes = await context.resourceManager.getRawFileContent(BuiltinGames.ASSET_NAME);
    const text = util.TextDecoder.create('utf-8', { ignoreBOM: true }).decodeWithStream(bytes);
    return BuiltinGames.parse(text);
  }

  /** Synchronous read, for callers that project the catalog on the UI thread. */
  static loadSync(context: common.UIAbilityContext): BuiltinGames {
    const bytes = context.resourceManager.getRawFileContentSync(BuiltinGames.ASSET_NAME);
    const text = util.TextDecoder.create('utf-8', { ignoreBOM: true }).decodeWithStream(bytes);
    return BuiltinGames.parse(text);
  }

  /**
   * Parses the manifest. Every failure throws an Error naming the manifest, so
   * callers can fall back to an empty list instead of crashing.
   */
  static parse(text: string): BuiltinGames {
    let payload: BuiltinGamesPayload;
    try {
      payload = JSON.parse(text) as BuiltinGamesPayload;
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'invalid JSON';
      throw new Error(`${BuiltinGames.ASSET_NAME} is not valid JSON: ${detail}`);
    }
    if (payload === undefined || payload.games === undefined) {
      throw new Error(`${BuiltinGames.ASSET_NAME} has no games array`);
    }
    if (payload.schemaVersion !== BuiltinGames.SUPPORTED_SCHEMA) {
      throw new Error(`${BuiltinGames.ASSET_NAME} schemaVersion ${payload.schemaVersion}`
        + ` is not supported (expected ${BuiltinGames.SUPPORTED_SCHEMA})`);
    }
    if (payload.games.length === 0) {
      throw new Error(`${BuiltinGames.ASSET_NAME} bundles no games`);
    }
    const games = new BuiltinGames();
    const ordered = payload.games.slice(0);
    ordered.sort((left: BuiltinGamePayload, right: BuiltinGamePayload) =>
      left.sortOrder - right.sortOrder);
    for (let index = 0; index < ordered.length; index++) {
      const row = ordered[index];
      if (row.canonicalId === undefined || row.canonicalId.length === 0
        || row.assetFilename === undefined || row.assetFilename.length === 0) {
        throw new Error(`${BuiltinGames.ASSET_NAME} game #${index} is missing its id or asset`);
      }
      const license = row.license;
      games.entries.push(new BuiltinGame(
        row.canonicalId,
        row.assetFilename,
        row.titleEn ?? '',
        row.titleZhHans ?? '',
        row.credit ?? '',
        license === undefined ? '' : (license.file ?? ''),
        license === undefined ? '' : (license.spdx ?? ''),
        license === undefined ? '' : (license.sourceUrl ?? ''),
        row.mapper ?? 0,
        row.sortOrder ?? 0));
    }
    return games;
  }

  /** Every bundled game, ordered by the manifest's sortOrder. */
  all(): BuiltinGame[] {
    return this.entries;
  }

  /** The bundled game with this canonical id, or undefined. */
  byCanonicalId(canonicalId: string): BuiltinGame | undefined {
    if (canonicalId === undefined || canonicalId.length === 0) {
      return undefined;
    }
    for (let index = 0; index < this.entries.length; index++) {
      if (this.entries[index].canonicalId === canonicalId) {
        return this.entries[index];
      }
    }
    return undefined;
  }

  /** The bundled game whose license text is this filename, or undefined. */
  byLicenseFile(licenseFile: string): BuiltinGame | undefined {
    if (licenseFile === undefined || licenseFile.length === 0) {
      return undefined;
    }
    for (let index = 0; index < this.entries.length; index++) {
      if (this.entries[index].licenseFile === licenseFile) {
        return this.entries[index];
      }
    }
    return undefined;
  }

  /** True when this canonical id names a bundled game. */
  isBundled(canonicalId: string): boolean {
    return this.byCanonicalId(canonicalId) !== undefined;
  }
}
