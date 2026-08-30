package com.flynes.emu.video.reference;

import android.content.res.AssetManager;
import android.opengl.EGL14;
import android.opengl.EGLConfig;
import android.opengl.EGLContext;
import android.opengl.EGLDisplay;
import android.opengl.EGLSurface;
import android.opengl.GLES20;
import android.opengl.GLES30;

import java.io.ByteArrayOutputStream;
import java.io.InputStream;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;
import java.nio.charset.StandardCharsets;

/** Android-test-only runner for the unmodified, pinned libretro five-pass source. */
public final class ScaleFxReferenceRenderer {
    public static final String UPSTREAM_COMMIT =
            "4f4eb801b2dbcaed0a9669a9deec1a098f3623d8";
    private static final int GL_HALF_FLOAT_OES = 0x8D61;

    private static final float[] POSITIONS = {
            -1f, -1f, 1f, -1f, -1f, 1f, 1f, 1f};
    private static final float[] TEXCOORDS = {
            0f, 0f, 1f, 0f, 0f, 1f, 1f, 1f};
    private static final float[] IDENTITY = {
            1f, 0f, 0f, 0f,
            0f, 1f, 0f, 0f,
            0f, 0f, 1f, 0f,
            0f, 0f, 0f, 1f};

    private ScaleFxReferenceRenderer() {}

    public static int[] render3x(AssetManager assets, int[] packedAabbggrr,
                                 int width, int height) throws Exception {
        try (Context context = Context.create()) {
            int source = createSourceTexture(packedAabbggrr, width, height);
            Target pass0 = Target.tryCreate(width, height, GLES20.GL_FLOAT, context.major);
            Target pass1 = pass0 == null ? null
                    : Target.tryCreate(width, height, GLES20.GL_FLOAT, context.major);
            if (pass0 == null || pass1 == null) {
                if (pass0 != null) pass0.close();
                if (pass1 != null) pass1.close();
                int halfType = context.major >= 3 ? GLES30.GL_HALF_FLOAT : GL_HALF_FLOAT_OES;
                pass0 = Target.create(width, height, halfType, context.major);
                pass1 = Target.create(width, height, halfType, context.major);
            }
            Target pass2 = Target.create(width, height, GLES20.GL_UNSIGNED_BYTE, context.major);
            Target pass3 = Target.create(width, height, GLES20.GL_UNSIGNED_BYTE, context.major);
            Target pass4 = Target.create(width * 3, height * 3, GLES20.GL_UNSIGNED_BYTE,
                    context.major);
            try {
                Target[] targets = {pass0, pass1, pass2, pass3, pass4};
                int[] primary = {source, pass0.texture, pass1.texture, pass2.texture,
                        pass3.texture};
                int[] secondary = {0, 0, pass0.texture, 0, source};
                for (int pass = 0; pass < 5; pass++) {
                    String upstream = readAsset(assets,
                            "shaders/scalefx/scalefx-pass" + pass + ".glsl");
                    int program = createProgram(stage(upstream, "VERTEX"),
                            stage(upstream, "FRAGMENT"));
                    try {
                        draw(program, primary[pass], secondary[pass], width, height,
                                targets[pass], pass);
                    } finally {
                        GLES20.glDeleteProgram(program);
                    }
                }
                return pass4.readPackedPixels();
            } finally {
                int[] texture = {source};
                GLES20.glDeleteTextures(1, texture, 0);
                pass4.close();
                pass3.close();
                pass2.close();
                pass1.close();
                pass0.close();
            }
        }
    }

