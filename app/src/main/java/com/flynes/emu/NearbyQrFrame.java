package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Paint;
import android.text.Layout;
import android.text.StaticLayout;
import android.text.TextPaint;
import android.util.AttributeSet;
import android.view.View;
import android.widget.FrameLayout;
import android.widget.ImageView;

/** Presentation only: a square invitation surface, including its empty state. */
public final class NearbyQrFrame extends FrameLayout {
    private final TextPaint label = new TextPaint(Paint.ANTI_ALIAS_FLAG);
    public NearbyQrFrame(Context context, AttributeSet attrs) { super(context, attrs); }
    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        int side = Math.min(MeasureSpec.getSize(widthSpec), MeasureSpec.getSize(heightSpec));
        side = Math.min(side, Math.round(170 * getResources().getDisplayMetrics().density));
        super.onMeasure(MeasureSpec.makeMeasureSpec(side, MeasureSpec.EXACTLY),
                MeasureSpec.makeMeasureSpec(side, MeasureSpec.EXACTLY));
    }
    @Override protected void dispatchDraw(Canvas canvas) {
        boolean hasQr = false;
        for (int i = 0; i < getChildCount(); i++)
            hasQr |= getChildAt(i) instanceof ImageView && getChildAt(i).getVisibility() == View.VISIBLE;
        String description = getContext().getString(hasQr
                ? R.string.nearby_invite_qrLabel : R.string.nearby_qr_unavailable);
        if (!android.text.TextUtils.equals(getContentDescription(), description)) {
            setContentDescription(description);
        }
        canvas.drawColor(getContext().getColor(hasQr ? android.R.color.white : R.color.fly_surface_variant));
        if (hasQr) super.dispatchDraw(canvas);
        else {
            label.setColor(getContext().getColor(R.color.fly_on_surface_muted));
            label.setTextSize(12 * getResources().getDisplayMetrics().scaledDensity);
            String message = getContext().getString(R.string.nearby_qr_unavailable);
            int inset = Math.round(8 * getResources().getDisplayMetrics().density);
            StaticLayout lines = StaticLayout.Builder.obtain(message, 0, message.length(),
                    label, Math.max(1, getWidth() - 2 * inset))
                    .setAlignment(Layout.Alignment.ALIGN_CENTER).setIncludePad(false).build();
            canvas.save();
            canvas.translate(inset, (getHeight() - lines.getHeight()) / 2f);
            lines.draw(canvas);
            canvas.restore();
        }
    }
}
