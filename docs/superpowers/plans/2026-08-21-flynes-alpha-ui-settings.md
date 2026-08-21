# FlyNES Alpha Product UI and Settings Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the Demo UI with a coherent, inset-safe product shell, correct display modes, discoverable pause/navigation, and persistent video/control/audio settings.

**Architecture:** `HomeActivity` is the launcher and `GameActivity` hosts the session, frame view, and virtual gamepad. XML/Material resources define product screens; `SettingsRepository` is the only settings persistence boundary and emits immutable `AppSettings` snapshots.

**Tech Stack:** Java 17, Android Views/XML, AppCompat 1.8.0, Activity 1.13.0, Material Components 1.14.0, RecyclerView, Preference 1.2.1, WindowInsets, vector/adaptive icons, Espresso.

---

## File map

- Create `res/values/{colors,dimens,styles,themes}.xml` and night equivalents.
- Create adaptive/round/monochrome launcher assets and internal vector icons.
- Create `HomeActivity.java`, `GameActivity.java`, `PauseSheet.java`, `SettingsActivity.java`, `SettingsFragment.java`.
- Create `settings/AppSettings.java`, `settings/SettingsRepository.java`, `settings/SettingsKeys.java`.
- Create XML layouts for Home, Game, Pause, Settings host, and Licenses.
- Replace the existing launcher `MainActivity`; retain a temporary compatibility redirect for one release.

### Task 1: Establish design tokens and the application theme

**Files:**
- Create: `app/src/main/res/values/colors.xml`
- Create: `app/src/main/res/values/dimens.xml`
- Create: `app/src/main/res/values/themes.xml`
- Create: `app/src/main/res/values/styles.xml`
- Create: `app/src/main/res/values-night/colors.xml`
- Modify: `app/src/main/AndroidManifest.xml`
- Test: `app/src/androidTest/java/com/flynes/emu/ui/ThemeContrastTest.java`

- [ ] **Step 1: Write a failing resource/contrast test**

```java
@Test public void coreThemeColorsMeetContrastTargets() {
    Context c = ApplicationProvider.getApplicationContext();
    int surface = ContextCompat.getColor(c, R.color.fly_surface);
    int onSurface = ContextCompat.getColor(c, R.color.fly_on_surface);
    int primary = ContextCompat.getColor(c, R.color.fly_primary);
    assertTrue(ColorUtils.calculateContrast(onSurface, surface) >= 4.5);
    assertTrue(ColorUtils.calculateContrast(primary, surface) >= 3.0);
}
```

- [ ] **Step 2: Run and observe missing resources**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.ThemeContrastTest --console=plain`

Expected: resource compilation FAIL.

- [ ] **Step 3: Add exact tokens and a Material 3 Views theme**

Use these base colors:

```xml
<color name="fly_background">#0B0D12</color>
<color name="fly_surface">#151A23</color>
<color name="fly_surface_variant">#212936</color>
<color name="fly_on_surface">#F3F6FA</color>
<color name="fly_on_surface_muted">#B7C0CE</color>
<color name="fly_primary">#63E6BE</color>
<color name="fly_on_primary">#06241B</color>
<color name="fly_secondary">#FFB86B</color>
<color name="fly_error">#FF6B6B</color>
<color name="fly_scrim">#B3000000</color>
```

Define 4/8/12/16/24/32 dp spacing, 12/16/24 dp radii, 48 dp minimum target, and a `Theme.FlyNES` parented to `Theme.Material3.Dark.NoActionBar`. Apply `@style/Theme.FlyNES` in the manifest and remove the platform fullscreen theme.

- [ ] **Step 4: Run contrast, resource, and Lint checks**

Run: `./gradlew.bat :app:connectedDebugAndroidTest :app:lintDebug --console=plain`

Expected: contrast test PASS; no hardcoded color warnings in new layouts.

- [ ] **Step 5: Commit the design system**

```powershell
git add app/src/main/res/values app/src/main/res/values-night app/src/main/AndroidManifest.xml app/src/androidTest/java/com/flynes/emu/ui/ThemeContrastTest.java
git commit -m "feat: add FlyNES product theme and design tokens"
```

### Task 2: Add versioned SettingsRepository

**Files:**
- Create: `app/src/main/java/com/flynes/emu/settings/AppSettings.java`
- Create: `app/src/main/java/com/flynes/emu/settings/SettingsRepository.java`
- Create: `app/src/main/java/com/flynes/emu/settings/SettingsStore.java`
- Test: `app/src/test/java/com/flynes/emu/settings/SettingsRepositoryTest.java`

- [ ] **Step 1: Write default, persistence, and corruption tests**

```java
@Test public void defaultsMatchApprovedDesign() {
    AppSettings s = new SettingsRepository(new MemoryStore()).load();
    assertEquals(AspectMode.FOUR_BY_THREE, s.aspectMode());
    assertEquals(RefreshMode.AUTO, s.refreshMode());
    assertEquals(HapticLevel.LIGHT, s.hapticLevel());
    assertTrue(s.distinctABHaptics());
    assertEquals(LayoutPreset.STANDARD_BA, s.layoutPreset());
}

