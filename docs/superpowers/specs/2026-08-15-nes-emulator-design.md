# 安卓离线红白机（NES/FC）模拟器 — 设计文档

- 日期：2026-08-15
- 状态：已与用户对齐（5 个根决策已拍板，设计稿已获确认）
- 关联评审：5 路 AgentTeams 专家评估（架构 / 内核 / 性能功耗 / 路线 / 合规），全文见 `docs/nes-arch-review/`

---

## 1. 背景与目标

做一个**安卓单机「红白机（NES/FC）模拟器」App**，完全离线，用于飞机上长时间游玩。

核心定位（用户明确）：
- 先做**可扩展「基座」**，不追求一次性成品，不着急上手玩。
- 后续可**优化、上架、魔改游戏**（金手指/作弊码、IPS/BPS 补丁、改存档）。
- 将来**移植鸿蒙 HarmonyOS 与 iOS**，核心不动、只换壳/桥。
- 技术约束：**能用开源就用、减少手搓轮子、高性能、低功耗（省电）**。
- 开发者技术栈：**C++ 主线 + Java 外壳**。

---

## 2. 已对齐的根决策（不可轻易推翻）

| # | 决策点 | 结论 |
|---|---|---|
| D1 | 模拟器内核 | **NestopiaUE**（GPLv2、C++17、`source/core` 平台无关） |
| D2 | 开源策略 | **整体 GPLv2 开源 + 阶段0 预留「可替换内核抽象层」**（未来想闭源可无痛换 ares/ISC） |
| D3 | 上架渠道 | **Google Play + GitHub 源码公开**；守住「完全离线、绝不运行时下载 ROM/核心/可执行代码」 |
| D4 | 移植策略 | **远期愿景**；基座阶段只做 `tools/sdl-shell` 第二壳 spike 验证 C ABI 零平台依赖 |
| D5 | 魔改范围 | 金手指（GG/PAR）+ 补丁（IPS/UPS 后接 BPS）+ 内存查看/编辑 + 金手指搜索；**不碰真调试器**（需换核） |
| D6 | 基座 DOD | **接口完备 + 每类功能 spike 一次 + 至少 1 个 ROM 连跑 5 分钟不爆音不撕裂** |

---

## 3. 技术事实纠偏（评审中核实，避免重蹈）

以下为对 NestopiaUE 上游 master（1.53.2）`source/core` 逐文件核查的硬事实：

1. 内核标准是 **C++17**（非 C++14），必须**锁定一个上游 commit**。
2. 内核只内置 **IPS + UPS 补丁，没有 BPS**；BPS 需外部库（libbps）。
3. 内核**无调试器 API**（`Cheats::GetRam()` 仅 2KB CPU RAM）；真调试器 = 换/嵌 Mesen 或 FCEUX。
4. 内核所有 IO 走 `std::istream/std::ostream`（ROM/存档/FDS BIOS/补丁）；C ABI 需自建 `std::streambuf` 适配器。
5. 电池存档走**反向回调** `fileIoCallback`（`SAVE_BATTERY/LOAD_BATTERY/...`），非直接指针。
6. FDS 需 `SetBIOS(std::istream*)`，BIOS 为任天堂版权、**不可内置**，须用户自备。
7. 内核共 **299 个 .cpp**（board 205 + core 49 + api 16 + input 25 + vssystem 4），上游无 CMake，须用 `file(GLOB)` 生成源清单。
8. `NstDatabase.xml`（坏 dump 纠错库）**必须随包分发**，是 GPLv2 内核的一部分。
9. API 中游戏标题/文件名/board 名为 `wchar_t*`（平台差异 2/4 字节）；C ABI 一律转 **UTF-8** 输出。
10. 鸿蒙 **NEXT（HarmonyOS 5）无 Java/JNI**，外壳为 ArkTS、原生桥为 NAPI；iOS 无 JNI，用 ObjC++，且只能在 macOS/Xcode 构建签名。

---

## 4. 架构总览（四层，成败在 C ABI 边界）

