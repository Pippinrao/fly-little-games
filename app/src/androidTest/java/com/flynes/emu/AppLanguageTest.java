package com.flynes.emu;

import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.isDisplayed;
import static androidx.test.espresso.matcher.ViewMatchers.withText;

import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.os.LocaleListCompat;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;

import android.os.SystemClock;

import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class AppLanguageTest {
    @Test public void chineseAppLocaleAppliesToTheLauncher() {
        try (ActivityScenario<HomeActivity> ignored =
                     ActivityScenario.launch(HomeActivity.class)) {
            try {
                InstrumentationRegistry.getInstrumentation().runOnMainSync(() ->
                        AppCompatDelegate.setApplicationLocales(
                                LocaleListCompat.forLanguageTags("zh-CN")));
                InstrumentationRegistry.getInstrumentation().waitForIdleSync();
                SystemClock.sleep(300L);
                onView(withText("游戏中心")).check(matches(isDisplayed()));
                onView(withText("内置")).check(matches(isDisplayed()));
            } finally {
                InstrumentationRegistry.getInstrumentation().runOnMainSync(() ->
                        AppCompatDelegate.setApplicationLocales(
                                LocaleListCompat.getEmptyLocaleList()));
            }
        }
    }
}
