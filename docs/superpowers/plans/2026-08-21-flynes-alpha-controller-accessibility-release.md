# FlyNES Alpha Controller, Accessibility, and Release Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Complete physical-controller support, make the game shell operable with Android accessibility services, and produce a reproducible Alpha release candidate backed by automated and device evidence.

**Architecture:** `GameControllerInput` converts Android key and axis events into source-scoped `InputRouter` masks through an Android-free `ControllerMapper`. `GamepadAccessibilityHelper` exposes every virtual control as one semantic node while reusing the final `GamepadHitMap` bounds. Release safety is enforced by Android, host-core, ABI, lint, instrumentation, and scripted acceptance gates.

**Tech Stack:** Java 17, Android Views, AndroidX CustomView/Test, Material Components 1.14.0, C++17, CMake/CTest, GitHub Actions, Android Emulator API 36, PowerShell.

---

## File map

- Create `app/src/main/java/com/flynes/emu/input/ControllerMapper.java`: Android-free key/axis mapping and dead-zone logic.
- Create `app/src/main/java/com/flynes/emu/input/GameControllerInput.java`: Android event adapter and device lifecycle.
- Create `app/src/main/java/com/flynes/emu/input/GamepadAccessibilityHelper.java`: virtual accessibility nodes backed by `GamepadHitMap`.
- Modify `app/src/main/java/com/flynes/emu/GamepadView.java`: delegate controller events and accessibility actions.
- Modify `app/src/main/java/com/flynes/emu/MainActivity.java`: register/unregister input-device listeners.
- Create `app/src/main/res/xml/backup_rules.xml` and `app/src/main/res/xml/data_extraction_rules.xml`: explicit save/settings backup policy.
- Modify `app/src/main/AndroidManifest.xml`: bind backup policy and declare controller-related capabilities without requiring a controller.
- Create `app/src/test/java/com/flynes/emu/input/ControllerMapperTest.java`.
- Create `app/src/androidTest/java/com/flynes/emu/GameControllerInputTest.java`.
- Create `app/src/androidTest/java/com/flynes/emu/GamepadAccessibilityTest.java`.
- Create `app/src/androidTest/java/com/flynes/emu/AlphaLifecycleSmokeTest.java`.
- Create `scripts/abi_symbols.golden.txt` and `scripts/check-abi.ps1`.
- Create `scripts/alpha-acceptance.ps1`: reproducible local acceptance runner and evidence collector.
- Create `docs/acceptance/alpha/release-candidate.md`: release checklist and device matrix.
- Modify `.github/workflows/stage0.yml`: add Android, ABI, and emulator gates.

### Task 1: Map physical controllers without contaminating touch state

**Files:**
- Create: `app/src/main/java/com/flynes/emu/input/ControllerMapper.java`
- Create: `app/src/test/java/com/flynes/emu/input/ControllerMapperTest.java`

- [ ] **Step 1: Write failing key and axis mapping tests**

```java
package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import org.junit.Test;

public final class ControllerMapperTest {
    @Test public void mapsStandardButtonsAndKeepsPauseOutsideNesMask() {
        ControllerMapper mapper = new ControllerMapper(0.22f);
        assertEquals(InputBits.A, mapper.keyMask(ControllerMapper.KEY_BUTTON_A));
        assertEquals(InputBits.B, mapper.keyMask(ControllerMapper.KEY_BUTTON_B));
        assertEquals(InputBits.START, mapper.keyMask(ControllerMapper.KEY_BUTTON_START));
        assertEquals(InputBits.SELECT, mapper.keyMask(ControllerMapper.KEY_BUTTON_SELECT));
        assertEquals(0, mapper.keyMask(ControllerMapper.KEY_MENU));
        assertEquals(InputRouter.AppAction.OPEN_PAUSE,
                mapper.appAction(ControllerMapper.KEY_MENU).orElseThrow());
        assertTrue(mapper.appAction(9999).isEmpty());
    }

    @Test public void appliesRadialDeadZoneAndMergesHatAxes() {
        ControllerMapper mapper = new ControllerMapper(0.22f);
        assertEquals(0, mapper.axisMask(0.10f, -0.08f, 0.0f, 0.0f));
        assertEquals(InputBits.RIGHT, mapper.axisMask(0.80f, 0.0f, 0.0f, 0.0f));
        assertEquals(InputBits.UP | InputBits.LEFT,
                mapper.axisMask(0.0f, 0.0f, -1.0f, -1.0f));
    }
}
```

