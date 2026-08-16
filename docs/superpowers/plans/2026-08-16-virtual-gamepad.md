# FlyNES 虚拟按键 Overlay 实现计划（Virtual Gamepad）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use `- [ ]` syntax.

**Goal:** 用手游风格（王者/吃鸡同款）的虚拟按键 Overlay 替换现有隐形触摸区：左下虚拟摇杆 + 右下 A/B 动作键 + 右上 START/SELECT 功能键 + START 弹暂停菜单，先在 Android 模拟器验证通过，再上真机。

**Architecture:** 纯 app 层改动，不碰 core/C ABI。新建单个自定义 View `GamepadView`（Canvas 绘制 + 多指触摸 + 摇杆 8 扇区数学 + 位掩码合成），替换并删除 `TouchController.java`；`MainActivity` 接线（输入回调 + 暂停菜单 + 移除旧 📚/ℹ️ 浮动按钮）。位掩码定义与 `core/include/nes/nes.h` 的 NES_BTN_* 一致，`onButtons` 接口签名保持兼容。

**Tech Stack:** Java 17 / Android View + Canvas（零第三方依赖、无 XML drawable）/ AGP 8.7.3 / Gradle 8.14.3 / 模拟器 MIT_Phone_API35 (x86_64)。

**依据:** `docs/superpowers/specs/2026-08-16-virtual-gamepad-design.md`（已批准）、`docs/superpowers/specs/2026-08-15-nes-emulator-design.md` §5 输入。验收方式（用户指示 2026-08-16）：**先模拟器验证通过、可真实游玩后，再上 vivo 真机**。

---

## 文件结构

```
app/src/main/java/com/flynes/emu/GamepadView.java   # 新建：Overlay（绘制+多指触摸+摇杆+位掩码+脉冲）
app/src/main/java/com/flynes/emu/MainActivity.java  # 修改：替换输入源、暂停菜单、移除旧浮动按钮
app/src/main/java/com/flynes/emu/TouchController.java # 删除（被替换的死代码）
```

验证产物（不提交）：模拟器截图 `app/build/emu-gamepad-*.png`（gitignored）、logcat 证据。

---

## Task T1: 新建 `GamepadView.java`（Overlay 完整实现）

**Files:**
- Create: `app/src/main/java/com/flynes/emu/GamepadView.java`

**设计要点（必须遵守）:**
- NES 位掩码常量与 `TouchController` 旧值一致：A=0x01 B=0x02 SELECT=0x04 START=0x08 UP=0x10 DOWN=0x20 LEFT=0x40 RIGHT=0x80。
- `Listener` 接口：`onButtons(int)`（按住态变化）+ `onPauseMenu()`（START 按下时）。
- 多指：`SparseArray<Pointer>`（key=pointerId）独立跟踪；同一控件已被占用时新指针忽略。
- 摇杆：底座固定左下常显；帽按下瞬移到手指、拖动限位（位移饱和）、松手回中；8 扇区（斜向=两键）；死区 = 0.15×底座半径。
- START/SELECT：脉冲（DOWN 置位 → 50ms 复位）；START 额外触发 `onPauseMenu()`。
- 所有尺寸 dp → px（`density`）；按压反馈 = 高亮填充 + 放大 1.1×。
- `reset()`：清空指针/脉冲/位掩码（供 onPause/onDestroy 调用）。

- [ ] **Step 1: 写完整实现**

