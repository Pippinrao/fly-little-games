# FlyNES 可公开试玩 Alpha 产品化设计

> **状态：** 总体方案已确认，等待书面规范复核
>
> **日期：** 2026-08-21
>
> **团队基线：** 2 人小队，8–10 周；一人主 Android/产品体验，一人主 Native/运行时，交叉评审与测试
>
> **输入依据：** `docs/audits/2026-08-21-demo-product-readiness-audit.md`

---

## 1. 背景与产品判断

当前 FlyNES 已完成 Android + JNI + NestopiaUE 的最小技术闭环，但输入、帧发布、存档隔离、生命周期、产品导航和本地化仍是 Demo 级实现。下一阶段目标不是增加更多模拟器功能，而是把现有纵切升级为一个能够安全交给外部用户试玩的 Alpha。

本设计采用“**可靠性闸门 + 产品化双轨**”：

- 运行时轨先解决输入正确性、逐帧发布、存档数据安全和生命周期。
- 产品体验轨在稳定接口之上完成视觉系统、设置、游戏库、i18n、外部手柄和无障碍。
- 任何视觉里程碑都不能绕过输入、帧率、存档和崩溃门禁进入公开试玩包。

## 2. 目标、非目标与成功定义

### 2.1 Alpha 目标

1. NES START、SELECT、A/B、方向输入语义正确，多指取消后不会卡键。
2. NTSC/PAL 每个模拟帧只生成和发布一次；在 60/90/120 Hz 屏幕上按正确 cadence 呈现。
3. 每个 ROM 的 autosave、手动状态预留和 battery SRAM 完全隔离；异常写盘不破坏最后一份好档。
4. ROM 切换、暂停、前后台、音频焦点和坏 ROM 失败均可恢复，不摧毁当前会话。
5. 建立统一视觉、导航和设置模型；正确处理 4:3、方形像素、整数缩放、安全区和系统返回。
6. 游戏库支持首次使用、来源管理、最近/收藏、搜索无结果和异常恢复。
7. UI 支持英语与简体中文；常见游戏支持离线中文标题和中英文别名搜索。
8. 支持标准 Android 键盘/USB/蓝牙手柄，并提供基础 TalkBack 语义。
9. target/compile API 36，构建、Lint、单元测试、native 测试和模拟器 smoke 成为发布门禁。

### 2.2 非目标

- 不做网络下载 ROM、在线商店、账号、云存档或联网元数据服务。
- 不做运动插帧；120 Hz 支持不生成伪造的 120 个 NES 游戏帧。
- 不在 Alpha 内实现金手指 UI、补丁管理、倒带、FDS BIOS 管理或调试器。
- 不在 Alpha 内建立覆盖所有 NES ROM 的完整中文数据库；先覆盖经过许可审查的内置 ROM和一组常见标题，并保证数据库可版本化扩充。
- 不进行 Compose 全量迁移；保留 Java 与 Android Views，产品页面改用资源化布局和标准组件。
- 不以 HarmonyOS/iOS 移植为本阶段交付，但不破坏现有 C ABI 跨平台边界。

### 2.3 Alpha 成功定义

- P0 验收项全部通过，无已知跨 ROM 串档、输入卡死、START 冲突、native 越界或后台模拟线程。
- 在至少一台 60 Hz、一台 90/120 Hz Android 真机连续运行 30 分钟，无崩溃、ANR、严重热状态或明显音画漂移。
- 首次安装用户在两次主要操作内开始内置试玩或发起 ROM 导入。
- 中文和英文核心流程完整，无混合语言、占位 URL 或不可恢复空状态。

## 3. 取代的旧设计决策

本规范是下列决策的后继，冲突时以本规范为准：

| 旧决策 | 新决策 |
|---|---|
| START 同时发送 NES 输入并弹 App 暂停菜单 | NES START 与模拟器暂停完全分离；暂停使用独立按钮和系统 Back |
| A 在左上、B 在右下且 B 更大 | 默认使用常见 NES 的 B 左/A 右横向预设；允许交换和自定义 |
| 触控热区可以扩张并相互重叠 | 所有命中区域互斥；边界按最近中心裁决，单坐标只能产生一个动作 |
| `1024×960` 被当作 4:3 | 明确区分 4:3 校正、方形像素和整数缩放；1024:960 是 16:15 |
| 默认强制 HQ4X，UI 每个 vsync 搬运整帧 | 默认自动性能档；逐帧发布，只在新 sequence 上传，GPU/Surface 负责缩放呈现 |
| UI 坚持纯 Java 代码构建、零第三方 UI 依赖 | 产品页面使用 XML 资源、AndroidX 与 Material Components；自绘仅保留游戏画面和手柄需要的部分 |
| 游戏库只是暂停菜单中的工具页 | Home/Library 是明确的产品入口；暂停页只是从游戏返回产品导航的一条路径 |