@Test public void invalidOldValuesFallBackWithoutCrashing() {
    MemoryStore store = new MemoryStore().put("video.aspect", "BROKEN").put("schema", 0);
    assertEquals(AspectMode.FOUR_BY_THREE, new SettingsRepository(store).load().aspectMode());
}

private static final class MemoryStore implements SettingsStore {
    private final Map<String, Object> values = new HashMap<>();
    MemoryStore put(String key, Object value) { values.put(key, value); return this; }
    @Override public String getString(String key, String fallback) {
        Object value = values.get(key);
        return value instanceof String ? (String) value : fallback;
    }
    @Override public int getInt(String key, int fallback) {
        Object value = values.get(key);
        return value instanceof Integer ? (Integer) value : fallback;
    }
    @Override public boolean getBoolean(String key, boolean fallback) {
        Object value = values.get(key);
        return value instanceof Boolean ? (Boolean) value : fallback;
    }
    @Override public void putString(String key, String value) { values.put(key, value); }
    @Override public void putInt(String key, int value) { values.put(key, value); }
    @Override public void putBoolean(String key, boolean value) { values.put(key, value); }
}
```

- [ ] **Step 2: Run and observe missing settings types**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*SettingsRepositoryTest' --console=plain`

Expected: compile FAIL.

- [ ] **Step 3: Implement schema 1 and immutable snapshots**

Use this storage boundary so the repository stays JVM-testable:

```java
public interface SettingsStore {
    String getString(String key, String fallback);
    int getInt(String key, int fallback);
    boolean getBoolean(String key, boolean fallback);
    void putString(String key, String value);
    void putInt(String key, int value);
    void putBoolean(String key, boolean value);
}
```

`AppSettings` contains `AspectMode`, `FilterMode`, `RefreshMode`, `LayoutPreset`, button scale, vertical offset, opacity, joystick scale, dead zone, `HapticLevel`, distinct-A/B flag, audio enabled/focus policy, locale tag, autosave enabled, and last-played ROM ID. Clamp numeric values to approved ranges and write schema version last so partially written settings re-run migration.

- [ ] **Step 4: Run all settings tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*settings*' --console=plain`

Expected: default, round-trip, migration, and corruption tests PASS.

- [ ] **Step 5: Commit the settings model**

```powershell
git add app/src/main/java/com/flynes/emu/settings app/src/test/java/com/flynes/emu/settings
git commit -m "feat: add versioned emulator settings repository"
```

### Task 3: Add launcher assets and a Home/Game activity shell

**Files:**
- Create: `app/src/main/res/mipmap-anydpi-v26/ic_launcher.xml`
- Create: `app/src/main/res/mipmap-anydpi-v26/ic_launcher_round.xml`
- Create: `app/src/main/res/drawable/ic_launcher_foreground.xml`
- Create: `app/src/main/res/drawable/ic_pause.xml`
- Create: `app/src/main/res/layout/activity_home.xml`
- Create: `app/src/main/res/layout/activity_game.xml`
- Create: `app/src/main/java/com/flynes/emu/HomeActivity.java`
- Create: `app/src/main/java/com/flynes/emu/GameActivity.java`
- Modify: `app/src/main/AndroidManifest.xml`
- Test: `app/src/androidTest/java/com/flynes/emu/ui/FirstRunNavigationTest.java`

- [ ] **Step 1: Write a failing first-run navigation test**

```java
@Test public void firstRunShowsBuiltinAndAddSourceActions() {
    ActivityScenario.launch(HomeActivity.class);
    onView(withId(R.id.builtin_game)).check(matches(isDisplayed()));
    onView(withId(R.id.add_source)).check(matches(isDisplayed()));
    onView(withId(R.id.add_source)).perform(click());
    intended(hasAction(Intent.ACTION_OPEN_DOCUMENT_TREE));
}
```

