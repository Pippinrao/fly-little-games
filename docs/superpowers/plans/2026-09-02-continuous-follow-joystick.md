# Continuous-Follow Joystick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让默认移动拨杆从按下到抬起持续跟手，越过原始可视范围、划回或反向时都不丢失方向和指针所有权，并产出可安装的 Release 测试包。

**Architecture:** 保留现有 `GamepadHitMap → GamepadInputState → GamepadView` 链路，在 `GamepadInputState` 内增加唯一的摇杆会话状态（所有者、动态中心、拨杆头和触点），避免再造并行输入系统。`GamepadHitMap` 提供无外圈截断的方向数学与安全区约束；`GamepadView` 只适配 `MotionEvent`、绘制同一会话快照、保留 Insets 中的状态，并设置有界系统手势排除区域。

**Tech Stack:** Android Java 17、Canvas、MotionEvent、AndroidX Test、JUnit 4、Gradle/AGP、Android build-tools 35.0.0。

---

## 文件结构

- Modify: `app/src/main/java/com/flynes/emu/input/GamepadHitMap.java` — 摇杆安全起控区、中心夹紧、行程和无外圈截断的方向映射。
- Modify: `app/src/main/java/com/flynes/emu/input/GamepadInputState.java` — 唯一摇杆所有者、动态中心/拨杆头、重配置保活和按钮并发。
- Create: `app/src/test/java/com/flynes/emu/input/ContinuousJoystickSessionTest.java` — 真实断联路径、跟随几何、所有权、多指和 Insets 回归。
- Modify: `app/src/test/java/com/flynes/emu/input/JoystickDirectionTest.java` — 明确验证任意远距离只饱和、不归零。
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java` — 历史采样、动态绘制、父级拦截、Insets 保活、释放语义和手势排除。
- Create: `app/src/androidTest/java/com/flynes/emu/GamepadJoystickContinuityTest.java` — 真实 `MotionEvent` 的划出、划回、历史采样、取消、Insets 和排除区验证。
- Modify: `app/src/main/res/values/strings.xml` — 将模式名称从固定拨杆改为跟手拨杆。
- Modify: `app/src/main/res/values-zh-rCN/strings.xml` — 同步中文模式名称。

### Task 1: 在纯输入状态中建立连续摇杆会话

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/input/GamepadHitMap.java`
- Modify: `app/src/main/java/com/flynes/emu/input/GamepadInputState.java`
- Create: `app/src/test/java/com/flynes/emu/input/ContinuousJoystickSessionTest.java`
- Modify: `app/src/test/java/com/flynes/emu/input/JoystickDirectionTest.java`

- [ ] **Step 1: 写下能稳定复现当前缺陷的失败测试**

创建 `ContinuousJoystickSessionTest`，使用推荐布局和默认 `.22f` 死区。先只调用现有 API，确保失败来自当前行为而不是测试脚手架：

