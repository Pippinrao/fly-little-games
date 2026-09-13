package com.flynes.emu.cover;

/** Cheap content score used to reject boot black screens and low-information menus. */
public final class FrameQuality {
    public static final double MIN_ACCEPTABLE = 18.0;

    private FrameQuality() {}

    /**
     * Scores one frame on a 32x30 grid of block averages.
     *
     * <p>Each grid cell averages its pixels instead of sampling a single one.
     * Homebrew titles open on a black screen carrying a few lines of one-pixel
     * white credit text; sampling one pixel out of every 64 grid-stepped pixel
     * stepped over those strokes, scored the frame as an empty black screen, and
     * refused every bundled game a cover. Averaging keeps thin text energy while
     * a blank screen still averages to a flat, rejected grid.
     */
    public static double score(CoverFrame frame) {
        byte[] pixels = frame.pixelsUnsafe();
        final int columns = 32;
        final int rows = 30;
        final int blockWidth = Math.max(1, frame.width() / columns);
        final int blockHeight = Math.max(1, frame.height() / rows);
        double sum = 0.0;
        double sumSquares = 0.0;
        double transitions = 0.0;
        int count = 0;
        for (int blockY = 0; blockY < rows; blockY++) {
            double previous = -1.0;
            for (int blockX = 0; blockX < columns; blockX++) {
                double luma = blockLuma(frame, pixels, blockX * blockWidth, blockY * blockHeight,
                        blockWidth, blockHeight);
                sum += luma;
                sumSquares += luma * luma;
                if (previous >= 0.0 && Math.abs(luma - previous) >= 20.0) transitions += 1.0;
                previous = luma;
                count++;
            }
        }
        if (count == 0) return 0.0;
        double mean = sum / count;
        double variance = Math.max(0.0, sumSquares / count - mean * mean);
        double standardDeviation = Math.sqrt(variance);
        double transitionRatio = transitions / count;
        double exposurePenalty = mean < 8.0 || mean > 247.0 ? 20.0 : 0.0;
        return standardDeviation + transitionRatio * 45.0 - exposurePenalty;
    }

    /** Mean luma over one grid cell, clamped to the frame for non-divisible sizes. */
    private static double blockLuma(CoverFrame frame, byte[] pixels, int originX, int originY,
                                    int blockWidth, int blockHeight) {
        double total = 0.0;
        int samples = 0;
        final int endY = Math.min(frame.height(), originY + blockHeight);
        final int endX = Math.min(frame.width(), originX + blockWidth);
        for (int y = originY; y < endY; y++) {
            for (int x = originX; x < endX; x++) {
                total += luma(frame, pixels, x, y);
                samples++;
            }
        }
        return samples == 0 ? 0.0 : total / samples;
    }

    private static int luma(CoverFrame frame, byte[] pixels, int x, int y) {
        int index;
        int red;
        int green;
        int blue;
        switch (frame.format()) {
            case RGB565 -> {
                index = (y * frame.width() + x) * 2;
                int value = (pixels[index] & 0xff) | ((pixels[index + 1] & 0xff) << 8);
                red = ((value >>> 11) & 0x1f) * 255 / 31;
                green = ((value >>> 5) & 0x3f) * 255 / 63;
                blue = (value & 0x1f) * 255 / 31;
            }
            case RGB888 -> {
                index = (y * frame.width() + x) * 3;
                red = pixels[index] & 0xff;
                green = pixels[index + 1] & 0xff;
                blue = pixels[index + 2] & 0xff;
            }
            case RGBA8888 -> {
                index = (y * frame.width() + x) * 4;
                red = pixels[index] & 0xff;
                green = pixels[index + 1] & 0xff;
                blue = pixels[index + 2] & 0xff;
            }
            default -> throw new IllegalStateException("unknown pixel format");
        }
        return (red * 77 + green * 150 + blue * 29) >>> 8;
    }
}
