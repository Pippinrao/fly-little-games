export interface NearbyTypeRole {
  size: number;
  weight: number;
  lineHeight: number;
  trackingEm: number;
}

export const NEARBY_MIN_TAP_HEIGHT_VP: number = 48;
export const NEARBY_SCALE2_VIEWPORT_WIDTH_VP: number = 640;
export const NEARBY_SCALE2_VIEWPORT_HEIGHT_VP: number = 360;

interface NearbyTypeTable {
  pageTitle: NearbyTypeRole;
  paneTitle: NearbyTypeRole;
  sectionTitle: NearbyTypeRole;
  body: NearbyTypeRole;
  muted: NearbyTypeRole;
  action: NearbyTypeRole;
  primaryAction: NearbyTypeRole;
  kicker: NearbyTypeRole;
  inviteCode: NearbyTypeRole;
  codeInput: NearbyTypeRole;
}

export const NEARBY_TYPE: NearbyTypeTable = {
  pageTitle: { size: 18, weight: 600, lineHeight: 23.4, trackingEm: 0 },
  paneTitle: { size: 21, weight: 600, lineHeight: 27.3, trackingEm: 0 },
  sectionTitle: { size: 15, weight: 600, lineHeight: 21.75, trackingEm: 0 },
  body: { size: 14, weight: 400, lineHeight: 20.3, trackingEm: 0 },
  muted: { size: 12, weight: 400, lineHeight: 17.4, trackingEm: 0 },
  action: { size: 14, weight: 400, lineHeight: 20.3, trackingEm: 0 },
  primaryAction: { size: 14, weight: 600, lineHeight: 20.3, trackingEm: 0 },
  kicker: { size: 11, weight: 400, lineHeight: 15.95, trackingEm: 0.13 },
  inviteCode: { size: 29, weight: 600, lineHeight: 40.6, trackingEm: 0.17 },
  codeInput: { size: 28, weight: 400, lineHeight: 40.6, trackingEm: 0.16 }
};

export function nearbyLetterSpacingFp(role: NearbyTypeRole): number {
  return Number((role.size * role.trackingEm).toFixed(2));
}

export function nearbyRoleFontSizeFp(role: NearbyTypeRole, _fontScale: number): number {
  // System fontSizeScale applies to fp. Do not shrink role tokens to fit.
  return role.size;
}

export function nearbyTapHeightVp(fontScale: number, roleLineHeight: number): number {
  const grown = roleLineHeight * fontScale;
  if (grown > NEARBY_MIN_TAP_HEIGHT_VP) {
    return grown;
  }
  return NEARBY_MIN_TAP_HEIGHT_VP;
}

export function nearbyShouldScrollVertically(contentHeightVp: number, viewportHeightVp: number): boolean {
  return contentHeightVp > viewportHeightVp;
}

export function nearbyAllowsEllipsisToFit(_role: NearbyTypeRole): boolean {
  return false;
}

export function nearbyEstimatedContentHeightVp(fontScale: number, reasonChars: number,
  _locale: string): number {
  const kicker = NEARBY_TYPE.kicker.lineHeight * fontScale;
  const title = NEARBY_TYPE.paneTitle.lineHeight * fontScale;
  const subtitle = NEARBY_TYPE.muted.lineHeight * fontScale;
  const glyph = NEARBY_TYPE.muted.size * fontScale;
  const perLine = Math.max(1, Math.floor(NEARBY_SCALE2_VIEWPORT_WIDTH_VP / glyph));
  const reasonLines = Math.max(1, Math.ceil(reasonChars / perLine));
  const reason = reasonLines * NEARBY_TYPE.muted.lineHeight * fontScale;
  const buttons = 3 * nearbyTapHeightVp(fontScale, NEARBY_TYPE.action.lineHeight);
  const chrome = 64 + 58;
  const gaps = 48;
  return kicker + title + subtitle + reason + buttons + chrome + gaps;
}