- [ ] **Step 2: Run the test and verify it fails because `ControllerMapper` does not exist**

Run:

```powershell
./gradlew :app:testDebugUnitTest --tests "com.flynes.emu.input.ControllerMapperTest"
```

Expected: compilation fails on unresolved `ControllerMapper`.

- [ ] **Step 3: Implement the Android-free mapper**

```java
public final class ControllerMapper {
    public static final int KEY_BUTTON_A = 96;
    public static final int KEY_BUTTON_B = 97;
    public static final int KEY_BUTTON_SELECT = 109;
    public static final int KEY_BUTTON_START = 108;
    public static final int KEY_DPAD_UP = 19;
    public static final int KEY_DPAD_DOWN = 20;
    public static final int KEY_DPAD_LEFT = 21;
    public static final int KEY_DPAD_RIGHT = 22;
    public static final int KEY_MENU = 82;

    private final float deadZone;

    public ControllerMapper(float deadZone) {
        if (deadZone < 0f || deadZone >= 1f) throw new IllegalArgumentException("deadZone");
        this.deadZone = deadZone;
    }

    public int keyMask(int keyCode) {
        return switch (keyCode) {
            case KEY_BUTTON_A -> InputBits.A;
            case KEY_BUTTON_B -> InputBits.B;
            case KEY_BUTTON_SELECT -> InputBits.SELECT;
            case KEY_BUTTON_START -> InputBits.START;
            case KEY_DPAD_UP -> InputBits.UP;
            case KEY_DPAD_DOWN -> InputBits.DOWN;
            case KEY_DPAD_LEFT -> InputBits.LEFT;
            case KEY_DPAD_RIGHT -> InputBits.RIGHT;
            default -> 0;
        };
    }

    public java.util.Optional<InputRouter.AppAction> appAction(int keyCode) {
        return keyCode == KEY_MENU
                ? java.util.Optional.of(InputRouter.AppAction.OPEN_PAUSE)
                : java.util.Optional.empty();
    }

    public int axisMask(float x, float y, float hatX, float hatY) {
        float resolvedX = Math.abs(hatX) >= 0.5f ? hatX : x;
        float resolvedY = Math.abs(hatY) >= 0.5f ? hatY : y;
        if (Math.hypot(resolvedX, resolvedY) < deadZone) return 0;
        int mask = 0;
        if (resolvedX <= -deadZone) mask |= InputBits.LEFT;
        if (resolvedX >= deadZone) mask |= InputBits.RIGHT;
        if (resolvedY <= -deadZone) mask |= InputBits.UP;
        if (resolvedY >= deadZone) mask |= InputBits.DOWN;
        return mask;
    }
}
```

- [ ] **Step 4: Run the focused unit test**

Run the Step 2 command.

Expected: both tests pass.

- [ ] **Step 5: Commit the mapper**

```powershell
git add app/src/main/java/com/flynes/emu/input/ControllerMapper.java app/src/test/java/com/flynes/emu/input/ControllerMapperTest.java
git commit -m "feat: map standard game controllers"
```

### Task 2: Integrate Android controller events and device lifecycle

**Files:**
- Create: `app/src/main/java/com/flynes/emu/input/GameControllerInput.java`
- Create: `app/src/androidTest/java/com/flynes/emu/GameControllerInputTest.java`
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`

- [ ] **Step 1: Write instrumentation tests for source isolation and disconnect cleanup**

```java
@RunWith(AndroidJUnit4.class)
public final class GameControllerInputTest {
    @Test public void disconnectClearsOnlyControllerSource() {
        InputRouter router = new InputRouter(mask -> { }, action -> { });
        router.setMask(InputRouter.Source.TOUCH, InputBits.A);
        router.setMask(InputRouter.Source.GAMEPAD, InputBits.RIGHT);
        GameControllerInput input = new GameControllerInput(router, new ControllerMapper(0.22f));

        input.onDeviceRemoved(7);

        assertEquals(InputBits.A, router.currentMask());
    }

