package com.flynes.emu.video.reference;

import java.util.Objects;

/**
 * Deliberately test-only, direct semantic port of the official MMPX 2x reference.
 *
 * <p>This is an oracle, not production renderer code. Keep it structurally independent from
 * shaders and native pipeline implementations so it can detect their errors.</p>
 */
public final class MmpxCpuOracle {
    public static final String ORACLE_VERSION = "MMPX-2X-JCGT-2021-SUPPLEMENT";
    public static final String SOURCE_ARCHIVE_SHA256 =
            "1211c4b59d5bea3c5ebeb1dc3b31fe8a2f0c58f64955cee50e939f96543996e1";

    private MmpxCpuOracle() {}

    public static int outputWidth(int inputWidth) {
        if (inputWidth <= 0 || inputWidth > Integer.MAX_VALUE / 2) {
            throw new IllegalArgumentException("inputWidth cannot be scaled safely");
        }
        return inputWidth * 2;
    }

    public static int outputHeight(int inputHeight) {
        if (inputHeight <= 0 || inputHeight > Integer.MAX_VALUE / 2) {
            throw new IllegalArgumentException("inputHeight cannot be scaled safely");
        }
        return inputHeight * 2;
    }

    public static int[] scale2x(int[] source, int width, int height) {
        Objects.requireNonNull(source, "source");
        int scaledWidth = outputWidth(width);
        int scaledHeight = outputHeight(height);
        if ((long) width * height != source.length) {
            throw new IllegalArgumentException("source size does not match dimensions");
        }
        int[] destination = new int[Math.multiplyExact(scaledWidth, scaledHeight)];

        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int a = source(source, width, height, x - 1, y - 1);
                int b = source(source, width, height, x, y - 1);
                int c = source(source, width, height, x + 1, y - 1);
                int d = source(source, width, height, x - 1, y);
                int e = source(source, width, height, x, y);
                int f = source(source, width, height, x + 1, y);
                int g = source(source, width, height, x - 1, y + 1);
                int h = source(source, width, height, x, y + 1);
                int i = source(source, width, height, x + 1, y + 1);
                int q = source(source, width, height, x - 2, y);
                int r = source(source, width, height, x + 2, y);
                int p = source(source, width, height, x, y - 2);
                int s = source(source, width, height, x, y + 2);

                int j = e;
                int k = e;
                int l = e;
                int m = e;

                if (((a ^ e) | (b ^ e) | (c ^ e) | (d ^ e)
                        | (f ^ e) | (g ^ e) | (h ^ e) | (i ^ e)) != 0) {
                    int bl = luma(b);
                    int dl = luma(d);
                    int el = luma(e);
                    int fl = luma(f);
                    int hl = luma(h);

                    if (d == b && d != h && d != f
                            && (el >= dl || e == a)
                            && anyEq3(e, a, c, g)
                            && (el < dl || a != d || e != p || e != q)) {
                        j = d;
                    }
                    if (b == f && b != d && b != h
                            && (el >= bl || e == c)
                            && anyEq3(e, a, c, i)
                            && (el < bl || c != b || e != p || e != r)) {
                        k = b;
                    }
                    if (h == d && h != f && h != b
                            && (el >= hl || e == g)
                            && anyEq3(e, a, g, i)
                            && (el < hl || g != h || e != s || e != q)) {
                        l = h;
                    }
                    if (f == h && f != b && f != d
                            && (el >= fl || e == i)
                            && anyEq3(e, c, g, i)
                            && (el < fl || i != h || e != r || e != s)) {
                        m = f;
                    }

                    if (e != f && allEq4(e, c, i, d, q) && allEq2(f, b, h)
                            && f != source(source, width, height, x + 3, y)) {
                        k = f;
                        m = f;
                    }
                    if (e != d && allEq4(e, a, g, f, r) && allEq2(d, b, h)
                            && d != source(source, width, height, x - 3, y)) {
                        j = d;
                        l = d;
                    }
                    if (e != h && allEq4(e, g, i, b, p) && allEq2(h, d, f)
                            && h != source(source, width, height, x, y + 3)) {
                        l = h;
                        m = h;
                    }
                    if (e != b && allEq4(e, a, c, h, s) && allEq2(b, d, f)
                            && b != source(source, width, height, x, y - 3)) {
                        j = b;
                        k = b;
                    }

                    if (bl < el && allEq4(e, g, h, i, s) && noneEq4(e, a, d, c, f)) {
                        j = b;
                        k = b;
                    }
                    if (hl < el && allEq4(e, a, b, c, p) && noneEq4(e, d, g, i, f)) {
                        l = h;
                        m = h;
                    }
                    if (fl < el && allEq4(e, a, d, g, q) && noneEq4(e, b, c, i, h)) {
                        k = f;
                        m = f;
                    }
                    if (dl < el && allEq4(e, c, f, i, r) && noneEq4(e, b, a, g, h)) {
                        j = d;
                        l = d;
                    }

                    if (h != b) {
                        if (h != a && h != e && h != c) {
                            if (allEq3(h, g, f, r)
                                    && noneEq2(h, d, source(source, width, height, x + 2, y - 1))) {
                                l = m;
                            }
                            if (allEq3(h, i, d, q)
                                    && noneEq2(h, f, source(source, width, height, x - 2, y - 1))) {
                                m = l;
                            }
                        }
                        if (b != i && b != g && b != e) {
                            if (allEq3(b, a, f, r)
                                    && noneEq2(b, d, source(source, width, height, x + 2, y + 1))) {
                                j = k;
                            }
                            if (allEq3(b, c, d, q)
                                    && noneEq2(b, f, source(source, width, height, x - 2, y + 1))) {
                                k = j;
                            }
                        }
                    }

                    if (f != d) {
                        if (d != i && d != e && d != c) {
                            if (allEq3(d, a, h, s)
                                    && noneEq2(d, b, source(source, width, height, x + 1, y + 2))) {
                                j = l;
                            }
                            if (allEq3(d, g, b, p)
                                    && noneEq2(d, h, source(source, width, height, x + 1, y - 2))) {
                                l = j;
                            }
                        }
                        if (f != e && f != a && f != g) {
                            if (allEq3(f, c, h, s)
                                    && noneEq2(f, b, source(source, width, height, x - 1, y + 2))) {
                                k = m;
                            }
                            if (allEq3(f, i, b, p)
                                    && noneEq2(f, h, source(source, width, height, x - 1, y - 2))) {
                                m = k;
                            }
                        }
                    }
                }

                int destinationIndex = x * 2 + y * 4 * width;
                destination[destinationIndex] = j;
                destination[destinationIndex + 1] = k;
                destination[destinationIndex + scaledWidth] = l;
                destination[destinationIndex + scaledWidth + 1] = m;
            }
        }
        return destination;
    }

    private static int source(int[] pixels, int width, int height, int x, int y) {
        int clampedX = Math.max(0, Math.min(x, width - 1));
        int clampedY = Math.max(0, Math.min(y, height - 1));
        return pixels[clampedX + clampedY * width];
    }

    private static int luma(int color) {
        int alpha = color >>> 24;
        int channelSum = ((color & 0x00ff0000) >>> 16)
                + ((color & 0x0000ff00) >>> 8)
                + (color & 0x000000ff) + 1;
        return channelSum * (256 - alpha);
    }

    private static boolean allEq2(int value, int a, int b) {
        return value == a && value == b;
    }

    private static boolean allEq3(int value, int a, int b, int c) {
        return value == a && value == b && value == c;
    }

    private static boolean allEq4(int value, int a, int b, int c, int d) {
        return value == a && value == b && value == c && value == d;
    }

    private static boolean anyEq3(int value, int a, int b, int c) {
        return value == a || value == b || value == c;
    }

    private static boolean noneEq2(int value, int a, int b) {
        return value != a && value != b;
    }

    private static boolean noneEq4(int value, int a, int b, int c, int d) {
        return value != a && value != b && value != c && value != d;
    }
}
