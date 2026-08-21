# FlyNES Alpha Foundation, Input, and Session Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Establish the Android test/build foundation, correct all touch-input semantics, and introduce a deterministic emulator session state machine.

**Architecture:** `InputRouter` merges touch, key, and controller masks while keeping App actions separate from NES buttons. `EmulationSession` becomes the only owner of `NesCore`; `MainActivity` delegates input and pause/resume instead of manipulating the core and audio thread directly.

**Tech Stack:** AGP 8.13.2, API 36, Java 17, Android Views, AndroidX Activity/AppCompat/Test, Material Components 1.14.0, JUnit 4.13.2, Espresso 3.7.0.

---

## File map

- Create `app/src/main/java/com/flynes/emu/input/InputBits.java`: NES button constants.
- Create `app/src/main/java/com/flynes/emu/input/InputRouter.java`: source-mask merger and App actions.
- Create `app/src/main/java/com/flynes/emu/input/GamepadHitMap.java`: Android-free hit geometry.
- Create `app/src/main/java/com/flynes/emu/input/HapticController.java`: capability-aware haptic patterns.
- Create `app/src/main/java/com/flynes/emu/session/CoreFacade.java`: testable core boundary.
- Create `app/src/main/java/com/flynes/emu/session/SessionState.java`: legal state enum.
- Create `app/src/main/java/com/flynes/emu/session/SessionResult.java`: typed transition result.
- Create `app/src/main/java/com/flynes/emu/session/EmulationSession.java`: serialized state machine.
- Modify `GamepadView.java`: consume hit map/router; remove pause callback; cancel all pointers correctly.
- Modify `MainActivity.java`: use session/router and independent pause action.
- Test under `app/src/test/java/com/flynes/emu/input`, `app/src/test/java/com/flynes/emu/session`, and `app/src/androidTest/java/com/flynes/emu`.

### Task 1: Upgrade the Android build and add test gates

**Files:**
- Modify: `build.gradle`
- Modify: `app/build.gradle`
- Create: `app/src/test/java/com/flynes/emu/BuildSmokeTest.java`

- [ ] **Step 1: Write the failing unit smoke test**

```java
package com.flynes.emu;

import static org.junit.Assert.assertEquals;
import org.junit.Test;

public final class BuildSmokeTest {
    @Test public void java17IsActive() {
        assertEquals(17, Runtime.version().feature());
    }
}
```

- [ ] **Step 2: Verify the missing JUnit dependency fails compilation**

Run: `./gradlew.bat :app:testDebugUnitTest --console=plain`

Expected: FAIL with unresolved `org.junit` imports.

- [ ] **Step 3: Pin API 36, AGP, dependencies, and the test runner**

Replace the root plugin version with:

```groovy
plugins {
    id 'com.android.application' version '8.13.2' apply false
}
```

Set `compileSdk 36`, `targetSdk 36`, and add inside `defaultConfig`:

```groovy
testInstrumentationRunner "androidx.test.runner.AndroidJUnitRunner"
```

Append to `app/build.gradle`:

```groovy
dependencies {
    implementation 'androidx.activity:activity:1.13.0'
    implementation 'androidx.appcompat:appcompat:1.8.0'
    implementation 'androidx.recyclerview:recyclerview:1.4.0'
    implementation 'androidx.preference:preference:1.2.1'
    implementation 'com.google.android.material:material:1.14.0'

    testImplementation 'junit:junit:4.13.2'
    androidTestImplementation 'androidx.test:core:1.7.0'
    androidTestImplementation 'androidx.test:runner:1.7.0'
    androidTestImplementation 'androidx.test:rules:1.7.0'
    androidTestImplementation 'androidx.test.ext:junit:1.3.0'
    androidTestImplementation 'androidx.test.espresso:espresso-core:3.7.0'
}
```

- [ ] **Step 4: Run the new foundation gate**

Run: `./gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest --console=plain`

Expected: `BUILD SUCCESSFUL`; `BuildSmokeTest` passes.

