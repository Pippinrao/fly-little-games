package com.flynes.emu.video;

import com.flynes.emu.settings.AspectMode;

/** Pure viewport sizing for safe-area constrained landscape play. */
public final class ViewportLayout {
    public static final class Size {
        private final int width;
        private final int height;

        Size(int width, int height) {
            this.width = width;
            this.height = height;
        }

        public int width() { return width; }
        public int height() { return height; }
    }

    private ViewportLayout() { }

    public static Size compute(int windowWidth, int windowHeight, int insetLeft,
                               int insetRight, AspectMode mode,
                               int sourceWidth, int sourceHeight) {
        int safeWidth = Math.max(1, windowWidth - Math.max(0, insetLeft)
                - Math.max(0, insetRight));
        int safeHeight = Math.max(1, windowHeight);
        int width = Math.max(1, sourceWidth);
        int height = Math.max(1, sourceHeight);

        if (mode == AspectMode.INTEGER_SCALE) {
            int scale = Math.max(1, Math.min(safeWidth / width, safeHeight / height));
            return new Size(Math.min(safeWidth, width * scale),
                    Math.min(safeHeight, height * scale));
        }

        float aspect = mode == AspectMode.FOUR_BY_THREE
                ? 4f / 3f : width / (float) height;
        int targetWidth = Math.round(safeHeight * aspect);
        int targetHeight = safeHeight;
        if (targetWidth > safeWidth) {
            targetWidth = safeWidth;
            targetHeight = Math.round(safeWidth / aspect);
        }
        return new Size(Math.max(1, targetWidth), Math.max(1, targetHeight));
    }
}
