# FlyNES 画质滤镜 + 游戏库界面美化 设计（hq4x Filter & Game Library UI）

> **Status:** 已批准（用户确认 2026-08-17）
> **依据:** `docs/superpowers/specs/2026-08-15-nes-emulator-design.md`（§5 视频、§3.8 UI 风格）、用户反馈"画质太差，调高清一点；初始化界面太简陋"。

---

## 1. 问题

1. **画质差**：当前渲染 = nearest-neighbor 纯像素放大（`blit_scale_rgb565`），NES 原生 256×240 放大 4 倍 → 巨大马赛克/锯齿。`core` 层滤镜未启用（`nes_set_video_format` 只接受 `NES_FILTER_NONE`）。
2. **游戏库简陋**：`GameLibraryActivity` 纯黑底 + 系统默认 ListView/Button/EditText，无视觉层次。

## 2. 目标与非目标

**目标：**
- core 层接入 NestopiaUE 内置 hq 系列滤镜（hq2x/hq3x/hq4x），默认 **hq4x**（1024×960），平滑锯齿、保留像素艺术感。
- 游戏库界面整体美化（深色渐变、圆角卡片、胶囊控件），**保持纯 Java 代码构建 UI、零第三方依赖**（项目铁律）。
- ABI 只追加枚举值，不破坏既有签名（追加安全）。
- 滤镜切换走既有 `nes_set_video_format` 的 `filter` 参数（设计已预留，本次放开实现）。

**非目标（YAGNI）：**
- 不做设置页/运行时切换 UI（ABI 支持切换，UI 入口留给"设置页"后续任务；本次默认 hq4x 硬编码）。
- 不做 NTSC/CRT 复古滤镜（枚举已存在，本次不放开实现；ABI 可后续加）。
- 不改 core 视频管线内部结构（只调整 framebuffer 尺寸分配与 Output pitch）。
- 不改游戏库功能逻辑（搜索/排序/SAF 扫描/启动）。

## 3. 滤镜实现方案（core 层）

### 3.1 机制（已探明）

NestopiaUE 渲染管线：核心把 PPU 渲染到内部 256×240 缓冲（`Input`），`Renderer::Blit` 时若设置了 filter，调用 `filter->Blit(input, output)` —— **滤镜负责把 256×240 放大/过滤写入 output**（hq4x → 1024×960）。关键约束：
- `output.pitch` 是**行步长字节数**，滤镜按放大后宽度写行（hq4x 时每行 1024×2 字节）。
- `Renderer::SetState` 只在 filter/width/height/bpp/mask 变化时重建 Filter 对象；`RenderState.filter = FILTER_HQ4X` 即创建 `FilterHqX`。
- `nes_run_frames` 当前把 `ctx->framebuffer`（256×240）直接当 Output —— **启用滤镜后必须换成放大尺寸的缓冲，否则溢出**。

### 3.2 ABI 改动（全部追加，ABI 安全）

`core/include/nes/nes.h`：

```c
/* nes_video_filter 枚举（当前实现仅 NONE 与 HQ 系；值不可重排） */
typedef enum nes_video_filter {
    NES_FILTER_NONE  = 0,
    NES_FILTER_HQ2X  = 1,
    NES_FILTER_HQ3X  = 2,
    NES_FILTER_HQ4X  = 3,
    /* NTSC/xBR/2xSaI 等后续版本再放开 */
    NES_FILTER_COUNT
} nes_video_filter;
```

（若枚举已存在于 nes.h，则只补值域语义注释 + 实现；以实际文件为准。）

### 3.3 core 实现（`core/src/nes_core.cpp`）

