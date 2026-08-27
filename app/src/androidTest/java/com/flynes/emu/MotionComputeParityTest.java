package com.flynes.emu;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.video.MotionOffscreenRenderer;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.Random;

@RunWith(AndroidJUnit4.class)
public final class MotionComputeParityTest {
    private static final int WIDTH = 256;
    private static final int HEIGHT = 240;

    @Test public void es31IntermediateMatchesPortableRgb565Oracle() {
        assertParity("two-pixel motion", movingPair());
        assertParity("small occlusion", occlusionPair());
        assertParity("converging overlap arbitration", convergingPair());
        assertParity("full flash hold", new Pair(
                filled((short) 0x0000), filled((short) 0xffff)));
        assertParity("scene cut hold", randomPair());
    }

    private static void assertParity(String name, Pair pair) {
        MotionOffscreenRenderer.Result cpu = MotionOffscreenRenderer.renderCpu(
                pair.a, pair.b, WIDTH, HEIGHT);
        MotionOffscreenRenderer.Result gpu = MotionOffscreenRenderer.renderGpu(
                pair.a, pair.b, WIDTH, HEIGHT, false);
        assertTrue(name + " CPU: " + cpu.failureDetail(), cpu.succeeded());
        assertTrue(name + " GPU: " + gpu.failureDetail(), gpu.succeeded());
        assertEquals(name + " width", WIDTH, gpu.width());
        assertEquals(name + " height", HEIGHT, gpu.height());
        assertArrayEquals(name, cpu.pixels(), gpu.pixels());
    }

    private static Pair movingPair() {
        short[] a = filled((short) 0x001f);
        short[] b = filled((short) 0x001f);
        rect(a, 80, 100, 84, 104, (short) 0xf800);
        rect(b, 82, 100, 86, 104, (short) 0xf800);
        rect(a, 0, 0, WIDTH, 8, (short) 0xffff);
        rect(b, 0, 0, WIDTH, 8, (short) 0xffff);
        return new Pair(a, b);
    }

    private static Pair occlusionPair() {
        short[] a = filled((short) 0x07e0);
        short[] b = a.clone();
        rect(b, 200, 180, 204, 184, (short) 0xffe0);
        return new Pair(a, b);
    }

    private static Pair convergingPair() {
        short[] a = filled((short) 0x0000);
        short[] b = filled((short) 0x0000);
        rect(a, 68, 100, 72, 104, (short) 0xf800);
        rect(a, 74, 100, 78, 104, (short) 0x001f);
        rect(b, 70, 100, 74, 104, (short) 0xf800);
        rect(b, 74, 100, 76, 104, (short) 0x001f);
        return new Pair(a, b);
    }

    private static Pair randomPair() {
        short[] a = new short[WIDTH * HEIGHT];
        short[] b = new short[WIDTH * HEIGHT];
        Random random = new Random(0x46594e45L);
        for (int i = 0; i < a.length; ++i) {
            a[i] = (short) random.nextInt();
            b[i] = (short) random.nextInt();
        }
        return new Pair(a, b);
    }

    private static short[] filled(short value) {
        short[] result = new short[WIDTH * HEIGHT];
        java.util.Arrays.fill(result, value);
        return result;
    }

    private static void rect(short[] pixels, int left, int top, int right, int bottom,
                             short color) {
        for (int y = top; y < bottom; ++y)
            for (int x = left; x < right; ++x) pixels[x + y * WIDTH] = color;
    }

    private record Pair(short[] a, short[] b) {}
}