    private static void draw(int program, int primary, int secondary, int width, int height,
                             Target target, int pass) {
        target.bind();
        GLES20.glUseProgram(program);
        setMatrix(program, "MVPMatrix", IDENTITY);
        set2f(program, "TextureSize", width, height);
        set2f(program, "InputSize", width, height);
        set2f(program, "OutputSize", target.width, target.height);
        set2f(program, "PassPrev5TextureSize", width, height);
        set2f(program, "PassPrev5InputSize", width, height);
        bindTexture(program, "Texture", primary, 0);
        if (pass == 2) bindTexture(program, "PassPrev2Texture", secondary, 1);
        if (pass == 4) bindTexture(program, "PassPrev5Texture", secondary, 1);

        FloatBuffer positions = floatBuffer(POSITIONS);
        FloatBuffer texcoords = floatBuffer(TEXCOORDS);
        int vertex = GLES20.glGetAttribLocation(program, "VertexCoord");
        int texcoord = GLES20.glGetAttribLocation(program, "TexCoord");
        if (vertex >= 0) {
            GLES20.glVertexAttribPointer(vertex, 2, GLES20.GL_FLOAT, false, 0, positions);
            GLES20.glEnableVertexAttribArray(vertex);
        }
        if (texcoord >= 0) {
            GLES20.glVertexAttribPointer(texcoord, 2, GLES20.GL_FLOAT, false, 0, texcoords);
            GLES20.glEnableVertexAttribArray(texcoord);
        }
        int color = GLES20.glGetAttribLocation(program, "COLOR");
        if (color >= 0) GLES20.glVertexAttrib4f(color, 1f, 1f, 1f, 1f);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        if (vertex >= 0) GLES20.glDisableVertexAttribArray(vertex);
        if (texcoord >= 0) GLES20.glDisableVertexAttribArray(texcoord);
        checkGl("ScaleFX pass " + pass);
    }

