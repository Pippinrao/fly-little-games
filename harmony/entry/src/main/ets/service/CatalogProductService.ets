import nativeProduct, { CatalogUserState, GameCenterRow } from 'libentry.so';
import { BuiltinGame, BuiltinGames } from './BuiltinGames';

/** Pure projection of cached metadata; changing language never opens a ROM. */
export function localizedGameTitle(titleEn: string, titleZhHans: string, fallback: string,
  localeTag: string, systemLanguage: string): string {
  const effective = (localeTag === 'system' ? systemLanguage : localeTag).toLowerCase();
  const chinese = effective === 'zh' || effective.startsWith('zh-') || effective.startsWith('zh_');
  const primary = chinese ? titleZhHans : titleEn;
  const secondary = chinese ? titleEn : titleZhHans;
  return primary.length > 0 ? primary : secondary.length > 0 ? secondary : fallback;
}

/**
 * Collapses the native per-variant snapshot into one card per canonical game,
 * with every bundled game first and the rest in snapshot order.
 */
export function uniqueGameCenterRows(snapshot: GameCenterRow[]): GameCenterRow[] {
  const merged: GameCenterRow[] = [];
  const indexByCanonicalId: Map<string, number> = new Map();
  for (let index = 0; index < snapshot.length; index++) {
    const row = snapshot[index];
    const existing = indexByCanonicalId.get(row.canonicalId);
    if (existing === undefined) {
      indexByCanonicalId.set(row.canonicalId, merged.length);
      merged.push(row);
      continue;
    }
    const target = merged[existing];
    target.builtin = target.builtin || row.builtin;
  }
  const builtinRows: GameCenterRow[] = [];
  const otherRows: GameCenterRow[] = [];
  for (let index = 0; index < merged.length; index++) {
    if (merged[index].builtin) {
      builtinRows.push(merged[index]);
    } else {
      otherRows.push(merged[index]);
    }
  }
  return builtinRows.concat(otherRows);
}
/**
 * Catalog projection for Game Center.
 *
 * The bundled games come from the shared manifest, never from code: every game
 * the manifest declares is present even before the native scan has produced a
 * row for it, and the manifest owns the trusted bilingual titles.
 */

/**
 * Shared, versioned two-player capability projection (design 2026-09-13 3.2).
 * The only legal source of two-player eligibility: never inferred from file
 * names, a P2 in a title, or controller counts. Unread ids and entries whose
 * profile version mismatches project UNKNOWN, never UNSUPPORTED.
 */
export enum MultiplayerEligibility {
  UNSUPPORTED = 0,
  SUPPORTED = 1,
  UNKNOWN = 2
}

export class MultiplayerCapabilityRegistry {
  private readonly profileVersion: number;
  private readonly entries: Map<string, [number, number]> = new Map();

  constructor(profileVersion: number) {
    this.profileVersion = profileVersion;
  }

  public put(canonicalId: string, eligibility: MultiplayerEligibility,
    profileVersion: number): void {
    this.entries.set(canonicalId, [eligibility as number, profileVersion]);
  }

  public eligibilityFor(canonicalId: string): MultiplayerEligibility {
    const entry = this.entries.get(canonicalId);
    if (entry === undefined || entry[1] !== this.profileVersion) {
      return MultiplayerEligibility.UNKNOWN;
    }
    return entry[0] as MultiplayerEligibility;
  }
}

 * Stable post-filter for the independent two-player switch (design U04):
 * removes rows whose eligibility is not SUPPORTED, never re-sorts, never
 * re-ranks, and never changes the category/query/filter state itself. Applied
 * AFTER the existing category+search pipeline and the canonical-id dedupe.
 */
export function applyMultiplayerFilter(rows: GameCenterRow[],
  registry: MultiplayerCapabilityRegistry): GameCenterRow[] {
  const result: GameCenterRow[] = [];
  for (let index = 0; index < rows.length; index++) {
    const row = rows[index];
    if (registry.eligibilityFor(row.canonicalId) === MultiplayerEligibility.SUPPORTED) {
      result.push(row);
    }

/**
 * Catalog projection for Game Center.
 *
 * The bundled games come from the shared manifest, never from code: every game
 * the manifest declares is present even before the native scan has produced a
 * row for it, and the manifest owns the trusted bilingual titles.
 */
export class CatalogProductService {
  loadRows(games: BuiltinGames): GameCenterRow[] {
    let snapshot: GameCenterRow[] = [];
    try {
      snapshot = nativeProduct.catalogSnapshot();
    } catch (error) {
      snapshot = [];
    }

    const rows: GameCenterRow[] = uniqueGameCenterRows(snapshot);
    const bundled: BuiltinGame[] = games.all();
    const bundledRows: GameCenterRow[] = [];
    const claimed: string[] = [];
    for (let index = 0; index < bundled.length; index++) {
      const game = bundled[index];
      let row: GameCenterRow | undefined = undefined;
      for (let scan = 0; scan < rows.length; scan++) {
        if (rows[scan].canonicalId === game.canonicalId) {
          row = rows[scan];
          break;
        }
      }
      if (row === undefined) {
        row = {
          canonicalId: game.canonicalId,
          titleEn: game.titleEn,
          titleZhHans: game.titleZhHans,
          builtin: true,
          favorite: false,
          lastPlayedSequence: 0,
          originalFilename: game.assetFilename,
          sourceUuidHex: '',
          sourceRelativePath: '',
          packageFormat: 0
        };
      } else {
        // The manifest owns the trusted titles of a bundled game.
        row.titleEn = game.titleEn;
        row.titleZhHans = game.titleZhHans;
        row.builtin = true;
      }
      try {
        const user: CatalogUserState = nativeProduct.catalogUserStateGet(game.canonicalId);
        row.favorite = user.favorite;
        row.lastPlayedSequence = user.lastPlayedSequence;
      } catch (error) {
        const detail = error instanceof Error ? error.message : 'user state read failed';
        console.error(`FlyNES builtin user state failed: ${detail}`);
      }
      claimed.push(game.canonicalId);
      bundledRows.push(row);
    }

    const rest: GameCenterRow[] = [];
    for (let index = 0; index < rows.length; index++) {
      if (rows[index].builtin || claimed.indexOf(rows[index].canonicalId) >= 0) {
        continue;
      }
      rest.push(rows[index]);
    }
    // Bundled games stay first so the default selection matches Android.
    return bundledRows.concat(rest);
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