- [ ] **Step 5: Commit the build foundation**

```powershell
git add build.gradle app/build.gradle app/src/test/java/com/flynes/emu/BuildSmokeTest.java
git commit -m "build: target API 36 and add Android test foundation"
```

### Task 2: Implement the source-aware InputRouter

**Files:**
- Create: `app/src/main/java/com/flynes/emu/input/InputBits.java`
- Create: `app/src/main/java/com/flynes/emu/input/InputRouter.java`
- Test: `app/src/test/java/com/flynes/emu/input/InputRouterTest.java`

- [ ] **Step 1: Write failing merger and separation tests**

```java
package com.flynes.emu.input;

import static org.junit.Assert.*;
import java.util.ArrayList;
import java.util.List;
import org.junit.Test;

public final class InputRouterTest {
    @Test public void mergesSourcesAndCancelAllPublishesZero() {
        List<Integer> masks = new ArrayList<>();
        InputRouter router = new InputRouter(masks::add, action -> {});
        router.setMask(InputRouter.Source.TOUCH, InputBits.RIGHT);
        router.setMask(InputRouter.Source.GAMEPAD, InputBits.A);
        assertEquals(InputBits.RIGHT | InputBits.A, router.currentMask());
        router.cancelAll();
        assertEquals(Integer.valueOf(0), masks.get(masks.size() - 1));
    }

    @Test public void appPauseNeverGeneratesNesStart() {
        List<InputRouter.AppAction> actions = new ArrayList<>();
        InputRouter router = new InputRouter(mask -> {}, actions::add);
        router.dispatch(InputRouter.AppAction.OPEN_PAUSE);
        assertEquals(0, router.currentMask());
        assertEquals(List.of(InputRouter.AppAction.OPEN_PAUSE), actions);
    }
}
```

- [ ] **Step 2: Run the tests and observe missing classes**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*InputRouterTest' --console=plain`

Expected: FAIL because `InputRouter` and `InputBits` do not exist.

- [ ] **Step 3: Add the minimal router implementation**

```java
package com.flynes.emu.input;

public final class InputBits {
    public static final int A=0x01, B=0x02, SELECT=0x04, START=0x08;
    public static final int UP=0x10, DOWN=0x20, LEFT=0x40, RIGHT=0x80;
    private InputBits() {}
}
```

```java
package com.flynes.emu.input;

import java.util.EnumMap;
import java.util.function.Consumer;
import java.util.function.IntConsumer;

public final class InputRouter {
    public enum Source { TOUCH, KEYBOARD, GAMEPAD, ACCESSIBILITY }
    public enum AppAction { OPEN_PAUSE, CLOSE_PAUSE, OPEN_LIBRARY, OPEN_SETTINGS }

    private final EnumMap<Source,Integer> masks = new EnumMap<>(Source.class);
    private final IntConsumer nesListener;
    private final Consumer<AppAction> appListener;
    private int currentMask;

    public InputRouter(IntConsumer nesListener, Consumer<AppAction> appListener) {
        this.nesListener = nesListener;
        this.appListener = appListener;
        for (Source source : Source.values()) masks.put(source, 0);
    }

    public synchronized void setMask(Source source, int mask) {
        masks.put(source, mask & 0xFF);
        int merged = 0;
        for (int value : masks.values()) merged |= value;
        if (merged != currentMask) {
            currentMask = merged;
            nesListener.accept(merged);
        }
    }

    public synchronized int currentMask() { return currentMask; }
    public void cancel(Source source) { setMask(source, 0); }
    public synchronized void cancelAll() {
        for (Source source : Source.values()) masks.put(source, 0);
        currentMask = 0;
        nesListener.accept(0);
    }
    public void dispatch(AppAction action) { appListener.accept(action); }
}
```

- [ ] **Step 4: Run the router tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*InputRouterTest' --console=plain`

Expected: both tests PASS.

- [ ] **Step 5: Commit the router contract**

