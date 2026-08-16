# FlyNES hq4x 滤镜 + 游戏库美化 实现计划

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement this plan task-by-task. Steps use `- [ ]` syntax.

**Goal:** 启用 core 层 hq 系列滤镜（默认 hq4x，1024×960 平滑画面）并美化游戏库界面（深色渐变+圆角卡片+胶囊控件），保持零依赖与 ABI 兼容。

**Architecture:** core 层：`nes_video_filter` 枚举追加 HQ2X/3X/4X（ABI 追加安全），`nes_set_video_format` 放开实现，framebuffer 按倍率分配（hq4x=1024×960），`nes_run_frames` 的 Output pitch 与 `nes_get_video_frame` 尺寸同步放大——NestopiaUE 滤镜由 Renderer 内部处理，只需喂对缓冲。app 层：JNI +Java 加 `setVideoFilter`，MainActivity 默认 hq4x 且 blit scale=1（1:1 拷贝）；`GameLibraryActivity` 纯代码美化（GradientDrawable 圆角/渐变/胶囊，无新依赖）。

**Tech Stack:** C++17 / NestopiaUE 1.53.2（内置 FilterHqX）/ zlib / Java 17 / AGP 8.7.3 / 模拟器 MIT_Phone_API35。

**依据:** `docs/superpowers/specs/2026-08-17-hq4x-filter-game-library-ui-design.md`（已批准）。

---

## 文件结构

```
core/include/nes/nes.h                 # +NES_FILTER_HQ2X/HQ3X/HQ4X 枚举（追加）
core/src/nes_core.cpp                  # nes_set_video_format 放开 filter；framebuffer 按倍率分配；pitch/尺寸同步
core/tests/test_core.cpp               # +滤镜尺寸断言
app/src/main/cpp/nes_jni.cpp           # +nativeSetVideoFilter
app/src/main/java/com/flynes/emu/NesCore.java   # +setVideoFilter + 滤镜常量
app/src/main/java/com/flynes/emu/MainActivity.java # 默认 hq4x；scale 逻辑调整
app/src/main/java/com/flynes/emu/GameLibraryActivity.java # UI 美化（纯代码）
```

---

## Task F1: core 层滤镜支持

**Files:**
- Modify: `core/include/nes/nes.h`、`core/src/nes_core.cpp`、`core/tests/test_core.cpp`

- [ ] **Step 1: nes.h 追加枚举**（现有：`NES_FILTER_NONE = 0, NES_FILTER_NTSC = 1` 之后追加，值不可重排）

```c
typedef enum nes_video_filter {
    NES_FILTER_NONE = 0,        /* Nes::Api::Video::RenderState::FILTER_NONE */
    NES_FILTER_NTSC = 1,        /* 保留（后续放开，本次不实现） */
    NES_FILTER_HQ2X = 2,        /* RenderState::FILTER_HQ2X */
    NES_FILTER_HQ3X = 3,        /* RenderState::FILTER_HQ3X */
    NES_FILTER_HQ4X = 4         /* RenderState::FILTER_HQ4X */
} nes_video_filter;
```

- [ ] **Step 2: nes_core.cpp 增加 filter 倍率辅助**（`apply_render_state` 前的 anonymous namespace 内）：

```cpp
// 滤镜输出倍率（与 RenderState::FILTER_* 对应；NONE=1, HQ2X=2, HQ3X=3, HQ4X=4）
int filter_scale(nes_video_filter filter)
{
    switch (filter)
    {
        case NES_FILTER_HQ2X: return 2;
        case NES_FILTER_HQ3X: return 3;
        case NES_FILTER_HQ4X: return 4;
        case NES_FILTER_NONE:
        default:              return 1;
    }
}

// filter 枚举 → RenderState::Filter；不支持(NTSC)返回 -1
int filter_to_render(nes_video_filter filter)
{
    switch (filter)
    {
        case NES_FILTER_NONE: return Nes::Api::Video::RenderState::FILTER_NONE;
        case NES_FILTER_HQ2X: return Nes::Api::Video::RenderState::FILTER_HQ2X;
        case NES_FILTER_HQ3X: return Nes::Api::Video::RenderState::FILTER_HQ3X;
        case NES_FILTER_HQ4X: return Nes::Api::Video::RenderState::FILTER_HQ4X;
        default:              return -1;
    }
}
```

