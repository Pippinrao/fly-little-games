# Dynamic Joystick Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 让 FlyNES 左侧摇杆在左半屏可动态起控，并在拖动到行程边缘时持续跟随手指。

**Architecture:** `GamepadView` 保留固定的静止锚点，并为正在操纵摇杆的指针保存动态中心。输入命中改为左半屏操作区，位移和方向都相对动态中心计算；触点越过行程极限时，中心同步补位。

**Tech Stack:** Android Java、Canvas、MotionEvent、现有 Gradle 单元测试。

---

### Task 1: 覆盖动态中心与边缘跟随的数学

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Test: `app/src/test/java/com/flynes/emu/GamepadViewTest.java`

- [x] **Step 1: 编写失败测试**

新增纯数学 helper 的测试：给定初始中心和超出行程的触点，断言中心向触点移动后，摇杆帽位移不超过 `baseRadius - knobRadius * 0.5f`；同时测试未超过行程的触点不移动中心。

- [x] **Step 2: 运行测试并确认失败**

Run: `./gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.GamepadViewTest`

Expected: FAIL，因为动态中心 helper 尚不存在。

- [x] **Step 3: 实现最小数学 helper**

将动态中心移动量抽取为包可见静态方法，计算超出最大行程的向量部分；`clampKnob` 使用补位后的中心后再限制帽位移。

- [x] **Step 4: 运行测试并确认通过**

Run: `./gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.GamepadViewTest`

Expected: PASS。

### Task 2: 接入动态起控、绘制与复位

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GamepadView.java`
- Test: `app/src/test/java/com/flynes/emu/GamepadViewTest.java`

- [x] **Step 1: 编写失败测试**

新增左半屏命中测试，断言远离固定锚点的左半屏触点分配为 `ROLE_JOY`，右半屏对应点不分配摇杆；新增动态中心复位 helper 测试。

- [x] **Step 2: 运行测试并确认失败**

Run: `./gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.GamepadViewTest`

Expected: FAIL，因为当前命中逻辑只接受固定底座四倍半径内的触点。

- [x] **Step 3: 实现最小触摸改动**

在 `Pointer` 记录动态中心；摇杆按下时把中心设为触点并限制到左半屏安全区，绘制和方向映射使用该中心。抬起、取消与 `reset()` 后丢弃动态中心并显示固定锚点。

- [x] **Step 4: 运行针对性测试；APK 构建受未初始化子模块阻断**

Run: `./gradlew.bat :app:testDebugUnitTest --tests com.flynes.emu.GamepadViewTest :app:assembleDebug`

Expected: BUILD SUCCESSFUL。

- [ ] **Step 5: 提交**

Run: `git add app/src/main/java/com/flynes/emu/GamepadView.java app/src/test/java/com/flynes/emu/GamepadViewTest.java docs/superpowers/specs/2026-08-26-dynamic-joystick-design.md docs/superpowers/plans/2026-08-26-dynamic-joystick.md && git commit -m "feat: improve virtual joystick tracking"`