```java
package com.flynes.emu.input;

import static org.junit.Assert.assertEquals;
import static org.junit.Assert.assertFalse;
import static org.junit.Assert.assertTrue;

import org.junit.Test;

public final class ContinuousJoystickSessionTest {
    private final GamepadHitMap map = GamepadHitMap.fromLayout(
            2340, 1080, 2.75f, 0, 132, 0, 0,
            ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);

    @Test public void centerDownIsCapturedBeforeDirectionBegins() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        state.down(1, base.centerX(), base.centerY(), 1L);
        assertEquals(0, state.mask());
        assertEquals(1, state.activePointerCount());

        state.move(1, base.centerX() + base.width(), base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask());
    }

    @Test public void dragPastFiveRadiiStaysDirectionalAndReversesWithoutLift() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        float radius = base.width() / 2f;
        state.down(7, base.centerX(), base.centerY(), 1L);

        state.move(7, base.centerX() + radius * 5f, base.centerY(), 2L);
        assertEquals(InputBits.RIGHT, state.mask());
        assertEquals(1, state.activePointerCount());

        state.move(7, base.centerX() - radius * 5f, base.centerY(), 3L);
        assertEquals(InputBits.LEFT, state.mask());
        assertEquals(1, state.activePointerCount());
    }

    @Test public void secondLeftPointerCannotStealWhileAStillCombines() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Bounds base = map.dpadBounds();
        GamepadHitMap.Target a = map.target(GamepadHitMap.Control.A);
        state.down(1, base.centerX(), base.centerY(), 1L);
        state.down(2, base.centerX() + 80f, base.centerY(), 2L);
        assertEquals(1, state.activePointerCount());

        state.down(3, a.centerX(), a.centerY(), 3L);
        state.move(1, base.centerX() + base.width(), base.centerY(), 4L);
        assertEquals(InputBits.RIGHT | InputBits.A, state.mask());
        assertEquals(2, state.activePointerCount());
        assertTrue(state.hasConsistentOwnership());
    }

    @Test public void leftSideSelectTargetWinsOverJoystickActivation() {
        GamepadInputState state = new GamepadInputState(map);
        GamepadHitMap.Target select = map.target(GamepadHitMap.Control.SELECT);
        state.down(9, select.centerX(), select.centerY(), 1L);
        assertEquals(InputBits.SELECT, state.mask());
    }
}
```

在 `JoystickDirectionTest` 增加：

```java
@Test public void distanceOutsideVisualBaseSaturatesInsteadOfClearing() {
    GamepadHitMap map = GamepadHitMap.fromLayout(
            2340, 1080, 2.75f, 0, 132, 0, 0,
            ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);
    GamepadHitMap.Bounds base = map.dpadBounds();
    float radius = base.width() / 2f;
    assertEquals(InputBits.RIGHT, map.directionBits(
            base.centerX() + radius * 5f, base.centerY(), InputBits.RIGHT));
}
```

- [ ] **Step 2: 运行红测并确认是当前固定中心/外圈截断导致失败**

Run:

```powershell
.\gradlew.bat :app:testDebugUnitTest `
  --tests com.flynes.emu.input.ContinuousJoystickSessionTest `
  --tests com.flynes.emu.input.JoystickDirectionTest --console=plain
```

Expected: `centerDownIsCapturedBeforeDirectionBegins` 的活动指针数为 `0`，远距离方向实际为 `0`；测试进程正常结束为 FAIL。

- [ ] **Step 3: 为 `GamepadHitMap` 增加最小且单一来源的摇杆几何 API**

保持 DPAD 分支原样。新增以下方法，并让摇杆 `directionBits()` 委托给无外圈上限的中心映射：

```java
public boolean joystickMode() { return directionMode == DirectionControlMode.JOYSTICK; }

public Control buttonHit(float x, float y) {
    for (Control control : new Control[]{Control.A, Control.B, Control.SELECT, Control.START}) {
        if (target(control).contains(x, y)) return control;
    }
    return Control.NONE;
}

public boolean canStartJoystick(float x, float y) {
    return joystickMode() && safeBounds.contains(x, y) && x < safeBounds.centerX();
}

public float joystickRadius() {
    return Math.min(dpadBounds.width(), dpadBounds.height()) / 2f;
}

public float joystickTravelRadius() { return joystickRadius() * .5625f; }

public float clampJoystickCenterX(float x) {
    float minimum = safeBounds.left + joystickRadius();
    float maximum = Math.max(minimum, safeBounds.centerX() - joystickRadius());
    return clamp(x, minimum, maximum);
}

public float clampJoystickCenterY(float y) {
    float minimum = safeBounds.top + joystickRadius();
    float maximum = Math.max(minimum, safeBounds.bottom - joystickRadius());
    return clamp(y, minimum, maximum);
}

public int joystickDirectionBits(float centerX, float centerY,
                                 float x, float y, int previousBits) {
    float dx = x - centerX;
    float dy = y - centerY;
    float normalized = (float) Math.hypot(dx, dy) / joystickRadius();
    float threshold = previousBits == 0 ? deadZone : Math.max(.08f, deadZone - .06f);
    if (normalized < threshold) return 0;
    float ax = Math.abs(dx);
    float ay = Math.abs(dy);
    int bits = 0;
    if (ax >= ay * .55f) bits |= dx < 0 ? InputBits.LEFT : InputBits.RIGHT;
    if (ay >= ax * .55f) bits |= dy < 0 ? InputBits.UP : InputBits.DOWN;
    return bits;
}
```