- [ ] **Step 3: apply_render_state 设置 filter**

`rs.filter = Nes::Api::Video::RenderState::FILTER_NONE;` →

```cpp
        const int rf = filter_to_render(ctx->cfg.filter);
        rs.filter = rf >= 0 ? rf : Nes::Api::Video::RenderState::FILTER_NONE;
```

（`ctx->cfg.filter` 字段若不存在，在 `nes_config` 中追加 `nes_video_filter filter;` 并默认 NONE——见 Step 5。）

- [ ] **Step 4: update_video_frame 按倍率报尺寸**

```cpp
        ctx->video_frame.width  = static_cast<int32_t>(kScreenWidth  * filter_scale(ctx->cfg.filter));
        ctx->video_frame.height = static_cast<int32_t>(kScreenHeight * filter_scale(ctx->cfg.filter));
        ctx->video_frame.pitch  = ctx->video_frame.width * pixfmt_bpp(ctx->cfg.pixfmt);
```

- [ ] **Step 5: nes_set_video_format 放开 filter + 按倍率分配 framebuffer**

当前实现（约 line 864-879）：
```cpp
    // phase0: 仅 RGB565 + FILTER_NONE
    if (format != NES_PIXFMT_RGB565 || filter != NES_FILTER_NONE)
        return NES_ERR_NOT_IMPLEMENTED;

    const size_t fb_size = kScreenWidth * kScreenHeight * pixfmt_bpp(format);
    if (fb_size != ctx->framebuffer_size)
```
改为：
```cpp
    // 仅 RGB565 + NONE/HQ 系列（NTSC 未实现）；非法组合返回 NOT_IMPLEMENTED
    const int scale = filter_scale(filter);
    if (format != NES_PIXFMT_RGB565 || scale <= 0 || filter_to_render(filter) < 0)
        return NES_ERR_NOT_IMPLEMENTED;

    ctx->cfg.filter = filter;

    const size_t fb_size = static_cast<size_t>(kScreenWidth * scale)
                         * static_cast<size_t>(kScreenHeight * scale)
                         * pixfmt_bpp(format);
    if (fb_size != ctx->framebuffer_size)
```
（后续 realloc 分支保持既有逻辑不变；`cfg.filter` 需要时在 `nes_config` 结构加字段并初始化默认 NONE。）

- [ ] **Step 6: nes_run_frames 的 Output pitch 按倍率**

`Nes::Core::Video::Output vo(ctx->framebuffer, static_cast<long>(kScreenWidth * pixfmt_bpp(ctx->cfg.pixfmt)));` →

```cpp
    const int scale = filter_scale(ctx->cfg.filter);
    Nes::Core::Video::Output vo(
        ctx->framebuffer,
        static_cast<long>(kScreenWidth * scale * pixfmt_bpp(ctx->cfg.pixfmt)));
```

- [ ] **Step 7: 宿主测试扩展**（`core/tests/test_core.cpp`，在既有视频段后追加）：

```cpp
    // 滤镜尺寸：默认 NONE = 256x240
    nes_video_frame vf; memset(&vf, 0, sizeof(vf)); vf.struct_size = sizeof(vf);
    rc = nes_get_video_frame(ctx, &vf);
    assert(rc >= 0 && vf.width == 256 && vf.height == 240);

    // hq4x → 1024x960，跑帧正常
    rc = nes_set_video_format(ctx, NES_PIXFMT_RGB565, NES_FILTER_HQ4X);
    assert(rc >= 0);
    rc = nes_get_video_frame(ctx, &vf);
    assert(rc >= 0 && vf.width == 1024 && vf.height == 960 && vf.pixels != NULL);
    rc = nes_run_frames(ctx, 2, audio, cap, &fr, &sw);
    assert(rc >= 0 && fr == 2);

    // hq2x → 512x480
    rc = nes_set_video_format(ctx, NES_PIXFMT_RGB565, NES_FILTER_HQ2X);
    assert(rc >= 0);
    rc = nes_get_video_frame(ctx, &vf);
    assert(rc >= 0 && vf.width == 512 && vf.height == 480);

    // 回 NONE → 256x240
    rc = nes_set_video_format(ctx, NES_PIXFMT_RGB565, NES_FILTER_NONE);
    assert(rc >= 0);
    rc = nes_get_video_frame(ctx, &vf);
    assert(rc >= 0 && vf.width == 256 && vf.height == 240);

    // 非法 filter（NTSC 未实现）
    rc = nes_set_video_format(ctx, NES_PIXFMT_RGB565, NES_FILTER_NTSC);
    assert(rc == NES_ERR_NOT_IMPLEMENTED);
```