```powershell
git add app/src/main/java/com/flynes/emu/input app/src/test/java/com/flynes/emu/input
git commit -m "feat: add source-aware input router"
```

### Task 3: Build an overlap-free hit map

**Files:**
- Create: `app/src/main/java/com/flynes/emu/input/GamepadHitMap.java`
- Test: `app/src/test/java/com/flynes/emu/input/GamepadHitMapTest.java`

- [ ] **Step 1: Write boundary tests for A/B and START/SELECT**

```java
package com.flynes.emu.input;

import static org.junit.Assert.*;
import org.junit.Test;

public final class GamepadHitMapTest {
    @Test public void standardLayoutHasNoOverlaps() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, 2.75f, 0, 132, 0, 0);
        assertTrue(map.validate().isEmpty());
    }

    @Test public void aAndBHaveAnUnambiguousGap() {
        GamepadHitMap map = GamepadHitMap.standard(2340, 1080, 2.75f, 0, 132, 0, 0);
        GamepadHitMap.Circle b = map.circle(GamepadHitMap.Control.B);
        GamepadHitMap.Circle a = map.circle(GamepadHitMap.Control.A);
        assertEquals(GamepadHitMap.Control.B, map.hit(b.cx(), b.cy()));
        assertEquals(GamepadHitMap.Control.A, map.hit(a.cx(), a.cy()));
        assertEquals(GamepadHitMap.Control.NONE,
                map.hit((b.cx() + a.cx()) / 2f, b.cy()));
    }
}
```

- [ ] **Step 2: Run the hit-map tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*GamepadHitMapTest' --console=plain`

Expected: FAIL because `GamepadHitMap` does not exist.

- [ ] **Step 3: Implement pure-Java geometry and validation**

Create `GamepadHitMap` with this public contract:

```java
public final class GamepadHitMap {
    public enum Control { NONE, JOY, B, A, SELECT, START }
    public record Circle(Control control, float cx, float cy, float radius) {
        boolean contains(float x, float y) {
            float dx=x-cx, dy=y-cy;
            return dx*dx + dy*dy <= radius*radius;
        }
        boolean overlaps(Circle other) {
            float dx=cx-other.cx, dy=cy-other.cy;
            float sum=radius+other.radius;
            return dx*dx + dy*dy < sum*sum;
        }
    }

    private final float left;
    private final float top;
    private final float right;
    private final float bottom;
    private final java.util.List<Circle> circles;

    private GamepadHitMap(float left, float top, float right, float bottom,
            java.util.List<Circle> circles) {
        this.left = left;
        this.top = top;
        this.right = right;
        this.bottom = bottom;
        this.circles = java.util.List.copyOf(circles);
    }

    public static GamepadHitMap standard(int width, int height, float density,
            int insetLeft, int insetRight, int insetTop, int insetBottom) {
        float safeRight = width - insetRight;
        float bottom = height - insetBottom - 24*density;
        float r = 28*density;
        Circle joy = new Circle(Control.JOY, insetLeft + 100*density,
                bottom - 76*density, 76*density);
        Circle select = new Circle(Control.SELECT, width/2f - 28*density,
                bottom - 24*density, 24*density);
        Circle start = new Circle(Control.START, width/2f + 28*density,
                bottom - 24*density, 24*density);
        Circle b = new Circle(Control.B, safeRight - 104*density, bottom-r, r);
        Circle a = new Circle(Control.A, safeRight - 32*density, bottom-r, r);
        GamepadHitMap map = new GamepadHitMap(insetLeft, insetTop,
                width-insetRight, height-insetBottom,
                java.util.List.of(joy, select, start, b, a));
        java.util.List<String> errors = map.validate();
        if (!errors.isEmpty()) throw new IllegalArgumentException(String.join("; ", errors));
        return map;
    }

    public Control hit(float x, float y) {
        for (Circle circle : circles) {
            if (circle.contains(x, y)) return circle.control();
        }
        return Control.NONE;
    }

    public Circle circle(Control control) {
        return circles.stream().filter(c -> c.control() == control).findFirst()
                .orElseThrow(() -> new IllegalArgumentException(control.name()));
    }

    public java.util.List<String> validate() {
        java.util.ArrayList<String> errors = new java.util.ArrayList<>();
        for (int i = 0; i < circles.size(); i++) {
            Circle a = circles.get(i);
            if (a.cx()-a.radius() < left || a.cx()+a.radius() > right
                    || a.cy()-a.radius() < top || a.cy()+a.radius() > bottom) {
                errors.add(a.control() + " outside safe rect");
            }
            for (int j = i+1; j < circles.size(); j++) {
                Circle b = circles.get(j);
                if (a.overlaps(b)) errors.add(a.control() + " overlaps " + b.control());
            }
        }
        return java.util.List.copyOf(errors);
    }
}
```

