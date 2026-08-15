package com.flynes.emu;

import android.content.Context;
import android.view.SurfaceHolder;
import android.view.SurfaceView;

/** The game surface. The render loop (Choreographer) blits into this holder. */
public class EmuView extends SurfaceView implements SurfaceHolder.Callback {

    public EmuView(Context context) {
        super(context);
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    @Override public void surfaceCreated(SurfaceHolder holder) { /* blit guards surface validity */ }
    @Override public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {}
    @Override public void surfaceDestroyed(SurfaceHolder holder) {}
}
