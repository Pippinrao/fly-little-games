import nativeProduct, { GameCenterRow } from 'libentry.so';

/** Catalog projection for Game Center. Builtin From Below is always present. */
export class CatalogProductService {
  loadRows(): GameCenterRow[] {
    const builtin: GameCenterRow = {
      canonicalId: 'builtin',
      titleEn: 'From Below',
      titleZhHans: '来自下方',
      builtin: true,
      favorite: false,
      lastPlayedSequence: 0,
      originalFilename: 'from_below.nes'
    };
    let snapshot: GameCenterRow[] = [];
    try {
      snapshot = nativeProduct.catalogSnapshot();
    } catch (error) {
      snapshot = [];
    }
    const rows: GameCenterRow[] = [];
    let hasBuiltin = false;
    for (let index = 0; index < snapshot.length; index++) {
      const row = snapshot[index];
      rows.push(row);
      if (row.canonicalId === 'builtin') {
        hasBuiltin = true;
      }
    }
    if (!hasBuiltin) {
      rows.unshift(builtin);
    }
    return rows;
  }

  /** Matches Android HomeHeaderLayoutPolicy.compact (fontScale >= 1.8 or en-XA). */
  compactHeader(fontScale: number, language: string): boolean {
    const parts = language.split('-');
    const lang = parts.length > 0 ? parts[0] : '';
    const region = parts.length > 0 ? parts[parts.length - 1] : '';
    const pseudoExpanded = lang === 'en' && region === 'XA';
    return fontScale >= 1.8 || pseudoExpanded;
  }

  detailTitleMaxLines(fontScale: number): number {
    return fontScale >= 1.8 ? 2 : 1;
  }

  launchButtonHeightVp(fontScale: number): number {
    return fontScale >= 1.8 ? 88 : 56;
  }
}