## 4. 总体架构

```text
┌────────────────────────────────────────────────────────────────┐
│ Product UI                                                     │
│ Home/Library · Game Screen · Pause Sheet · Settings · Licenses │
├──────────────────────┬─────────────────────┬───────────────────┤
│ InputRouter          │ SettingsRepository  │ GameRepository    │
│ touch/key/controller │ video/input/locale  │ source/title/meta │
├──────────────────────┴──────────┬──────────┴───────────────────┤
│ EmulationSession                │ SaveRepository               │
│ lifecycle/state/core ownership  │ per-ROM state/SRAM/atomic IO │
├──────────────────────┬──────────┴──────────────────────────────┤
│ AudioEngine          │ FramePublisher + FramePresenter         │
│ source clock/focus   │ sequence + buffers + display cadence    │
├──────────────────────┴─────────────────────────────────────────┤
│ JNI / C ABI / NestopiaUE                                       │
└────────────────────────────────────────────────────────────────┘
```

### 4.1 边界原则

- `MainActivity` 不再直接管理 core、AudioThread、存档文件和 Choreographer；这些职责进入独立组件。
- 只有 `EmulationSession` 拥有当前 `NesCore` 句柄并决定何时运行、暂停、替换和销毁。
- 所有输入源只向 `InputRouter` 提交语义动作，不直接调用 `NesCore.setInput()`。
- UI 不读取正在被 core 写入的 framebuffer；只消费 `FramePublisher` 已发布的完整帧。
- ROM 文件名不再是数据主键；ROM 内容 hash 是存档、收藏、最近记录和本地化标题的共同身份。
- 所有用户可见错误来自类型化错误码与 string resources，不从底层异常文本直接拼接 UI。

## 5. 核心组件设计

### 5.1 EmulationSession：唯一会话状态机

`EmulationSession` 在单线程 executor 上串行处理 core 命令，状态如下：

```text
EMPTY → LOADING → READY → RUNNING ⇄ PAUSED
                  │          │         │
                  └──────→ SWITCHING ←─┘
                                │
                             RUNNING

任何活动状态 → STOPPING → EMPTY
可恢复失败 → 回到原状态；不可恢复失败 → ERROR → EMPTY
```

状态语义：

- `EMPTY`：没有活动 ROM，无 core 线程和 AudioTrack。
- `LOADING`：首次创建候选 core 并加载 ROM；失败回到 EMPTY。
- `READY`：ROM 已加载，但未开始模拟。
- `RUNNING`：音频、模拟和帧发布活动。
- `PAUSED`：frame sequence、音频写入和 NES 输入均停止，core 状态仍在内存。
- `SWITCHING`：当前会话保持可恢复，同时在候选 core 中验证新 ROM。
- `STOPPING`：不可逆停止；stop-before-start 不能重新进入 RUNNING。
- `ERROR`：仅用于当前 core 无法继续的不可恢复错误；保存诊断后清理到 EMPTY。

所有状态转换返回 `SessionResult`。Activity 只观察状态和一次性事件，不持有 AudioThread 或 core 生命周期细节。

### 5.2 InputRouter：统一输入与命中契约

输入源包括：

- `VirtualGamepadView`：触屏摇杆、A/B、START/SELECT。
- `GameControllerInput`：Android KeyEvent、D-pad、标准 gamepad button 和 MotionEvent axis。
- `SystemActionInput`：独立暂停按钮、系统 Back、失焦和生命周期释放。

`InputRouter` 维护每个来源的当前位掩码，合并为不可变 `InputSnapshot`。NES 输入与 App 行为分成两个命名空间：

- NES：A、B、SELECT、START、UP、DOWN、LEFT、RIGHT。
- App：OPEN_PAUSE、CLOSE_PAUSE、OPEN_LIBRARY、OPEN_SETTINGS。