The fixed geometry gives A/B 56 dp targets with a 16 dp edge-to-edge gap, Select/Start 48 dp targets, and a joystick target fully contained by the safe rect. `hit()` returns exactly one concrete control or `NONE`; Pause is a separate Material button and is not part of this hit map.

- [ ] **Step 4: Run geometry tests and inspect validation output**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*GamepadHitMapTest' --console=plain`

Expected: PASS with an empty validation list.

- [ ] **Step 5: Commit the hit-map contract**

```powershell
git add app/src/main/java/com/flynes/emu/input/GamepadHitMap.java app/src/test/java/com/flynes/emu/input/GamepadHitMapTest.java
git commit -m "fix: make virtual gamepad hit regions mutually exclusive"
```

### Task 4: Refactor GamepadView and add capability-aware haptics

**Files:**
- Create: `app/src/main/java/com/flynes/emu/input/HapticController.java`
- Create: `app/src/main/java/com/flynes/emu/input/HapticLevel.java`
- Create: `app/src/main/java/com/flynes/emu/input/HapticPattern.java`
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Test: `app/src/test/java/com/flynes/emu/input/HapticPatternTest.java`
- Test: `app/src/androidTest/java/com/flynes/emu/GamepadCancelTest.java`

- [ ] **Step 1: Write a failing multi-touch cancellation test**

```java
@RunWith(AndroidJUnit4.class)
public final class GamepadCancelTest {
    @Test public void cancelClearsEveryPointerAndPublishesZero() {
        Context context = ApplicationProvider.getApplicationContext();
        GamepadView view = new GamepadView(context);
        AtomicInteger lastMask = new AtomicInteger(-1);
        view.setInputListener(lastMask::set);
        view.cancelAllPointersForTest();
        assertEquals(0, lastMask.get());
        assertEquals(0, view.buttons());
    }
}
```

Add the Android-free A/B distinction test:

```java
@Test public void distinctABUsesDifferentCadenceAndCanBeDisabled() {
    assertNotEquals(HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.LIGHT, true),
            HapticPattern.forControl(GamepadHitMap.Control.B, HapticLevel.LIGHT, true));
    assertEquals(HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.LIGHT, false),
            HapticPattern.forControl(GamepadHitMap.Control.B, HapticLevel.LIGHT, false));
    assertEquals(HapticPattern.NONE,
            HapticPattern.forControl(GamepadHitMap.Control.A, HapticLevel.OFF, true));
}
```

- [ ] **Step 2: Run the instrumentation test**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadCancelTest --console=plain`

Expected: FAIL because the new listener and cancellation methods do not exist.

- [ ] **Step 3: Route touch input and separate App pause**

Change `GamepadView` to expose:

```java
public void setInputRouter(InputRouter router) { this.router = router; }
public void setInputListener(java.util.function.IntConsumer listener) {
    this.testListener = listener;
}
public void cancelAllPointersForTest() { cancelAllPointers(); }

