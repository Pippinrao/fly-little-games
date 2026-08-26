package com.flynes.emu.video;

import android.opengl.GLES20;
import android.opengl.GLSurfaceView;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.status.VideoStatusAccumulator;

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
    private static final String FRAGMENT_HEADER =
            "precision mediump float;\n" +
            "uniform sampler2D uTexture;\n" +
            "varying vec2 vTexCoord;\n";
    private static final String NEAREST_SHADER = FRAGMENT_HEADER +
            "void main(){ gl_FragColor=texture2D(uTexture,vTexCoord); }";
    private static final String SHARP_BILINEAR_SHADER = FRAGMENT_HEADER +
            "uniform vec2 uTextureSize;\n" +
            "void main(){\n" +
            " vec2 pixel=vTexCoord*uTextureSize-vec2(0.5);\n" +
            " vec2 base=floor(pixel);\n" +
            " vec2 sharpFraction=clamp((fract(pixel)-vec2(0.5))*2.0+vec2(0.5),0.0,1.0);\n" +
            " vec2 uv=(base+sharpFraction+vec2(0.5))/uTextureSize;\n" +
            " gl_FragColor=texture2D(uTexture,uv);\n" +
            "}";
    private static final String EDGE_ENHANCED_SHADER = FRAGMENT_HEADER +
            "uniform vec2 uTextureSize;\n" +
            "float colorDistance(vec4 a,vec4 b){ return dot(abs(a.rgb-b.rgb),vec3(0.299,0.587,0.114)); }\n" +
            "void main(){\n" +
            " vec2 pixel=vTexCoord*uTextureSize-vec2(0.5);\n" +
            " vec2 centre=(floor(pixel)+vec2(0.5))/uTextureSize;\n" +
            " vec2 f=fract(pixel); vec2 t=vec2(1.0)/uTextureSize;\n" +
            " vec4 e=texture2D(uTexture,centre);\n" +
            " vec4 b=texture2D(uTexture,centre-vec2(0.0,t.y));\n" +
            " vec4 d=texture2D(uTexture,centre-vec2(t.x,0.0));\n" +
            " vec4 r=texture2D(uTexture,centre+vec2(t.x,0.0));\n" +
            " vec4 h=texture2D(uTexture,centre+vec2(0.0,t.y));\n" +
            " vec4 outColor=e; float same=0.075;\n" +
            " if(colorDistance(d,r)>same && colorDistance(b,h)>same){\n" +
            "  if(f.x<0.5 && f.y<0.5 && colorDistance(d,b)<same) outColor=mix(e,d,0.72);\n" +
            "  else if(f.x>=0.5 && f.y<0.5 && colorDistance(b,r)<same) outColor=mix(e,r,0.72);\n" +
            "  else if(f.x<0.5 && f.y>=0.5 && colorDistance(d,h)<same) outColor=mix(e,d,0.72);\n" +
            "  else if(f.x>=0.5 && f.y>=0.5 && colorDistance(h,r)<same) outColor=mix(e,r,0.72);\n" +
            " }\n" +
            " gl_FragColor=outColor;\n" +
            "}";
    private static final String CRT_SHADER = FRAGMENT_HEADER +
            "uniform vec2 uTextureSize; uniform vec2 uOutputSize;\n" +
            "void main(){\n" +
            " vec4 color=texture2D(uTexture,vTexCoord);\n" +
            " float scanline=0.88+0.12*sin(vTexCoord.y*uOutputSize.y*3.14159265);\n" +
            " float mask=0.96+0.04*sin(vTexCoord.x*uOutputSize.x*2.0943951);\n" +
            " vec2 edge=vTexCoord*(vec2(1.0)-vTexCoord);\n" +
            " float vignette=clamp(pow(16.0*edge.x*edge.y,0.12),0.78,1.0);\n" +
            " gl_FragColor=vec4(color.rgb*scanline*mask*vignette,color.a);\n" +
            "}";

    private static final float[] QUAD = {
            -1f, -1f, 0f, 1f,
             1f, -1f, 1f, 1f,
            -1f,  1f, 0f, 0f,
             1f,  1f, 1f, 0f
    };

    private final FramePublisher publisher;
    private final VideoStatusAccumulator statusAccumulator;
    private final FloatBuffer vertices;
    private volatile FilterMode filterMode = FilterMode.EDGE_ENHANCED;
    private FilterMode activeFilterMode;
    private int program;
    private int texture;
    private int textureWidth;
    private int textureHeight;
    private PublishedFrame.Format textureFormat;
    private int appliedTextureFilter;
    private int outputWidth;
    private int outputHeight;

    public FrameRenderer(FramePublisher publisher) {
        this(publisher, null);
    }

    public FrameRenderer(FramePublisher publisher, VideoStatusAccumulator statusAccumulator) {
        this.publisher = publisher;
        this.statusAccumulator = statusAccumulator;
        vertices = ByteBuffer.allocateDirect(QUAD.length * Float.BYTES)
                .order(ByteOrder.nativeOrder()).asFloatBuffer();
        vertices.put(QUAD).position(0);
    }

    public void setFilterMode(FilterMode filterMode) {
        this.filterMode = filterMode == null ? FilterMode.EDGE_ENHANCED : filterMode;
    }

    static int textureFilter(FilterMode mode) {
        return mode == FilterMode.SHARP_BILINEAR || mode == FilterMode.CRT
                ? GLES20.GL_LINEAR : GLES20.GL_NEAREST;
    }

    static String fragmentShader(FilterMode mode) {
        FilterMode safeMode = mode == null ? FilterMode.EDGE_ENHANCED : mode;
        switch (safeMode) {
            case SHARP_BILINEAR: return SHARP_BILINEAR_SHADER;
            case NEAREST: return NEAREST_SHADER;
            case CRT: return CRT_SHADER;
            case EDGE_ENHANCED:
            default: return EDGE_ENHANCED_SHADER;
        }
    }

    @Override public void onSurfaceCreated(GL10 ignored, EGLConfig config) {
        program = 0;
        activeFilterMode = null;
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
        outputWidth = width;
        outputHeight = height;
        GLES20.glViewport(0, 0, width, height);
    }

    @Override public void onDrawFrame(GL10 ignored) {
        GLES20.glClear(GLES20.GL_COLOR_BUFFER_BIT);
        ensureProgram();
        if (program == 0 || texture == 0) return;

        publisher.poll().ifPresent(this::upload);
        if (textureWidth <= 0 || textureHeight <= 0) return;

        GLES20.glUseProgram(program);
        setUniform2f("uTextureSize", textureWidth, textureHeight);
        setUniform2f("uOutputSize", outputWidth, outputHeight);
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
        if (statusAccumulator != null) statusAccumulator.onTextureUploaded();
    }

    private void applyTextureFilter() {
        int requested = textureFilter(filterMode);
        if (requested == appliedTextureFilter) return;
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MIN_FILTER, requested);
        GLES20.glTexParameteri(GLES20.GL_TEXTURE_2D, GLES20.GL_TEXTURE_MAG_FILTER, requested);
        appliedTextureFilter = requested;
    }

    private void ensureProgram() {
        FilterMode requested = filterMode;
        if (program != 0 && activeFilterMode == requested) return;
        int replacement = createProgram(VERTEX_SHADER, fragmentShader(requested));
        if (program != 0) GLES20.glDeleteProgram(program);
        program = replacement;
        activeFilterMode = requested;
        appliedTextureFilter = 0;
    }

    private void setUniform2f(String name, float first, float second) {
        int location = GLES20.glGetUniformLocation(program, name);
        if (location >= 0) GLES20.glUniform2f(location, first, second);
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