App 行为永远不能隐式生成 NES 按键。`cancelAll()` 清空所有来源并向 session 发布一次 mask=0。

### 5.3 VirtualGamepadView：人体工学与触觉

默认布局：

- 左下为固定摇杆；默认死区 18%，可在 12%–30% 调整。
- 右下 B 左/A 右，两键默认同尺寸；触控目标至少 48×48 dp，视觉直径默认 56 dp。
- A/B 中心距必须满足“两个 hit shape 不相交且视觉间隔至少 8 dp”。
- START/SELECT 位于右侧中上部，与独立暂停按钮分区；功能键目标至少 48 dp 高。
- 所有坐标基于 `WindowInsets` 计算的 safe rect；不把目标放入 cutout、三键导航或手势边缘。

命中规则：

- DOWN 时分配唯一 pointer role；同一控件已有 owner 时忽略第二指。
- MOVE 不把一个 pointer 偷换到另一个动作键；摇杆可以在自己的活动范围内连续移动。
- `ACTION_CANCEL`、窗口失焦、暂停、detach 和 Activity stop 均调用同一个 `cancelAll()`。
- hit map 在布局保存前进行碰撞检查；有重叠、越界或不可达目标时拒绝保存。

触觉规则：

- 设置提供“关闭 / 轻 / 标准 / 强”，默认“轻”；另有“区分 A/B”开关，默认开启。
- A 使用单次短 tick；B 使用较重 pulse 或短双击；START 使用中等确认脉冲；SELECT 使用轻 tick。
- 优先使用系统 haptic/predefined effect，能力不足时退化到不同节奏或时长；不承诺跨设备精确振动频率。
- 只在 DOWN 边沿触发，持续按住不重复；快速连打限流且不能阻塞输入发布。

布局设置支持三种预设：标准 B/A、交换 A/B、左撇子。Alpha 同时支持按钮大小、整体垂直偏移、透明度、摇杆大小/死区；自由拖拽编辑作为可选增强，只有在第 7 周前核心风险收敛时进入 Alpha。

### 5.4 FramePublisher 与 FramePresenter

模拟线程每次只执行一个 NES 帧。完成后把帧发布为：

```text
PublishedFrame = {
  sequence,
  bufferIndex,
  width,
  height,
  pitch,
  pixelFormat,
  machineMode,
  sourceTimestamp
}
```

使用双缓冲起步；若性能测试显示 producer/consumer 争用，再启用三缓冲。发布通过原子 sequence/index 完成，UI 永远不读取 producer 当前写入的 buffer。

`FramePresenter` 负责：

- 只在 sequence 变化时上传新纹理或提交新像素；重复显示同一帧不重复搬运。
- 正确处理 4:3 校正、方形像素、整数缩放和窗口 safe rect。
- 根据设置与设备能力选择 Auto/60/90/120 Hz 窗口模式；API 30+ 使用平台 frame-rate/display-mode API。
- NTSC 约 60.0988 个源帧、PAL 约 50.007 个源帧；120 Hz 只重复呈现源帧，不做运动插帧。

实现顺序分两步：

1. 先建立逐帧发布、双缓冲和 sequence，保留安全的 Surface 路径，关闭“每 vsync 重拷贝”。
2. 再以 OpenGL ES 纹理 presenter 替代 CPU HQ4X 全帧搬运；nearest/linear/低成本 shader 作为 Alpha 选项，HQ4X 不再是强制默认。

### 5.5 AudioEngine

- 音频继续作为模拟节拍来源，但一次只请求一个模拟帧；使用样本余数累加避免长期速度漂移。
- 内部排队目标不超过两个 NES 帧；处理 partial write、负错误和 `ERROR_DEAD_OBJECT`。
- 集成 AudioFocus、来电/闹钟竞争、`ACTION_AUDIO_BECOMING_NOISY` 和有线/蓝牙路由变化。
- 音频初始化、重建和停止均在会话线程完成；Activity 主线程不 join 5 秒。
- urgent-audio 线程不承担 CPU HQ4X；滤镜和呈现移出音频关键路径。

### 5.6 RomIdentity 与 SaveRepository

`RomIdentity` 以 core 返回的 SHA-1 为主键，CRC32 与规范文件名作为辅助信息：