- [ ] **Step 2: Run and observe missing shell**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.FirstRunNavigationTest --console=plain`

Expected: compile/resource FAIL.

- [ ] **Step 3: Build the shell and exact icon resources**

`HomeActivity` is the launcher and contains Recent, Favorites, Built-in, Local Games, and an always-visible add-source action. `GameActivity` contains `GlFrameView`, `GamepadView`, and a 48 dp `ImageButton` using `ic_pause` with `@string/open_pause`.

The adaptive foreground uses a simple controller silhouette: a rounded controller body, D-pad, and two action circles in `fly_primary`/`fly_secondary`; background is `fly_background`. Add monochrome foreground in a themed icon resource. `MainActivity` becomes a one-release redirect to `HomeActivity` and has no launcher filter.

- [ ] **Step 4: Run manifest, icon, and first-run tests**

Run: `./gradlew.bat :app:lintDebug :app:connectedDebugAndroidTest --console=plain`

Expected: placeholder-icon warning absent; first-run actions visible.

- [ ] **Step 5: Commit the product shell**

```powershell
git add app/src/main/res/mipmap-anydpi-v26 app/src/main/res/drawable app/src/main/res/layout/activity_home.xml app/src/main/res/layout/activity_game.xml app/src/main/java/com/flynes/emu/HomeActivity.java app/src/main/java/com/flynes/emu/GameActivity.java app/src/main/java/com/flynes/emu/MainActivity.java app/src/main/AndroidManifest.xml app/src/androidTest/java/com/flynes/emu/ui/FirstRunNavigationTest.java
git commit -m "feat: add branded Home and Game activity shell"
```

### Task 4: Implement a real Pause Sheet and predictable Back behavior

**Files:**
- Create: `app/src/main/java/com/flynes/emu/ui/PauseSheet.java`
- Create: `app/src/main/res/layout/sheet_pause.xml`
- Modify: `app/src/main/java/com/flynes/emu/GameActivity.java`
- Test: `app/src/androidTest/java/com/flynes/emu/ui/PauseBackBehaviorTest.java`

- [ ] **Step 1: Write pause and Back tests**

```java
@Test public void backPausesWithoutInjectingStartAndSecondBackResumes() {
    launchGame();
    pressBack();
    onView(withId(R.id.pause_sheet)).check(matches(isDisplayed()));
    assertEquals(SessionState.PAUSED, TestSessionRegistry.state());
    assertEquals(0, TestSessionRegistry.lastInput());
    pressBack();
    assertEquals(SessionState.RUNNING, TestSessionRegistry.state());
}
```

- [ ] **Step 2: Run and observe old/default Back behavior**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.PauseBackBehaviorTest --console=plain`

Expected: FAIL because Back finishes or old AlertDialog semantics remain.

- [ ] **Step 3: Implement the sheet**

Use `BottomSheetDialogFragment`. Opening awaits `session.pause()`, calls `inputRouter.cancelAll()`, then shows Continue, latest autosave status, Settings, Library, and Exit Game. Continue calls `session.resume()` only. Back while the sheet is visible invokes Continue; Back while playing opens the sheet through `OnBackPressedDispatcher`.

- [ ] **Step 4: Run pause and lifecycle tests**

Run: `./gradlew.bat :app:connectedDebugAndroidTest --console=plain`

Expected: frame sequence and audio stop while sheet is visible; no START is injected.

- [ ] **Step 5: Commit navigation semantics**

```powershell
git add app/src/main/java/com/flynes/emu/ui/PauseSheet.java app/src/main/java/com/flynes/emu/GameActivity.java app/src/main/res/layout/sheet_pause.xml app/src/androidTest/java/com/flynes/emu/ui/PauseBackBehaviorTest.java
git commit -m "feat: add real emulator pause sheet and Back handling"
```

### Task 5: Build Settings UI and live preview

**Files:**
- Create: `app/src/main/java/com/flynes/emu/SettingsActivity.java`
- Create: `app/src/main/java/com/flynes/emu/settings/SettingsFragment.java`
- Create: `app/src/main/res/xml/preferences.xml`
- Create: `app/src/main/res/layout/activity_settings.xml`
- Test: `app/src/androidTest/java/com/flynes/emu/ui/SettingsPersistenceTest.java`

- [ ] **Step 1: Write a failing persistence test**

```java
@Test public void hapticAndAspectPersistAcrossRecreation() {
    ActivityScenario<SettingsActivity> scenario = ActivityScenario.launch(SettingsActivity.class);
    onView(withText(R.string.aspect_mode)).perform(click());
    onView(withText(R.string.aspect_square_pixels)).perform(click());
    onView(withText(R.string.haptic_level)).perform(click());
    onView(withText(R.string.haptic_off)).perform(click());
    scenario.recreate();
    scenario.onActivity(activity -> {
        AppSettings saved = activity.settingsRepositoryForTest().load();
        assertEquals(AspectMode.SQUARE_PIXELS, saved.aspectMode());
        assertEquals(HapticLevel.OFF, saved.hapticLevel());
    });
}
```