`hit()` 继续先调用 `buttonHit()`；在摇杆模式下只把固定底座附近用于无障碍虚拟方向命中，活动手势不得依赖该范围。删除现有 `normalized > 1.18f` 的方向清零条件。

- [ ] **Step 4: 在 `GamepadInputState` 中实现唯一会话和连续跟随**

将 `map` 改为可重配置字段，在 `Pointer` 增加 `x/y`、`joystick`、`centerX/centerY`、`knobX/knobY`。新增不可变视觉快照：

```java
private static final int NO_POINTER = -1;

private static final class Pointer {
    final long downTime;
    final boolean dpad;
    final boolean joystick;
    int bits;
    long sequence;
    float x, y, centerX, centerY, knobX, knobY;

    Pointer(long downTime, boolean dpad, boolean joystick, int bits, long sequence) {
        this.downTime = downTime;
        this.dpad = dpad;
        this.joystick = joystick;
        this.bits = bits;
        this.sequence = sequence;
    }

    static Pointer joystick(long downTime, float x, float y, long sequence) {
        Pointer pointer = new Pointer(downTime, true, true, 0, sequence);
        pointer.x = x;
        pointer.y = y;
        return pointer;
    }
}

public static final class JoystickVisual {
    private final boolean active;
    private final float centerX, centerY, knobX, knobY;
    JoystickVisual(boolean active, float centerX, float centerY, float knobX, float knobY) {
        this.active = active;
        this.centerX = centerX;
        this.centerY = centerY;
        this.knobX = knobX;
        this.knobY = knobY;
    }
    public boolean active() { return active; }
    public float centerX() { return centerX; }
    public float centerY() { return centerY; }
    public float knobX() { return knobX; }
    public float knobY() { return knobY; }
}
```

`down(...)` 返回是否接管。顺序必须是按钮优先、首个左侧摇杆、最后才是 DPAD：

```java
public boolean down(int pointerId, float x, float y, long eventTime) {
    remove(pointerId);
    GamepadHitMap.Control button = map.buttonHit(x, y);
    if (button != GamepadHitMap.Control.NONE) {
        putButton(pointerId, x, y, eventTime, bitsFor(button));
    } else if (map.canStartJoystick(x, y) && joystickPointerId == NO_POINTER) {
        Pointer pointer = Pointer.joystick(eventTime, x, y, ++sequence);
        pointer.centerX = map.clampJoystickCenterX(x);
        pointer.centerY = map.clampJoystickCenterY(y);
        pointers.put(pointerId, pointer);
        joystickPointerId = pointerId;
        updateJoystick(pointer);
    } else if (!map.joystickMode()) {
        putDpadIfHit(pointerId, x, y, eventTime);
    }
    recompute();
    return pointers.containsKey(pointerId);
}
```

上述 `down()` 使用的三个小 helper 必须完整实现，避免分散角色清理逻辑：

```java
private void remove(int pointerId) {
    Pointer removed = pointers.remove(pointerId);
    if (removed != null && removed.joystick) joystickPointerId = NO_POINTER;
}

private void putButton(int pointerId, float x, float y, long eventTime, int bits) {
    Pointer pointer = new Pointer(eventTime, false, false, bits, ++sequence);
    pointer.x = x;
    pointer.y = y;
    pointers.put(pointerId, pointer);
}

private void putDpadIfHit(int pointerId, float x, float y, long eventTime) {
    int bits = map.directionBits(x, y, 0);
    if (bits == 0) return;
    Pointer pointer = new Pointer(eventTime, true, false, bits, ++sequence);
    pointer.x = x;
    pointer.y = y;
    pointers.put(pointerId, pointer);
}
```

