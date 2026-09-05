# FlyNES Harmony / iOS 对齐 Android 产品的设计

**日期：** 2026-09-05

**状态：** 已按用户确认的架构、屏幕、数据流、失败/验收写成规格；待用户审阅本文后进入实现计划

**范围：** 把 Android 现有单机产品（游戏中心、对局 overlay、暂停、设置/手柄布局）搬到 HarmonyOS NEXT 与 iOS。两端用原生控件做皮肤，但页面、信息架构、布局几何和操作语义必须以 Android 为唯一源头，禁止另做一套。

## 1. 结论先行

Harmony 当前竖屏字母键调试页、画面下方 Save/Load，以及 iOS 现有游戏库壳，都不是验收对象。验收对象是 Android 已经在用的那套产品，只是跑在另外两个系统上。

1. Android 是行为与布局的源头。`HomeActivity`、`MainActivity` 暂停侧栏、`GamepadView` / `GamepadHitMap` / `ControlLayoutV2`、`SettingsSection` 定义有哪些屏幕、控件在哪、按下算什么。Harmony 与 iOS 必须实现同一张清单，不得增删改操作。
2. 允许不用 Android 的颜色、字体、系统栏、列表控件去像素级拷贝。不允许发明新页面、新分类、新暂停项、对局 HUD 上手动存读档、把 NES START 当成暂停，或把默认手柄位置改成「看起来更顺手」的创作稿。
3. 共享层收游戏中心导航纯函数、手柄布局与命中契约、暂停动作集合；已有 `flynes_app` / `fly_settings` / `flynes_runtime` 继续管目录、设置快照、逐帧 step 和 checkpoint。共享层不出现 Android URI、iOS bookmark、Harmony FilePicker。
4. 平台层只做：原生皮肤、文件授权映射、视频/音频送进系统、触感。
5. Nearby / 三端联机不在本文。高级画质解锁与 120Hz 证据门禁不给 Harmony/iOS 另开捷径。

## 2. 已确认的产品决定

| 主题 | 决定 |
|---|---|
| 产品范围 | 整套：游戏中心 + 对局 overlay + 暂停 + 设置/布局 |
| 平台 | HarmonyOS NEXT 与 iOS 共用本文；实现可 Harmony 先落地，iOS 按同一清单补齐 |
| 视觉 | 游戏中心/设置用各平台原生控件；不要求与 Android 像素级一致 |
| 布局与操作 | 必须与 Android 相同；禁止随意创作 |
| 对局 overlay | 仍为自定义绘制（摇杆跟随、多指、命中区），坐标与命中来自共享契约 |
| 暂停入口 | 右上角独立暂停钮；不是 NES START |
| 存档 | 暂停时自动 checkpoint，对局 HUD 无手动 Save/Load |
| 冷启动 | 进入游戏中心，不进入调试对局页 |
| 联机 | 不在本文 |

## 3. 架构

四层，产品一份、皮肤三份。

1. **共享产品契约（新，可 host 测试）**
   - 游戏中心导航：分类最近 / 收藏 / 全部 / 内置、搜索、当前分类的选中 canonical id。参考 Android `GameCenterState`，迁成三端共用实现，不再在 ArkTS/Swift 里各写一份筛选。
   - 手柄：`ControlLayoutV2`（归一化安全区坐标、scale、opacity）、`DirectionControlMode`（跟随摇杆 / 固定摇杆 / 十字键）、`GamepadHitMap` 命中 → NES 键位与独立 `PAUSE`。
   - 暂停动作固定为：继续、游戏中心、设置。集合不可由平台增减。
2. **共享 app / runtime（已有）**
   - 目录快照、来源状态、收藏/最近、`fly_settings` 快照、`fly_runtime_step_frame`、checkpoint。UI 不另开存档协议。
3. **平台皮肤**
   - Harmony ArkTS、iOS SwiftUI：游戏中心与设置用原生列表/开关/搜索。
   - 对局 overlay 自己画，但元素集合、相对位置、命中规则用契约数字。
   - 选 ROM 目录、持久授权、触感走平台 API。
4. **平台视频 / 音频**
   - 把 runtime 的 RGB565 与 PCM 送进系统。Harmony 可先沿用 PixelMap（必须声明 `srcPixelFormat`），再换成更合适的 surface；iOS 用已有 Metal 路径。音频失败则静音，画面继续。

Harmony `pages/Index.ets` 调试壳在产品落地后删除或降为非默认入口，不得再作为用户打开 App 看到的第一屏。

## 4. 屏幕与操作（Android 源头，禁止创作）

对照时以 Android 源码与现网行为为准，不以概念草图为准。缺控件、改顺序、改默认位置，都算规格失败。

### 4.1 游戏中心

横屏优先。分类仅此四个：最近、收藏、全部、内置。搜索对英文标题、简中标题、原始文件名过滤。点选一张卡，再启动。来源管理是同一产品里的另一态（添加目录、扫描、失效来源），不是第二个 App。

大字号时分类控件仍要可及，行为对齐 `HomeHeaderLayoutPolicy` 的压缩规则，不另发明一种「手机竖屏底部 Tab」。

### 4.2 对局

横屏。NES 画面居中。Overlay 覆盖其上，元素仅允许：

| 元素 | 角色 |
|---|---|
| 方向（摇杆或十字） | NES 方向键；模式来自设置 `DirectionControlMode` |
| A、B | NES A/B；相对位置由 `ControlLayoutV2` 的 A/B placement 决定，默认 A 偏右上、B 偏右下 |
| SELECT、START | NES SELECT/START，不是暂停 |
| 暂停钮 | 应用级 `PAUSE`，打开暂停侧栏 |