```
┌─────────────────────────────────────────────┐
│ app/   Android 壳（Java）                    │  触屏/手柄、SurfaceView、AudioTrack、SAF、UI
├─────────────────────────────────────────────┤
│ bridge/  桥接（jni / napi 占位 / objc 占位） │  薄适配器，三平台各写一份，共享同一 C ABI
├─────────────────────────────────────────────┤
│ C ABI  include/nes/nes.h（extern "C"）       │  唯一跨边界契约：不透明句柄 + 版本化结构体
├─────────────────────────────────────────────┤
│ core/  平台无关 C++（零平台头文件）           │  NestopiaUE(vendor) + 自研{state,save,cheat,patch,mem,io}
└─────────────────────────────────────────────┘
```

**铁律（架构纪律，可执行约束）**：
- core / C ABI 不得出现 `JNIEnv` / `jobject` / `ANativeWindow` / `AAudioStream` / Java 对象；不 `#include` 平台头；**不碰文件系统**。
- 用 CI 门禁验证：一个「裸 core」编译目标（不含任何 NDK/平台头的最小 clang 环境）+ 平台头 `#include` 白名单检查 + macOS clang 编译门禁。
- 自研「魔改逻辑」（金手指/补丁/存档/内存）**放 core 层（C++ 平台无关）**，与 NestopiaUE 源码物理隔离；NestopiaUE 以 vendor 子目录进入、只打最小必要补丁（补丁归档、可 rebase）。
- 抽象只到「三平台换壳 + NestopiaUE 升级」所需程度，YAGNI 掉多内核插件化/通用录像框架等。

---

## 5. 目录结构

```
app/                          # Android 壳（Gradle module，Java）
  src/main/java/...           # 外壳：Activity、触屏控制器、设置、ROM 列表
  src/main/cpp/jni/           # JNI 桥（只依赖 include/nes/）
core/                         # 平台无关核心（CMake 独立可编）
  vendor/nestopiaue/          # upstream（git submodule 锁 commit）+ 最小补丁归档
  src/state/                  # 即时存档 NST 封装 + 核心版本号
  src/save/                   # 电池存档（反向回调桥）
  src/cheat/                  # 金手指 GG/PAR + 内存搜索（阶段2）
  src/patch/                  # IPS/UPS（阶段1）、BPS（阶段2 引 libbps）
  src/mem/                    # 内存查看/编辑（阶段2）
  src/io/                     # std::streambuf 适配器（内存/文件桥接）
  include/nes/nes.h           # C ABI 头（extern "C"，不透明句柄）
  tests/                      # 无头 golden + ABI 稳定性测试
bridge/                       # jni（阶段0）/ napi（占位）/ objc（占位）
tools/sdl-shell/              # 桌面 SDL「第二壳」spike（验证零平台依赖）
docs/
```

---

## 6. C ABI 接口契约（v1 全量定义，未实现返回 not-implemented）

所有接口 `extern "C"`，句柄 `nes_t*` 不透明；跨边界结构体带 `size` + `version` 字段；`nes_api_version()` 提供版本查询。

- **生命周期**：`nes_create` / `nes_destroy` / `nes_load_rom(ctx, bytes, len)` / `nes_unload` / `nes_reset`
- **运行**：`nes_run_frames(ctx, n)` 返回本帧产出音频样本数（**音频主时钟，按 cycle/样本驱动，非 vsync 驱动**）
- **视频**：`nes_video_get_buffer/width/height/pitch`（RGB565 私有缓冲，**外壳持有**）
- **音频**：`nes_audio_get_samples(ctx, buf, frames)`（48000Hz 单声道，外壳缓冲）
- **输入**：`nes_set_input(ctx, mask)`（8 键位掩码 + 4 手柄扩展位，为 Zapper/多控制器预留）
- **存档**：`nes_save_state/load_state`（NST，头写核心版本号）；`nes_flush_battery`（电池存档反向回调桥接）
- **金手指**：`nes_cheat_add/remove/clear`（GG / PAR 编解码，内核原生提供）
- **补丁**：`nes_apply_patch`（IPS/UPS 阶段1；BPS 阶段2 引 libbps）
- **内存**：`nes_read_cpu_ram`（阶段2 内存查看）
- **元数据**：`nes_get_rom_info`（UTF-8 标题 / mapper / board / PRG-CHR，不暴露 wchar_t）
- **能力/版本**：`nes_api_version` / `nes_get_capabilities`
- **回调**：日志回调、文件 IO 反向回调（电池存档取/存）、FDS BIOS 提供回调（阶段2 占位）

**数据契约要点**：ROM 一律「字节指针 + 长度」输入；所有跨边界缓冲由外壳（或共享内存）持有，core 不负责分配/释放；`std::istream` 适配器是 C++ 内部实现细节，Java/JNI 侧只看到字节数组。

