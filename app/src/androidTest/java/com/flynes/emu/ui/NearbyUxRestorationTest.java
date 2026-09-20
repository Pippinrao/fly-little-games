package com.flynes.emu.ui;

import android.content.Intent;
import android.content.pm.ActivityInfo;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.widget.HorizontalScrollView;
import android.widget.LinearLayout;
import android.widget.TextView;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.filters.LargeTest;
import androidx.test.platform.app.InstrumentationRegistry;

import com.flynes.emu.NearbyFriendsActivity;
import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;
import com.google.android.material.button.MaterialButton;

import org.junit.Test;
import org.junit.runner.RunWith;

import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;
import static org.junit.Assert.fail;

/**
 * TYPO-02.A: real TextView reads for Nearby roles, plus PAIR content-width
 * stacking at 320 / 580 / 640 after insets. HeadlineSmall 24sp must fail until
 * local paneTitle 21 is wired.
 */
@RunWith(AndroidJUnit4.class)
@LargeTest
public final class NearbyUxRestorationTest {

    private static final float SIZE_EPS = 0.6f;
    private static final float LINE_EPS = 2.0f;

    @Test
    public void TYPO_A_headline_resolves21() {
        try (ActivityScenario<NearbyFriendsActivity> n00 =
                     ActivityScenario.launch(NearbyFriendsActivity.class)) {
            n00.onActivity(activity -> {
                assertEquals(1f, activity.getResources().getConfiguration().fontScale, 0.01f);
                TextView headline = activity.findViewById(R.id.nearby_entry_headline);
                TextView subtitle = activity.findViewById(R.id.nearby_entry_subtitle);
                MaterialButton create = activity.findViewById(R.id.nearby_action_create);
                MaterialButton enterCode = activity.findViewById(R.id.nearby_action_enter_code);
                assertEquals("paneTitle must be 21sp, not HeadlineSmall 24sp",
                        21f, sp(headline), SIZE_EPS);
                assertEquals("paneTitle line box ~27.3 after fontPadding",
                        27.3f, lineBox(headline), LINE_EPS);
                assertEquals("muted must be 12sp", 12f, sp(subtitle), SIZE_EPS);
                assertEquals("primaryAction must be 14sp", 14f, sp(create), SIZE_EPS);
                assertEquals("action must be 14sp", 14f, sp(enterCode), SIZE_EPS);
                assertTrue("create tap target >= 48dp", minHeightDp(create) >= 47.5f);
                assertTrue("enter-code tap target >= 48dp", minHeightDp(enterCode) >= 47.5f);
            });
        }

        Intent invite = new Intent(ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        invite.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> n01 = ActivityScenario.launch(invite)) {
            try {
                n01.onActivity(activity -> {
                    TextView code = activity.findViewById(R.id.nearby_invite_code_value);
                    assertEquals("inviteCode must be 29sp, not HeadlineMedium 28sp",
                            29f, sp(code), SIZE_EPS);
                    assertEquals("inviteCode tracking .17em, not 0.12",
                            0.17f, code.getLetterSpacing(), 0.02f);
                });
            } finally {
                // Closing a display does not cancel its process-scoped invitation.
                n01.onActivity(activity -> activity.findViewById(R.id.nearby_invite_cancel)
                        .performClick());
            }
        }
    }

    @Test
    public void TYPO_A_pair_width320() {
        assertPairAtContentWidth(320f, true);
        assertJoinInputAtContentWidth(320f);
        assertPairAtContentWidth(580f, true);
        assertPairAtContentWidth(640f, false);
    }

