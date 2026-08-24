package com.flynes.emu.ui;

import android.content.res.Resources;

import androidx.recyclerview.widget.GridLayoutManager;
import androidx.recyclerview.widget.RecyclerView;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.HomeActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

@RunWith(AndroidJUnit4.class)
public final class HomeContinuousLibraryTest {
    @Test public void libraryIsTwoAdaptiveRowsWithHorizontalScrollingOnly() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> {
                RecyclerView library = activity.findViewById(R.id.game_grid);
                GridLayoutManager layout = (GridLayoutManager) library.getLayoutManager();

                assertEquals(RecyclerView.HORIZONTAL, layout.getOrientation());
                assertEquals(2, layout.getSpanCount());
                assertTrue(layout.canScrollHorizontally());
                assertFalse(layout.canScrollVertically());
                assertEquals(activity.getString(R.string.game_list),
                        library.getContentDescription().toString());
                assertFalse(library.getContentDescription().toString()
                        .toLowerCase(java.util.Locale.ROOT).contains("page"));
            });
        }
    }

    @Test public void paginationControlsNoLongerExist() {
        try (ActivityScenario<HomeActivity> scenario = ActivityScenario.launch(HomeActivity.class)) {
            scenario.onActivity(activity -> {
                Resources resources = activity.getResources();
                String packageName = activity.getPackageName();
                assertEquals(0, resources.getIdentifier("page_previous", "id", packageName));
                assertEquals(0, resources.getIdentifier("page_next", "id", packageName));
                assertEquals(0, resources.getIdentifier("page_indicator", "id", packageName));
            });
        }
    }
}