跟随算法必须移动“超出最大行程”的部分，再夹紧基座并限制拨杆头：

```java
private void updateJoystick(Pointer pointer) {
    pointer.centerX = map.clampJoystickCenterX(pointer.centerX);
    pointer.centerY = map.clampJoystickCenterY(pointer.centerY);
    float dx = pointer.x - pointer.centerX;
    float dy = pointer.y - pointer.centerY;
    float distance = (float) Math.hypot(dx, dy);
    float travel = map.joystickTravelRadius();
    if (distance > travel && distance > 0f) {
        float follow = (distance - travel) / distance;
        pointer.centerX = map.clampJoystickCenterX(pointer.centerX + dx * follow);
        pointer.centerY = map.clampJoystickCenterY(pointer.centerY + dy * follow);
        dx = pointer.x - pointer.centerX;
        dy = pointer.y - pointer.centerY;
        distance = (float) Math.hypot(dx, dy);
    }
    float scale = distance > travel && distance > 0f ? travel / distance : 1f;
    pointer.knobX = pointer.centerX + dx * scale;
    pointer.knobY = pointer.centerY + dy * scale;
    pointer.bits = map.joystickDirectionBits(
            pointer.centerX, pointer.centerY, pointer.x, pointer.y, pointer.bits);
}
```

`move()` 只更新既有角色；摇杆所有者越过中线或按钮区仍保持角色。`up()`/`cancelAll()`/重复 pointer ID 必须同步释放 `joystickPointerId`。`joystickVisual()` 在活动时返回会话快照，空闲时返回固定锚点。

- [ ] **Step 5: 写重配置、视觉行程、释放和随机不变量红测**

在 `ContinuousJoystickSessionTest` 增加：

```java
@Test public void baseFollowsOnlyExcessAndKnobNeverExceedsTravel() {
    GamepadInputState state = new GamepadInputState(map);
    GamepadHitMap.Bounds base = map.dpadBounds();
    float travel = map.joystickTravelRadius();
    state.down(1, base.centerX(), base.centerY(), 1L);
    state.move(1, base.centerX() + travel * 2f, base.centerY(), 2L);
    GamepadInputState.JoystickVisual visual = state.joystickVisual();
    assertTrue(visual.active());
    assertEquals(travel, visual.centerX() - base.centerX(), .01f);
    assertEquals(travel, visual.knobX() - visual.centerX(), .01f);
}

@Test public void insetReconfigurationPreservesOwnerAndDirection() {
    GamepadInputState state = new GamepadInputState(map);
    GamepadHitMap.Bounds base = map.dpadBounds();
    state.down(1, base.centerX(), base.centerY(), 1L);
    state.move(1, base.centerX() + base.width() * 2f, base.centerY(), 2L);
    GamepadHitMap insetMap = GamepadHitMap.fromLayout(
            2340, 1080, 2.75f, 60, 132, 20, 30,
            ControlLayoutV2.recommended(), DirectionControlMode.JOYSTICK, .22f);
    state.reconfigure(insetMap);
    assertEquals(InputBits.RIGHT, state.mask());
    assertEquals(1, state.activePointerCount());
    assertTrue(state.joystickVisual().active());
}

@Test public void ownerUpAndCancelAlwaysClear() {
    GamepadInputState state = new GamepadInputState(map);
    GamepadHitMap.Bounds base = map.dpadBounds();
    state.down(1, base.centerX(), base.centerY(), 1L);
    state.move(1, base.centerX() + base.width(), base.centerY(), 2L);
    state.up(1, 3L);
    assertEquals(0, state.mask());
    assertFalse(state.joystickVisual().active());
    state.down(2, base.centerX(), base.centerY(), 4L);
    state.cancelAll();
    assertEquals(0, state.activePointerCount());
    assertFalse(state.joystickVisual().active());
}
```

将现有固定种子随机测试同时用于摇杆 map，并在每个事件后断言 `hasConsistentOwnership()`。

