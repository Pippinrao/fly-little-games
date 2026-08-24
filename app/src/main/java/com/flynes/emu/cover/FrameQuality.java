package com.flynes.emu.cover;

/** Cheap content score used to reject boot black screens and low-information menus. */
public final class FrameQuality {
    public static final double MIN_ACCEPTABLE = 18.0;

    private FrameQuality() {}

    public static double score(CoverFrame frame) {
        byte[] pixels = frame.pixelsUnsafe();
        int stepX = Math.max(1, frame.width() / 32);
        int stepY = Math.max(1, frame.height() / 30);
        double sum = 0.0;
        double sumSquares = 0.0;
        double transitions = 0.0;
        int count = 0;
        for (int y = 0; y < frame.height(); y += stepY) {
            int previous = -1;
            for (int x = 0; x < frame.width(); x += stepX) {
                int luma = luma(frame, pixels, x, y);
                sum += luma;
                sumSquares += (double) luma * luma;
                if (previous >= 0 && Math.abs(luma - previous) >= 20) transitions += 1.0;
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
