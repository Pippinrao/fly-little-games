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
  new LicenseEntry('zlib-license.txt', 'zlib', 'https://zlib.net'),
  new LicenseEntry('LICENSE-from-below.txt', 'From Below', 'https://mhughson.itch.io/from-below')
];

export class LicenseModel {
  static all(): LicenseEntry[] {
    return ENTRIES.slice();
  }

  static byFileName(fileName: string): LicenseEntry | undefined {
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