- [ ] **Step 6: 实现 `reconfigure()`、快照和唯一所有权不变量**

```java
public void reconfigure(GamepadHitMap replacement) {
    if (replacement == null) throw new IllegalArgumentException("map");
    if (map.joystickMode() != replacement.joystickMode()) {
        cancelAll();
        map = replacement;
        return;
    }
    map = replacement;
    Pointer joystick = pointers.get(joystickPointerId);
    if (joystick != null) updateJoystick(joystick);
    recompute();
}

public JoystickVisual joystickVisual() {
    Pointer pointer = pointers.get(joystickPointerId);
    if (pointer != null) {
        return new JoystickVisual(true, pointer.centerX, pointer.centerY,
                pointer.knobX, pointer.knobY);
    }
    GamepadHitMap.Bounds base = map.dpadBounds();
    return new JoystickVisual(false, base.centerX(), base.centerY(),
            base.centerX(), base.centerY());
}
```

扩展 `hasConsistentOwnership()`：摇杆 pointer 数不得超过一个，`joystickPointerId` 必须指向该 pointer，且拨杆头到中心的距离不得超过 `joystickTravelRadius() + 0.01f`。

- [ ] **Step 7: 运行目标测试和完整 JVM 测试，确认全部变绿**

Run:

```powershell
.\gradlew.bat :app:testDebugUnitTest `
  --tests com.flynes.emu.input.ContinuousJoystickSessionTest `
  --tests com.flynes.emu.input.JoystickDirectionTest `
  --tests com.flynes.emu.input.GamepadInputStateTest --console=plain
.\gradlew.bat :app:testDebugUnitTest --console=plain
```

Expected: 两条命令均 `BUILD SUCCESSFUL`，真实断联路径在 `5R` 外仍输出方向，完整 JVM 套件无回归。

- [ ] **Step 8: 提交纯输入状态改动**

```powershell
git add -- `
  app/src/main/java/com/flynes/emu/input/GamepadHitMap.java `
  app/src/main/java/com/flynes/emu/input/GamepadInputState.java `
  app/src/test/java/com/flynes/emu/input/ContinuousJoystickSessionTest.java `
  app/src/test/java/com/flynes/emu/input/JoystickDirectionTest.java
git commit -m "fix: keep joystick ownership across long drags"
```

### Task 2: 让 Android 事件、视觉与系统边缘策略使用同一会话

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Create: `app/src/androidTest/java/com/flynes/emu/GamepadJoystickContinuityTest.java`
- Modify: `app/src/main/res/values/strings.xml`
- Modify: `app/src/main/res/values-zh-rCN/strings.xml`

- [ ] **Step 1: 创建真实 MotionEvent 红测**

`GamepadJoystickContinuityTest` 使用 `ApplicationProvider` 创建 View、强制默认 JOYSTICK 设置并 `layout(0, 0, 2340, 1080)`。实现本地 `send(view, downTime, eventTime, action, x, y)` helper，并增加：

```java
private static final int DIRECTIONS = InputBits.UP | InputBits.DOWN
        | InputBits.LEFT | InputBits.RIGHT;

private static GamepadView joystickView() {
    Context context = ApplicationProvider.getApplicationContext();
    GamepadView view = new GamepadView(context);
    view.setControlSettings(AppSettings.defaults());
    view.measure(View.MeasureSpec.makeMeasureSpec(2340, View.MeasureSpec.EXACTLY),
            View.MeasureSpec.makeMeasureSpec(1080, View.MeasureSpec.EXACTLY));
    view.layout(0, 0, 2340, 1080);
    return view;
}

private static void send(GamepadView view, long downTime, long eventTime,
                         int action, float x, float y) {
    MotionEvent event = MotionEvent.obtain(downTime, eventTime, action, x, y, 0);
    view.onTouchEvent(event);
    event.recycle();
}

