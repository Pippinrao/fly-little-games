export class ViewportSize {
  width: number;
  height: number;

  constructor(width: number, height: number) {
    this.width = width;
    this.height = height;
  }
}

/** Computes the game image bounds in physical pixels. */
export function computeViewport(safeWidth: number, safeHeight: number, aspectMode: number,
  sourceWidth: number, sourceHeight: number): ViewportSize {
  const availableWidth = Math.max(1, Math.floor(safeWidth));
  const availableHeight = Math.max(1, Math.floor(safeHeight));
  const contentWidth = Math.max(1, Math.floor(sourceWidth));
  const contentHeight = Math.max(1, Math.floor(sourceHeight));
  if (aspectMode === 3) {
    const scale = Math.max(1, Math.min(Math.floor(availableWidth / contentWidth),
      Math.floor(availableHeight / contentHeight)));
    return new ViewportSize(Math.min(availableWidth, contentWidth * scale),
      Math.min(availableHeight, contentHeight * scale));
  }
  const aspect = aspectMode === 1 ? 4 / 3 : contentWidth / contentHeight;
  let width = Math.round(availableHeight * aspect);
  let height = availableHeight;
  if (width > availableWidth) {
    width = availableWidth;
    height = Math.round(availableWidth / aspect);
  }
  return new ViewportSize(Math.max(1, width), Math.max(1, height));
}