    @Test public void menuDispatchesPauseAndNeverStart() {
        AtomicReference<InputRouter.AppAction> action = new AtomicReference<>();
        InputRouter router = new InputRouter(mask -> { }, action::set);
        GameControllerInput input = new GameControllerInput(router, new ControllerMapper(0.22f));

        assertTrue(input.onKey(7, ControllerMapper.KEY_MENU, true));

        assertEquals(0, router.currentMask());
        assertEquals(InputRouter.AppAction.OPEN_PAUSE, action.get());
    }
}
```

- [ ] **Step 2: Run instrumentation and verify the missing adapter failure**

```powershell
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GameControllerInputTest
```

Expected: test compilation fails on unresolved `GameControllerInput`.

- [ ] **Step 3: Implement `GameControllerInput`**

The class must:

- accept only `SOURCE_GAMEPAD`, `SOURCE_JOYSTICK`, and `SOURCE_DPAD` events;
- translate `KeyEvent` codes through `ControllerMapper` and preserve press/release state per device;
- read `AXIS_X`, `AXIS_Y`, `AXIS_HAT_X`, and `AXIS_HAT_Y` from the event's historical and current samples;
- merge all connected device masks into the single `InputRouter.Source.GAMEPAD` mask;
- clear the removed device before publishing the merged mask;
- consume a key only when it maps to a NES button or an App action.

```java
private void publishMergedMask() {
    int merged = 0;
    for (int mask : deviceMasks.values()) merged |= mask;
    router.setMask(InputRouter.Source.GAMEPAD, merged);
}

public void onDeviceRemoved(int deviceId) {
    deviceMasks.remove(deviceId);
    publishMergedMask();
}
```

- [ ] **Step 4: Wire activity dispatch and listener registration**

Register `InputManager.InputDeviceListener` in `onStart`, unregister in `onStop`, forward `dispatchKeyEvent` and `dispatchGenericMotionEvent`, and call `onDeviceRemoved` before delegating the listener callback. `GamepadView` continues to publish only `Source.TOUCH`.

```java
@Override public boolean dispatchKeyEvent(KeyEvent event) {
    return controllerInput.onKeyEvent(event) || super.dispatchKeyEvent(event);
}

@Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
    return controllerInput.onMotionEvent(event) || super.dispatchGenericMotionEvent(event);
}
```

- [ ] **Step 5: Run tests and manually verify two-controller merge/disconnect**

Run:

```powershell
./gradlew :app:testDebugUnitTest :app:connectedDebugAndroidTest
```

Expected: all tests pass; unplugging either of two controllers releases only that controller's held inputs.

- [ ] **Step 6: Commit controller integration**

```powershell
git add app/src/main/java/com/flynes/emu/input/GameControllerInput.java app/src/main/java/com/flynes/emu/MainActivity.java app/src/main/java/com/flynes/emu/GamepadView.java app/src/androidTest/java/com/flynes/emu/GameControllerInputTest.java
git commit -m "feat: integrate physical game controllers"
```

### Task 3: Expose the virtual gamepad to accessibility services

**Files:**
- Modify: `app/build.gradle`
- Create: `app/src/main/java/com/flynes/emu/input/GamepadAccessibilityHelper.java`
- Create: `app/src/androidTest/java/com/flynes/emu/GamepadAccessibilityTest.java`
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Modify: `app/src/main/res/values/strings.xml`
- Modify: `app/src/main/res/values-zh-rCN/strings.xml`

- [ ] **Step 1: Add the CustomView accessibility dependency**

```groovy
implementation "androidx.customview:customview:1.2.0"
```

- [ ] **Step 2: Write failing semantic-node tests**

```java
@RunWith(AndroidJUnit4.class)
public final class GamepadAccessibilityTest {
    @Rule public ActivityScenarioRule<MainActivity> rule =
            new ActivityScenarioRule<>(MainActivity.class);