```text
files/
  saves/<rom-sha1>/
    autosave.nst
    autosave.nst.bak
    battery.sav
    metadata.json
```

`SaveRepository` 负责：

- autosave、battery SRAM 和未来手动槽均按 ROM ID 隔离。
- 使用临时文件 + flush/fsync + 原子替换；Android 层优先使用 `AtomicFile` 语义。
- state bytes、ROM ID、core/state 版本、时间戳和校验信息作为一个事务写入。
- 写入失败保留上一份有效档，向 UI 发布可重试状态；不能只写日志。
- 读取先验证 state header、ROM hash 和 CRC；主文件损坏时尝试 `.bak`。

旧单文件 `autosave.nst` 的迁移：读取其 state header 中的 ROM hash，成功后复制到对应目录并保留原文件一个版本周期；无法验证时不自动删除或绑定到当前 ROM。

### 5.7 ROM 切换事务

切换新 ROM 时不在当前 core 上直接调用破坏性的 load：

1. 当前 session 进入 SWITCHING，冻结输入但保留当前 core。
2. 先完成当前 ROM 的 state/SRAM 快照。
3. 创建候选 `NesCore`，应用音频/视频配置并加载新 ROM。
4. 候选成功后读取 RomIdentity、绑定其 SaveRepository、恢复自己的 state/SRAM。
5. 启动候选会话并原子替换 current core；随后销毁旧 core。
6. 任一步失败都销毁候选、恢复原 session 和原输入状态，并显示可操作错误。

### 5.8 SettingsRepository

设置采用版本化 schema，按职责拆分：

- Video：aspectMode、filterMode、refreshMode、integerScale。
- Controls：layoutPreset、buttonScale、verticalOffset、opacity、joystickScale、deadZone、hapticLevel、distinctABHaptics。
- Audio：enabled、focusPolicy。
- General：appLocale、autosaveEnabled、lastPlayedRomId。

读取未知/旧值时使用明确默认值并记录一次迁移；设置预览可以临时应用，用户取消时回滚。损坏设置不能阻止 App 启动。

### 5.9 GameRepository 与离线标题元数据

游戏库拆分为三类数据：

- `GameSource`：SAF tree/document 来源、授权状态、上次扫描时间。
- `GameEntry`：RomIdentity、原文件名、来源、大小、region、lastPlayedAt、playCount、favorite。
- `LocalizedGameMetadata`：canonical title、zh-Hans title、aliases、region/revision 匹配信息。

要求：

- 首次启动始终显示内置试玩与“添加文件/目录”；不出现“1 个游戏但还没有游戏”。
- 常驻添加、刷新、管理来源入口；授权失效时提供重新绑定，不清空其他来源。
- 扫描返回结构化结果：entries、skipped、warnings、fatalError。fatal error 保留旧库。
- ZIP 在扫描阶段解析目录；多 ROM ZIP 以 archive entry 为稳定子项，不在启动时任意选第一个 `.nes`。
- 搜索覆盖 localized title、canonical title、aliases 和原文件名；使用 Unicode normalization 与 locale-independent casefold。
- 排序显示使用 locale-aware Collator；ROM hash 相同的不同包装不产生重复游戏记录。

离线标题库使用版本化 JSON asset，先覆盖合法测试集和常见标题；每条数据记录来源、匹配键与审阅状态。无法命中时回退到规范化文件名，不伪造翻译。

## 6. 产品导航与视觉系统

### 6.1 信息架构

```text
Home / Library
├─ 最近玩
├─ 收藏
├─ 内置试玩
├─ 本地游戏
└─ 添加/管理来源

Game Screen
├─ 游戏画面
├─ 虚拟手柄
└─ 独立暂停按钮

Pause Sheet
├─ 继续
├─ 自动存档状态
├─ 设置
├─ 游戏库
└─ 退出当前游戏
```

系统 Back 规则：游戏中首次 Back 打开 Pause Sheet；Pause Sheet 中 Back 继续游戏；二级页面 Back 回到上一级并保留搜索/滚动状态；只有 Home 根页面再次 Back 才退出。

### 6.2 UI 技术与设计 token

