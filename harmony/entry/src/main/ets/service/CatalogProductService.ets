import nativeProduct, { CatalogUserState, GameCenterRow } from 'libentry.so';

/** Pure projection of cached metadata; changing language never opens a ROM. */
export function localizedGameTitle(titleEn: string, titleZhHans: string, fallback: string,
  localeTag: string, systemLanguage: string): string {
  const effective = (localeTag === 'system' ? systemLanguage : localeTag).toLowerCase();
  const chinese = effective === 'zh' || effective.startsWith('zh-') || effective.startsWith('zh_');
  const primary = chinese ? titleZhHans : titleEn;
  const secondary = chinese ? titleEn : titleZhHans;
  return primary.length > 0 ? primary : secondary.length > 0 ? secondary : fallback;
}

/** Collapses the native per-variant snapshot into one card per canonical game. */
export function uniqueGameCenterRows(snapshot: GameCenterRow[]): GameCenterRow[] {
  const rows: GameCenterRow[] = [];
  const seen: string[] = [];
  for (let index = 0; index < snapshot.length; index++) {
    const row = snapshot[index];
    if (row.builtin && seen.indexOf(row.canonicalId) < 0) {
      rows.unshift(row);
      seen.push(row.canonicalId);
      break;
    }
  }
  for (let index = 0; index < snapshot.length; index++) {
    const row = snapshot[index];
    const existing = seen.indexOf(row.canonicalId);
    if (existing >= 0) {
      rows[existing].searchAliases = `${rows[existing].searchAliases ?? ''}\n${row.searchAliases ?? ''}\n${row.originalFilename}`;
      rows[existing].popularityScore = Math.max(rows[existing].popularityScore ?? -1,
        row.popularityScore ?? -1);
      continue;
    }
    if (row.builtin) {
      continue;
    }
    rows.push(row);
    seen.push(row.canonicalId);
  }
  return rows;
}

/** Catalog projection for Game Center. Builtin From Below is always present. */
export class CatalogProductService {
  loadRows(): GameCenterRow[] {
    let snapshot: GameCenterRow[] = [];
    try {
      snapshot = nativeProduct.catalogSnapshot();
    } catch (error) {
      snapshot = [];
    }
    // The snapshot is the source of truth: builtin entries carry their real
    // canonical id (e.g. "game:<sha256>"), not the literal "builtin". Only
    // inject the fallback row when the scan has not produced a builtin entry
    // yet. The fallback's user state (favorite / last played) is overlaid from
    // the native catalog so the UI reflects persisted toggles and play marks.
    const rows: GameCenterRow[] = uniqueGameCenterRows(snapshot);
    let builtinRow: GameCenterRow | undefined = undefined;
    if (rows.length > 0 && rows[0].builtin) {
      builtinRow = rows[0];
      rows.shift();
    }
    if (builtinRow === undefined) {
      builtinRow = {
        canonicalId: 'builtin',
        titleEn: 'From Below',
        titleZhHans: '来自下方',
        builtin: true,
        favorite: false,
        lastPlayedSequence: 0,
        originalFilename: 'from_below.nes',
        sourceUuidHex: '',
        sourceRelativePath: '',
        packageFormat: 0
      };
    }
    try {
      const user: CatalogUserState =
        nativeProduct.catalogUserStateGet(builtinRow.canonicalId);
      builtinRow.favorite = user.favorite;
      builtinRow.lastPlayedSequence = user.lastPlayedSequence;
    } catch (error) {
      const detail = error instanceof Error ? error.message : 'user state read failed';
      console.error(`FlyNES builtin user state failed: ${detail}`);
    }
    // Keep the fallback available; gameCenterFilter determines the visible category order.
    rows.unshift(builtinRow);
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