private void cancelAllPointers() {
    pointers.clear();
    buttons = 0;
    handler.removeCallbacksAndMessages(null);
    if (router != null) router.cancel(InputRouter.Source.TOUCH);
    if (testListener != null) testListener.accept(0);
    invalidate();
}
```

Handle `ACTION_CANCEL` in its own switch branch by calling `cancelAllPointers()`. Remove `Listener.onPauseMenu()` and remove the START-triggered menu call; START only contributes `InputBits.START`.

Define `HapticLevel { OFF, LIGHT, STANDARD, STRONG }`. `HapticPattern` is an immutable cadence/amplitude value; when A/B distinction is on, A is one click and B is a heavy click or two-pulse fallback separated by 12 ms. When distinction is off, both use the A pattern. Level scales only intensity/duration and OFF maps to `NONE`. Implement `HapticController.feedback(Control)` using system `performHapticFeedback`/predefined effects first and the cadence fallback only when needed. START uses confirmation and SELECT uses one light click. Android does not expose portable motor-frequency control, so the acceptance target is perceptually distinct predefined effects or timing patterns, not a claimed exact Hz value.

- [ ] **Step 4: Run unit, instrumentation, and Lint gates**

Run:

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:connectedDebugAndroidTest :app:lintDebug --console=plain
```

Expected: geometry and cancellation tests PASS; no `ClickableViewAccessibility` warning remains after implementing `performClick()`.

- [ ] **Step 5: Commit the corrected touch layer**

```powershell
git add app/src/main/java/com/flynes/emu/GamepadView.java app/src/main/java/com/flynes/emu/input/HapticController.java app/src/main/java/com/flynes/emu/input/HapticLevel.java app/src/main/java/com/flynes/emu/input/HapticPattern.java app/src/test/java/com/flynes/emu/input/HapticPatternTest.java app/src/androidTest/java/com/flynes/emu/GamepadCancelTest.java
git commit -m "fix: separate pause action and clear canceled touches"
```

### Task 5: Introduce the deterministic EmulationSession state machine

**Files:**
- Create: `app/src/main/java/com/flynes/emu/session/CoreFacade.java`
- Create: `app/src/main/java/com/flynes/emu/session/SessionState.java`
- Create: `app/src/main/java/com/flynes/emu/session/SessionResult.java`
- Create: `app/src/main/java/com/flynes/emu/session/EmulationSession.java`
- Test: `app/src/test/java/com/flynes/emu/session/EmulationSessionTest.java`

- [ ] **Step 1: Write state and stop-before-start tests with a fake core**

```java
@Test public void stopBeforeStartCannotResurrectSession() throws Exception {
    FakeCore core = new FakeCore();
    EmulationSession session = EmulationSession.forTest(core, Runnable::run);
    session.stop().get();
    SessionResult result = session.start().get();
    assertFalse(result.isSuccess());
    assertEquals(SessionState.EMPTY, session.state());
    assertEquals(0, core.runCalls);
}

@Test public void pausePublishesZeroInputBeforeStoppingClock() throws Exception {
    FakeCore core = new FakeCore();
    EmulationSession session = EmulationSession.forTest(core, Runnable::run);
    session.load(new byte[]{'N','E','S',0x1A}).get();
    session.start().get();
    session.setInput(InputBits.A);
    session.pause().get();
    assertEquals(0, core.lastInput);
    assertEquals(SessionState.PAUSED, session.state());
}

private static final class FakeCore implements CoreFacade {
    int runCalls;
    int lastInput;
    @Override public int create() { return 0; }
    @Override public int loadRom(byte[] rom) { return 0; }
    @Override public void setInput(int mask) { lastInput = mask; }
    @Override public int runOneFrame() { runCalls++; return 0; }
    @Override public byte[] saveState() { return new byte[0]; }
    @Override public int loadState(byte[] state) { return 0; }
    @Override public void destroy() { }
}
```

- [ ] **Step 2: Run the session tests**

Run: `./gradlew.bat :app:testDebugUnitTest --tests '*EmulationSessionTest' --console=plain`

