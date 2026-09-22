package com.flynes.emu;

import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.media.AudioAttributes;
import android.media.AudioFormat;
import android.media.AudioTrack;
import android.view.View;
import java.nio.ByteBuffer;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

/** Frame/input adapter used by the existing MainActivity game page. */
final class NearbyMvpPlayController implements AutoCloseable {
    private final MainActivity activity;
    private final NearbyMvpSession session;
    private final Runnable returnToLobby;
    private final NearbyFrameView frameView;
    private ScheduledExecutorService loop;
    private AudioTrack audio;
    private volatile int buttons;
    private boolean navigationQueued;
    private final byte[] frameBuffer = new byte[256 * 240 * 2];
    private final short[] pcmBuffer = new short[2048];

    NearbyMvpPlayController(MainActivity activity, NearbyMvpSession session, Runnable returnToLobby) {
        this.activity = activity;
        this.session = session;
        this.returnToLobby = returnToLobby;
        frameView = new NearbyFrameView(activity);
        frameView.setId(R.id.game_surface);
    }
    View surface() { return frameView; }
    void setButtons(int value) {
        android.util.Log.i("FlyNesNearby", "event=touch buttons=" + value);
        buttons = value;
    }
    void pause(boolean value) { session.setPaused(value); }
    void returnLobby() { buttons = 0; session.returnLobby(); }
    void start(boolean audioEnabled) {
        if (loop != null) return;
        if (audio == null && audioEnabled) {
            int minimum = AudioTrack.getMinBufferSize(48000, AudioFormat.CHANNEL_OUT_MONO,
                    AudioFormat.ENCODING_PCM_16BIT);
            audio = new AudioTrack.Builder()
                    .setAudioAttributes(new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_GAME).build())
                    .setAudioFormat(new AudioFormat.Builder().setSampleRate(48000)
                            .setEncoding(AudioFormat.ENCODING_PCM_16BIT)
                            .setChannelMask(AudioFormat.CHANNEL_OUT_MONO).build())
                    .setBufferSizeInBytes(Math.max(minimum, 4096)).build();
            if (audio.getState() == AudioTrack.STATE_INITIALIZED) audio.play();
        }
        loop = Executors.newSingleThreadScheduledExecutor();
        loop.scheduleAtFixedRate(this::tick, 0, 16639, TimeUnit.MICROSECONDS);
    }
    private void tick() {
        int[] state = session.snapshot();
        if (state[0] != NearbyMvpSession.RUNNING) {
            if (!navigationQueued) {
                navigationQueued = true;
                activity.runOnUiThread(returnToLobby);
            }
            return;
        }
        session.submitInput(buttons);
        long frame = session.copyLatestFrame(frameBuffer);
        if (frame >= 0) frameView.publish(frameBuffer, frame);
        int count = session.pullPcm(pcmBuffer);
        if (audio != null && count > 0 && audio.getPlayState() == AudioTrack.PLAYSTATE_PLAYING)
            audio.write(pcmBuffer, 0, count, AudioTrack.WRITE_NON_BLOCKING);
    }
    void stop() {
        buttons = 0;
        if (loop != null) {
            loop.shutdownNow();
            try { loop.awaitTermination(2, TimeUnit.SECONDS); }
            catch (InterruptedException e) { Thread.currentThread().interrupt(); }
            loop = null;
        }
    }
    @Override public void close() {
        stop();
        if (audio != null) { audio.pause(); audio.flush(); audio.release(); audio = null; }
    }
    static final class NearbyFrameView extends View {
        private final Bitmap bitmap = Bitmap.createBitmap(256, 240, Bitmap.Config.RGB_565);
        private final Paint paint = new Paint(Paint.FILTER_BITMAP_FLAG);
        private long published = -1;

        NearbyFrameView(android.content.Context context) { super(context); }

        void publish(byte[] bytes, long frame) {
            if (frame == published) return;
            synchronized (bitmap) {
                bitmap.copyPixelsFromBuffer(ByteBuffer.wrap(bytes));
                published = frame;
            }
            postInvalidateOnAnimation();
        }

        long publishedFrameForTest() { return published; }

        @Override protected void onDraw(Canvas canvas) {
            super.onDraw(canvas);
            synchronized (bitmap) {
                float scale = Math.min(getWidth() / 256f, getHeight() / 240f);
                float width = 256 * scale;
                float height = 240 * scale;
                canvas.drawBitmap(bitmap, null,
                        new android.graphics.RectF((getWidth() - width) / 2f,
                                (getHeight() - height) / 2f,
                                (getWidth() + width) / 2f,
                                (getHeight() + height) / 2f), paint);
            }
        }
    }
}
