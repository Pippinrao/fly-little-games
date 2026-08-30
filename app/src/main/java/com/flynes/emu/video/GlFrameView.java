package com.flynes.emu.video;

import android.content.Context;
import android.opengl.GLSurfaceView;

import com.flynes.emu.settings.FilterMode;
import com.flynes.emu.video.status.VideoStatusAccumulator;

/** Vsync-paced OpenGL game surface. Duplicate sequences reuse the current texture. */
public final class GlFrameView extends GLSurfaceView {
    private final FrameRenderer frameRenderer;

    public GlFrameView(Context context, FramePublisher publisher) {
        this(context, publisher, null);
    }

    public GlFrameView(Context context, FramePublisher publisher,
                       VideoStatusAccumulator statusAccumulator) {
        super(context);
        setEGLContextClientVersion(2);
        setPreserveEGLContextOnPause(true);
        frameRenderer = new FrameRenderer(publisher, statusAccumulator);
        setRenderer(frameRenderer);
        setRenderMode(RENDERMODE_WHEN_DIRTY);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    public void setFilterMode(FilterMode mode) {
        frameRenderer.setFilterMode(mode);
    }

    public void onFrameAvailable(long sequence) {
        if (sequence >= 0L) requestRender();
    }
}
