package com.flynes.emu.video.status;

import android.opengl.GLES20;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

/** Executes an actual render-to-texture and sample round trip in an owned probe context. */
final class GlColorTargetProbe {
    static final int GL_HALF_FLOAT_OES = 0x8D61;

    record Result(boolean renderableAndSampled, boolean linearlyFilterable) {}

    private GlColorTargetProbe() {}

    static Result probe(int type) {
        boolean nearest = roundTrip(type, false);
        return new Result(nearest, nearest && roundTrip(type, true));
    }

    private static boolean roundTrip(int type, boolean linear) {
        drainErrors();
        int sourceTexture = 0;
        int destinationTexture = 0;
        int sourceFramebuffer = 0;
        int destinationFramebuffer = 0;
        int program = 0;
        try {
            int[] value = new int[1];
            GLES20.glGenTextures(1, value, 0);
            sourceTexture = value[0];
            configureTexture(sourceTexture, linear ? GLES20.GL_LINEAR : GLES20.GL_NEAREST);
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, 2, 2, 0,
                    GLES20.GL_RGBA, type, null);
            if (GLES20.glGetError() != GLES20.GL_NO_ERROR) return false;

            GLES20.glGenFramebuffers(1, value, 0);
            sourceFramebuffer = value[0];
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, sourceFramebuffer);
            GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0,
                    GLES20.GL_TEXTURE_2D, sourceTexture, 0);
            if (GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
                    != GLES20.GL_FRAMEBUFFER_COMPLETE) return false;
            GLES20.glViewport(0, 0, 2, 2);
            GLES20.glClearColor(0.25f, 0.5f, 0.75f, 1.0f);
            GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
            if (GLES20.glGetError() != GLES20.GL_NO_ERROR) return false;

            GLES20.glGenTextures(1, value, 0);
            destinationTexture = value[0];
            configureTexture(destinationTexture, GLES20.GL_NEAREST);
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, 2, 2, 0,
                    GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, null);
            GLES20.glGenFramebuffers(1, value, 0);
            destinationFramebuffer = value[0];
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, destinationFramebuffer);
            GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0,
                    GLES20.GL_TEXTURE_2D, destinationTexture, 0);
            if (GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
                    != GLES20.GL_FRAMEBUFFER_COMPLETE) return false;

            program = createProgram();
            if (program == 0) return false;
            GLES20.glUseProgram(program);
            GLES20.glActiveTexture(GLES20.GL_TEXTURE0);
            GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, sourceTexture);
            GLES20.glUniform1i(GLES20.glGetUniformLocation(program, "uTexture"), 0);
            FloatBuffer vertices = ByteBuffer.allocateDirect(8 * Float.BYTES)
                    .order(ByteOrder.nativeOrder()).asFloatBuffer();
            vertices.put(new float[] {-1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f}).position(0);
            int position = GLES20.glGetAttribLocation(program, "aPosition");
            GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false, 0, vertices);
            GLES20.glEnableVertexAttribArray(position);
            GLES20.glViewport(0, 0, 2, 2);
            GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
            GLES20.glDisableVertexAttribArray(position);
            if (GLES20.glGetError() != GLES20.GL_NO_ERROR) return false;

            ByteBuffer pixel = ByteBuffer.allocateDirect(4);
            GLES20.glReadPixels(0, 0, 1, 1, GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, pixel);
            if (GLES20.glGetError() != GLES20.GL_NO_ERROR) return false;
            int red = pixel.get(0) & 0xff;
            int green = pixel.get(1) & 0xff;
            int blue = pixel.get(2) & 0xff;
            int alpha = pixel.get(3) & 0xff;
            return Math.abs(red - 64) <= 2 && Math.abs(green - 128) <= 2
                    && Math.abs(blue - 191) <= 2 && alpha >= 253;
        } finally {
            if (program != 0) GLES20.glDeleteProgram(program);
            int[] value = new int[1];
            if (sourceFramebuffer != 0) {
                value[0] = sourceFramebuffer;
                GLES20.glDeleteFramebuffers(1, value, 0);
            }
            if (destinationFramebuffer != 0) {
                value[0] = destinationFramebuffer;
                GLES20.glDeleteFramebuffers(1, value, 0);
            }
            if (sourceTexture != 0) {
                value[0] = sourceTexture;
                GLES20.glDeleteTextures(1, value, 0);
            }
            if (destinationTexture != 0) {
                value[0] = destinationTexture;
                GLES20.glDeleteTextures(1, value, 0);
            }
            drainErrors();
        }
    }

    private static void configureTexture(int texture, int filter) {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, filter);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, filter);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE);
    }

    private static int createProgram() {
        String vertex = "attribute vec2 aPosition; varying vec2 vTexCoord;"
                + "void main(){gl_Position=vec4(aPosition,0.,1.);"
                + "vTexCoord=(aPosition+vec2(1.))*0.5;}";
        String fragment = "precision highp float; uniform sampler2D uTexture;"
                + "varying vec2 vTexCoord; void main(){gl_FragColor=texture2D(uTexture,vTexCoord);}";
        int vs = compile(GLES20.GL_VERTEX_SHADER, vertex);
        int fs = compile(GLES20.GL_FRAGMENT_SHADER, fragment);
        if (vs == 0 || fs == 0) {
            if (vs != 0) GLES20.glDeleteShader(vs);
            if (fs != 0) GLES20.glDeleteShader(fs);
            return 0;
        }
        int program = GLES20.glCreateProgram();
        GLES20.glAttachShader(program, vs);
        GLES20.glAttachShader(program, fs);
        GLES20.glLinkProgram(program);
        GLES20.glDeleteShader(vs);
        GLES20.glDeleteShader(fs);
        int[] linked = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0);
        if (linked[0] == 0) {
            GLES20.glDeleteProgram(program);
            return 0;
        }
        return program;
    }

    private static int compile(int type, String source) {
        int shader = GLES20.glCreateShader(type);
        GLES20.glShaderSource(shader, source);
        GLES20.glCompileShader(shader);
        int[] compiled = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
        if (compiled[0] == 0) {
            GLES20.glDeleteShader(shader);
            return 0;
        }
        return shader;
    }

    private static void drainErrors() {
        while (GLES20.glGetError() != GLES20.GL_NO_ERROR) {
            // The probe owns this context and deliberately resets its error boundary.
        }
    }
}