@Test public void dragCanLeaveOriginalBaseAndReverseWithoutAnotherDown() {
    GamepadView view = joystickView();
    GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
    long now = SystemClock.uptimeMillis();
    send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
    send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1800f, base.centerY());
    assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);
    send(view, now, now + 20, MotionEvent.ACTION_MOVE, 10f, base.centerY());
    assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
    send(view, now, now + 30, MotionEvent.ACTION_UP, 10f, base.centerY());
    assertEquals(0, view.buttons() & DIRECTIONS);
}
```

增加合并 MOVE 历史采样测试。先创建位于远右的 MOVE，再用 `addBatch` 把当前点放回初始中心；处理历史采样后最终应为 LEFT，忽略 history 的当前实现会得到 0：

```java
MotionEvent move = MotionEvent.obtain(now, now + 10, MotionEvent.ACTION_MOVE,
        1800f, base.centerY(), 0);
move.addBatch(now + 20, base.centerX(), base.centerY(), 1f, 1f, 0);
view.onTouchEvent(move);
assertEquals(InputBits.LEFT, view.buttons() & DIRECTIONS);
```

- [ ] **Step 2: 运行 Android 红测并记录预期失败**

Run:

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest `
  -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadJoystickContinuityTest `
  --console=plain
```

Expected: 当前 View 在中心 DOWN 不接管，划出后方向为 0；测试以断言失败结束，而不是设备/安装错误。

- [ ] **Step 3: 接入动态绘制、历史采样和正确释放语义**

`drawJoystick()` 从 `touchState.joystickVisual()` 读取实际中心与拨杆头，不再从离散按键位推算位置：

```java
GamepadInputState.JoystickVisual visual = touchState.joystickVisual();
float radius = hitMap.joystickRadius();
canvas.drawCircle(visual.centerX(), visual.centerY(), radius, fill);
canvas.drawCircle(visual.centerX(), visual.centerY(), radius, stroke);
float knobRadius = radius * .4375f;
canvas.drawCircle(visual.knobX(), visual.knobY(), knobRadius, fill);
canvas.drawCircle(visual.knobX(), visual.knobY(), knobRadius, stroke);
```

`ACTION_DOWN/POINTER_DOWN` 使用 `boolean accepted = touchState.down(...)`，接管成功时调用 `getParent().requestDisallowInterceptTouchEvent(true)`。`ACTION_MOVE` 先按 history index、再按当前采样处理所有 pointer，但每个 `MotionEvent` 只调用一次 `recompute()` 和一次方向反馈。`UP` 后仅对 `A/B/SELECT/START` 应用最短点击脉冲，方向位必须立即清零；最后一个 pointer 释放、`reset()` 或 `ACTION_CANCEL` 时恢复父容器拦截。

- [ ] **Step 4: 写 Insets 保活、视觉一致和手势排除红测**

在 Android 测试增加：

```java
@SdkSuppress(minSdkVersion = 29)
@Test public void insetsUpdateKeepsActiveDirectionAndVisualSession() {
    GamepadView view = joystickView();
    GamepadHitMap.Bounds base = view.hitMapForTest().dpadBounds();
    long now = SystemClock.uptimeMillis();
    send(view, now, now, MotionEvent.ACTION_DOWN, base.centerX(), base.centerY());
    send(view, now, now + 10, MotionEvent.ACTION_MOVE, 1500f, base.centerY());
    GamepadInputState.JoystickVisual before = view.joystickVisualForTest();

    WindowInsets changed = new WindowInsets.Builder()
            .setSystemWindowInsets(Insets.of(40, 20, 80, 30)).build();
    view.onApplyWindowInsets(changed);

    assertEquals(InputBits.RIGHT, view.buttons() & DIRECTIONS);
    assertTrue(view.joystickVisualForTest().active());
    assertTrue(before.active());
}

@SdkSuppress(minSdkVersion = 29)
@Test public void systemGestureExclusionIsBoundedToJoystickFootprint() {
    GamepadView view = joystickView();
    assertEquals(1, view.getSystemGestureExclusionRects().size());
    Rect exclusion = view.getSystemGestureExclusionRects().get(0);
    assertTrue(exclusion.width() < view.getWidth() / 2);
    assertTrue(exclusion.height() < view.getHeight());
}
```

