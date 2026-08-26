package com.flynes.emu;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import android.content.res.AssetManager;

import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import com.flynes.emu.video.SpatialOffscreenRenderer;
import com.flynes.emu.video.reference.ScaleFxReferenceRenderer;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class SpatialFilterGoldenTest {
    private static final List<String> FIXTURES = List.of(
            "solid-1x1.fixture",
            "diagonal-3x3.fixture",
            "thin-line-3x5.fixture",
            "checkerboard-4x3.fixture",
            "palette-corner-5x4.fixture",
            "edge-odd-1x5.fixture");

    @Test
    public void nearest2xIsPixelExact() throws Exception {
        for (String fileName : FIXTURES) {
            Fixture fixture = loadFixture(fileName);
            int[] expected = nearest2x(fixture.input, fixture.width, fixture.height);

            SpatialOffscreenRenderer.Result result = SpatialOffscreenRenderer.renderForTesting(
                    assets(), SpatialOffscreenRenderer.Algorithm.NEAREST_2X,
                    fixture.input, fixture.width, fixture.height);

            assertSuccessful(fixture.name, fixture.width * 2, fixture.height * 2, result);
            assertArrayEquals(fixture.name, expected, result.pixels());
        }
    }

    @Test
    public void mmpx2xMatchesTheFrozenIndependentOraclePixelForPixel() throws Exception {
        for (String fileName : FIXTURES) {
            Fixture fixture = loadFixture(fileName);

            SpatialOffscreenRenderer.Result result = SpatialOffscreenRenderer.renderForTesting(
                    assets(), SpatialOffscreenRenderer.Algorithm.MMPX_2X,
                    fixture.input, fixture.width, fixture.height);

            assertSuccessful(fixture.name, fixture.width * 2, fixture.height * 2, result);
            assertArrayEquals(fixture.name, fixture.expected, result.pixels());
        }
    }

    @Test
    public void scaleFx3xMatchesPinnedUpstreamReferenceWithinOneLsb() throws Exception {
        for (String fileName : FIXTURES) {
            Fixture fixture = loadFixture(fileName);
            Fixture frozen = loadFixture("scalefx", fileName);
            assertArrayEquals(fixture.name + " frozen input", fixture.input, frozen.input);
            int[] independentReference = ScaleFxReferenceRenderer.render3x(
                    assets(), fixture.input, fixture.width, fixture.height);
            assertWithinOneLsb(fixture.name + " frozen upstream",
                    frozen.expected, independentReference);

            SpatialOffscreenRenderer.Result result = SpatialOffscreenRenderer.renderForTesting(
                    assets(), SpatialOffscreenRenderer.Algorithm.SCALEFX_3X,
                    fixture.input, fixture.width, fixture.height);

            assertSuccessful(fixture.name, fixture.width * 3, fixture.height * 3, result);
            assertWithinOneLsb(fixture.name, frozen.expected, result.pixels());
        }
    }

    private static void assertSuccessful(String name, int width, int height,
                                         SpatialOffscreenRenderer.Result result) {
        assertTrue(name + ": " + result.failureDetail(), result.succeeded());
        assertEquals(name + " width", width, result.width());
        assertEquals(name + " height", height, result.height());
    }

    private static AssetManager assets() {
        return InstrumentationRegistry.getInstrumentation().getTargetContext().getAssets();
    }

    private static Fixture loadFixture(String fileName) throws Exception {
        return loadFixture("oracle", fileName);
    }

    private static Fixture loadFixture(String directory, String fileName) throws Exception {
        Map<String, String> values = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(new InputStreamReader(
                InstrumentationRegistry.getInstrumentation().getContext().getAssets().open(
                        "spatial-golden/" + directory + "/" + fileName),
                StandardCharsets.UTF_8))) {
            for (String line; (line = reader.readLine()) != null; ) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith("#")) continue;
                int separator = trimmed.indexOf('=');
                if (separator <= 0) throw new IllegalArgumentException("Malformed " + line);
                values.put(trimmed.substring(0, separator), trimmed.substring(separator + 1));
            }
        }
        String name = values.get("name");
        int width = Integer.parseInt(values.get("width"));
        int height = Integer.parseInt(values.get("height"));
        return new Fixture(name, width, height, parsePixels(values.get("input")),
                parsePixels(values.get("expected")));
    }

    private static int[] parsePixels(String encoded) {
        String[] tokens = encoded.split(",");
        int[] pixels = new int[tokens.length];
        for (int index = 0; index < tokens.length; index++) {
            pixels[index] = (int) Long.parseUnsignedLong(tokens[index], 16);
        }
        return pixels;
    }

    private static int[] nearest2x(int[] input, int width, int height) {
        int[] output = new int[width * height * 4];
        int outputWidth = width * 2;
        for (int y = 0; y < height; y++) {
            for (int x = 0; x < width; x++) {
                int value = input[x + y * width];
                int index = x * 2 + y * 2 * outputWidth;
                output[index] = value;
                output[index + 1] = value;
                output[index + outputWidth] = value;
                output[index + outputWidth + 1] = value;
            }
        }
        return output;
    }

    private static void assertWithinOneLsb(String name, int[] expected, int[] actual) {
        assertEquals(name + " pixel count", expected.length, actual.length);
        for (int pixelIndex = 0; pixelIndex < expected.length; pixelIndex++) {
            for (int shift = 0; shift <= 24; shift += 8) {
                int expectedChannel = (expected[pixelIndex] >>> shift) & 0xff;
                int actualChannel = (actual[pixelIndex] >>> shift) & 0xff;
                assertTrue(name + " pixel=" + pixelIndex + " channel=" + shift,
                        Math.abs(expectedChannel - actualChannel) <= 1);
            }
        }
    }

    private record Fixture(String name, int width, int height, int[] input, int[] expected) {}
}