1. **filter 倍率**：`filter_scale(nes_video_filter) -> {1,2,3,4}`（NONE=1, HQ2X=2, HQ3X=3, HQ4X=4）。
2. **framebuffer 分配**：`nes_set_video_format` 放开 filter 参数（仅接受 NONE/HQ2X/HQ3X/HQ4X），按 `kScreenWidth*scale × kScreenHeight*scale` 重新分配 `ctx->framebuffer`（realloc 语义：尺寸变化才重分配；失败保持旧缓冲 + 返回 OOM）。
3. **RenderState**：`apply_render_state` 中 `rs.filter` 按 cfg.filter 映射（FILTER_NONE→FILTER_NONE、HQ2X→FILTER_HQ2X 等）；`rs.width/height` 保持 256×240（核心 input 尺寸不变）。
4. **nes_run_frames**：`Video::Output vo(framebuffer, kScreenWidth*scale*pixfmt_bpp)` —— pitch 用放大后行宽。
5. **update_video_frame**：`video_frame.width = kScreenWidth*scale`、`height = kScreenHeight*scale`（`nes_get_video_frame` 自动返回新尺寸）；pitch 同步。
6. `nes_set_video_format` 返回逻辑不变（非法 filter → `NES_ERR_NOT_IMPLEMENTED`）。

### 3.4 JNI / Java / App

| 文件 | 改动 |
|---|---|
| `app/src/main/cpp/nes_jni.cpp` | +`nativeSetVideoFilter(jlong, jint)` → `nes_set_video_format(ctx, pixfmt, filter)`（pixfmt 保持 RGB565 不变） |
| `app/src/main/java/com/flynes/emu/NesCore.java` | +`setVideoFilter(int)` + 常量 `FILTER_HQ4X=3` 等 |
| `app/src/main/java/com/flynes/emu/MainActivity.java` | `onCreate` 中 `core.setAudioFormat` 后调用 `core.setVideoFilter(NesCore.FILTER_HQ4X)`；`computeScale()` 改为基于 1024×960 计算（`Math.min(w/1024, h/960)` 向下取整 ≥1）；`blit` 时 scale 传 1（framebuffer 已是 1024×960，`nativeBlit` 的 nearest 1:1 拷贝即可，无马赛克放大） |

> 注意：hq4x 已放大 4 倍，`nativeBlit` 的 `scale` 参数传 1（1:1 拷贝 1024×960 → 窗口），保持"一个像素=一个像素"，避免二次缩放失真。窗口几何 = 1024×960 居中，两边留黑边（等比例完美）。

## 4. 游戏库界面美化方案（app 层，纯代码）

`GameLibraryActivity.java` 重构 `buildUi()` 与 `GameAdapter`，**功能零改动**：

| 元素 | 实现 |
|---|---|
| 背景 | `LinearLayout` 垂直渐变：新建 `GradientDrawable(TOP_BOTTOM, [0xFF0F2027, 0xFF203A43, 0xFF2C5364])`（深蓝黑） |
| 标题栏 | `TextView` "🎮 FlyNES 游戏库"（22sp，白色，加粗）+ 副行"共 N 个游戏 · 长按管理"（12sp，灰）；`reloadData` 后更新计数 |
| 搜索框 | `EditText` 包进圆角背景（`GradientDrawable` 圆角 24dp，`0x33FFFFFF` 填充、`0x55FFFFFF` 描边）+ 左侧 "🔍" 前缀 TextView |
| 排序 | 3 个按钮改**分段胶囊**：外层圆角容器 + 选中项高亮（`StateListDrawable`/手动切背景），选中色 `0xFF4A90D9` |
| 列表项 | 卡片式：`GradientDrawable` 圆角 12dp + `0x22FFFFFF` 填充，内边距加大；两行文本（标题行 16sp 白 + ⭐热度橙色；副行 12sp 灰）；按压态 `0x33FFFFFF`（`StateListDrawable`） |
| 空状态 | 居中 "🎮"（64sp）+ "还没有游戏"（18sp 白）+ 原按钮美化（圆角胶囊渐变）+ 说明文字 |
| 分隔线 | 移除系统 divider，改用卡片间距（`ListView` 自带 padding 或卡片 margin） |

**保持**：`ArrayAdapter` 结构、搜索/排序/SAF/启动逻辑、`GameEntry` 字段不变。只改视觉容器与样式。

## 5. 错误处理与边界