    @Test public void everyControlHasOneClickableNamedNode() {
        onView(withId(R.id.gamepad)).check((view, error) -> {
            if (error != null) throw error;
            GamepadView gamepad = (GamepadView) view;
            List<GamepadAccessibilityHelper.NodeSnapshot> nodes =
                    gamepad.accessibilityNodesForTest();
            assertEquals(8, nodes.size());
            for (GamepadAccessibilityHelper.NodeSnapshot node : nodes) {
                assertFalse(node.label().isBlank());
                assertTrue(node.clickable());
                assertFalse(node.bounds().isEmpty());
            }
        });
    }
}
```

The eight nodes are Up, Down, Left, Right, Select, Start, B, and A. Pause remains a normal Material button outside the gamepad node tree.

- [ ] **Step 3: Run the test and verify the helper is missing**

```powershell
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadAccessibilityTest
```

Expected: compilation fails on unresolved accessibility helper APIs.

- [ ] **Step 4: Implement `ExploreByTouchHelper` semantics from `GamepadHitMap`**

```java
@Override protected void onPopulateNodeForVirtualView(
        int virtualViewId, AccessibilityNodeInfoCompat node) {
    GamepadHitMap.Control control = controls.get(virtualViewId);
    node.setClassName(Button.class.getName());
    node.setContentDescription(labels.label(control));
    node.setBoundsInParent(hitMap.bounds(control));
    node.setClickable(true);
    node.addAction(AccessibilityNodeInfoCompat.ACTION_CLICK);
}

@Override protected boolean onPerformActionForVirtualView(
        int virtualViewId, int action, Bundle arguments) {
    if (action != AccessibilityNodeInfoCompat.ACTION_CLICK) return false;
    actionSink.pressAndRelease(controls.get(virtualViewId));
    sendEventForVirtualView(virtualViewId, AccessibilityEvent.TYPE_VIEW_CLICKED);
    return true;
}
```

Return only controls whose final bounds are non-empty, invalidate the virtual tree after calibration/layout changes, and keep labels in localized string resources.

- [ ] **Step 5: Attach the helper and provide keyboard focus behavior**

```java
accessibilityHelper = new GamepadAccessibilityHelper(this, hitMap, labels, this::pressAndRelease);
ViewCompat.setAccessibilityDelegate(this, accessibilityHelper);
setFocusable(true);
setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_YES);
```

Ensure Tab/D-pad focus order follows D-pad → Select → Start → B → A, minimum touch target remains 48dp, and color is never the only pressed/focused state cue.

Add package-private `GamepadView.accessibilityNodesForTest()` that returns immutable snapshots generated by the helper's production node list; it must not maintain a second set of bounds or labels.

- [ ] **Step 6: Verify with automated tests and TalkBack**

Run:

```powershell
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadAccessibilityTest
```

Then enable TalkBack on one API 36 device and record evidence that all eight controls are named once, focusable, activatable, and do not expose the whole canvas as a duplicate node.

Expected: automated test passes and the evidence row in the release checklist is green.

- [ ] **Step 7: Commit accessibility semantics**

```powershell
git add app/build.gradle app/src/main/java/com/flynes/emu/input/GamepadAccessibilityHelper.java app/src/main/java/com/flynes/emu/GamepadView.java app/src/main/res/values/strings.xml app/src/main/res/values-zh-rCN/strings.xml app/src/androidTest/java/com/flynes/emu/GamepadAccessibilityTest.java
git commit -m "feat: expose game controls to accessibility"
```

### Task 4: Lock down backup, storage, and lifecycle behavior

**Files:**
- Create: `app/src/main/res/xml/backup_rules.xml`
- Create: `app/src/main/res/xml/data_extraction_rules.xml`
- Modify: `app/src/main/AndroidManifest.xml`
- Create: `app/src/androidTest/java/com/flynes/emu/AlphaLifecycleSmokeTest.java`

- [ ] **Step 1: Write the lifecycle smoke test**

```java
@RunWith(AndroidJUnit4.class)
public final class AlphaLifecycleSmokeTest {
    @Rule public ActivityScenarioRule<MainActivity> rule =
            new ActivityScenarioRule<>(MainActivity.class);