    private static void assertPairAtContentWidth(float widthDp, boolean expectStack) {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            try {
                if (widthDp > 400f) {
                    scenario.onActivity(activity -> activity.setRequestedOrientation(
                            ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE));
                    waitForContentWidthAtLeast(scenario, R.id.nearby_pairing_root, 580f);
                }
                forceContentWidthAfterInsets(scenario, R.id.nearby_pairing_root, widthDp);
                waitForOrientation(scenario, expectStack ? LinearLayout.VERTICAL : LinearLayout.HORIZONTAL);
                scenario.onActivity(activity -> {
                    View root = activity.findViewById(R.id.nearby_pairing_root);
                    View left = activity.findViewById(R.id.nearby_create_block);
                    ViewGroup columns = (ViewGroup) left.getParent();
                    View qrWrap = activity.findViewById(R.id.nearby_invite_qr_wrap);
                    View qr = activity.findViewById(R.id.nearby_invite_qr);
                    if (qr == null) {
                        qr = ((ViewGroup) qrWrap).getChildAt(0);
                    }
                    View footerCopy = activity.findViewById(R.id.nearby_pairing_footer_copy);
                    View cancel = activity.findViewById(R.id.nearby_invite_cancel);
                    float density = activity.getResources().getDisplayMetrics().density;
                    float inner = (columns.getWidth() - columns.getPaddingLeft()
                            - columns.getPaddingRight()) / density;
                    float leftW = left.getWidth() / density;
                    float remaining = inner - leftW - 18f;
                    String why = "content " + widthDp + "dp after insets; inner=" + inner
                            + " left=" + leftW + " remaining=" + remaining
                            + " (224+18+16*2+170=444 needed for side-by-side QR)";
                    assertFalse("PAIR must not use a HorizontalScrollView workaround " + why,
                            hasHorizontalScrollView(root));
                    assertTrue("QR 170 must be fully on-screen at " + why,
                            fullyInside(qr, root));
                    assertTrue("footer copy must not overflow horizontally at " + why,
                            fullyInside(footerCopy, root));
                    assertTrue("footer action must not overflow horizontally at " + why,
                            fullyInside(cancel, root));
                    assertTrue("QR side must stay 170dp", Math.abs(qr.getWidth() / density - 170f) < 2f);
                    if (expectStack) {
                        assertEquals("<=580 after insets must vertical-stack, " + why,
                                LinearLayout.VERTICAL, ((LinearLayout) columns).getOrientation());
                    } else {
                        assertEquals(">580 after insets must keep left/right, " + why,
                                LinearLayout.HORIZONTAL, ((LinearLayout) columns).getOrientation());
                        assertEquals(224f, leftW, 2f);
                        View right = activity.findViewById(R.id.nearby_pairing_right);
                        assertEquals(18f, (right.getLeft() - left.getRight()) / density, 2f);
                    }
                    assertTrue("cancel tap target >= 48dp", minHeightDp(cancel) >= 47.5f);
                });
            } finally {
                scenario.onActivity(activity -> activity.findViewById(R.id.nearby_invite_cancel)
                        .performClick());
            }
        }
    }

    private static void assertJoinInputAtContentWidth(float widthDp) {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(),
                NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_JOIN_CODE);
        try (ActivityScenario<NearbyPairingActivity> scenario = ActivityScenario.launch(intent)) {
            forceContentWidthAfterInsets(scenario, R.id.nearby_pairing_root, widthDp);
            waitForOrientation(scenario, LinearLayout.VERTICAL);
            scenario.onActivity(activity -> {
                View root = activity.findViewById(R.id.nearby_pairing_root);
                View input = activity.findViewById(R.id.nearby_join_code_input);
                View submit = activity.findViewById(R.id.nearby_join_submit);
                assertTrue("join input must not overflow at " + widthDp,
                        fullyInside(input, root));
                assertTrue("join submit must not overflow at " + widthDp,
                        fullyInside(submit, root));
                assertTrue("code input minHeight >= 60dp", minHeightDp(input) >= 59.5f);
                assertTrue("submit tap target >= 48dp", minHeightDp(submit) >= 47.5f);
                assertEquals("codeInput 28sp", 28f, sp((TextView) input), SIZE_EPS);
            });
        }
    }

    private static float sp(TextView view) {
        return view.getTextSize() / view.getResources().getDisplayMetrics().scaledDensity;
    }

    private static float lineBox(TextView view) {
        return view.getLineHeight() / view.getResources().getDisplayMetrics().scaledDensity;
    }

    private static float minHeightDp(View view) {
        return view.getHeight() / view.getResources().getDisplayMetrics().density;
    }

    private static boolean fullyInside(View child, View container) {
        int[] childLoc = new int[2];
        int[] parentLoc = new int[2];
        child.getLocationOnScreen(childLoc);
        container.getLocationOnScreen(parentLoc);
        int left = parentLoc[0] + container.getPaddingLeft();
        int right = parentLoc[0] + container.getWidth() - container.getPaddingRight();
        int childLeft = childLoc[0];
        int childRight = childLoc[0] + child.getWidth();
        return childLeft + 2 >= left && childRight <= right + 2;
    }

    private static boolean hasHorizontalScrollView(View view) {
        if (view instanceof HorizontalScrollView) {
            return true;
        }
        if (!(view instanceof ViewGroup)) {
            return false;
        }
        ViewGroup group = (ViewGroup) view;
        for (int i = 0; i < group.getChildCount(); i++) {
            if (hasHorizontalScrollView(group.getChildAt(i))) {
                return true;
            }
        }
        return false;
    }

    private static void forceContentWidthAfterInsets(ActivityScenario<?> scenario, int rootId,
                                                     float widthDp) {
        CountDownLatch laidOut = new CountDownLatch(1);
        AtomicReference<String> last = new AtomicReference<>("not laid out");
        scenario.onActivity(activity -> {
            View root = activity.findViewById(rootId);
            applyExactContentWidth(root, widthDp, laidOut, last);
        });
        awaitLatch(laidOut, "content width after insets never reached " + widthDp + "dp: "
                + last.get());
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        // Pairing column adaptation posts itself; wait one more frame.
        CountDownLatch idle = new CountDownLatch(1);
        scenario.onActivity(activity -> activity.findViewById(rootId).post(idle::countDown));
        awaitLatch(idle, "idle frame after resize to " + widthDp);
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
    }

    private static void waitForOrientation(ActivityScenario<?> scenario, int expected) {
        long deadline = SystemClock.elapsedRealtime() + 8000L;
        int last = -1;
        while (SystemClock.elapsedRealtime() < deadline) {
            int[] got = {-1};
            scenario.onActivity(activity -> {
                View left = activity.findViewById(R.id.nearby_create_block);
                if (left == null) {
                    left = activity.findViewById(R.id.nearby_join_block);
                }
                LinearLayout columns = (LinearLayout) left.getParent();
                got[0] = columns.getOrientation();
            });
            last = got[0];
            if (last == expected) {
                InstrumentationRegistry.getInstrumentation().waitForIdleSync();
                return;
            }
            SystemClock.sleep(50L);
        }
        fail("pairing columns orientation stayed " + last + "; expected " + expected);
    }

    private static void waitForContentWidthAtLeast(ActivityScenario<?> scenario, int rootId,
                                                   float minDp) {
        long deadline = SystemClock.elapsedRealtime() + 8000L;
        float last = 0f;
        while (SystemClock.elapsedRealtime() < deadline) {
            float[] got = {0f};
            scenario.onActivity(activity -> {
                View root = activity.findViewById(rootId);
                got[0] = (root.getWidth() - root.getPaddingLeft() - root.getPaddingRight())
                        / activity.getResources().getDisplayMetrics().density;
            });
            last = got[0];
            if (last >= minDp - 2f) {
                InstrumentationRegistry.getInstrumentation().waitForIdleSync();
                return;
            }
            SystemClock.sleep(50L);
        }
        fail("content width stayed at " + last + "dp; needed at least " + minDp);
    }

    private static void applyExactContentWidth(View root, float widthDp, CountDownLatch laidOut,
                                               AtomicReference<String> last) {
        float density = root.getResources().getDisplayMetrics().density;
        int targetPx = Math.round(widthDp * density) + root.getPaddingLeft() + root.getPaddingRight();
        ViewGroup.LayoutParams lp = root.getLayoutParams();
        lp.width = targetPx;
        root.setLayoutParams(lp);
        View.OnLayoutChangeListener listener = new View.OnLayoutChangeListener() {
            @Override
            public void onLayoutChange(View v, int l, int t, int r, int b, int ol, int ot, int or,
                                       int ob) {
                float got = (v.getWidth() - v.getPaddingLeft() - v.getPaddingRight()) / density;
                last.set("got " + got + "dp width=" + v.getWidth());
                if (Math.abs(got - widthDp) <= 2f) {
                    v.removeOnLayoutChangeListener(this);
                    laidOut.countDown();
                }
            }
        };
        root.addOnLayoutChangeListener(listener);
        root.requestLayout();
        float got = (root.getWidth() - root.getPaddingLeft() - root.getPaddingRight()) / density;
        last.set("got " + got + "dp width=" + root.getWidth());
        if (Math.abs(got - widthDp) <= 2f) {
            laidOut.countDown();
        }
    }

    private static void awaitLatch(CountDownLatch latch, String message) {
        try {
            if (!latch.await(8, TimeUnit.SECONDS)) {
                fail(message);
            }
        } catch (InterruptedException interrupted) {
            throw new AssertionError(interrupted);
        }
    }
}