同时断言动态视觉的 `knob-center` 距离不超过 `hitMap.joystickTravelRadius()`，并复用现有 `GamepadCancelTest` 验证真实取消清零。

- [ ] **Step 5: 保留 Insets 会话并更新有界系统手势排除区**

把重建拆成 `rebuildHitMap(boolean preserveTouch)`：尺寸/模式变化显式 reset 后创建新状态；`onApplyWindowInsets()` 构建新 map 后调用 `touchState.reconfigure(newMap)`。不允许 Insets 路径替换活动 `GamepadInputState`。

在 API 29+ 根据空闲锚点或活动中心设置一个矩形：垂直范围是 `centerY ± (radius + 8dp)`，水平方向只从左侧安全边缘延伸到 `centerX + radius + 8dp`，并夹紧到 View；DPAD 模式设置空列表。尺寸、Insets、会话移动和结束后都更新该列表。

- [ ] **Step 6: 更新准确的模式文案**

```xml
<!-- values/strings.xml -->
<string name="direction_joystick">Floating joystick (recommended)</string>

<!-- values-zh-rCN/strings.xml -->
<string name="direction_joystick">跟手浮动拨杆（推荐）</string>
```

- [ ] **Step 7: 运行目标 Android 测试、已有取消测试和编译检查**

Run:

```powershell
.\gradlew.bat :app:connectedDebugAndroidTest `
  -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadJoystickContinuityTest,com.flynes.emu.GamepadCancelTest `
  --console=plain
.\gradlew.bat :app:compileDebugJavaWithJavac :app:lintDebug --console=plain
```

Expected: MotionEvent 划出/划回、history、Insets、取消和手势排除全部 PASS；编译与 Lint 成功。

- [ ] **Step 8: 提交 Android 集成改动**

```powershell
git add -- `
  app/src/main/java/com/flynes/emu/GamepadView.java `
  app/src/androidTest/java/com/flynes/emu/GamepadJoystickContinuityTest.java `
  app/src/main/res/values/strings.xml `
  app/src/main/res/values-zh-rCN/strings.xml
git commit -m "fix: make the virtual joystick follow continuously"
```

### Task 3: 全量验证、合并与 Release 交付

**Files:**
- Build output: `app/build/outputs/apk/release/app-release-unsigned.apk`
- Build output: `app/build/outputs/apk/release/app-release-local-test.apk`

- [ ] **Step 1: 初始化固定子模块并确认工作树只有计划内提交**

```powershell
git submodule update --init --recursive
git status --short
git diff --check main...HEAD
```

Expected: NestopiaUE 位于仓库固定的 gitlink 提交；没有未提交源码，`git diff --check` 无输出。

- [ ] **Step 2: 运行完整自动化验证**

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:lintDebug :app:assembleDebug --console=plain
.\gradlew.bat :app:connectedDebugAndroidTest `
  -Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.GamepadJoystickContinuityTest,com.flynes.emu.GamepadCancelTest `
  --console=plain
.\gradlew.bat clean :app:assembleRelease --no-daemon --console=plain
```

Expected: 所有命令 `BUILD SUCCESSFUL`，最后生成未签名 Release APK。

- [ ] **Step 3: 用本机 Android 调试证书生成可安装的 Release 测试包**

使用固定的 build-tools 35.0.0；密钥口令只通过进程环境传递，不写入仓库或日志：

```powershell
$buildTools = 'C:\Users\pippin\AppData\Local\Android\Sdk\build-tools\35.0.0'
$unsigned = 'app\build\outputs\apk\release\app-release-unsigned.apk'
$aligned = 'app\build\outputs\apk\release\app-release-local-test-aligned.apk'
$signed = 'app\build\outputs\apk\release\app-release-local-test.apk'
$env:FLYNES_LOCAL_KEYSTORE_PASS = 'android'
$env:FLYNES_LOCAL_KEY_PASS = 'android'
& "$buildTools\zipalign.exe" -f -P 16 4 $unsigned $aligned
& "$buildTools\apksigner.bat" sign `
  --ks 'C:\Users\pippin\.android\debug.keystore' `
  --ks-key-alias androiddebugkey `
  --ks-pass env:FLYNES_LOCAL_KEYSTORE_PASS `
  --key-pass env:FLYNES_LOCAL_KEY_PASS `
  --out $signed $aligned
Remove-Item Env:FLYNES_LOCAL_KEYSTORE_PASS,Env:FLYNES_LOCAL_KEY_PASS
```

