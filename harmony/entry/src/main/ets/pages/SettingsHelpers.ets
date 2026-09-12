/** Aspect / haptic preview helpers used by Settings (pure, no native). */

export function aspectModeLabelKey(aspectMode: number): string {
  if (aspectMode === 1) {
    return 'aspect_four_three';
  }
  if (aspectMode === 2) {
    return 'aspect_square_pixels';
  }
  if (aspectMode === 3) {
    return 'aspect_integer';
  }
  return 'aspect_four_three';
}

export function normalizeAspectMode(aspectMode: number): number {
  if (aspectMode === 2 || aspectMode === 3) {
    return aspectMode;
  }
  return 1;
}

/** Returns delay ms between A preview and B preview (Android uses 260ms). */
export function hapticPreviewGapMs(): number {
  return 260;
}

export function hapticPreviewSequence(): string[] {
  return ['A', 'B'];
}

export function runtimeLanguageFor(localeTag: string, systemLanguage: string): string {
  return localeTag === 'system' ? systemLanguage : localeTag;
}