```java
package com.flynes.emu;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.os.Handler;
import android.os.Looper;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;

/**
 * Virtual gamepad overlay for FlyNES (mobile-game style): fixed joystick at
 * bottom-left, A/B action buttons at bottom-right, START/SELECT at top-right.
 *
 * Every control is drawn with Canvas (no XML drawables) and every pointer is
 * tracked independently (SparseArray keyed by pointerId), so the left thumb
 * can steer while the right thumb presses A/B at the same time. The resulting
 * NES button bitmask is reported via {@link Listener#onButtons(int)} whenever
 * it changes; a START press additionally fires {@link Listener#onPauseMenu()}.
 */
public class GamepadView extends View {

    // NES button bitmask (matches core/include/nes/nes.h NES_BTN_*).
    public static final int A = 0x01;
    public static final int B = 0x02;
    public static final int SELECT = 0x04;
    public static final int START = 0x08;
    public static final int UP = 0x10;
    public static final int DOWN = 0x20;
    public static final int LEFT = 0x40;
    public static final int RIGHT = 0x80;

    /** Input + UI events; all callbacks arrive on the UI thread. */
    public interface Listener {
        /** Bitmask of currently-held NES buttons (directions + A/B + SELECT/START). */
        void onButtons(int buttons);
        /** A START press just happened; the host should show its pause menu. */
        void onPauseMenu();
    }

    private static final int PULSE_MS = 50;
    private static final int ROLE_NONE = 0;
    private static final int ROLE_JOY = 1;
    private static final int ROLE_B = 2;
    private static final int ROLE_A = 3;
    private static final int ROLE_START = 4;
    private static final int ROLE_SELECT = 5;

    private final float density;
    private final Handler handler = new Handler(Looper.getMainLooper());

    // Control sizes in px (dp * density, computed on size change).
    private float joyBaseR, joyKnobR, bR, aR, startW, startH, selR, gap;
    // Control centers in px.
    private float joyCX, joyCY, bCX, bCY, aCX, aCY, startCX, startCY, selCX, selCY;

    private final SparseArray<Pointer> pointers = new SparseArray<>(4);
    private int buttons = 0;
    private Listener listener;

    private static final class Pointer {
        int role = ROLE_NONE;
        float x, y;         // current position in view coords
        float knobX, knobY; // joystick knob offset from base center
    }

    public GamepadView(Context context) {
        super(context);
        density = context.getResources().getDisplayMetrics().density;
        setWillNotDraw(false);
    }

    public void setListener(Listener l) {
        this.listener = l;
    }

    /** Current NES button bitmask (also delivered via {@link Listener#onButtons}). */
    public int buttons() {
        return buttons;
    }

    /** Releases every held control and cancels pending pulses (pause/resume). */
    public void reset() {
        handler.removeCallbacksAndMessages(null);
        pointers.clear();
        if (buttons != 0) {
            buttons = 0;
            notifyButtons();
        }
        invalidate();
    }

    // ------------------------------------------------------------------ layout

    @Override
    protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        super.onSizeChanged(w, h, oldw, oldh);
        float dp = density;
        joyBaseR = 56 * dp;
        joyKnobR = 28 * dp;
        bR = 40 * dp;
        aR = 32 * dp;
        startW = 56 * dp;
        startH = 24 * dp;
        selR = 18 * dp;
        gap = 8 * dp;

        float margin = 24 * dp;
        float topMargin = 16 * dp;

        joyCX = margin + joyBaseR;
        joyCY = h - margin - joyBaseR;

        bCX = w - margin - bR;
        bCY = h - margin - bR;

        float dist = bR + aR + gap;
        double rad45 = Math.toRadians(45);
        aCX = bCX - (float) (dist * Math.cos(rad45));
        aCY = bCY - (float) (dist * Math.sin(rad45));

        startCX = w - margin - startW / 2f;
        startCY = topMargin + startH / 2f;

        selCX = startCX - startW / 2f - selR - gap;
        selCY = startCY;
    }

    // ------------------------------------------------------------------ drawing

    private final Paint fill = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint stroke = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final Paint label = new Paint(Paint.ANTI_ALIAS_FLAG);

    @Override
    protected void onDraw(Canvas c) {
        super.onDraw(c);

        Pointer joy = findRole(ROLE_JOY);
        Pointer b = findRole(ROLE_B);
        Pointer a = findRole(ROLE_A);
        Pointer start = findRole(ROLE_START);
        Pointer sel = findRole(ROLE_SELECT);

        // Joystick: fixed base + knob (knob follows the finger while held).
        drawCircle(c, joyCX, joyCY, joyBaseR, false, null);
        float knobX = joyCX, knobY = joyCY;
        if (joy != null) {
            knobX = joyCX + joy.knobX;
            knobY = joyCY + joy.knobY;
        }
        drawCircle(c, knobX, knobY, joyKnobR, true, null);

        // Action buttons.
        drawCircle(c, bCX, bCY, bR, b != null, "B");
        drawCircle(c, aCX, aCY, aR, a != null, "A");

        // Function buttons.
        drawRounded(c, startCX - startW / 2f, startCY - startH / 2f, startW, startH,
                start != null, "START");
        drawCircle(c, selCX, selCY, selR, sel != null, "SEL");
    }

    private void drawCircle(Canvas c, float cx, float cy, float r, boolean pressed, String text) {
        float scale = pressed ? 1.1f : 1f;
        float rr = r * scale;
        fill.setColor(pressed ? 0xAAFFFFFF : 0x55FFFFFF);
        fill.setStyle(Paint.Style.FILL);
        stroke.setColor(0xCCFFFFFF);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f * density);
        c.drawCircle(cx, cy, rr, fill);
        c.drawCircle(cx, cy, rr, stroke);
        if (text != null) {
            label.setColor(Color.WHITE);
            label.setTextSize((pressed ? 1.1f : 1f) * r * 0.8f);
            label.setTextAlign(Paint.Align.CENTER);
            Paint.FontMetrics fm = label.getFontMetrics();
            c.drawText(text, cx, cy - (fm.ascent + fm.descent) / 2f, label);
        }
    }

    private void drawRounded(Canvas c, float left, float top, float w, float h,
                             boolean pressed, String text) {
        float scale = pressed ? 1.08f : 1f;
        float ww = w * scale, hh = h * scale;
        float cx = left + w / 2f, cy = top + h / 2f;
        RectF rect = new RectF(cx - ww / 2f, cy - hh / 2f, cx + ww / 2f, cy + hh / 2f);
        fill.setColor(pressed ? 0xAAFFFFFF : 0x55FFFFFF);
        fill.setStyle(Paint.Style.FILL);
        stroke.setColor(0xCCFFFFFF);
        stroke.setStyle(Paint.Style.STROKE);
        stroke.setStrokeWidth(2f * density);
        c.drawRoundRect(rect, hh / 2f, hh / 2f, fill);
        c.drawRoundRect(rect, hh / 2f, hh / 2f, stroke);
        label.setColor(Color.WHITE);
        label.setTextSize((pressed ? 1.08f : 1f) * h * 0.5f);
        label.setTextAlign(Paint.Align.CENTER);
        Paint.FontMetrics fm = label.getFontMetrics();
        c.drawText(text, cx, cy - (fm.ascent + fm.descent) / 2f, label);
    }

    private Pointer findRole(int role) {
        for (int i = 0; i < pointers.size(); i++) {
            Pointer p = pointers.valueAt(i);
            if (p.role == role) return p;
        }
        return null;
    }

    // ------------------------------------------------------------------ input

    @Override
    public boolean onTouchEvent(MotionEvent e) {
        int action = e.getActionMasked();
        int index = e.getActionIndex();
        int id = e.getPointerId(index);

        switch (action) {
            case MotionEvent.ACTION_DOWN:
            case MotionEvent.ACTION_POINTER_DOWN: {
                float x = e.getX(index);
                float y = e.getY(index);
                int role = hitTest(x, y);
                if (role != ROLE_NONE && findRole(role) != null) {
                    role = ROLE_NONE; // control already held by another finger
                }
                Pointer p = new Pointer();
                p.role = role;
                p.x = x;
                p.y = y;
                if (role == ROLE_JOY) {
                    clampKnob(p);
                }
                pointers.put(id, p);
                if (role == ROLE_START) {
                    pulse(START);
                    if (listener != null) listener.onPauseMenu();
                } else if (role == ROLE_SELECT) {
                    pulse(SELECT);
                }
                recompute();
                break;
            }
            case MotionEvent.ACTION_MOVE: {
                for (int i = 0; i < e.getPointerCount(); i++) {
                    Pointer p = pointers.get(e.getPointerId(i));
                    if (p == null) continue;
                    p.x = e.getX(i);
                    p.y = e.getY(i);
                    if (p.role == ROLE_JOY) {
                        clampKnob(p);
                    }
                }
                recompute();
                break;
            }
            case MotionEvent.ACTION_UP:
            case MotionEvent.ACTION_POINTER_UP:
            case MotionEvent.ACTION_CANCEL: {
                pointers.remove(id);
                recompute();
                break;
            }
        }
        return true;
    }

    /** Assigns a control role to a touch point (action zone checked first). */
    private int hitTest(float x, float y) {
        float pad = 8 * density;
        if (dist2(x, y, bCX, bCY) <= bR * bR * 1.6f) return ROLE_B;
        if (dist2(x, y, aCX, aCY) <= aR * aR * 1.6f) return ROLE_A;
        if (x >= startCX - startW / 2f - pad && x <= startCX + startW / 2f + pad
                && y >= startCY - startH / 2f - pad && y <= startCY + startH / 2f + pad) {
            return ROLE_START;
        }
        if (dist2(x, y, selCX, selCY) <= selR * selR * 1.6f) return ROLE_SELECT;
        if (dist2(x, y, joyCX, joyCY) <= joyBaseR * joyBaseR * 4f) return ROLE_JOY;
        return ROLE_NONE;
    }

    private static float dist2(float x1, float y1, float x2, float y2) {
        float dx = x1 - x2, dy = y1 - y2;
        return dx * dx + dy * dy;
    }

    /** Clamps the knob offset to the travel limit (saturation: full direction). */
    private void clampKnob(Pointer p) {
        float dx = p.x - joyCX, dy = p.y - joyCY;
        float dist = (float) Math.hypot(dx, dy);
        float max = joyBaseR - joyKnobR * 0.5f;
        if (dist <= max || dist == 0f) {
            p.knobX = dx;
            p.knobY = dy;
        } else {
            float s = max / dist;
            p.knobX = dx * s;
            p.knobY = dy * s;
        }
    }

    /** START/SELECT fire a short pulse (down then auto-release). */
    private void pulse(int mask) {
        buttons |= mask;
        notifyButtons();
        handler.postDelayed(() -> {
            buttons &= ~mask;
            notifyButtons();
        }, PULSE_MS);
    }

    /** Recomputes the held-bitmask from live pointers and repaints. */
    private void recompute() {
        int b = 0;
        Pointer joy = findRole(ROLE_JOY);
        if (joy != null) {
            b |= joystickBits(joy.knobX, joy.knobY, joyBaseR);
        }
        if (findRole(ROLE_B) != null) b |= B;
        if (findRole(ROLE_A) != null) b |= A;
        b |= buttons & (START | SELECT); // pulse bits survive recompute
        if (b != buttons) {
            buttons = b;
            notifyButtons();
        }
        invalidate();
    }

    /**
     * Maps a joystick knob offset (px) to NES direction bits.
     * 8 sectors; diagonals set both keys; dead zone below 15% of base radius.
     */
    static int joystickBits(float knobX, float knobY, float baseRadius) {
        float dist = (float) Math.hypot(knobX, knobY);
        if (dist < 0.15f * baseRadius) return 0;
        double deg = Math.toDegrees(Math.atan2(knobY, knobX)); // 0=right, 90=down
        if (deg < -157.5 || deg >= 157.5) return LEFT;
        if (deg < -112.5) return UP | LEFT;
        if (deg < -67.5) return UP;
        if (deg < -22.5) return UP | RIGHT;
        if (deg < 22.5) return RIGHT;
        if (deg < 67.5) return DOWN | RIGHT;
        if (deg < 112.5) return DOWN;
        if (deg < 157.5) return DOWN | LEFT;
        return LEFT;
    }

    private void notifyButtons() {
        if (listener != null) listener.onButtons(buttons);
    }
}
```