    private static int createSourceTexture(int[] pixels, int width, int height) {
        ByteBuffer rgba = ByteBuffer.allocateDirect(pixels.length * 4);
        for (int pixel : pixels) {
            rgba.put((byte) pixel);
            rgba.put((byte) (pixel >>> 8));
            rgba.put((byte) (pixel >>> 16));
            rgba.put((byte) (pixel >>> 24));
        }
        rgba.position(0);
        int[] value = new int[1];
        GLES20.glGenTextures(1, value, 0);
        configureTexture(value[0]);
        GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
        GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, GLES20.GL_RGBA, width, height, 0,
                GLES20.GL_RGBA, GLES20.GL_UNSIGNED_BYTE, rgba);
        checkGl("source upload");
        return value[0];
    }

    private static String stage(String upstream, String stage) {
        int newline = upstream.indexOf('\n');
        if (!upstream.startsWith("#version 130") || newline < 0) {
            throw new IllegalArgumentException("Unexpected pinned ScaleFX source");
        }
        return "#define " + stage + "\n" + upstream.substring(newline + 1);
    }

    private static String readAsset(AssetManager assets, String path) throws Exception {
        try (InputStream input = assets.open(path);
             ByteArrayOutputStream output = new ByteArrayOutputStream()) {
            input.transferTo(output);
            return output.toString(StandardCharsets.UTF_8);
        }
    }

    private static int createProgram(String vertexSource, String fragmentSource) {
        int vertex = compile(GLES20.GL_VERTEX_SHADER, vertexSource);
        int fragment = compile(GLES20.GL_FRAGMENT_SHADER, fragmentSource);
        int program = GLES20.glCreateProgram();
        GLES20.glAttachShader(program, vertex);
        GLES20.glAttachShader(program, fragment);
        GLES20.glLinkProgram(program);
        GLES20.glDeleteShader(vertex);
        GLES20.glDeleteShader(fragment);
        int[] linked = new int[1];
        GLES20.glGetProgramiv(program, GLES20.GL_LINK_STATUS, linked, 0);
        if (linked[0] == 0) {
            String log = GLES20.glGetProgramInfoLog(program);
            GLES20.glDeleteProgram(program);
            throw new IllegalStateException("ScaleFX link failed: " + log);
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
            String log = GLES20.glGetShaderInfoLog(shader);
            GLES20.glDeleteShader(shader);
            throw new IllegalStateException("ScaleFX compile failed: " + log);
        }
        return shader;
    }

    private static void bindTexture(int program, String name, int texture, int unit) {
        int location = GLES20.glGetUniformLocation(program, name);
        if (location < 0) return;
        GLES20.glActiveTexture(GLES20.GL_TEXTURE0 + unit);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        GLES20.glUniform1i(location, unit);
    }

    private static void set2f(int program, String name, float x, float y) {
        int location = GLES20.glGetUniformLocation(program, name);
        if (location >= 0) GLES20.glUniform2f(location, x, y);
    }

    private static void setMatrix(int program, String name, float[] matrix) {
        int location = GLES20.glGetUniformLocation(program, name);
        if (location >= 0) GLES20.glUniformMatrix4fv(location, 1, false, matrix, 0);
    }

    private static FloatBuffer floatBuffer(float[] values) {
        FloatBuffer result = ByteBuffer.allocateDirect(values.length * Float.BYTES)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();
        result.put(values).position(0);
        return result;
    }

    private static void configureTexture(int texture) {
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER,
                GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER,
                GLES20.GL_NEAREST);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE);
    }

    private static void checkGl(String operation) {
        int error = GLES20.glGetError();
        if (error != GLES20.GL_NO_ERROR) {
            throw new IllegalStateException(operation + " GL error 0x"
                    + Integer.toHexString(error));
        }
    }

    private static final class Target implements AutoCloseable {
        final int width;
        final int height;
        final int texture;
        final int framebuffer;

        private Target(int width, int height, int texture, int framebuffer) {
            this.width = width;
            this.height = height;
            this.texture = texture;
            this.framebuffer = framebuffer;
        }

        static Target create(int width, int height, int type, int contextMajor) {
            Target result = tryCreate(width, height, type, contextMajor);
            if (result == null) {
                throw new IllegalStateException("Pinned ScaleFX target is unsupported");
            }
            return result;
        }

        static Target tryCreate(int width, int height, int type, int contextMajor) {
            while (GLES20.glGetError() != GLES20.GL_NO_ERROR) {}
            int[] value = new int[1];
            GLES20.glGenTextures(1, value, 0);
            int texture = value[0];
            configureTexture(texture);
            int internalFormat = GLES20.GL_RGBA;
            if (contextMajor >= 3 && type == GLES20.GL_FLOAT) internalFormat = GLES30.GL_RGBA32F;
            if (contextMajor >= 3 && type == GLES30.GL_HALF_FLOAT) {
                internalFormat = GLES30.GL_RGBA16F;
            }
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, internalFormat, width, height, 0,
                    GLES20.GL_RGBA, type, null);
            GLES20.glGenFramebuffers(1, value, 0);
            int framebuffer = value[0];
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, framebuffer);
            GLES20.glFramebufferTexture2D(GLES20.GL_FRAMEBUFFER, GLES20.GL_COLOR_ATTACHMENT0,
                    GLES20.GL_TEXTURE_2D, texture, 0);
            if (GLES20.glGetError() != GLES20.GL_NO_ERROR
                    || GLES20.glCheckFramebufferStatus(GLES20.GL_FRAMEBUFFER)
                    != GLES20.GL_FRAMEBUFFER_COMPLETE) {
                value[0] = framebuffer;
                GLES20.glDeleteFramebuffers(1, value, 0);
                value[0] = texture;
                GLES20.glDeleteTextures(1, value, 0);
                while (GLES20.glGetError() != GLES20.GL_NO_ERROR) {}
                return null;
            }
            checkGl("target create");
            return new Target(width, height, texture, framebuffer);
        }

        void bind() {
            GLES20.glBindFramebuffer(GLES20.GL_FRAMEBUFFER, framebuffer);
            GLES20.glViewport(0, 0, width, height);
        }

        int[] readPackedPixels() {
            bind();
            ByteBuffer bytes = ByteBuffer.allocateDirect(width * height * 4);
            GLES20.glReadPixels(0, 0, width, height, GLES20.GL_RGBA,
                    GLES20.GL_UNSIGNED_BYTE, bytes);
            checkGl("readback");
            int[] result = new int[width * height];
            for (int index = 0; index < result.length; index++) {
                result[index] = (bytes.get(index * 4) & 0xff)
                        | ((bytes.get(index * 4 + 1) & 0xff) << 8)
                        | ((bytes.get(index * 4 + 2) & 0xff) << 16)
                        | ((bytes.get(index * 4 + 3) & 0xff) << 24);
            }
            return result;
        }

        @Override public void close() {
            int[] value = {framebuffer};
            GLES20.glDeleteFramebuffers(1, value, 0);
            value[0] = texture;
            GLES20.glDeleteTextures(1, value, 0);
        }
    }

    private static final class Context implements AutoCloseable {
        int major;
        EGLDisplay display = EGL14.EGL_NO_DISPLAY;
        EGLSurface surface = EGL14.EGL_NO_SURFACE;
        EGLContext context = EGL14.EGL_NO_CONTEXT;

        static Context create() {
            Context owned = new Context();
            owned.display = EGL14.eglGetDisplay(EGL14.EGL_DEFAULT_DISPLAY);
            int[] version = new int[2];
            if (owned.display == EGL14.EGL_NO_DISPLAY
                    || !EGL14.eglInitialize(owned.display, version, 0, version, 1)) {
                throw new IllegalStateException("Reference EGL display unavailable");
            }
            int[] attributes = {EGL14.EGL_RENDERABLE_TYPE, 0x40,
                    EGL14.EGL_SURFACE_TYPE, EGL14.EGL_PBUFFER_BIT,
                    EGL14.EGL_RED_SIZE, 8, EGL14.EGL_GREEN_SIZE, 8,
                    EGL14.EGL_BLUE_SIZE, 8, EGL14.EGL_ALPHA_SIZE, 8, EGL14.EGL_NONE};
            EGLConfig[] configs = new EGLConfig[1];
            int[] count = new int[1];
            if (!EGL14.eglChooseConfig(owned.display, attributes, 0, configs, 0, 1,
                    count, 0) || count[0] != 1) {
                owned.close();
                throw new IllegalStateException("Reference EGL config unavailable");
            }
            int[] pbuffer = {EGL14.EGL_WIDTH, 1, EGL14.EGL_HEIGHT, 1, EGL14.EGL_NONE};
            owned.surface = EGL14.eglCreatePbufferSurface(owned.display, configs[0], pbuffer, 0);
            int[] context = {EGL14.EGL_CONTEXT_CLIENT_VERSION, 3, EGL14.EGL_NONE};
            owned.context = EGL14.eglCreateContext(owned.display, configs[0],
                    EGL14.EGL_NO_CONTEXT, context, 0);
            if (owned.surface == EGL14.EGL_NO_SURFACE || owned.context == EGL14.EGL_NO_CONTEXT
                    || !EGL14.eglMakeCurrent(owned.display, owned.surface, owned.surface,
                    owned.context)) {
                owned.close();
                throw new IllegalStateException("Reference EGL context unavailable");
            }
            owned.major = 3;
            return owned;
        }

        @Override public void close() {
            if (display != EGL14.EGL_NO_DISPLAY) {
                EGL14.eglMakeCurrent(display, EGL14.EGL_NO_SURFACE, EGL14.EGL_NO_SURFACE,
                        EGL14.EGL_NO_CONTEXT);
                if (context != EGL14.EGL_NO_CONTEXT) EGL14.eglDestroyContext(display, context);
                if (surface != EGL14.EGL_NO_SURFACE) EGL14.eglDestroySurface(display, surface);
                EGL14.eglTerminate(display);
            }
            display = EGL14.EGL_NO_DISPLAY;
            surface = EGL14.EGL_NO_SURFACE;
            context = EGL14.EGL_NO_CONTEXT;
        }
    }
}
