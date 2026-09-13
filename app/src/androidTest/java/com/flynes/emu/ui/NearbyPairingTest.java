package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.Visibility;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.isEnabled;
import static androidx.test.espresso.matcher.ViewMatchers.withContentDescription;
import static androidx.test.espresso.matcher.ViewMatchers.withEffectiveVisibility;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;
import static org.hamcrest.Matchers.not;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * Real emulator evidence for slice A1a-2: the 配对 page shows both approved entry paths with the
 * exact stage that blocks them, and it does not ship the anonymous-join control set that the
 * approved design never asked for.
 *
 * <p>The page is reached by discovery in the finished product; with discovery blocked at 权限 there
 * is no path to it yet, so this test drives the activity directly. That is why the assertions are
 * about what the page renders, not about how a user arrives.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyPairingTest {

    @Test
    public void sixDigitCodePathIsPresentAndCannotConfirmWithoutASession() {
        try (ActivityScenario<NearbyPairingActivity> ignored =
                     ActivityScenario.launch(NearbyPairingActivity.class)) {
            onView(withId(R.id.nearby_code_label)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_code_confirm)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_code_confirm)).check(matches(not(isEnabled())));
            onView(withId(R.id.nearby_code_confirm_reason)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_code_confirm_reason))
                    .check(matches(withText(R.string.nearby_blocked_auth)));
            // A disabled control must still say why it cannot act, including to a screen reader.
            onView(withId(R.id.nearby_code_confirm)).check(matches(withContentDescription(
                    string(R.string.nearby_code_confirm) + ", "
                            + string(R.string.nearby_blocked_auth))));
        }
    }

    @Test
    public void wifiPathIsExplainedAndStatesTheSingleSystemPrompt() {
        try (ActivityScenario<NearbyPairingActivity> ignored =
                     ActivityScenario.launch(NearbyPairingActivity.class)) {
            onView(withId(R.id.nearby_wifi_path_building)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_wifi_system_confirm_once)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_wifi_blocked))
                    .check(matches(withText(R.string.nearby_blocked_wifi)));
        }
    }

    @Test
    public void pipelineMarksOnlyTheFirstFailingStage() {
        try (ActivityScenario<NearbyPairingActivity> ignored =
                     ActivityScenario.launch(NearbyPairingActivity.class)) {
            // Same element, same rule as 好友/附近设备 (D5): 权限 is current and explains itself.
            onView(withId(R.id.nearby_stage_permission_row)).check(matches(withContentDescription(
                    string(R.string.nearby_stage_permission) + ", "
                            + string(R.string.nearby_stage_status_current))));
            onView(withId(R.id.nearby_stage_permission_reason))
                    .check(matches(withEffectiveVisibility(Visibility.VISIBLE)));
            onView(withId(R.id.nearby_stage_auth_reason))
                    .check(matches(withEffectiveVisibility(Visibility.GONE)));
            onView(withId(R.id.nearby_stage_codec_reason))
                    .check(matches(withEffectiveVisibility(Visibility.GONE)));
        }
    }

    @Test
    public void anonymousJoinControlSetIsNotBuilt() {
        try (ActivityScenario<NearbyPairingActivity> ignored =
                     ActivityScenario.launch(NearbyPairingActivity.class)) {
            // Design §22.2 names an anonymous join request only for the QR path; a generic
            // request/accept/reject set is over-delivery pending §30 review (spec §4). The i18n
            // keys exist, the controls must not.
            onView(withText(R.string.nearby_join_request_anonymous)).check(doesNotExist());
            onView(withText(R.string.nearby_join_accept)).check(doesNotExist());
            onView(withText(R.string.nearby_join_reject)).check(doesNotExist());
        }
    }

    private static String string(int id) {
        return ApplicationProvider.getApplicationContext().getString(id);
    }
}