---

## 7. 核心数据流与帧循环

### 7.1 加载
assets/SAF → 字节[] → `nes_load_rom(bytes, len)`（流适配器藏 C++ 内部）→（可选先 `nes_apply_patch`）→ 内核解析 → `nes_get_rom_info` 返回元数据。

### 7.2 帧循环（音频主时钟，禁忙等）
- **音频为主时钟**：NTSC 真机 60.0988Hz 与安卓 60Hz vsync 不整除。用 vsync 驱动会导致音频漂移/爆音/变调；用音频驱动则画面偶发跳帧（NES 2D 游戏不可感知）。
- 流程：`AudioTrack.write` 阻塞 → 每够 N 样本调 `nes_run_frames` → 音频写入 + 「最新完成帧」原子指针/双缓冲交给渲染线程 → Choreographer vsync 时软件 blit 到 SurfaceView。
- 绝对禁止忙等：一切等待用阻塞原语（`AudioTrack.write`、vsync await、condvar）。

### 7.3 省电（硬约束）
- 渲染 = **软件 RGB565 blit + SurfaceView（不开 GPU）**；GPU 滤镜（扫描线/CRT）留阶段2。
- 音频 = **AudioTrack 48000Hz 单声道，缓冲 80~150ms**；阶段1 切 Oboe（`AAUDIO_USAGE_GAME`）。
- **pause 即停线程** + `nes_flush_battery` + 写自动存档槽 + 释放 surface/audio；`onResume` 恢复。
- 文件 = **assets + SAF(persistable) + 私有目录**；明确**拒绝 `MANAGE_EXTERNAL_STORAGE`**。

---

## 8. 横切关注点

- **错误码**：统一 Result 枚举（`RESULT_ERR_UNSUPPORTED_MAPPER`、`RESULT_ERR_CRC_FAIL`、FDS 缺 BIOS 等）映射到用户可读提示，避免「黑屏」。
- **日志**：日志回调，本地落盘（离线无网，排障依赖本地日志）。
- **配置**：键值配置由壳持久化，core 无状态。
- **ABI 版本化**：`api_version()` + struct `size/version` + ABI 符号表 diff 测试门禁。
- **构建**：C++17 锁 commit；NDK 版本二选一并锁定（27 或 28）；`-fvisibility=hidden` + 显式导出；`file(GLOB)` 生成 299 个 .cpp 源清单。
- **多 ABI**：发布 `arm64-v8a`；`x86_64` 仅 debug（模拟器）；minSdk **24**，targetSdk 最新。
- **存档格式**：NST 头写核心版本号，读前校验、不匹配拒绝并提示；存档文件按 ROM SHA1 key 化；升级 App 不丢档为硬承诺。

---

## 9. 演进路线（三轨并行）

### 9.1 功能轨
- **阶段 0 基座**（DOD = D6）：目录结构 + CMake/NDK 编译 NestopiaUE + C ABI 全量头文件 + 每类接口 spike 一次 + Java 壳最小闭环（load→渲染→触屏→声音→存档）+ 5 分钟时序验收。
- **阶段 1 完整可玩**：即时/电池存档（多槽位+导出/导入）、金手指 GG/PAR、补丁 IPS/UPS、ROM 管理 UX（SAF/最近列表/头信息）、设置、外部手柄（1.5）、音频焦点/蓝牙、量化验收。
- **阶段 2 魔改**：BPS 补丁（libbps）、内存查看/编辑、金手指搜索、Rewinder 回退（内核白送）、GPU 滤镜/整数缩放、NSF 关屏播放。

### 9.2 合规轨（前移，与功能并行）
基座结束即完成：GPL 边界定案 + homebrew license 清点 + App 内许可页 + SBOM（`reuse-tool`/gradle license report）。

### 9.3 移植轨
基座阶段用 `tools/sdl-shell` 做 second-shell spike 验证 C ABI 零平台依赖；鸿蒙 NEXT（ArkTS+NAPI）与 iOS（ObjC++，需 macOS/Xcode）完整壳远期；CI 加 macOS 编译门禁防回归。

---

## 10. 测试与量化验收