- [ ] **Step 8: 宿主测试 + 构建**

Run: `.\gradlew.bat` 不适用（core 独立）——用 core 宿主构建：
```powershell
cmake --build core/build/host --target nes_core_test 2>&1 | Select-Object -Last 5
core/build/host/nes_core_test.exe
```
Expected: 全部 PASS（含新滤镜断言）。

- [ ] **Step 9: 提交**

```bash
git add core/include/nes/nes.h core/src/nes_core.cpp core/tests/test_core.cpp
git commit -m "feat: enable HQ video filters in core (framebuffer scales 2x/3x/4x)"
```

---

## Task F2: JNI + Java 滤镜接口

**Files:**
- Modify: `app/src/main/cpp/nes_jni.cpp`、`app/src/main/java/com/flynes/emu/NesCore.java`

- [ ] **Step 1: nes_jni.cpp 增加 native**

在 `nativeSetAudioFormat` 后：

```cpp
JNIEXPORT void JNICALL
Java_com_flynes_emu_NesCore_nativeSetVideoFilter(JNIEnv*, jclass, jlong handle, jint filter)
{
    nes_t* ctx = reinterpret_cast<nes_t*>(handle);
    if (ctx)
        nes_set_video_format(ctx, NES_PIXFMT_RGB565, static_cast<nes_video_filter>(filter));
}
```

- [ ] **Step 2: NesCore.java 增加常量与方法**

```java
    /** Video filter constants (match core/include/nes/nes.h nes_video_filter). */
    public static final int FILTER_NONE = 0;
    public static final int FILTER_HQ2X = 2;
    public static final int FILTER_HQ3X = 3;
    public static final int FILTER_HQ4X = 4;

    private static native void nativeSetVideoFilter(long h, int filter);

    /** @param filter one of {@link #FILTER_NONE} / {@link #FILTER_HQ2X} / {@link #FILTER_HQ3X} / {@link #FILTER_HQ4X} */
    public void setVideoFilter(int filter) {
        if (handle != 0) nativeSetVideoFilter(handle, filter);
    }
```

- [ ] **Step 3: 编译**

Run: `.\gradlew.bat :app:compileDebugJavaWithJavac --console=plain -q`
Expected: BUILD SUCCESSFUL。

- [ ] **Step 4: 提交**

```bash
git add app/src/main/cpp/nes_jni.cpp app/src/main/java/com/flynes/emu/NesCore.java
git commit -m "feat: expose HQ video filter selection through JNI/NesCore"
```

---

## Task F3: MainActivity 默认 hq4x + 缩放逻辑

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/MainActivity.java`

- [ ] **Step 1: onCreate 启用 hq4x**

`core.setAudioFormat(AUDIO_SAMPLE_RATE, 0);` 后加：

```java
        // HQ4X smooth filter: 256x240 -> 1024x960 in the core; blit 1:1 below.
        core.setVideoFilter(NesCore.FILTER_HQ4X);
```

- [ ] **Step 2: computeScale 适配 1024x960**

```java
    private int computeScale() {
        // The core framebuffer is already filter-scaled (hq4x = 1024x960);
        // blit 1:1 (scale=1) and let the SurfaceView fit via setBuffersGeometry.
        return 1;
    }
```

> 说明：`nativeBlit` 的 `setBuffersGeometry(win, 1024, 960, RGB565)` 会把 SurfaceView 内容按窗口尺寸显示；`EmuView` 是 MATCH_PARENT，SurfaceFlinger 自动等比缩放（画面居中，左右留黑边）。这避免在 JNI 再做一次缩放。

- [ ] **Step 3: 编译 + 提交**

```bash
git add app/src/main/java/com/flynes/emu/MainActivity.java
git commit -m "feat: default to hq4x filter (1024x960, blit 1:1)"
```

---

## Task F4: 游戏库界面美化（纯代码）

**Files:**
- Modify: `app/src/main/java/com/flynes/emu/GameLibraryActivity.java`

- [ ] **Step 1: 背景渐变 + 标题栏**

`buildUi()` 开头（root 创建处）：

```java
        // 深蓝黑渐变背景。
        GradientDrawable bg = new GradientDrawable(
                GradientDrawable.Orientation.TOP_BOTTOM,
                new int[]{0xFF0F2027, 0xFF203A43, 0xFF2C5364});
        root.setBackground(bg);
