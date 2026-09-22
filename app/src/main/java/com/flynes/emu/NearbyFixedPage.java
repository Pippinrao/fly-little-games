package com.flynes.emu;

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.LinearLayout;

/** Removes only decorative entry copy when the usable landscape height is tight. */
public final class NearbyFixedPage extends LinearLayout {
    public NearbyFixedPage(Context context, AttributeSet attrs) { super(context, attrs); }
    @Override protected void onMeasure(int widthSpec, int heightSpec) {
        View headline = findViewById(R.id.nearby_entry_headline);
        if (headline != null) {
            float height = (MeasureSpec.getSize(heightSpec) - getPaddingTop() - getPaddingBottom())
                    / getResources().getDisplayMetrics().density;
            boolean compact = height < 340 || getResources().getConfiguration().fontScale > 1.3f;
            headline.setVisibility(compact ? View.GONE : View.VISIBLE);
        }
        boolean largeText = getResources().getConfiguration().fontScale > 1.3f;
        int[] decorative = {R.id.nearby_invite_headline, R.id.nearby_invite_subtitle,
                R.id.nearby_entry_subtitle};
        for (int id : decorative) {
            View copy = findViewById(id);
            if (copy != null) copy.setVisibility(largeText ? View.GONE : View.VISIBLE);
        }
        super.onMeasure(widthSpec, heightSpec);
    }
}