Expected: FAIL because session contracts do not exist.

- [ ] **Step 3: Add explicit contracts and serialized transitions**

```java
public interface CoreFacade {
    int create();
    int loadRom(byte[] rom);
    void setInput(int mask);
    int runOneFrame();
    byte[] saveState();
    int loadState(byte[] state);
    void destroy();
}
```

```java
public enum SessionState { EMPTY, LOADING, READY, RUNNING, PAUSED, SWITCHING, STOPPING, ERROR }
```

`EmulationSession` must own a single-thread `ExecutorService`, expose `CompletableFuture<SessionResult>` for `load/start/pause/resume/stop`, reject illegal transitions, set input 0 before PAUSED/STOPPING, and never change STOPPING back to RUNNING. `forTest` accepts a deterministic executor.

- [ ] **Step 4: Run the deterministic tests 1,000 times**

Run:

```powershell
1..1000 | ForEach-Object { .\gradlew.bat :app:testDebugUnitTest --tests '*EmulationSessionTest.stopBeforeStartCannotResurrectSession' --quiet; if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE } }
```

Expected: 1,000 successful invocations and no `runOneFrame` call after stop.

- [ ] **Step 5: Commit the session contract**

```powershell
git add app/src/main/java/com/flynes/emu/session app/src/test/java/com/flynes/emu/session
git commit -m "feat: add deterministic emulation session state machine"
```

### Task 6: Wire MainActivity without reintroducing START/menu coupling

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/NesCore.java`
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`
- Test: `app/src/androidTest/java/com/flynes/emu/StartAndPauseSeparationTest.java`

- [ ] **Step 1: Write a failing separation test**

```java
@RunWith(AndroidJUnit4.class)
public final class StartAndPauseSeparationTest {
    @Test public void nesStartDoesNotOpenPauseButPauseButtonDoes() {
        try (ActivityScenario<MainActivity> scenario = ActivityScenario.launch(MainActivity.class)) {
            scenario.onActivity(activity -> {
                activity.inputRouterForTest().setMask(InputRouter.Source.TOUCH, InputBits.START);
                assertFalse(activity.isPauseVisibleForTest());
                activity.inputRouterForTest().dispatch(InputRouter.AppAction.OPEN_PAUSE);
                assertTrue(activity.isPauseVisibleForTest());
            });
        }
    }
}
```

- [ ] **Step 2: Run the separation test**

Run: `./gradlew.bat :app:connectedDebugAndroidTest -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.StartAndPauseSeparationTest --console=plain`

Expected: FAIL because `MainActivity` does not expose the router/session behavior.

- [ ] **Step 3: Adapt NesCore and MainActivity to the new contracts**

Make `NesCore` implement `CoreFacade`. In `MainActivity`, create:

```java
inputRouter = new InputRouter(session::setInput, this::handleAppAction);
gamepad.setInputRouter(inputRouter);
```

`handleAppAction(OPEN_PAUSE)` calls `session.pause()` and shows the existing dialog temporarily; continuing calls `session.resume()` without injecting START. Add an independent pause `ImageButton` with `contentDescription=@string/open_pause`. Remove `pressStart()`, `KEY_AUTOSAVE_ROM_HASH`, direct `core.setInput()` calls, and START-based `showPauseMenu()` invocation.

- [ ] **Step 4: Run all foundation gates**

Run:

```powershell
.\gradlew.bat :app:assembleDebug :app:lintDebug :app:testDebugUnitTest :app:connectedDebugAndroidTest --console=plain
```

Expected: `BUILD SUCCESSFUL`; START separation, CANCEL, router, geometry, and session tests PASS.

- [ ] **Step 5: Commit the vertical integration**

```powershell
git add app/src/main/java/com/flynes/emu/NesCore.java app/src/main/java/com/flynes/emu/MainActivity.java app/src/androidTest/java/com/flynes/emu/StartAndPauseSeparationTest.java
git commit -m "refactor: route gameplay through emulation session"
```