```

root 内（searchBox 之前）插入标题栏：

```java
        TextView title = new TextView(this);
        title.setText("🎮 FlyNES 游戏库");
        title.setTextSize(22f);
        title.setTextColor(0xFFFFFFFF);
        title.setTypeface(null, android.graphics.Typeface.BOLD);
        title.setPadding(pad, dp(16), pad, dp(4));
        root.addView(title, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        titleCount = new TextView(this);   // 新增字段
        titleCount.setTextColor(0xFF9FB6C9);
        titleCount.setTextSize(12f);
        titleCount.setPadding(pad, 0, pad, dp(8));
        root.addView(titleCount, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
```

- [ ] **Step 2: 搜索框圆角胶囊**

替换 searchBox 创建块（保留 TextWatcher 不变），在 `setPadding` 前加圆角背景：

```java
        searchBox.setBackground(roundedBox(this, 0x33000000, 0x66FFFFFF));
```

新增私有方法：

```java
    private static GradientDrawable roundedBox(Context c, int fill, int stroke) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp2(c, 24));
        d.setColor(fill);
        d.setStroke(1, stroke);
        return d;
    }
```

（注意：`dp()` 是实例方法，static 工具需要 `context.getResources().getDisplayMetrics().density`——或把 `roundedBox` 做成实例方法，本项目文件风格为实例 `dp()`，故 **Step 2 用实例方法**：`private GradientDrawable roundedBox(int fill, int stroke)` 内部用 `dp(24)`。）

- [ ] **Step 3: 排序分段胶囊**

替换 `makeSortButton` 返回的按钮样式：圆角、选中高亮。新增字段 `List<Button> sortButtons`，`makeSortButton` 内：

```java
    private Button makeSortButton(String label, final int mode) {
        Button b = new Button(this);
        b.setText(label);
        b.setTextSize(13f);
        b.setAllCaps(false);
        b.setPadding(dp(18), 0, dp(18), 0);
        b.setBackground(segmentedStyle(false));
        b.setOnClickListener(v -> {
            sortMode = mode;
            updateSortHighlight();
            applyFilterAndSort();
        });
        return b;
    }

    private void updateSortHighlight() {
        for (int i = 0; i < sortButtons.size(); i++) {
            sortButtons.get(i).setBackground(segmentedStyle(i == sortMode));
        }
    }

    private GradientDrawable segmentedStyle(boolean selected) {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(18));
        d.setColor(selected ? 0xFF4A90D9 : 0x22000000);
        return d;
    }
```

（`sortButtons` 在 `buildUi` 中填充；`updateSortHighlight()` 在 `buildUi` 末尾调用一次。）

- [ ] **Step 4: 列表项卡片化**

`GameAdapter.getView` 中 row 样式：

```java
                row = new LinearLayout(GameLibraryActivity.this);
                row.setOrientation(LinearLayout.VERTICAL);
                row.setPadding(dp(16), dp(10), dp(16), dp(10));
                row.setBackground(cardStyle());
```

新增：

```java
    private GradientDrawable cardStyle() {
        GradientDrawable d = new GradientDrawable();
        d.setCornerRadius(dp(12));
        d.setColor(0x22FFFFFF);
        return d;
    }
```

`ListView` 设置：`listView.setDivider(null); listView.setDividerHeight(0); listView.setPadding(dp(10), 0, dp(10), dp(10));`（卡片间距靠 padding 与 item 内 margin——ListView 无 margin，用 `listView.setVerticalScrollBarEnabled(false)` + item padding 16dp 形成间距感；若需要行间距，在 row 外包一层带 bottom padding 的容器，本项目取简单方案：row padding 顶部/底部各 10dp）。

- [ ] **Step 5: 空状态美化**

`emptyState` 内（chooseButton 前）加图标与标题：

```java
        TextView emptyIcon = new TextView(this);
        emptyIcon.setText("🎮");
        emptyIcon.setTextSize(64f);
        emptyIcon.setGravity(Gravity.CENTER);
        emptyState.addView(emptyIcon, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));

        TextView emptyTitle = new TextView(this);
        emptyTitle.setText("还没有游戏");
        emptyTitle.setTextSize(18f);
        emptyTitle.setTextColor(0xFFDDE6EE);
        emptyTitle.setGravity(Gravity.CENTER);
        emptyTitle.setPadding(0, dp(8), 0, 0);
        emptyState.addView(emptyTitle, new LinearLayout.LayoutParams(
                ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT));
