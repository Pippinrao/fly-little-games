package com.flynes.emu.video;

import android.content.res.AssetManager;

import java.util.Objects;

/** Deterministic offscreen entry point used by renderer conformance tests and diagnostics. */
public final class SpatialOffscreenRenderer {
    public enum Algorithm { NEAREST_2X, MMPX_2X, SCALEFX_3X }

    public record Result(boolean succeeded, int width, int height, int[] pixels,
                         String failureDetail) {
        private static Result failure(int code) {
            return new Result(false, 0, 0, new int[0], failureName(code));
        }
    }

    static {
        System.loadLibrary("nescore");
    }

    private SpatialOffscreenRenderer() {}

    public static Result renderForTesting(AssetManager assets, Algorithm algorithm,
                                          int[] pixels, int width, int height) {
        Objects.requireNonNull(assets, "assets");
        Objects.requireNonNull(algorithm, "algorithm");
        Objects.requireNonNull(pixels, "pixels");
        int[] encoded = nativeRender(assets, algorithm.ordinal(), pixels, width, height);
        if (encoded == null || encoded.length < 3 || encoded[0] != 0) {
            return Result.failure(encoded == null || encoded.length == 0 ? -1 : encoded[0]);
        }
        int outputWidth = encoded[1];
        int outputHeight = encoded[2];
        if (outputWidth <= 0 || outputHeight <= 0
                || encoded.length != 3 + outputWidth * outputHeight) {
            return Result.failure(-2);
        }
        int[] output = new int[outputWidth * outputHeight];
        System.arraycopy(encoded, 3, output, 0, output.length);
        return new Result(true, outputWidth, outputHeight, output, "");
    }

    private static String failureName(int code) {
        return switch (code) {
            case 1 -> "INVALID_INPUT";
            case 2 -> "EGL_CONTEXT_UNAVAILABLE";
            case 3 -> "SHADER_ASSET_MISSING";
            case 4 -> "SHADER_COMPILE_OR_LINK_FAILED";
            case 5 -> "FRAMEBUFFER_UNSUPPORTED";
            case 6 -> "GL_OPERATION_FAILED";
            case 7 -> "ALGORITHM_NOT_IMPLEMENTED";
            case -2 -> "MALFORMED_NATIVE_RESULT";
            default -> "NATIVE_RENDER_FAILED";
        };
    }

    private static native int[] nativeRender(AssetManager assets, int algorithm,
                                             int[] pixels, int width, int height);
}