默认 placement 必须是 Android `ControlLayoutV2.recommended()`：

- D_PAD `(0.10, 0.76)`
- A `(0.94, 0.64)`
- B `(0.87, 0.86)`
- SELECT `(0.09, 0.28)`
- START `(0.94, 0.28)`
- opacity `0.52`，仅横屏

命中、死区、多指独立跟踪、松手取消、跟随摇杆回中，对齐 `GamepadHitMap` / `GamepadView`，不换成「四个独立 Button 拼出来的 D-pad」。NES START 只发 START 脉冲，不打开暂停。

禁止：竖屏字母键 U/D/L/R、对局画面下的 Save/Load、把暂停做进 START、把摇杆改到右半屏「因为手机习惯」。

### 4.3 暂停

仅由暂停钮打开。侧栏动作仅三项：继续、游戏中心、设置。点遮罩等于继续。打开时停步、停音频、自动 checkpoint。回游戏中心则拆 runtime。

### 4.4 设置

分区仅此五个，顺序与 Android `SettingsSection` 一致：显示、操作、音频、游戏语言、关于。操作区进入布局编辑（移动、缩放、透明度），读写同一份 `ControlLayoutV2`。非法布局回落 `recommended()`。

高级画质 / 运动补偿仍按 Android 证据门禁，Harmony/iOS 不得用「模拟器能跑」或 `maximumFramesPerSecond` 直接解锁。

### 4.5 导航

冷启动 → 游戏中心。游戏中心启动 → 对局。暂停 → 继续 | 游戏中心 | 设置。不得冷启动直达内置 ROM 对局。

## 5. 数据流

**游戏中心列表**来自 `fly_catalog_snapshot` 加用户态（收藏、最近）。导航纯函数不写入 FLYCAT01。

**来源授权**只留平台映射，不进 catalog：

- Android：source UUID → SAF URI
- iOS：source UUID → security-scoped bookmark
- Harmony：source UUID → 可持久访问的 URI 或路径

映射丢失则该来源失效，必须重新授权。扫描只使用 borrowed FD，遵守现有 8 MiB / ZIP 2048 / 解压 32 MiB / 文件名 1024 / 压缩比 200。

**设置与布局**走 `fly_settings` 快照；`ControlLayoutV2` 必须进入该快照或 app 根下的兄弟文件，停止仅存在于 Android `ControlLayoutRepository`。三端 overlay 只消费这份数据。

**启动**用 canonical id 在平台解开 FD，再交给 `fly_runtime`。内置 *From Below* 是内置来源的一种落地（Harmony 现有 rawfile 可继续作为 builtin），不是唯一 UI。

**封面**留在平台封面仓库，共享层不存位图。

## 6. 失败表现

| 情况 | 表现 |
|---|---|
| 用户取消选目录 | 目录不变 |
| 授权映射丢失 | 来源失效，提示重新授权，不猜路径 |
| 扫描 PARTIAL/FATAL | 按现有 ABI：旧条目标 stale，半次扫描不充当成功 |
| 打开 ROM 失败 | 留在游戏中心并说明原因，不掉进调试对局页 |
| 画面 / surface 失败 | 停步并提示；禁止再出现未声明 `srcPixelFormat` 导致的 SIGSEGV |
| 音频无法启动或 underflow | 静音，画面与输入继续 |
| 暂停自动存失败 | 允许继续或回游戏中心，必须让用户看见没存上 |
| 布局数据非法 | `ControlLayoutV2.recommended()` |

## 7. 验收

不使用 Android 截图像素金标（皮肤允许原生）。使用两道门：

1. **共享 host 测试**：分类/搜索/选中；`ControlLayoutV2` 编解码与默认 placement；命中坐标 → 键位（含 PAUSE 与 START 分离）；暂停动作集合恰好三项。Android 现有 JVM 测试是迁入参考，迁完后不得在 ArkTS/Swift 再分叉一份规则。
2. **平台操作清单**（Harmony 官方模拟器或真机；iOS 模拟器或真机 + 现有产品契约测试）：游戏中心四分类可切换；搜索过滤；启动进入横屏对局；暂停三按钮；设置五分区；改布局后 overlay 位置跟着变；NES START 不打开暂停；对局 HUD 没有 Save/Load。

未跑过对应平台操作清单，不得声称该平台产品已对齐 Android。HAP/IPA 打包成功不等于对齐。

## 8. 明确不做

- Nearby、QUIC、双人联机、M2–M5
- 为 Harmony/iOS 另开设置 schema 或另一套手柄默认几何
- 对局 overlay 手动存读档
- 像素级拷贝 Android 状态栏、Material 色板、系统字体
- 把 FilePicker / bookmark / SAF 写入共享层
- 用「更符合某平台 HIG」为理由增删控件或改默认 placement

## 9. 实现落点（计划阶段再拆任务）

- 共享：迁 `GameCenterState`、`ControlLayoutV2`、`GamepadHitMap`、暂停动作枚举；布局写入 settings 快照。
- Harmony：用契约重做游戏中心 / 对局 overlay / 暂停 / 设置；去掉调试 HUD；FilePicker 映射；官方模拟器按第 7 节清单验收。
- iOS：把现有 `CatalogLibraryView` / `GamepadOverlayView` / `SettingsView` 收到同一契约，补暂停侧栏与布局编辑，按同一清单验收。
- Android：改为消费共享契约，保持现网操作不变；JVM 回归继续绿。