Expected: 生成 `app-release-local-test.apk`；该包是 Release 代码路径、Android Debug 证书签名，仅用于本地安装，不表述为商店生产包。

- [ ] **Step 4: 验证 APK 对齐、签名、内容和哈希**

```powershell
& "$buildTools\zipalign.exe" -c -P 16 4 $signed
& "$buildTools\apksigner.bat" verify --verbose --print-certs $signed
& 'C:\Users\pippin\AppData\Local\Android\Sdk\build-tools\35.0.0\aapt2.exe' dump badging $signed
$archive = [System.IO.Compression.ZipFile]::OpenRead((Resolve-Path $signed))
$nativeAbis = $archive.Entries.FullName |
  Where-Object { $_ -match '^lib/(arm64-v8a|x86_64)/' } |
  ForEach-Object { ($_ -split '/')[1] } | Sort-Object -Unique
$archive.Dispose()
if (($nativeAbis -join ',') -ne 'arm64-v8a,x86_64') { throw "Unexpected ABIs: $nativeAbis" }
Get-FileHash -Algorithm SHA256 $signed
```

Expected: zipalign 返回 0；签名至少验证 v2/v3；包名为 `com.flynes.emu`，APK 同时包含 `arm64-v8a` 和 `x86_64`；记录 SHA-256。

- [ ] **Step 5: 完成代码审查并合并回主干**

对 `main...codex/continuous-follow-joystick` 做规格符合性和代码质量审查；所有问题修复且验证重跑通过后，在主工作区执行非快进合并：

```powershell
git checkout main
git merge --no-ff codex/continuous-follow-joystick `
  -m "merge: continuous-follow virtual joystick"
```

Expected: 主干包含设计、计划、两次实现提交和合并提交；现有未跟踪 `docs/audits/` 仍未被暂存或修改。

- [ ] **Step 6: 从合并后的主干复核并安装在线设备**

在主干重新执行目标 JVM 测试和 `assembleRelease`，再生成并验证同名签名包。列出设备；vivo 在线时优先覆盖安装，否则安装当前 emulator 并保留真机待安装说明：

```powershell
$adb = 'C:\Users\pippin\AppData\Local\Android\Sdk\platform-tools\adb.exe'
& $adb devices -l
$serial = if ((& $adb devices) -match '^10ADBP18BQ0011Y\s+device$') {
  '10ADBP18BQ0011Y'
} elseif ((& $adb devices) -match '^emulator-5554\s+device$') {
  'emulator-5554'
} else {
  throw 'No authorized target device is online'
}
& $adb -s $serial install -r `
  'app\build\outputs\apk\release\app-release-local-test.apk'
& $adb -s $serial shell am start -W -n com.flynes.emu/.HomeActivity
```

Expected: `Success`，启动等待结果为 `Status: ok`。若证书不一致，停止并报告，不卸载应用、不清用户数据。

- [ ] **Step 7: 删除已合并 worktree 与功能分支**

先在主工作区确认合并和 clean 状态，再删除精确目标：

```powershell
git merge-base --is-ancestor codex/continuous-follow-joystick main
git worktree remove '.worktrees/continuous-follow-joystick'
git branch -d codex/continuous-follow-joystick
git worktree prune
git worktree list --porcelain
```

Expected: 只保留主工作树；不得处理未注册且被外部进程锁定的其他目录，也不得删除 `docs/audits/`。