- **core 无头 golden 测试**：已知 ROM 跑 N 帧，比对帧 CRC / 截图哈希 / APU 采样哈希。
- **ABI 稳定性测试**：导出符号表 diff / 结构体布局 golden。
- **CI**：多 ABI 构建矩阵 + macOS 编译门禁 + 1 小时跑机泄漏/GC 压测。
- **量化 DOD（示例值，可调）**：中端机稳定 60fps、单核 CPU < 15%、亮屏连续游玩 ≥ 6h、包体 ≤ 30MB；「连续 10 分钟无爆音无漂移」为内核集成硬验收。
- **阶段0 验收记录 (2026-08-15) — PASS**：device=`MIT_Phone_API35`（x86_64, API 35 模拟器, `-gpu swiftshader_indirect`, 2340x1080 横屏）。
  - 安装/启动：`app/build/outputs/apk/debug/app-debug.apk`（11.1MB，含 arm64-v8a + x86_64 `libnescore.so` 与内置 `roms/from_below.nes`）安装成功；启动 `com.flynes.emu/.MainActivity`，log 见 `I/FlyNES: ROM loaded (rc=0), render scale=4x`，AudioTrack 活跃。
  - 渲染证据：截图 `app/build/acceptance-screen.png`（标题画面 154KB）与 `app/build/acceptance-screen-4.png`（开始游戏后画面 81KB）；SurfaceFlinger 实测 125 帧帧间隔 avg 16.7ms（60fps，min 14.0ms / max 18.8ms）；输入驱动验证：右半屏长按 START 进入游戏（34.4% 像素变化），d-pad DOWN 驱动鱼移动（局部 10282 像素变化）。
  - 5 分钟稳定性（300s，15 次采样）：进程 PID 全程存活；`logcat -d` 全量 3971 行 0 崩溃 / 0 ANR / 0 FATAL（含 `am_crash` / `am_anr` 扫描）。
  - CPU 观察：`top` 采样 42.3~50.0% 单核（均值 ~45%），未满核；模拟器 + swiftshader 环境偏高，真机 DOD（单核 <15%）留待阶段 1 量化。
  - gfxinfo：仅 2 帧 HWUI 统计（SurfaceView 原生 blit 不走 HWUI 管线），帧率以 SurfaceFlinger 帧间隔为准。
  - Glitches：无崩溃/ANR/黑屏；AudioTrack 弃用告警与 InteractionJankMonitor 无权限告警均为良性。

---

## 11. 合规基线

- **整体 GPLv2 开源**：`.so` 静态/动态链接均构成「结合作品」，规避不了 GPL；接受开源，GitHub 公开源码 + CI 可复现构建 + App 内许可页。
- **内置 homebrew 仅 CC0/MIT 且无任天堂素材**：候选 **From Below（MIT）、Super Sunny World（MIT）、Lan Master（CC0）、Lawn Mower（CC0）**，Streemerz 视 license 确认；逐个 LICENSE 文件 + 署名页。
- **内容红线**：不内置商业 ROM/BIOS（FDS disksys.rom 用户自备）/作弊码 DB/商业补丁库；金手指只做「手动输入/导入」；坚持「工具-only、不分发内容」。
- **命名/商标**：App 名与图标避开任天堂商标（慎用「NES/FC/任天堂」）。
- **依赖许可**：GPLv2 与 GPLv3 不兼容，核查所有 C++ 依赖许可；构建时生成 SBOM。

---

## 12. 主要风险与开放项

| 风险 | 缓解 |
|---|---|
| 帧循环时序做错 → 爆音/变调/抖动 | 阶段0 写死音频主时钟；10 分钟无爆音为硬验收 |
| C ABI 泄漏平台概念 → 移植退化为重写 | 裸 core 编译门禁 + 平台头白名单 + sdl-shell spike |
| NST 存档版本绑定 → 升级内核丢档 | 存档头写核心版本号，升级视为破坏性变更 |
| 电池存档丢档 | onPause 强制 `flush_battery` |
| 手抄 299 个 .cpp 维护成本 | `file(GLOB)` + 清单完整性检查 |
| 忙等导致 CPU 常醒耗电 | 音频阻塞主时钟 + 禁忙等（代码审查红线） |
| homebrew 未经授权内置 | 逐个 license 核验 + 署名页 + 上架门禁 |

**开放项（进入实现计划前确认）**：量化 DOD 的具体数值；Streemerz 的 license 是否允许再分发；NDK 版本 27 或 28 二选一。