- Java 17 + Android Views；产品页面迁移到 XML layout。
- 使用 AndroidX AppCompat/Activity、RecyclerView、Preference 和 Material Components；不引入 Compose。
- 自绘 View 只用于游戏手柄；游戏画面由专用 presenter 管理。
- 颜色、字号、间距、圆角、elevation、icon 尺寸和交互状态全部资源化。
- 采用克制的复古工具风：深色中性 surface、单一高识别强调色；像素元素用于品牌，不用于长文本字体。
- 提供 adaptive、round、monochrome launcher icon；App 内结构图标全部使用统一 vector drawable，不使用 emoji。
- 所有文本和非文本控件分别达到 4.5:1 与 3:1 对比度目标。

### 6.3 i18n 与可访问性

- 完整英文基准资源 `values`，简体中文资源 `values-zh-rCN`，并声明 locale config。
- 数量使用 plurals，动态消息使用格式资源，内部技术/品牌字符串明确 `translatable=false`。
- CI 运行 pseudolocale；至少验证 en、zh-Hans、tr-TR、en-XA、ar-XB。
- 手柄通过虚拟 accessibility node 或语义化子 View 暴露 A/B/START/SELECT/方向/暂停节点、边界和状态。
- 排序、选择、加载状态具有 selected/state description 和 live region；200% 字体下页面可滚动且不裁切。

## 7. 错误模型与降级策略

错误分为：

- `Recoverable`：坏 ROM、来源授权失效、AudioTrack dead object、120 Hz 不可用、滤镜分配失败、autosave 写失败。
- `FatalSession`：当前 core 不可恢复、state 与 ROM 不匹配且无备份、native 内部一致性失败。
- `FatalApp`：仅保留给无法初始化 native library 等启动级错误。

降级规则：

- 新 ROM 加载失败 → 保留并恢复旧游戏。
- 高成本滤镜失败 → 降级到低成本/nearest，不改变 core filter 状态后继续写旧 buffer。
- 120/90 Hz 请求失败 → 回到 Auto/60，设置页说明设备不支持。
- haptic 能力不足 → 无声退化为支持的节奏；输入不受影响。
- autosave 失败 → 保留旧档，在 Pause Sheet 显示失败和重试；不能假装成功。
- 扫描 fatal error → 保留旧列表并提供重新授权/重试；不能保存空结果覆盖旧库。

本地诊断记录最近一次 session transition、ROM ID 前 8 位、错误码、AudioTrack/refresh mode 和 save 结果；不记录 ROM 内容或用户路径全文。

## 8. 两人协作与交付边界

### 8.1 Developer A：Android 产品体验主责

- InputRouter、VirtualGamepadView、haptic、GameControllerInput。
- XML/Material 主题、Home/Library、Pause Sheet、Settings、insets、Back。
- i18n、标题解析、搜索/排序、无障碍和 UI instrumentation。

### 8.2 Developer B：Native/运行时主责

- EmulationSession、AudioEngine、帧单步发布、buffer/sequence。
- FramePresenter/OpenGL ES、显示模式和性能测量。
- RomIdentity、SaveRepository、battery bridge、候选 core ROM 切换和 native fault tests。

### 8.3 共享职责

- 第 1 周共同锁定接口和测试 doubles；任何跨边界变更需另一人评审。
- 每周至少一次集成主线；不允许两条轨道各自工作数周后一次性合并。
- Developer A 不直接绕过 Session 调 core；Developer B 不在 native 层生成用户文案或读取 Android 文件路径。
- 第 4、7、9 周设置集成门禁，未通过时先修复，不继续扩大功能范围。

## 9. 交付阶段与依赖

### 阶段 A：可靠性基线（第 1–2 周）

- Android/app CI、unit/instrumentation 骨架、API 36 构建。
- InputRouter、START/暂停拆分、互斥 hit map、CANCEL 全释放。
- EmulationSession 状态机和 stop-before-start 测试。
- SettingsRepository 与 RomIdentity 接口定型。

### 阶段 B：运行时与数据安全（第 2–4 周）

- `runFrames(1)`、样本余数、FramePublisher 双缓冲和 sequence。
- per-ROM autosave/SRAM、AtomicFile、旧存档迁移。
- 候选 core ROM 切换、坏 ROM 回滚、filter OOM 安全。
- AudioFocus、dead object 和后台停止。

### 阶段 C：产品体验（第 4–7 周）

