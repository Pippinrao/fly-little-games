package com.flynes.emu;

import static org.junit.Assert.*;
import static androidx.test.espresso.Espresso.onView;
import static androidx.test.espresso.action.ViewActions.click;
import static androidx.test.espresso.assertion.ViewAssertions.matches;
import static androidx.test.espresso.matcher.ViewMatchers.*;

import android.content.Context;
import androidx.appcompat.app.AppCompatDelegate;
import androidx.core.os.LocaleListCompat;
import androidx.test.core.app.ActivityScenario;
import androidx.test.core.app.ApplicationProvider;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import com.flynes.emu.app.FlyNesApp;
import com.flynes.emu.catalog.*;
import com.flynes.emu.launch.LaunchRequest;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Locale;
import org.json.JSONObject;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class GameTitleIndexTest {
    @Test public void newlyLoadedBytesCannotReuseAStaleLegacyTitle() {
        GameEntry game = new GameEntry();
        game.name = "Unindexed replacement 89315";
        game.source = "saf";
        game.titleMetadata = new com.flynes.emu.app.NativeGameTitle("old-index", "Old game",
                "旧游戏", List.of(), 1);
        assertEquals(0, LegacyGameTitles.forLoadedRom(game, new byte[]{1, 2, 3}).matchKind());
    }

    @Test public void everyBundledFingerprintIgnoresNamesAndSwitchesBothLanguages() throws Exception {
        String json;
        try (var in = InstrumentationRegistry.getInstrumentation().getContext()
                .getAssets().open("game_titles.json")) {
            json = new String(RomScanner.readFully(in, 4 * 1024 * 1024), StandardCharsets.UTF_8);
        }
        var entries = new JSONObject(json).getJSONArray("entries");
        assertEquals(2222, entries.length());
        for (int i = 0; i < entries.length(); ++i) {
            var row = entries.getJSONObject(i);
            var title = FlyNesApp.resolveGameTitle(LegacyGameTitles.parseHash(row.getString("sha256")),
                    "random/" + i + ".nes");
            assertEquals(1, title.matchKind());
            assertEquals(row.getString("en"), title.displayName("random", Locale.ENGLISH));
            assertEquals(row.getString("zh"), title.displayName("random", Locale.SIMPLIFIED_CHINESE));
            assertEquals(row.getString("en"), title.displayName("random", Locale.JAPANESE));
            GameEntry cached = new GameEntry();
            cached.source = "saf";
            cached.payloadSha256 = row.getString("sha256");
            cached.name = "random";
            LegacyGameTitles.hydrate(null, cached);
            assertEquals(title, cached.titleMetadata);
        }
        assertEquals(0, FlyNesApp.resolveGameTitle(null, "Definitely Unknown 7319.nes").matchKind());
        assertEquals(0, FlyNesApp.resolveGameTitle(null, "Super Mario Bros. 39999.nes").matchKind());
    }

    @Test public void changingLanguageKeepsTheRunningGameTitle() throws Exception {
        var instrumentation = InstrumentationRegistry.getInstrumentation();
        instrumentation.getUiAutomation();
        // Original synthetic NROM: a JMP-to-self program, no private/commercial ROM bytes.
        byte[] rom = new byte[16 + 16384 + 8192];
        rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1a; rom[4] = 1; rom[5] = 1;
        rom[16] = 0x4c; rom[18] = (byte) 0x80;
        rom[16 + 0x3ffb] = (byte) 0x80;
        rom[16 + 0x3ffd] = (byte) 0x80;
        rom[16 + 0x3fff] = (byte) 0x80;
        var hashes = new RomHashes("1".repeat(40), "2".repeat(64), "3".repeat(64), "12345678");
        var request = new LaunchRequest("title-test", "title-test-variant", "builtin",
                "file:///android_asset/roms/from_below.nes", null, PackageFormat.RAW,
                RomFormat.INES, CompatibilityState.PLAYABLE, hashes, null, null);
        PendingGameLaunch.stage(request, rom, new CanonicalGame("title-test",
                "Title switching test", "名称切换测试", List.of()));
        var scenario = ActivityScenario.launch(MainActivity.class);
        var previous = AppCompatDelegate.getApplicationLocales();
        try {
            instrumentation.runOnMainSync(() -> AppCompatDelegate.setApplicationLocales(
                    LocaleListCompat.forLanguageTags("en")));
            instrumentation.waitForIdleSync();
            assertRetainedRom(scenario, rom);
            onView(withId(R.id.pause_button)).perform(click());
            onView(withId(R.id.pause_game_title)).check(matches(withText("Title switching test")));
            instrumentation.runOnMainSync(() -> AppCompatDelegate.setApplicationLocales(
                    LocaleListCompat.forLanguageTags("zh-CN")));
            instrumentation.waitForIdleSync();
            onView(withId(R.id.pause_button)).perform(click());
            onView(withId(R.id.pause_game_title)).check(matches(withText("名称切换测试")));
            assertRetainedRom(scenario, rom);
            instrumentation.runOnMainSync(() -> AppCompatDelegate.setApplicationLocales(
                    LocaleListCompat.forLanguageTags("en")));
            instrumentation.waitForIdleSync();
            assertRetainedRom(scenario, rom);
            onView(withId(R.id.pause_button)).perform(click());
            onView(withId(R.id.pause_game_title)).check(matches(withText("Title switching test")));
            scenario.onActivity(activity -> {
                android.widget.TextView view = activity.findViewById(R.id.pause_game_title);
                view.setText("Kunio-kun no Nekketsu Soccer League - World Championship Special Edition (Translation)");
                view.setTextSize(42);
            });
            onView(withId(R.id.pause_settings)).check(matches(isCompletelyDisplayed()));
        } finally {
            try {
                instrumentation.runOnMainSync(() -> AppCompatDelegate.setApplicationLocales(previous));
                instrumentation.waitForIdleSync();
            } finally {
                scenario.close();
            }
        }
    }

    private static void assertRetainedRom(ActivityScenario<MainActivity> scenario, byte[] expected) {
        scenario.onActivity(activity -> {
            try {
                var field = MainActivity.class.getDeclaredField("currentRom");
                field.setAccessible(true);
                // Same bytes object proves language changes reuse the staged ROM without reopening it.
                assertSame(expected, field.get(activity));
            } catch (ReflectiveOperationException error) {
                throw new AssertionError(error);
            }
        });
    }
}