    @Test public void recreateAndBackgroundKeepTheSameRomIdentity() {
        AtomicReference<String> before = new AtomicReference<>();
        AtomicReference<String> after = new AtomicReference<>();
        rule.getScenario().onActivity(a -> before.set(a.sessionForTest().romIdentity().sha1()));
        rule.getScenario().moveToState(Lifecycle.State.CREATED);
        rule.getScenario().moveToState(Lifecycle.State.RESUMED);
        rule.getScenario().recreate();
        rule.getScenario().onActivity(a -> after.set(a.sessionForTest().romIdentity().sha1()));
        assertEquals(before.get(), after.get());
    }
}
```

Use the bundled authorized ROM fixture configured by the test runner; the test build must never scan the user's document providers.

- [ ] **Step 2: Run the focused smoke test before the lifecycle contract exists**

```powershell
./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.AlphaLifecycleSmokeTest
```

Expected: test fails until session identity survives recreation and background/foreground transitions.

Implement package-private `MainActivity.sessionForTest()` as a read-only accessor to the production session. The accessor must not create, load, pause, or mutate a core.

- [ ] **Step 3: Define explicit backup and device-transfer rules**

`backup_rules.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<full-backup-content>
    <include domain="sharedpref" path="." />
    <include domain="database" path="." />
    <exclude domain="file" path="roms/" />
    <exclude domain="file" path="states/" />
    <exclude domain="file" path="battery/" />
    <exclude domain="cache" path="." />
</full-backup-content>
```

`data_extraction_rules.xml`:

```xml
<?xml version="1.0" encoding="utf-8"?>
<data-extraction-rules>
    <cloud-backup>
        <include domain="sharedpref" path="." />
        <include domain="database" path="." />
        <exclude domain="file" path="roms/" />
        <exclude domain="file" path="states/" />
        <exclude domain="file" path="battery/" />
    </cloud-backup>
    <device-transfer>
        <include domain="sharedpref" path="." />
        <include domain="database" path="." />
        <exclude domain="file" path="roms/" />
        <exclude domain="file" path="states/" />
        <exclude domain="file" path="battery/" />
    </device-transfer>
</data-extraction-rules>
```

The policy backs up settings and the library index, excludes copyrighted ROM content and volatile states from cloud backup, and permits battery/state transfer only through explicit user export added after Alpha.

- [ ] **Step 4: Bind policy and make controller features optional**

```xml
<uses-feature android:name="android.hardware.gamepad" android:required="false" />
<application
    android:allowBackup="true"
    android:fullBackupContent="@xml/backup_rules"
    android:dataExtractionRules="@xml/data_extraction_rules">
```

- [ ] **Step 5: Pass lifecycle, rotation, audio-route, and process-death checks**

Run the focused test, then execute these matrix rows on API 24, 29, 34, and 36:

| Transition | Required result |
|---|---|
| Home → return | Same ROM and save identity; audio resumes once |
| Screen off → unlock | No stuck input or duplicate audio engine |
| Rotate/recreate | Same session or deterministic restore; no global byte handoff |
| Bluetooth/headset route change | No crash; audio sink restarts without core reset |
| Process death after committed save | Last atomic save loads and CRC validates |

- [ ] **Step 6: Commit lifecycle and data policy**

```powershell
git add app/src/main/res/xml/backup_rules.xml app/src/main/res/xml/data_extraction_rules.xml app/src/main/AndroidManifest.xml app/src/androidTest/java/com/flynes/emu/AlphaLifecycleSmokeTest.java
git commit -m "test: lock alpha lifecycle and backup policy"
```

### Task 5: Add ABI, Android, and emulator CI gates

**Files:**
- Create: `scripts/abi_symbols.golden.txt`
- Create: `scripts/check-abi.ps1`
- Modify: `.github/workflows/stage0.yml`

- [ ] **Step 1: Generate and review the ABI allowlist**

Run:

```powershell
cmake -S core -B build -DNES_BUILD_TESTS=ON
cmake --build build --config Release
dumpbin /symbols build/Release/nes_abi.lib | Select-String "External.*nes_" | ForEach-Object { ($_ -split '\|')[-1].Trim() } | Sort-Object -Unique
```

Copy only the reviewed public `nes_*` symbols into `scripts/abi_symbols.golden.txt`, one symbol per line. Include the battery import/export and frame snapshot symbols introduced by Plans 2A/2B.

- [ ] **Step 2: Implement a deterministic ABI comparison script**

```powershell
param([Parameter(Mandatory = $true)][string]$ActualSymbols)
$expectedPath = Join-Path $PSScriptRoot 'abi_symbols.golden.txt'
$expected = Get-Content -LiteralPath $expectedPath | Where-Object { $_ -and -not $_.StartsWith('#') } | Sort-Object -Unique
$actual = Get-Content -LiteralPath $ActualSymbols | Where-Object { $_ -and -not $_.StartsWith('#') } | Sort-Object -Unique
$missing = Compare-Object $expected $actual | Where-Object SideIndicator -eq '<=' | ForEach-Object InputObject
$added = Compare-Object $expected $actual | Where-Object SideIndicator -eq '=>' | ForEach-Object InputObject
if ($added) { Write-Warning ("Unreviewed ABI symbols: " + ($added -join ', ')) }
if ($missing) { throw "Missing ABI symbols: $($missing -join ', ')" }
```

- [ ] **Step 3: Add the Android build gate**

Add a GitHub Actions `android-build` job that uses JDK 17, Android SDK platform 36, and runs:

```yaml
- name: Android verification
  shell: pwsh
  run: ./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug --stacktrace