| 场景 | 处理 |
|---|---|
| 滤镜分配失败（OOM） | framebuffer 保持旧尺寸，`nes_set_video_format` 返回 `NES_ERR_OUT_OF_MEMORY`；App 侧 log 警告继续旧画面 |
| 非法 filter 值 | `NES_ERR_NOT_IMPLEMENTED`（保持既有语义） |
| 切换滤镜后存档 | NST 存档不含渲染状态，切换滤镜不破坏存档兼容（版本/SHA1/CRC 机制不变） |
| 画面比例 | 1024×960 恒为 4:3；窗口两边留黑边（居中），不拉伸变形 |
| 低端机性能 | hq4x 单帧约几 ms（ARM64）；若真机帧率不足，可降 hq2x（枚举已支持，改一行默认值） |

## 6. 测试与验收

1. **宿主测试**（`core/tests/test_core.cpp` 扩展）：设 hq4x → `nes_get_video_frame` 返回 1024×960、跑 2 帧 rc≥0；设回 NONE → 256×240；非法 filter → `NES_ERR_NOT_IMPLEMENTED`；hq2x → 512×480。
2. **CI**：`scripts/ci-check.ps1` 全绿（ABI golden 更新：无新符号，枚举无影响，确认 golden 不需变）。
3. **构建**：`assembleDebug` 通过。
4. **模拟器验证**（MIT_Phone_API35）：
   - 截图对比：hq4x 画面 vs（可选）hq2x/无滤镜画面 —— 确认锯齿明显减少、画面平滑。
   - 游戏库界面截图：渐变背景、圆角卡片、胶囊排序、标题栏可见。
   - 功能回归：搜索、排序、START 暂停菜单、换游戏、自动存档均不回归。
5. **MCP 复核**：用 mobile-mcp 截图游戏库 + 游戏画面，确认布局与滤镜效果。
6. **提交**：core 与 app 分两步提交（`feat: enable HQ video filters in core ABI` / `feat: beautify game library UI`）。

## 7. 后续预留（不在本次范围）

- 设置页：滤镜选择（NONE/HQ2X/HQ4X/NTSC）、亮度/饱和度（Renderer 已支持 `SetBrightness` 等）、按钮布局自定义。
- xBR/2xSaI 滤镜放开（枚举已在 RenderState 中，仅需 nes.h 扩展 + 实现放开）。

### 验收记录（模拟器 MIT_Phone_API35, 2026-08-17）

- **核心修复**（排查中发现并修复的渲染 bug）：`Renderer::FilterHqX::Check` 要求 `RenderState.width/height == 放大后输出尺寸`（HQ4X→1024×960），原实现传 256×240 导致 filter 创建失败 → 黑屏。修正 `apply_render_state` 按 `filter_scale` 传放大尺寸，并透传 `SetRenderState` 返回值（失败返回错误码而非静默 NES_OK）。
- **宿主测试**：HQ4X→1024×960 非黑（94%）、HQ2X→512×480、NONE→256×240、NTSC→-200，全 PASS。
- **模拟器验证**：
  - hq4x 画面渲染正常（`fb sample nonzero=64/64`，blit 1:1 1024×960，SurfaceFlinger scale 1.125 等比显示）。
  - 画质对比：像素块一致性 94.5%（nearest-4x）→ 83.8%（hq4x），马赛克明显减少。
  - 输入响应正常（画面随摇杆变化），暂停菜单/换游戏回归通过。
  - 游戏库新界面：渐变背景（#11222A→#3F5760 平滑过渡）、标题栏、圆角搜索框、胶囊排序（选中蓝色 #4A90D9 1935px 确认）、圆角列表卡片，全部渲染正常；排序切换/返回/暂停菜单无崩溃。
- **环境注记**：模拟器 letterbox（view 宽 2072 居中，偏移 +134px）与导航栏（右侧 132px）不影响真机；真机全屏时 4:3 surface 自动居中。
- **待真机**：hq4x 性能（帧率）、游戏库竖屏观感、实际画面观感由用户确认。
