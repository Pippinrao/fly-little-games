package com.flynes.emu.video;

import java.util.Objects;

/** Device-only CPU/GPU intermediate renderer used to qualify Motion before UI unlock. */
public final class MotionOffscreenRenderer {
    public record Result(boolean succeeded, int width, int height, short[] pixels,
                         String failureDetail) {
        private static Result failure(int code) {
            return new Result(false, 0, 0, new short[0], failureName(code));
        }
    }

    static { System.loadLibrary("nescore"); }
    private MotionOffscreenRenderer() {}

    public static Result renderCpu(short[] a, short[] b, int width, int height) {
        return render(a, b, width, height, false, false);
    }

    public static Result renderGpu(short[] a, short[] b, int width, int height,
                                   boolean forceContextFailure) {
        return render(a, b, width, height, true, forceContextFailure);
    }

    private static Result render(short[] a, short[] b, int width, int height,
                                 boolean gpu, boolean forceContextFailure) {
        Objects.requireNonNull(a, "a");
        Objects.requireNonNull(b, "b");
        int[] encoded = nativeRender(a, b, width, height, gpu, forceContextFailure);
        if (encoded == null || encoded.length < 3 || encoded[0] != 0)
            return Result.failure(encoded == null || encoded.length == 0 ? -1 : encoded[0]);
        if (encoded[1] != width || encoded[2] != height
                || encoded.length != 3 + width * height) return Result.failure(-2);
        short[] pixels = new short[width * height];
        for (int i = 0; i < pixels.length; ++i) pixels[i] = (short) encoded[i + 3];
        return new Result(true, width, height, pixels, "");
    }

    private static String failureName(int code) {
        return switch (code) {
            case 1 -> "INVALID_INPUT";
            case 2 -> "EGL_CONTEXT_UNAVAILABLE";
            case 3 -> "ES31_UNAVAILABLE";
            case 4 -> "COMPUTE_SHADER_FAILURE";
            case 5 -> "GL_OPERATION_FAILED";
            default -> code == -2 ? "MALFORMED_NATIVE_RESULT" : "NATIVE_RENDER_FAILED";
        };
    }

    private static native int[] nativeRender(short[] a, short[] b, int width, int height,
                                             boolean gpu, boolean forceContextFailure);
}