```

Pin every action by a reviewed commit SHA before the release tag; Dependabot may update those SHAs in separate pull requests.

- [ ] **Step 4: Replace the existing phase-1.5 comment with a Linux ABI job**

The job builds `nes_abi`, extracts globally defined `nes_*` symbols with `nm --defined-only --extern-only`, normalizes them into `build/abi-symbols.txt`, and invokes:

```yaml
- name: Check public ABI
  shell: pwsh
  run: ./scripts/check-abi.ps1 -ActualSymbols build/abi-symbols.txt
```

- [ ] **Step 5: Add a deterministic API 36 emulator smoke job**

Run only `AlphaLifecycleSmokeTest`, `GameControllerInputTest`, `GamepadAccessibilityTest`, and the library navigation smoke tests on an `x86_64` API 36 AVD with animations disabled. Upload `app/build/reports/androidTests`, logcat, and screenshots on failure.

```yaml
script: ./gradlew :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.notAnnotation=com.flynes.emu.test.ManualOnly
```

- [ ] **Step 6: Run all equivalent local gates**

```powershell
./gradlew :app:assembleDebug :app:testDebugUnitTest :app:lintDebug
cmake -S core -B build -DNES_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Expected: all commands exit 0 and CI accepts the same commit.

- [ ] **Step 7: Commit release gates**

```powershell
git add .github/workflows/stage0.yml scripts/abi_symbols.golden.txt scripts/check-abi.ps1
git commit -m "ci: enforce android core and abi gates"
```

### Task 6: Script acceptance evidence and cut the Alpha release candidate

**Files:**
- Create: `scripts/alpha-acceptance.ps1`
- Create: `docs/acceptance/alpha/release-candidate.md`
- Modify: `README.md`
- Modify: `app/src/main/java/com/flynes/emu/LicensesActivity.java`

- [ ] **Step 1: Create the release checklist before collecting evidence**

The checklist must record commit SHA, version name/code, APK SHA-256, tester, device/build, locale, refresh rate, controller model, and pass/fail/evidence path for:

1. clean install and first-run empty/library states;
2. bundled authorized game launch and 30-minute play;
3. A/B mis-touch sweep and distinct/off haptic settings;
4. 60 Hz, 90 Hz, and 120 Hz frame pacing captures;
5. wired/Bluetooth audio start, route switch, and resume;
6. per-ROM state/battery isolation and corrupted-save recovery;
7. rapid A→B→A ROM switch without stale frames/audio/input;
8. English and Simplified Chinese UI/search/title fallback;
9. touch, keyboard, and at least two controller models;
10. TalkBack, font scale 1.3×, display scale 1.2×, contrast, and focus order;
11. license/source links and bundled-ROM authorization record;
12. API 24, 29, 34, and 36 lifecycle matrix.

- [ ] **Step 2: Implement the acceptance runner**