- Material/XML 主题、adaptive icon、Home/Library、Pause Sheet。
- 正确画面比例、safe rect、OpenGL ES presenter、刷新设置。
- 控制预设、触觉档位、设置页和布局校准。
- 游戏库来源管理、最近/收藏和结构化错误状态。

### 阶段 D：本地化与输入覆盖（第 6–8 周）

- 英文/简中资源、pseudolocale、离线标题数据与中英文搜索。
- 标准键盘/USB/蓝牙手柄映射。
- TalkBack、200% 字体、选中/加载语义。

### 阶段 E：发布候选（第 8–10 周）

- 60/90/120 Hz 真机、低端/中端设备、导航模式和生命周期矩阵。
- 性能、音频延迟、热、存档故障注入和 30 分钟稳定性。
- GPL 源码链接、内置 ROM 授权证据、target API 36 和发布清单。
- 只修阻断问题；第 8 周后不再接受新的 Alpha 功能。

关键依赖：

```text
EmulationSession ─→ Pause/Back ─→ Product navigation
FramePublisher ─→ FramePresenter ─→ Refresh settings
RomIdentity ─→ SaveRepository ─→ Recent/Favorite/Localized title
SettingsRepository ─→ Video/Controls/Locale UI
InputRouter ─→ Touch preset + Controller + Accessibility actions
Design tokens ─→ Home/Library + Pause + Settings + Licenses
```

## 10. 测试策略与发布门禁

### 10.1 PR 必过

- `:app:assembleDebug`
- `:app:lintDebug`，新增 warning 不得进入主线；目标逐步降到 0。
- `:app:testDebugUnitTest`
- host core test 与 ABI/header check。
- 输入命中、session state、RomIdentity、save transaction、title normalization 单元测试。

### 10.2 模拟器门禁

- API 24 与 API 36 代表性 smoke。
- START/暂停、三指 CANCEL、ROM A→B→A、坏 ROM 回滚、强杀写档、来源权限失效。
- en、zh-Hans、en-XA、ar-XB 与 200% 字体截图回归。

### 10.3 真机门禁

- 60 Hz 与 90/120 Hz 至少各一台；手势导航和三键导航各一次。
- 蓝牙/USB 手柄、耳机拔出、蓝牙断开、来电/闹钟抢焦点。
- 连续 30 分钟：无 crash/ANR、无严重热状态、模拟速度误差 <0.5%。
- NTSC 独立帧率 59.9–60.1、PAL 49.9–50.1；10 分钟源帧遗漏率 <0.5%。
- 1000 次快速 resume→pause 后 250 ms 内无活动模拟/音频线程。
- A/B 盲按误触率目标 <1%；区分触觉开启时识别正确率目标 ≥90%。
- autosave 写入任意中断后只能得到完整旧档或完整新档。

## 11. 发布与合规

- `compileSdk/targetSdk` 升到 36，minSdk 保持 24；回归 Android 15/16 edge-to-edge 和大屏行为。
- App 内源码链接指向与 versionCode 对应的不可变 tag/commit；发布归档包含构建说明。
- 每个内置 ROM 保存来源、作者、下载时间、SHA-256、明确许可原文和审核记录；证据不足时移除替换。
- 许可页使用可导航组件列表和可点击源码 URL，不再拼接成单个文本墙。
- SAF 只请求所需读权限；隐私说明明确 ROM、目录 URI、存档和游戏记录完全离线。

## 12. 范围控制与变更规则

- 第 1–4 周只接受阻断 P0 或接口必需变更。
- 第 5–7 周允许调整视觉和人体工学参数，但不改变 Session、Frame、RomIdentity 主接口。
- 第 8 周冻结 Alpha 功能；后续新增功能进入 Later backlog。
- 自由拖拽布局、完整中文 ROM 数据库、高级 shader、手动多槽存档若影响第 9 周稳定性，自动移出 Alpha，不阻断公开试玩。

## 13. 设计验收清单

- [x] 输入、会话、帧、存档、设置、游戏库边界明确。
- [x] START/暂停、A/B、120 Hz、per-ROM 存档的行为无歧义。
- [x] 正常、失败、降级和旧数据迁移路径已定义。
- [x] 两人所有权、集成门禁和冻结规则已定义。
- [x] 每个用户目标都有可测量的 Alpha 验收指标。
- [x] 与旧设计冲突的决策已明确列出并取代。
- [x] 没有依赖联网服务或未选定的实现占位。
