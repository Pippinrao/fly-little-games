package com.flynes.emu.video;

import android.content.Context;
import android.opengl.GLSurfaceView;

import com.flynes.emu.settings.FilterMode;

/** Vsync-paced OpenGL game surface. Duplicate sequences reuse the current texture. */
public final class GlFrameView extends GLSurfaceView {
    private final FrameRenderer frameRenderer;

    public GlFrameView(Context context, FramePublisher publisher) {
        super(context);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        frameRenderer = new FrameRenderer(publisher);
        setRenderer(frameRenderer);
        setRenderMode(RENDERMODE_CONTINUOUSLY);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    public void setFilterMode(FilterMode mode) {
        frameRenderer.setFilterMode(mode);
    }
}
