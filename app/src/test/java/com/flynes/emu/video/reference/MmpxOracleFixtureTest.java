package com.flynes.emu.video.reference;

import static org.junit.Assert.assertArrayEquals;
import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertTrue;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import java.util.HashSet;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import org.junit.Test;

public final class MmpxOracleFixtureTest {
    private static final List<String> FIXTURES = List.of(
            "solid-1x1.fixture",
            "diagonal-3x3.fixture",
            "thin-line-3x5.fixture",
            "checkerboard-4x3.fixture",
            "palette-corner-5x4.fixture",
            "edge-odd-1x5.fixture");

    @Test
    public void matchesOfficialReferenceFixturesExactly() throws Exception {
        for (String fixtureName : FIXTURES) {
            Fixture fixture = loadFixture(fixtureName);

            int[] actual = MmpxCpuOracle.scale2x(
                    fixture.input, fixture.width, fixture.height);

            assertEquals(fixture.name + " output width", fixture.width * 2,
                    MmpxCpuOracle.outputWidth(fixture.width));
            assertEquals(fixture.name + " output height", fixture.height * 2,
                    MmpxCpuOracle.outputHeight(fixture.height));
            assertArrayEquals(fixture.name, fixture.expected, actual);
        }
    }

    @Test
    public void preservesTheSourcePalette() throws Exception {
        for (String fixtureName : FIXTURES) {
            Fixture fixture = loadFixture(fixtureName);
            Set<Integer> palette = new HashSet<>();
            Arrays.stream(fixture.input).forEach(palette::add);

            for (int pixel : MmpxCpuOracle.scale2x(
                    fixture.input, fixture.width, fixture.height)) {
                assertTrue(fixture.name + " introduced " + Integer.toUnsignedString(pixel, 16),
                        palette.contains(pixel));
            }
        }
    }

    @Test
    public void exposesPinnedReferenceIdentity() {
        assertEquals("MMPX-2X-JCGT-2021-SUPPLEMENT", MmpxCpuOracle.ORACLE_VERSION);
        assertEquals(
                "1211c4b59d5bea3c5ebeb1dc3b31fe8a2f0c58f64955cee50e939f96543996e1",
                MmpxCpuOracle.SOURCE_ARCHIVE_SHA256);
    }

    private static Fixture loadFixture(String fileName) throws IOException {
        String resource = "/spatial-golden/oracle/" + fileName;
        InputStream stream = MmpxOracleFixtureTest.class.getResourceAsStream(resource);
        if (stream == null) {
            throw new IOException("Missing fixture " + resource);
        }
        Map<String, String> values = new LinkedHashMap<>();
        try (BufferedReader reader = new BufferedReader(
                new InputStreamReader(stream, StandardCharsets.UTF_8))) {
            for (String line; (line = reader.readLine()) != null; ) {
                String trimmed = line.trim();
                if (trimmed.isEmpty() || trimmed.startsWith("#")) {
                    continue;
                }
                int separator = trimmed.indexOf('=');
                if (separator <= 0) {
                    throw new IOException("Malformed fixture line: " + line);
                }
                values.put(trimmed.substring(0, separator), trimmed.substring(separator + 1));
            }
        }
        String name = require(values, "name");
        int width = Integer.parseInt(require(values, "width"));
        int height = Integer.parseInt(require(values, "height"));
        int[] input = parsePixels(require(values, "input"));
        int[] expected = parsePixels(require(values, "expected"));
        assertEquals(name + " input count", width * height, input.length);
        assertEquals(name + " expected count", width * height * 4, expected.length);
        return new Fixture(name, width, height, input, expected);
    }

    private static String require(Map<String, String> values, String key) throws IOException {
        String value = values.get(key);
        if (value == null) {
            throw new IOException("Missing fixture field " + key);
        }
        return value;
    }

    private static int[] parsePixels(String encoded) {
        if (encoded.isEmpty()) {
            return new int[0];
        }
        String[] tokens = encoded.split(",");
        int[] pixels = new int[tokens.length];
        for (int index = 0; index < tokens.length; index++) {
            pixels[index] = (int) Long.parseUnsignedLong(tokens[index], 16);
        }
        return pixels;
    }

    private record Fixture(String name, int width, int height, int[] input, int[] expected) {}
}
