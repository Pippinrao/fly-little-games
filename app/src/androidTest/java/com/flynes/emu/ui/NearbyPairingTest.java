package com.flynes.emu.ui;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.doesNotExist;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withId;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import android.content.Intent;

import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;

import com.flynes.emu.NearbyPairingActivity;
import com.flynes.emu.R;

import org.junit.Test;
import org.junit.runner.RunWith;

/**
 * N01/N02/N03 chrome from the approved HTML mockup. Pairing stages are N07/N10,
 * not these entry pages.
 */
@RunWith(AndroidJUnit4.class)
public final class NearbyPairingTest {

    @Test
    public void createPageMatchesApprovedInviteMockup() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withId(R.id.nearby_create_block)).check(matches(isDisplayed()));
            onView(withText(R.string.nearby_invite_kicker)).check(matches(isDisplayed()));
            onView(withText(R.string.nearby_invite_headline)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_code_value)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_qr_wrap)).check(matches(isDisplayed()));
            onView(withText(R.string.nearby_invite_footer)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_invite_cancel)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_stage_permission_row)).check(doesNotExist());
        }
    }

    @Test
    public void joinPageMatchesApprovedCodeMockup() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_JOIN_CODE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withId(R.id.nearby_join_block)).check(matches(isDisplayed()));
            onView(withText(R.string.nearby_join_kicker)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_code_input)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_join_submit)).check(matches(isDisplayed()));
            onView(withId(R.id.nearby_stage_permission_row)).check(doesNotExist());
        }
    }

    @Test
    public void anonymousJoinControlSetIsNotBuilt() {
        Intent intent = new Intent(ApplicationProvider.getApplicationContext(), NearbyPairingActivity.class);
        intent.putExtra("nearby_mode", NearbyPairingActivity.MODE_CREATE);
        try (ActivityScenario<NearbyPairingActivity> ignored = ActivityScenario.launch(intent)) {
            onView(withText(R.string.nearby_join_request_anonymous)).check(doesNotExist());
            onView(withText(R.string.nearby_join_accept)).check(doesNotExist());
            onView(withText(R.string.nearby_join_reject)).check(doesNotExist());
        }
    }
}