- [ ] **Step 2: Run and observe missing Settings UI**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.SettingsPersistenceTest --console=plain`

Expected: compile/resource FAIL.

- [ ] **Step 3: Add exact setting groups**

Video: aspect, filter, integer scale, refresh. Controls: B/A preset, button size, vertical offset, opacity, joystick size/dead zone, haptic level, distinguish A/B, reset. Audio: enabled and focus behavior. General: App language, autosave, library management, licenses. Preview changes through `SettingsRepository`; cancellation restores the pre-preview snapshot. Expose package-private `settingsRepositoryForTest()` in `SettingsActivity`; production callers continue to receive settings through constructor/listener injection.

- [ ] **Step 4: Run settings tests and 200% font smoke**

Run: `./gradlew.bat :app:connectedDebugAndroidTest --console=plain`

Expected: persistence PASS; every setting remains reachable at font scale 2.0.

- [ ] **Step 5: Commit Settings UI**

```powershell
git add app/src/main/java/com/flynes/emu/SettingsActivity.java app/src/main/java/com/flynes/emu/settings/SettingsFragment.java app/src/main/res/xml/preferences.xml app/src/main/res/layout/activity_settings.xml app/src/androidTest/java/com/flynes/emu/ui/SettingsPersistenceTest.java
git commit -m "feat: add persistent video control and audio settings"
```

### Task 6: Apply safe insets, aspect mode, and control calibration

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GameActivity.java`
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Create: `app/src/main/java/com/flynes/emu/ui/SafeGameLayout.java`
- Create: `app/src/androidTest/java/com/flynes/emu/ui/SafeInsetsTest.java`

- [ ] **Step 1: Write a failing safe-rect test**

```java
@Test public void controlsRemainInsideNavigationAndCutoutInsets() {
    SafeRect safe = SafeGameLayout.compute(2340,1080, Insets.of(0,0,132,0));
    GamepadHitMap map = GamepadHitMap.fromSettings(safe, AppSettings.defaults(), 2.75f);
    assertTrue(map.validate().isEmpty());
    assertTrue(map.bounds(GamepadHitMap.Control.A).right() <= 2208);
}
```

- [ ] **Step 2: Run and observe current fixed margins**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.SafeInsetsTest --console=plain`

Expected: FAIL or an A/B target crossing the safe boundary.

- [ ] **Step 3: Drive layout from WindowInsets and AppSettings**

Apply system bars, mandatory gestures, and display cutout insets. Regenerate the hit map on inset, size, orientation-side, or setting changes. Clamp size/dead-zone/offset values; reject any saved layout with overlap or unreachable targets. Use immersive sticky only on GameActivity and keep the explicit pause button available.

- [ ] **Step 4: Run navigation-mode and aspect screenshot tests**

Run the matrix for gesture and three-button navigation at aspect modes 4:3, square pixels, and integer scale.

Expected: no target or pause control overlaps system UI; 4:3 content is 1440×1080 on the audit device.

- [ ] **Step 5: Commit ergonomic layout integration**

```powershell
git add app/src/main/java/com/flynes/emu/GameActivity.java app/src/main/java/com/flynes/emu/GamepadView.java app/src/main/java/com/flynes/emu/ui/SafeGameLayout.java app/src/androidTest/java/com/flynes/emu/ui/SafeInsetsTest.java
git commit -m "fix: adapt game controls and viewport to safe insets"
```

### Task 7: Replace the license text wall and capture UI acceptance

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/LicensesActivity.java`
- Create: `app/src/main/res/layout/activity_licenses.xml`
- Create: `app/src/main/res/layout/item_license.xml`
- Create: `docs/acceptance/alpha/product-ui.md`

- [ ] **Step 1: Convert license assets into titled expandable rows**

Use a toolbar with Back, one RecyclerView row per license, expandable full text, and a clickable/copyable source URL. Persist scroll/expanded state on recreation.

- [ ] **Step 2: Verify source navigation and large text**

Run an instrumentation test that clicks the source row and asserts an `ACTION_VIEW` intent for the configured immutable repository URL. Repeat at font scale 2.0.

- [ ] **Step 3: Capture the product screen matrix**

Capture Home, Game, Pause, Settings, Library, empty/no-result/error states, and Licenses in en/zh, lightest/darkest ROM frames, and 100%/200% font.

- [ ] **Step 4: Record contrast and interaction results**

`product-ui.md` records all screenshots, text contrast ≥4.5:1, control contrast ≥3:1, target size ≥48 dp, and pressed/selected/disabled state checks.

- [ ] **Step 5: Commit UI acceptance evidence**

```powershell
git add app/src/main/java/com/flynes/emu/LicensesActivity.java app/src/main/res/layout/activity_licenses.xml app/src/main/res/layout/item_license.xml docs/acceptance/alpha/product-ui.md
git commit -m "feat: finish navigable licenses and product UI acceptance"
```
