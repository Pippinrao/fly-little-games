package com.flynes.emu.video.quality;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;

public final class BundledAlgorithmAvailabilityTest {
    @Test public void currentBuildPublishesPinnedSpatialImplementationsOnly() {
        BuildAlgorithmAvailability build = BundledAlgorithmAvailability.current();

        assertTrue(build.mmpxIncluded());
        assertTrue(build.scaleFxIncluded());
        assertFalse(build.motionCompensationIncluded());
        assertEquals(1, build.mmpxOracleVersion());
        assertEquals("4f4eb801b2dbcaed0a9669a9deec1a098f3623d8",
                build.scaleFxUpstreamCommit());
        assertTrue(build.mmpxImplementationHash().matches("[0-9a-f]{64}"));
        assertTrue(build.scaleFxImplementationHash().matches("[0-9a-f]{64}"));
    }

    @Test public void publishedHashesMatchThePackagedImplementationSources() throws Exception {
        BuildAlgorithmAvailability build = BundledAlgorithmAvailability.current();
        List<String> common = Arrays.asList(
                "app/src/main/cpp/video/shader_program.cpp",
                "app/src/main/cpp/video/shader_program.h",
                "app/src/main/cpp/video/framebuffer_target.cpp",
                "app/src/main/cpp/video/framebuffer_target.h",
                "app/src/main/cpp/video/spatial_pipeline.cpp",
                "app/src/main/cpp/video/spatial_pipeline.h",
                "app/src/main/cpp/video/baseline_pipeline.cpp",
                "app/src/main/cpp/video/baseline_pipeline.h");
        List<String> mmpx = new ArrayList<>(common);
        mmpx.addAll(Arrays.asList("app/src/main/cpp/video/mmpx_pass.cpp",
                "app/src/main/cpp/video/mmpx_pass.h",
                "app/src/main/assets/shaders/mmpx/mmpx_2x.frag"));
        List<String> scaleFx = new ArrayList<>(common);
        scaleFx.addAll(Arrays.asList("app/src/main/cpp/video/scalefx_pipeline.cpp",
                "app/src/main/cpp/video/scalefx_pipeline.h",
                "app/src/main/assets/shaders/scalefx/scalefx-pass0.glsl",
                "app/src/main/assets/shaders/scalefx/scalefx-pass1.glsl",
                "app/src/main/assets/shaders/scalefx/scalefx-pass2.glsl",
                "app/src/main/assets/shaders/scalefx/scalefx-pass3.glsl",
                "app/src/main/assets/shaders/scalefx/scalefx-pass4.glsl"));

        Path root = repositoryRoot();
        assertEquals(build.mmpxImplementationHash(), composite(root, mmpx));
        assertEquals(build.scaleFxImplementationHash(), composite(root, scaleFx));
    }

    private static String composite(Path root, List<String> files) throws Exception {
        List<String> sorted = new ArrayList<>(files);
        Collections.sort(sorted);
        List<String> rows = new ArrayList<>();
        for (String file : sorted) {
            String source = new String(Files.readAllBytes(root.resolve(file)),
                    StandardCharsets.UTF_8).replace("\r\n", "\n").replace('\r', '\n');
            rows.add(file + '\0' + sha256(source.getBytes(StandardCharsets.UTF_8)));
        }
        return sha256(String.join("\n", rows).getBytes(StandardCharsets.UTF_8));
    }

    private static Path repositoryRoot() throws IOException {
        Path path = Paths.get(System.getProperty("user.dir")).toAbsolutePath();
        while (path != null && !Files.isDirectory(path.resolve("app/src/main"))) {
            path = path.getParent();
        }
        if (path == null) throw new IOException("repository root not found");
        return path;
    }

    private static String sha256(byte[] bytes) throws NoSuchAlgorithmException {
        byte[] digest = MessageDigest.getInstance("SHA-256").digest(bytes);
        StringBuilder result = new StringBuilder(64);
        for (byte value : digest) result.append(String.format("%02x", value & 0xff));
        return result.toString();
    }
}
