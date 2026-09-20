export interface NearbyTestFontScaleResult {
  accepted: boolean;
  scale: number;
  rejection: string;
  followSystem: boolean;
}

const ALLOWED_TEST_FONT_SIZE_SCALES: number[] = [1, 1.3, 1.5, 2];

export function parseTestFontSizeScale(raw: string | undefined): NearbyTestFontScaleResult {
  if (raw === undefined || raw.length === 0) {
    return { accepted: false, scale: 0, rejection: '', followSystem: true };
  }
  const scale: number = Number(raw);
  for (let index = 0; index < ALLOWED_TEST_FONT_SIZE_SCALES.length; index++) {
    if (scale === ALLOWED_TEST_FONT_SIZE_SCALES[index]) {
      return { accepted: true, scale: scale, rejection: '', followSystem: true };
    }
  }
  return {
    accepted: false,
    scale: 0,
    rejection: `FlyNES test fontSizeScale rejected=${raw}`,
    followSystem: true
  };
}

export function resolveTestFontSizeScale(raw: string | undefined,
  isDebug: boolean): NearbyTestFontScaleResult {
  if (!isDebug) {
    const rejection = (raw === undefined || raw.length === 0)
      ? ''
      : 'FlyNES test fontSizeScale ignored in release';
    return { accepted: false, scale: 0, rejection: rejection, followSystem: true };
  }
  return parseTestFontSizeScale(raw);
}