```

chooseButton 美化：

```java
        chooseButton.setTextSize(16f);
        chooseButton.setAllCaps(false);
        GradientDrawable btnBg = new GradientDrawable();
        btnBg.setCornerRadius(dp(24));
        btnBg.setColor(0xFF4A90D9);
        chooseButton.setBackground(btnBg);
        chooseButton.setPadding(dp(32), dp(12), dp(32), dp(12));
```

- [ ] **Step 6: reloadData 更新计数**

`reloadData()` 内（hasUserGames 分支）与空态分支都更新：

```java
        titleCount.setText("共 " + allGames.size() + " 个游戏 · 从设备目录加载");
```

- [ ] **Step 7: 编译**

Run: `.\gradlew.bat :app:compileDebugJavaWithJavac --console=plain -q`
Expected: BUILD SUCCESSFUL（新增 import：`android.graphics.drawable.GradientDrawable`、`android.content.Context` 若需要）。

- [ ] **Step 8: 提交**

```bash
git add app/src/main/java/com/flynes/emu/GameLibraryActivity.java
git commit -m "feat: beautify game library UI (gradient bg, rounded cards, capsule sort)"
```

---

## Task F5: 模拟器验证 + 截图对比

**Files:**
- 无代码改动；验证产物 `app/build/*.png`（gitignored）。

- [ ] **Step 1: 构建 + 安装**

```bash
.\gradlew.bat assembleDebug
adb install -r app/build/outputs/apk/debug/app-debug.apk
adb shell am start -n com.flynes.emu/.MainActivity
```

- [ ] **Step 2: 截图 hq4x 游戏画面**

```bash
adb shell screencap -p /sdcard/hq.png; adb pull /sdcard/hq.png app/build/hq4x.png
```

像素验证：游戏画面区域采样，确认 1024×960 内容（SurfaceView 等比缩放显示）；对比旧 nearest 截图（`app/build/emu-frame-A.png`），确认锯齿减少——人工查看两张图对比。

- [ ] **Step 3: 截图游戏库新界面**

```bash
adb shell input tap 2063 77   # START → 菜单（模拟器 letterbox 偏移后坐标；若坐标失效用 uiautomator dump 定位）
# 点"游戏库"
adb shell screencap -p /sdcard/lib.png; adb pull /sdcard/lib.png app/build/library-new.png
```

Expected: 渐变背景、标题栏、圆角搜索框、胶囊排序、卡片列表可见。

- [ ] **Step 4: 功能回归**

- 搜索："魂" → 列表过滤正常
- 排序切换：热度/名称/大小
- START 暂停菜单：继续/游戏库/许可三路径
- 换游戏：库选另一个游戏（若有）→ 可玩
- 自动存档：返回桌面再进 → autosave restore rc=0

- [ ] **Step 5: MCP 复核**

用 mobile-mcp（stdio 客户端脚本 `C:\Users\pippin\.mobile-mcp-install\mcp-layout-shot.js`）对游戏库与游戏画面各截一张，确认布局。

- [ ] **Step 6: 验收记录 + 提交**

spec `docs/superpowers/specs/2026-08-17-hq4x-filter-game-library-ui-design.md` 追加验收记录小节，提交：

```bash
git add docs/superpowers/specs/2026-08-17-hq4x-filter-game-library-ui-design.md
git commit -m "docs: record emulator acceptance for HQ filter + library UI"
```

---

## 自检要点

- **ABI**：只追加枚举值（2/3/4），不重排；无新导出符号 → `scripts/abi_symbols.golden.txt` 不变；`ci-check.ps1` 应全绿。
- **Spec 覆盖**：§3 滤镜（F1/F2/F3）、§4 界面（F4）、§6 验收（F5）。
- **类型一致**：`nes_video_filter` 枚举值在 nes.h / nes_jni.cpp / NesCore.java 三处一致（2/3/4）。
- **性能**：hq4x 1024×960 每帧 CPU 滤镜；若真机掉帧，F3 默认值改 `FILTER_HQ2X`（一行）。