- [ ] **Step 2: 编译门禁**

Run: `.\gradlew.bat :app:compileDebugJavaWithJavac`
Expected: BUILD SUCCESSFUL（此时 GamepadView 未被引用，Java 编译仍会编译该类——javac 编译 src 下所有类；若报错修到绿）。

- [ ] **Step 3: 提交**

```bash
git add app/src/main/java/com/flynes/emu/GamepadView.java
git commit -m "feat: add GamepadView virtual overlay (joystick + A/B + START/SELECT + pause hook)"
```

---

## Task T2: `MainActivity` 接线（替换输入源 + 暂停菜单 + 移除旧浮动按钮）

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`

**改动清单:**
1. 字段：`private TouchController touch;` → `private GamepadView gamepad;`
2. imports：加 `android.app.AlertDialog`、`android.os.Handler`、`android.os.Looper`；删 `android.widget.TextView`（不再用）。
3. `onCreate`：删除 `TouchController touch = new TouchController(); touch.setListener(this); view.setOnTouchListener(touch);`，改为创建 `GamepadView`（listener 用匿名类：`onButtons` → log + `core.setInput`；`onPauseMenu` → `showPauseMenu()`），并作为最上层子 View 加入 root（先 addView(view) 再加 gamepad）。
4. 删除 `infoButton`（ℹ️ 许可）与 `libraryButton`（📚 游戏库）两个代码块及其 LayoutParams —— 功能收进暂停菜单。
5. 类声明 `implements TouchController.Listener` → 删除（listener 走匿名类）。
6. 删除旧 `onButtons` 方法。
7. 新增 `showPauseMenu()` / `pressStart()`。
8. `onPause()` 与 `onDestroy()` 开头加 `gamepad.reset()`。

- [ ] **Step 1: imports 调整**

在 `import android.app.Activity;` 后插入：

```java
import android.app.AlertDialog;
```

在 `import android.os.Bundle;` 后插入：

```java
import android.os.Handler;
import android.os.Looper;
```

删除行 `import android.widget.TextView;`。

- [ ] **Step 2: 类声明与字段**

`public class MainActivity extends Activity implements TouchController.Listener {` →

```java
public class MainActivity extends Activity {
```

字段 `private EmuView view;` 后加：

```java
    private GamepadView gamepad;
```

- [ ] **Step 3: onCreate 输入接线块替换**

删除：

```java
        view = new EmuView(this);
        TouchController touch = new TouchController();
        touch.setListener(this);
        view.setOnTouchListener(touch);
```

替换为：

```java
        view = new EmuView(this);
        gamepad = new GamepadView(this);
        gamepad.setListener(new GamepadView.Listener() {
            @Override
            public void onButtons(int buttons) {
                // Debug aid: the adb-injection verification asserts these lines.
                Log.d(TAG, "input=0x" + Integer.toHexString(buttons));
                core.setInput(buttons);
            }

            @Override
            public void onPauseMenu() {
                showPauseMenu();
            }
        });
```

- [ ] **Step 4: 删除旧浮动按钮 + 加入 gamepad 层**

删除从 `float density = getResources().getDisplayMetrics().density;` 起到 `root.addView(libraryButton, libraryLp);` 结束的整块（infoButton 与 libraryButton 两个 TextView 及其 LayoutParams）。

保留：

```java
        FrameLayout root = new FrameLayout(this);
        root.addView(view, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
```

在这之后（setContentView 之前）加：

```java
        // Gamepad overlay sits above the game surface and owns all touch input.
        root.addView(gamepad, new FrameLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.MATCH_PARENT));
```

> 注意：删除的 `density` 局部变量若下方还有引用会编译报错——已确认 `density` 只被那两个按钮块使用，可安全删除。

- [ ] **Step 5: 删除旧 onButtons**

删除：

```java
    @Override
    public void onButtons(int buttons) {
        core.setInput(buttons);
    }
```

- [ ] **Step 6: 新增暂停菜单方法**

在 `toastAndFinish` 方法前插入：

```java
    // ------------------------------------------------------------------
    // Pause menu (opened by the gamepad START key)
    // ------------------------------------------------------------------

    private void showPauseMenu() {
        if (isFinishing() || gamepad == null) return;
        new AlertDialog.Builder(this)
                .setTitle("FlyNES")
                .setItems(new String[]{"继续游戏", "游戏库", "许可信息", "取消"}, (d, which) -> {
                    switch (which) {
                        case 0:
                            d.dismiss();
                            pressStart();
                            break;
                        case 1:
                            d.dismiss();
                            startActivityForResult(new Intent(this, GameLibraryActivity.class), REQ_LIBRARY);
                            break;
                        case 2:
                            d.dismiss();
                            startActivity(new Intent(this, LicensesActivity.class));
                            break;
                        default:
                            d.dismiss();
                            break;
                    }
                })
                .setOnCancelListener(d -> {
                    // Back / outside-tap dismiss: stay paused in-game, user's choice.
                })
                .show();
    }

    /** Sends one START pulse to resume from the NES game's own pause state. */
    private void pressStart() {
        core.setInput(GamepadView.START);
        new Handler(Looper.getMainLooper()).postDelayed(
                () -> core.setInput(gamepad.buttons()), 50);
    }
```

- [ ] **Step 7: 生命周期复位**

`onPause()` 中 `stopRendering();` 之后、`if (!stopAudioThread()) {` 之前插入：

```java
        gamepad.reset();
```

`onDestroy()` 中 `stopRendering();` 之后、`if (audio != null && audio.isAlive()) {` 之前插入：

```java
        gamepad.reset();
```

- [ ] **Step 8: 编译**

Run: `.\gradlew.bat :app:compileDebugJavaWithJavac`
Expected: BUILD SUCCESSFUL（若报 `gamepad` 可能为 null 的 lint/编译问题，检查 Step 6 的 `gamepad == null` 守卫与 Step 7 的调用点均在 onCreate 之后）。

- [ ] **Step 9: 提交**

```bash
git add app/src/main/java/com/flynes/emu/MainActivity.java
git commit -m "feat: wire GamepadView into MainActivity with pause menu (joystick input + library/license entries)"
```

---

## Task T3: 删除 TouchController + 完整构建

**Files:**
- Delete: `app/src/main/java/com/flynes/emu/TouchController.java`

- [ ] **Step 1: 删除死代码**

```bash
git rm app/src/main/java/com/flynes/emu/TouchController.java
```

- [ ] **Step 2: 确认无残留引用**

```bash
grep -ri "TouchController" app/src
```

Expected: 无输出（无引用）。

- [ ] **Step 3: 完整构建**

Run: `.\gradlew.bat assembleDebug`
Expected: BUILD SUCCESSFUL，产物 `app/build/outputs/apk/debug/app-debug.apk`。

- [ ] **Step 4: 提交**

```bash
git commit -m "refactor: remove TouchController (superseded by GamepadView)"
```

---

## Task T4: 模拟器验证（构建 → 启动 → 注入触摸 → 截图 → 回归）

**Files:**
- 无代码改动；验证产物 `app/build/emu-gamepad-*.png`（gitignored，不提交）。

> 环境事实：AVD `MIT_Phone_API35`（x86_64）已存在；APK 含 x86_64 ABI；真机 vivo 未连接（本任务不依赖真机）。

- [ ] **Step 1: 启动模拟器（后台）**

Run（后台 job，勿阻塞）:

```powershell
& "$env:ANDROID_HOME\emulator\emulator.exe" -avd MIT_Phone_API35 -no-snapshot-load -no-boot-anim
```

Wait: `adb wait-for-device` 后轮询 `adb shell getprop sys.boot_completed` 直到输出 `1`（最多 ~180s）。

- [ ] **Step 2: 安装并启动 App**

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.flynes.emu/.MainActivity
```

Expected: App 启动，无 crash（`adb logcat -d -s AndroidRuntime:E` 无 FATAL）。

- [ ] **Step 3: 截图 1 —— 静止状态**

```bash
adb shell screencap -p /sdcard/emu-gamepad-1.png
adb pull /sdcard/emu-gamepad-1.png app/build/emu-gamepad-1.png
```

Expected（人工查看或用像素工具）：摇杆底座（左下）、B/A（右下）、START/SEL（右上）均渲染；半透明风格；游戏画面可见。

- [ ] **Step 4: 读取屏幕尺寸用于注入坐标**

```bash
adb shell wm size
adb shell wm density
```

记录 `WxH` 与 `densityDpi`。坐标换算公式（dp → px: px = dp × densityDpi/160）：
- 摇杆中心 ≈ `(24+56)dp, (Hpx - (24+56)dp)`
- B 中心 ≈ `(Wpx - (24+40)dp, Hpx - (24+40)dp)`
- A 中心 ≈ B 中心向左上 45° 移 `(40+32+8)dp`
- START 中心 ≈ `(Wpx - 24dp - 28dp, 16dp + 12dp)`

（densityDpi=420 时：摇杆≈(210, H-210)，B≈(W-168, H-168)，START≈(W-136, 74)。以实际 wm 输出为准。）

- [ ] **Step 5: 注入触摸 + 验证位掩码（logcat 断言）**

```bash
adb logcat -c
# 摇杆向右拖（约 100dp ≈ 260px @420dpi）
adb shell input swipe <joyX> <joyY> <joyX+260> <joyY> 300
adb shell input tap <BX> <BY>     # B
adb shell input tap <AX> <AY>     # A
adb shell input tap <startX> <startY>  # START → 菜单
adb logcat -d -s FlyNES:* | grep "input=0x"
```

Expected 序列（顺序近似）：
- 摇杆右拖 → `input=0x80`（RIGHT）
- tap B → `input=0x82`（RIGHT|B）
- tap A → `input=0x81`（RIGHT|A，B 已松开）
- tap START → 出现 `input=0x8` 脉冲且之后恢复，同时菜单弹出（下一步截图证明）
- 斜向断言（可选）：`input swipe` 向右上 45° → `input=0x90`（RIGHT|UP）

- [ ] **Step 6: 截图 2 —— 暂停菜单**

```bash
adb shell screencap -p /sdcard/emu-gamepad-2.png
adb pull /sdcard/emu-gamepad-2.png app/build/emu-gamepad-2.png
```

Expected: 截图含暂停菜单（继续游戏/游戏库/许可信息/取消）。

- [ ] **Step 7: 菜单路径验证**

```bash
# 点"继续游戏"（菜单第一项，坐标按截图估；或 adb shell input keyevent 66 确认默认项）
adb shell input tap <menuItem1X> <menuItem1Y>
```

Expected: 菜单关闭，logcat 出现一次 `input=0x8`（pressStart 脉冲），游戏继续。
再点 START → 菜单 → 点"游戏库"（第二项）→ Expected: 打开 GameLibraryActivity（`adb shell dumpsys activity top | grep GameLibrary` 或截图确认）。返回键回游戏。

- [ ] **Step 8: 多指回归**

```bash
# 模拟双指：先按下 B（input swipe 用长停留？）—— adb 单流限制；改用手动验证：
```

说明：adb `input` 无法模拟真双指。手动（模拟器窗口鼠标左键按住 B，同时触控板/鼠标另一输入拖摇杆）或在 GUI 上人工验证"左手摇杆 + 右手 B 同时有效"。若人工不可行，记录为「真机阶段验证」——不阻塞本次验收。

- [ ] **Step 9: 长跑回归（简版）**

```bash
# 模拟器上挂 3 分钟不操作，期间每秒截图一次对比（可选）；确认无 ANR/crash
adb logcat -d -s AndroidRuntime:E FlyNES:*
```

Expected: 无 FATAL/ANR；`input=0x0` 稳态。

- [ ] **Step 10: 验收记录 + 提交**

把截图路径与 logcat 断言结果写进 `docs/superpowers/specs/2026-08-16-virtual-gamepad-design.md` 的 §8 验收记录小节（追加，不覆盖设计正文）：

```markdown
### 验收记录（模拟器 MIT_Phone_API35, 2026-08-16）
- 构建: assembleDebug PASS
- 截图: app/build/emu-gamepad-1.png（静止）、emu-gamepad-2.png（暂停菜单）
- logcat: 摇杆右拖 0x80、B 0x82、A 0x81、START 0x8 脉冲、斜向 0x90 — 全部符合预期
- 回归: 无 crash/ANR；菜单三路径（继续/游戏库/许可）可用
- 待真机: 双指同时操作、音频 60fps 时序（真机阶段再做）
```

```bash
git add docs/superpowers/specs/2026-08-16-virtual-gamepad-design.md
git commit -m "docs: record emulator acceptance for virtual gamepad overlay"
```

---

## 自检要点（执行时逐项核对）

- **Spec 覆盖**：§3 布局 ✓（T1）、§4.1 摇杆 8 扇区+死区 ✓（T1 `joystickBits`）、§4.2 A/B ✓、§4.3 START/SELECT 脉冲 ✓（T1 `pulse`）、§5.1 多指 ✓（SparseArray）、§5.2 位掩码叠加 ✓（T1 `recompute`）、§5.3 暂停菜单 ✓（T2 `showPauseMenu`）、§5.4 生命周期复位 ✓（T2 Step 7）、§7 边界（控件占用忽略新指针 ✓、摇杆饱和 ✓、dialog 取消 ✓）。
- **类型一致性**：`Listener.onButtons/onPauseMenu` 在 T1 定义、T2 使用，签名一致；`GamepadView.START` 在 T2 `pressStart` 引用；位掩码值与旧 TouchController/NES_BTN_* 一致。
- **编译顺序**：T1 单独可编译 → T2 引用 GamepadView → T3 删旧类；每步提交。
- **真机待办**（本批不阻塞，留给真机阶段）：双指注入（adb 无法模拟双指）、音频 60fps 时序、真机布局手感（摇杆尺寸是否偏大/偏小由用户游玩反馈）。