```powershell
param(
    [string]$OutputRoot = (Join-Path $PSScriptRoot '..\artifacts\alpha-acceptance'),
    [string]$Serial = $env:ANDROID_SERIAL
)
$ErrorActionPreference = 'Stop'
$resolvedRoot = [System.IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Force -Path $resolvedRoot | Out-Null
./gradlew :app:clean :app:assembleDebug :app:testDebugUnitTest :app:lintDebug
cmake -S core -B build -DNES_BUILD_TESTS=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure | Tee-Object (Join-Path $resolvedRoot 'core-tests.txt')
$apk = Resolve-Path 'app\build\outputs\apk\debug\app-debug.apk'
Get-FileHash -Algorithm SHA256 -LiteralPath $apk | Format-List | Out-File (Join-Path $resolvedRoot 'apk-sha256.txt')
git rev-parse HEAD | Out-File (Join-Path $resolvedRoot 'commit.txt')
if ($Serial) {
    adb -s $Serial logcat -c
    ./gradlew :app:connectedDebugAndroidTest
    adb -s $Serial shell dumpsys display | Out-File (Join-Path $resolvedRoot 'display.txt')
    adb -s $Serial shell dumpsys media.audio_flinger | Out-File (Join-Path $resolvedRoot 'audio.txt')
    adb -s $Serial logcat -d | Out-File (Join-Path $resolvedRoot 'logcat.txt')
}
```

The script must stop on the first automated gate failure and must never delete files outside its resolved output directory.

- [ ] **Step 3: Make license and provenance evidence reachable in-product**

Update `LicensesActivity` and README so each bundled native component shows name, version/commit, license text, and source URL. Add the bundled game's authorization/provenance record to the release checklist without embedding private correspondence in the APK.

```java
public record ComponentLicense(
        String name, String revision, String licenseAsset, Uri sourceUri) { }
```

- [ ] **Step 4: Run the automated acceptance collector**

```powershell
./scripts/alpha-acceptance.ps1 -Serial $env:ANDROID_SERIAL
```

Expected: `artifacts/alpha-acceptance` contains commit, APK hash, core result, Android reports, display/audio dumps, and logcat for the exact candidate.

- [ ] **Step 5: Execute and sign off the manual matrix**

Two people must review data-safety rows: one executes and one independently confirms ROM identity, save path, restore result, and corrupted-save behavior. All P0/P1 rows must pass; a P2 exception requires an owner, linked issue, workaround, and target release in the checklist.

- [ ] **Step 6: Build and verify the signed release candidate**

```powershell
./gradlew :app:bundleRelease :app:assembleRelease
Get-FileHash -Algorithm SHA256 app/build/outputs/apk/release/app-release.apk
bundletool validate --bundle app/build/outputs/bundle/release/app-release.aab
```

Expected: release APK/AAB are signed by the configured Alpha key, validation exits 0, and hashes are copied into the checklist. Signing secrets remain outside the repository and logs.

- [ ] **Step 7: Commit the evidence framework and tag only after green gates**

```powershell
git add scripts/alpha-acceptance.ps1 docs/acceptance/alpha/release-candidate.md README.md app/src/main/java/com/flynes/emu/LicensesActivity.java
git commit -m "docs: add alpha release acceptance evidence"
git tag -s v0.1.0-alpha.1 -m "FlyNES 0.1.0 Alpha 1"
git tag --verify v0.1.0-alpha.1
```

Expected: the signed tag points to the exact reviewed candidate commit. Push the tag only after the product owner approves the completed checklist.

## Definition of done

- [ ] Touch, keyboard, and multiple controllers merge deterministically and release their own source state on cancel/disconnect.
- [ ] Pause is an App action and never aliases NES Start.
- [ ] The virtual gamepad has eight localized, unique, clickable accessibility nodes with stable focus order.
- [ ] Backup and transfer policy never uploads ROM files or volatile state files.
- [ ] Host core, public ABI, Android build/unit/lint, and API 36 emulator jobs pass on the release commit.
- [ ] API 24/29/34/36, 60/90/120 Hz, two controller, English/Chinese, audio-route, lifecycle, and TalkBack evidence is attached.
- [ ] License/source/provenance records are reachable and reviewed.
- [ ] The APK/AAB hash and signed tag resolve to the same reviewed source commit.
