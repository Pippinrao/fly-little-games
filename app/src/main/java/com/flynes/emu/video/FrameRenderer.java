package com.flynes.emu.video;

import android.opengl.GLES20;
import android.opengl.GLSurfaceView;

import com.flynes.emu.settings.FilterMode;

import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.FloatBuffer;

import javax.microedition.khronos.egl.EGLConfig;
import javax.microedition.khronos.opengles.GL10;

/** OpenGL ES presenter that uploads each native sequence at most once. */
public final class FrameRenderer implements GLSurfaceView.Renderer {
    private static final String VERTEX_SHADER =
            "attribute vec2 aPosition;\n" +
            "attribute vec2 aTexCoord;\n" +
            "varying vec2 vTexCoord;\n" +
            "void main(){ gl_Position=vec4(aPosition,0.0,1.0); vTexCoord=aTexCoord; }";
    private static final String FRAGMENT_SHADER =
            "precision mediump float;\n" +
            "uniform sampler2D uTexture;\n" +
            "varying vec2 vTexCoord;\n" +
            "void main(){ gl_FragColor=texture2D(uTexture,vTexCoord); }";

    private static final float[] QUAD = {
            -1f, -1f, 0f, 1f,
             1f, -1f, 1f, 1f,
            -1f,  1f, 0f, 0f,
             1f,  1f, 1f, 0f
    };

    private final FramePublisher publisher;
    private final FloatBuffer vertices;
    private volatile FilterMode filterMode = FilterMode.SMOOTH;
    private int program;
    private int texture;
    private int textureWidth;
    private int textureHeight;
    private PublishedFrame.Format textureFormat;
    private int appliedTextureFilter;

    public FrameRenderer(FramePublisher publisher) {
        this.publisher = publisher;
        vertices = ByteBuffer.allocateDirect(QUAD.length * Float.BYTES)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();
        vertices.put(QUAD).position(0);
    }

    public void setFilterMode(FilterMode filterMode) {
        this.filterMode = filterMode == null ? FilterMode.SMOOTH : filterMode;
    }

    static int textureFilter(FilterMode mode) {
        return mode == FilterMode.NEAREST ? GLES20.GL_NEAREST : GLES20.GL_LINEAR;
    }

    @Override public void onSurfaceCreated(GL10 ignored, EGLConfig config) {
        program = createProgram(VERTEX_SHADER, FRAGMENT_SHADER);
        int[] textures = new int[1];
        GLES20.glGenTextures(1, textures, 0);
        texture = textures[0];
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_S,
                GLES20.GL_CLAMP_TO_EDGE);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_WRAP_T,
                GLES20.GL_CLAMP_TO_EDGE);
        appliedTextureFilter = 0;
        GLES20.glClearColor(0f, 0f, 0f, 1f);
    }

    @Override public void onSurfaceChanged(GL10 ignored, int width, int height) {
        GLES20.glViewport(0, 0, width, height);
    }

    @Override public void onDrawFrame(GL10 ignored) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        if (program == 0 || texture == 0) return;

        publisher.poll().ifPresent(this::upload);
        if (textureWidth <= 0 || textureHeight <= 0) return;

        GLES20.glUseProgram(program);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        applyTextureFilter();

        int position = GLES20.glGetAttribLocation(program, "aPosition");
        int texCoord = GLES20.glGetAttribLocation(program, "aTexCoord");
        vertices.position(0);
        GLES20.glVertexAttribPointer(position, 2, GLES20.GL_FLOAT, false,
                4 * Float.BYTES, vertices);
        vertices.position(2);
        GLES20.glVertexAttribPointer(texCoord, 2, GLES20.GL_FLOAT, false,
                4 * Float.BYTES, vertices);
        GLES20.glEnableVertexAttribArray(position);
        GLES20.glEnableVertexAttribArray(texCoord);
        GLES20.glDrawArrays(GLES20.GL_TRIANGLE_STRIP, 0, 4);
        GLES20.glDisableVertexAttribArray(position);
        GLES20.glDisableVertexAttribArray(texCoord);
    }

    private void upload(PublishedFrame frame) {
        int glFormat;
        int glType;
        switch (frame.format()) {
            case RGBA8888:
                glFormat = GLES20.GL_RGBA;
                glType = GLES20.GL_UNSIGNED_BYTE;
                break;
            case RGB888:
                glFormat = GLES20.GL_RGB;
                glType = GLES20.GL_UNSIGNED_BYTE;
                break;
            case RGB565:
            default:
                glFormat = GLES20.GL_RGB;
                glType = GLES20.GL_UNSIGNED_SHORT_5_6_5;
                break;
        }
        ByteBuffer pixels = frame.pixels();
        pixels.position(0);
        GLES20.glBindTexture(GLES20.GL_TEXTURE_2D, texture);
        GLES20.glPixelStorei(GLES20.GL_UNPACK_ALIGNMENT, 1);
        if (textureWidth != frame.width() || textureHeight != frame.height()
                || textureFormat != frame.format()) {
            GLES20.glTexImage2D(GLES20.GL_TEXTURE_2D, 0, glFormat,
                    frame.width(), frame.height(), 0, glFormat, glType, pixels);
            textureWidth = frame.width();
            textureHeight = frame.height();
            textureFormat = frame.format();
        } else {
            GLES20.glTexSubImage2D(GLES20.GL_TEXTURE_2D, 0, 0, 0,
                    frame.width(), frame.height(), glFormat, glType, pixels);
        }
    }

    private void applyTextureFilter() {
        int requested = textureFilter(filterMode);
        if (requested == appliedTextureFilter) return;
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, requested);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, requested);
        appliedTextureFilter = requested;
    }

    private static int createProgram(String vertexSource, String fragmentSource) {
        int vertex = compileShader(GLES20.GL_VERTEX_SHADER, vertexSource);
        int fragment = compileShader(GLES20.GL_FRAGMENT_SHADER, fragmentSource);
        int result = GLES20.glCreateProgram();
        GLES20.glAttachShader(result, vertex);
        GLES20.glAttachShader(result, fragment);
        GLES20.glLinkProgram(result);
        int[] linked = new int[1];
        GLES20.glGetProgramiv(result, GLES20.GL_LINK_STATUS, linked, 0);
        GLES20.glDeleteShader(vertex);
        GLES20.glDeleteShader(fragment);
        if (linked[0] == 0) {
            String log = GLES20.glGetProgramInfoLog(result);
            GLES20.glDeleteProgram(result);
            throw new IllegalStateException("OpenGL program link failed: " + log);
        }
        return result;
    }

    private static int compileShader(int type, String source) {
        int shader = GLES20.glCreateShader(type);
        GLES20.glShaderSource(shader, source);
        GLES20.glCompileShader(shader);
        int[] compiled = new int[1];
        GLES20.glGetShaderiv(shader, GLES20.GL_COMPILE_STATUS, compiled, 0);
        if (compiled[0] == 0) {
            String log = GLES20.glGetShaderInfoLog(shader);
            GLES20.glDeleteShader(shader);
            throw new IllegalStateException("OpenGL shader compile failed: " + log);
        }
        return shader;
    }
}
