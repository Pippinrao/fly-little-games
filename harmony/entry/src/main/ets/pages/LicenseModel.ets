import { BuiltinGames } from '../service/BuiltinGames';

export class LicenseEntry {
  fileName: string = '';
  displayName: string = '';
  sourceUrl: string = '';

  constructor(fileName: string, displayName: string, sourceUrl: string) {
    this.fileName = fileName;
    this.displayName = displayName;
    this.sourceUrl = sourceUrl;
  }

  hasSourceUrl(): boolean {
    return this.sourceUrl.length > 0;
  }
}

const ENTRIES: LicenseEntry[] = [
  new LicenseEntry('nestopiaue-gplv2.txt', 'Nestopia UE (GPLv2)', 'https://github.com/0ldsk00l/nestopia'),
  new LicenseEntry('own-gplv2.txt', 'FlyNES (GPLv2)', ''),
  new LicenseEntry('zlib-license.txt', 'zlib', 'https://zlib.net')
];

export class LicenseModel {
  /**
   * Every licence in the module's rawfile directory: the fixed component
   * licences plus one row per bundled game, taken from the shared manifest.
   */
  static all(bundled: BuiltinGames): LicenseEntry[] {
    const entries: LicenseEntry[] = ENTRIES.slice();
    const games = bundled.all();
    for (let index = 0; index < games.length; index++) {
      const game = games[index];
      if (game.licenseFile.length === 0) {
        continue;
      }
      entries.push(new LicenseEntry(game.licenseFile, game.titleEn, game.licenseSourceUrl));
    }
    return entries;
  }

  static byFileName(fileName: string, bundled: BuiltinGames): LicenseEntry | undefined {
    const games = bundled.all();
    for (let index = 0; index < games.length; index++) {
      if (games[index].licenseFile === fileName) {
        return new LicenseEntry(
          games[index].licenseFile, games[index].titleEn, games[index].licenseSourceUrl);
      }
    }
    for (let index = 0; index < ENTRIES.length; index++) {
      if (ENTRIES[index].fileName === fileName) {
        return ENTRIES[index];
      }
    }
    return undefined;
  }

  static resolveBody(fileName: string, rawText: string | undefined): string {
    if (rawText === undefined || rawText.length === 0) {
      throw new Error(`license asset missing: ${fileName}`);
    }
    return rawText;
  }
}
