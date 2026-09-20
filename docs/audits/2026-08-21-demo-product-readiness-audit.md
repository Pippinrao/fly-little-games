# FlyNES Demo → 可用产品：能力缺口与整改优先级审计

审计日期：2026-08-21  
审计基线：`main@084351b`  
结论适用范围：当前 Android Demo、Java/JNI 桥接、NestopiaUE 核心、游戏库、触控层及现有发布材料。

## 1. 结论先行

当前版本已经证明了“Android 上能加载并运行 NES ROM”的技术纵切，但还不能称为稳定 Alpha。它目前最急需补齐的并不只是视觉皮肤，而是五项产品底座：

1. **输入正确性**：START 与 App 菜单冲突；A/B、START/SELECT 命中区重叠；多指取消可能卡键；没有安全区、布局预设和外部手柄。
2. **帧发布与高刷架构**：虽然显示层可在约 60 Hz 提交，核心却以两帧一批运行，稳定可见的独立 NES 帧趋近 NTSC 30 Hz / PAL 25 Hz；120 Hz 下仍大量重复帧并出现发布竞争。
3. **数据安全与生命周期**：所有 ROM 共用一个自动存档，电池 SRAM 也没有按 ROM 隔离；写盘非原子；暂停路径可能阻塞主线程或复活已停止线程。
4. **产品导航、视觉与设置系统**：默认系统图标、emoji 充当结构图标、OEM 对话框、错误的 4:3 比例、低对比度、无设置页、无明确首页/暂停层。
5. **游戏库、i18n 与发布门禁**：资源体系只有 App 名；中文游戏名、别名搜索、目录管理、异常恢复缺失；目标 API、GPL 源码入口和内置 ROM 授权证据尚不能支撑发布。

**建议的产品判定：** 当前可继续作为内部技术 Demo；在 P0 全部完成前，不建议扩大外部试玩，更不建议以“支持高刷”“自动存档可靠”或“可发布版本”对外宣传。

## 2. 审计方法与证据边界

### 2.1 实际完成的测试

- 从干净源码构建 `:app:assembleDebug`，构建成功。
- 运行 `:app:lintDebug`，结果为 **0 error / 11 warning**；其中包括本地化、默认 Locale、旧 target API、自定义触控可访问性和固定横屏警告。
- 在 Android API 35、x86_64、2340×1080、440 dpi 模拟器中安装并试玩。
- 实测启动、方向键、A、B、START、暂停菜单、游戏库、搜索、许可页、首次安装、200% 字体、横竖屏切换。
- 注入 Android A/B/方向键 KeyEvent，检查外部控制器路径。
- 分别在 60 Hz 与强制 120 Hz 显示环境采集 SurfaceFlinger 帧时间和线程 CPU。
- 执行现有 host core test，结果 `PASS (0 failures)`；它只证明部分核心功能，不覆盖 Android 调度、Surface、AudioTrack、存档事务和多点触控。

### 2.2 证据标签

- **[实测]**：本次模拟器中可重复观察或测量。
- **[代码证实]**：控制流、数学关系或数据模型可从当前源码确定。
- **[待真机]**：模拟器无法替代物理触屏、扬声器、振动马达、热管理、刘海/曲面屏或蓝牙手柄的部分。

### 2.3 测试限制

- 模拟器 CPU、音频延迟和热表现不能代表真机，所以 CPU 百分比与约 169 ms 的 AudioFlinger 延迟只用作风险信号，不作为真机性能结论。
- 未获得多款商业 ROM 做兼容性覆盖；ROM 兼容率、mapper 覆盖和每款游戏的 START 行为仍需合法测试集。
- 未在真实 90/120/144 Hz 手机、折叠屏、刘海屏、低端设备、TalkBack、手柄和振动马达上完成验证。

## 3. 用户已指出问题的实测结论

| 用户反馈 | 审计结论 | 优先级 |
|---|---|---|
| 界面丑、没有美学设计和 icon | 属实。Manifest 使用系统占位图标；库用 🎮/🔍/⭐ 充当图标；游戏、库、许可和暂停菜单是四套视觉语言，且存在低对比度。 | P1，但应在下一次公开 Demo 前完成 |
| 没有高刷新率 | 属实，而且根因比“没有打开 120 Hz”更深：独立模拟帧发布约为 30/25 Hz，直接请求 120 Hz 只会加倍重复拷贝和功耗。 | P0 |
| 操作按钮混乱、位置大小随意、拨杆误触 | 属实。命中区存在重叠，未使用安全区，布局固定，摇杆热区伸出左侧，系统导航仍可见。 | P0 |
| 没有 i18n、常见游戏无中文名 | 属实。`strings.xml` 只有 App 名；没有 locale 资源、localeConfig、标题数据库或中文别名搜索。 | P1 |
| A/B 容易按错，希望不同振动反馈且可关闭 | 已复现。振动建议纳入，但必须先修命中模型；精确“频率”并非所有 Android 马达都能稳定表达，应采用可回退的不同触觉节奏/强度。 | P0 |

## 4. P0：下一次外部试玩前必须修复

### P0-1：拆开 NES START 与模拟器暂停菜单

**证据**

- [代码证实] `GamepadView` 每次点击 START 都先向 NES 发送 START，再调用 App 暂停菜单。
- [代码证实] 当前 AlertDialog 出现时并没有停止 core、音频或渲染；点击“继续游戏”还会再发送一次 START。
- [实测] 在 From Below 标题画面点击 START，FlyNES 菜单立即弹出；取消菜单后，游戏已经进入下一级选项画面。也就是说，“开始游戏”这个最基本操作就被 App 菜单劫持。

**用户影响**

- 标题页不能自然开始；有些游戏会连续收到两次 START。
- 不把 START 当暂停键的游戏会在系统菜单背后继续运行。
- 玩家无法理解哪个是“游戏 START”、哪个是“模拟器暂停”。

**整改**

- START 只发送一次 NES 输入，不再承担 App 菜单职责。
- 在安全区内新增独立、清晰的暂停/菜单图标；系统 Back 首次触发真正暂停，而不是直接退出。
- 打开暂停页时冻结模拟帧、音频和输入，统一释放所有触点；恢复时不注入任何 NES 按键。

**验收**

- START 只产生一次 40–80 ms 脉冲，不弹 App 菜单。
- 暂停后 frame sequence 和音频写入均停止；恢复回到同一帧。
- 标题页、游戏中暂停、没有暂停能力的 ROM 三类用例都通过。

### P0-2：重做触控命中模型，先消除 A/B 误触，再加触觉反馈

**已定位的根因**

- [代码证实] A 半径 32 dp、B 半径 40 dp，中心距只有 80 dp；命中算法又把半径平方乘以 1.6。换算后两键有效半径总和约 91.1 dp，命中区重叠约 **11.1 dp**。
- [代码证实] 重叠时先检查 B，因此重叠区永远判为 B。
- [实测] 点击 A、B 中心分别得到正确输入；点击两者视觉间隙/重叠位置稳定得到 B，复现了用户所说的“明明想按 A 却触发 B”。
- [代码证实] START/SELECT 扩大命中区也重叠约 4.8 dp。
- [代码证实] 摇杆命中半径约 112 dp，中心离左边仅 80 dp，热区伸出屏幕左侧约 32 dp，容易与系统返回手势竞争。
- [代码证实] `ACTION_CANCEL` 只清除一个 pointer；多指被系统取消后，其他方向或按键可能保持按下。

**正确的改造顺序**

1. 用互斥命中区域替代相互重叠的圆；边界按最近中心裁决，任何坐标只能属于一个动作。
2. 默认提供符合常见 NES 肌肉记忆的 **B 左 / A 右** 预设，同时保留“交换 A/B”“左撇子”“单手”等预设。
3. 每个触控目标至少 48×48 dp，相邻目标建议保留至少 8 dp 的无歧义间隔；使用 `WindowInsets`、cutout 和系统手势安全区。
4. `ACTION_CANCEL`、失焦、打开暂停页、detach 时统一发送全按键释放并清空所有 pointer。
5. 再加入布局微调：大小、间距、垂直位置、透明度、摇杆死区、拖动校准和恢复默认。

**A/B 触觉反馈方案**

- 设置中提供总开关，并给出“关闭 / 轻 / 标准 / 强”档位；另可提供“区分 A/B”开关。
- A：一次短、清脆的 tick；B：一次更重的 pulse，或短促双击节奏。START 可用中等确认脉冲，SELECT 保持轻触。
- 优先使用系统语义化 haptic 或预定义 `VibrationEffect`；不支持 composition/幅度控制的设备自动退化为不同的时长/节奏。**不要把“不同 Hz 频率”当成跨设备保证。**
- 仅在按下边沿触发，不因持续按住而重复；连续连打要限流，不能阻塞输入线程，也不能成为区分按键的唯一线索。

**验收**

- 坐标边界单测保证 A/B、START/SELECT 无双重归属。
- 三指按住后注入 CANCEL，下一帧 mask 必须为 0，随后所有控件可重新操作。
- 真实手机完成至少 10 分钟盲按任务；A/B 误触率目标 <1%，无系统手势误退出。
- 开启区分触觉后，受试者不看屏幕识别 A/B 的正确率目标 ≥90%；关闭后零振动调用。

### P0-3：先恢复约 60/50 个独立模拟帧，再谈 90/120 Hz 呈现

**实测和代码结果**

- [实测] 60 Hz 环境下，Surface 层提交约 **59.95 fps**，p95 间隔约 17.97 ms；表面上像 60 fps。
- [代码证实] 音频线程每次 `runFrames(2)`，同一个 framebuffer 连续被覆盖两次，UI 只在调用返回后较稳定地看到第二帧。因此稳定独立更新趋近 **NTSC 30.075 Hz / PAL 25.026 Hz**，不是 60/50。
- [代码证实] 音频线程写 framebuffer 的同时，UI 线程可读取同一块内存；没有双缓冲、锁或发布序号，存在撕裂和 C++ 数据竞争。
- [实测] 在支持 120 Hz 的模拟器中，App 默认仍只工作在 60 Hz，因为没有请求显示模式/帧率。
- [实测] 强制系统 120 Hz 后，Surface 实际仅约 **75.93 fps**；p50 间隔约 15.25 ms，73/126 个间隔超过 12 ms。主线程占用由约 51% 上升到约 63.5%（模拟器数据），却没有增加独立 NES 帧。

**产品定义要纠正**

- NTSC NES 原生目标约 60.10 fps，PAL 约 50.01 fps；所谓“120 Hz 支持”不应该伪造 120 个游戏帧。
- 正确行为是每个模拟帧发布一次：120 Hz 屏对 NTSC 大致重复两次；90 Hz 屏使用稳定的 3:2 cadence；PAL 根据显示能力选低抖动策略。
- 像素游戏默认不建议做运动插帧，它会引入伪影和额外延迟。

**整改架构**

- `runFrames(1)` 或等价的逐帧发布；模拟、音频节拍和显示呈现解耦。
- 使用双/三缓冲 + 原子 frame sequence；渲染只读取完整发布帧。
- 把 HQ4X/缩放移到 GPU texture/shader 路径；只有新 sequence 才上传/呈现，避免每个 vsync 在 UI 线程复制 1.875 MiB。
- API 30+ 使用 `Surface.setFrameRate`/窗口显示模式请求；提供“自动 / 60 / 90 / 120 Hz”设置，并根据电量、温度和可用模式回退。

**验收**

- NTSC 独立发布率 59.9–60.1，PAL 49.9–50.1；10 分钟源帧遗漏率 <0.5%。
- 120 Hz 下 NTSC 每帧稳定重复约 2 次；90 Hz 为可预测 3:2 cadence。
- 新 NES 帧之外的 `nativeBlit`/纹理上传次数不超过 1%；并发压测在 TSan 下零 race。
- 真机连续 30 分钟模拟速度偏差 <0.5%，无严重热状态，输入到画面 p95 目标 <50 ms。

### P0-4：按 ROM 隔离存档与 SRAM，并保证写入原子性

**证据**

- [代码证实] Java 层只有一个固定 `autosave.nst` 和一个 hash；切到另一 ROM 后会覆盖上一款游戏的自动存档。
- [代码证实] core 只有一个不带 ROM key 的 `battery_sram`，新 ROM 可能复用旧 ROM 数据；Android 也没有把 battery file-I/O 回调桥接出来。
- [代码证实] autosave 直接截断正式文件后写入；hash 又通过单独的异步 preference 提交。掉电、强杀或存储错误会产生“新 hash + 旧档”“旧 hash + 新档”或截断文件。
- [代码证实] 冷启动固定载入内置 ROM，没有“恢复上次游玩游戏”的产品模型。

**整改**

- 以 core ROM SHA-1/SHA-256 为稳定 ID，建立 `saves/<rom-id>/autosave`、手动槽和 SRAM。
- 临时文件写入 → flush/fsync → 原子 rename，并保留上一份有效档；hash、metadata 和 state 作为同一事务。
- 记录 last-played ROM；旧单文件存档只迁移一次，迁移失败不得删除原文件。
- 明确备份策略：扫描缓存/SAF URI 不备份，用户存档可选择备份或导出；换机后重新绑定目录并按 hash 关联。

**验收**

- A→B→A、强杀→重启、设备重启三条路径均恢复各自进度。
- 至少 100 个 ROM 的 autosave/SRAM 互不覆盖。
- 对每个写入 offset 注入进程终止后，只能恢复完整旧档或完整新档，不能丢失最后一份好档。

### P0-5：修复生命周期、native 失败和坏 ROM 回滚

**代码已证实的高风险路径**

- `AudioThread.stopLoop()` 与 `run()` 的状态机不能区分“尚未启动”和“已请求停止”；极快地前台→后台可能把已停止线程重新启动。
- `onPause()` 最坏在主线程 join 5 秒，并同步分配/复制最多 4 MiB state 和写盘，接近 ANR 风险区。
- HQ4X 在 `realloc` 前先修改 filter；内存分配失败会留下“新 pitch + 旧小 buffer”的不一致状态，下一帧存在 native 越界写路径，JNI 又把返回错误丢弃。
- 加载坏 ROM 时，Nestopia 会先卸载当前 ROM；失败后 App 没有回滚，原本可玩的会话冻结。
- AudioTrack partial write、dead object、初始化失败、音频焦点和耳机/蓝牙断开均无完整恢复策略。

**整改**

- 音频/模拟线程使用明确的 NEW→RUNNING→STOPPING→STOPPED 状态机，stop-before-start 必须不可逆。
- 生命周期主线程只发停止信号和拍摄一致性快照；序列化、压缩、fsync 转后台事务。
- filter 切换采用“先成功分配新资源，再原子提交”；错误传回 Java 并降级到 NONE。
- 新 ROM 在临时 core/可回滚事务中验证成功后再替换当前会话。
- 集成 AudioFocus、becoming noisy、路由变化和 AudioTrack 重建。

**验收**

- 1000 次快速 resume→pause 后，250 ms 内活动模拟/音频线程为 0。
- `onPause` p99 <200 ms、最大 <500 ms；无 ANR、无孤儿线程。
- allocator fault + ASan/HWASan 下失败可安全降级；坏 ROM 选择后 500 ms 内继续原游戏。

### P0-6：若目标包含商店发布，立刻处理 API 与授权门禁

- 当前 `compileSdk/targetSdk` 均为 34。Google 官方要求显示：**2026-08-31 起**，普通 Android 新 App/更新需 target Android 16 / API 36；当前日期距门槛仅 10 天。[Google Play 官方要求](https://developer.android.com/google/play/requirements/target-sdk)
- App 内“源代码”仍指向 `https://github.com/` 占位地址，不足以支持 GPL 对应源码交付声明。
- 内置 From Below 的本地说明自己承认上游仓库没有 LICENSE；[上游 GitHub](https://github.com/mhughson/mbh-firstnes) 能证明作者和源码来源，但当前页面未给出足以独立确认二进制再分发权的明确许可。应取得作者书面授权/上游许可原文，或换成许可链清晰的测试 ROM。这里是“证据不足”的发布风险判断，不是侵权法律结论。

**验收**

- target/compile 36，API 24/30/33/35/36 的安装、SAF、生命周期和 edge-to-edge 回归通过。
- 已签名 APK 中源码链接可点击，定位到与 versionCode 对应的不可变 tag/commit 和可构建源码归档。
- 每个内置二进制都有来源 URL、下载时间、SHA-256、版权人、精确许可原文和审核记录。

## 5. P1：把“能玩”升级为“愿意持续玩”

### P1-1：建立完整的视觉系统，而不是只换一层皮肤

**当前问题**

- Launcher 使用 Android 系统默认占位图标；没有 adaptive、round 或 monochrome icon。
- 游戏库用 emoji 充当结构图标，外观受系统字体/OEM 影响。
- 游戏页为黑色 + Canvas 手柄，库为蓝黑渐变，许可页是纯黑等宽文本墙，暂停页是浅色 OEM AlertDialog；缺少统一品牌。
- 未选排序按钮黑字落在暗背景；底部提示约 1.85:1；白色半透明手柄遇亮色游戏画面可能接近不可见。
- 控件没有一致的 pressed/selected/disabled/focus 状态，按钮按下时还通过放大改变视觉边界。

**建议方向**

- 适合本项目的是“克制的复古游戏工具”而不是堆叠像素装饰：深色中性底、单一高识别强调色、像素元素只用于品牌/封面，功能文字保持高可读。
- 先定义 design token：8 dp spacing grid、圆角层级、surface/on-surface/primary/error、文字层级、icon 尺寸、elevation、交互状态和 motion。
- 建立统一 vector icon 集；提供 adaptive/round/themed launcher icon。
- 暂停菜单改成 App 自己的全屏/底部面板，显示当前 ROM、自动存档状态、继续、设置、游戏库、退出。

**验收**

- 普通文字对比度 ≥4.5:1，大字和非文本控件 ≥3:1；最亮/最暗 ROM 帧上手柄都清晰。
- App 内不再用 emoji 充当导航/状态图标；API 24–36 多种 Launcher mask 显示正确。
- 所有可点击元素 100 ms 内有稳定 pressed 反馈；选中/禁用状态可由视觉和语义同时识别。

### P1-2：修正画面比例、缩放和系统安全区

- 当前代码注释写“4:3”，实际使用 `1024/960 = 16:15`；[实测] 2340×1080 横屏中的游戏 Surface 是 1152×1080，而正确 4:3 应为 1440×1080，画面横向被压窄约 20%。
- 窄窗口只收缩宽、不同比例收缩高，会在分屏/折叠场景再次变形。
- 没有 `WindowInsets`/cutout 处理，系统导航按钮仍显示在右侧，B 靠近系统操作区。

**需要的显示能力**

- “4:3 校正 / 方形像素 / 整数缩放”三种明确模式，附即时预览和解释。
- `WindowMetrics + WindowInsets` 生成 safe rect，尺寸变化时重新布局 Surface 与手柄。
- 游戏态采用稳定 immersive 行为，同时提供可靠的暂停/退出入口，不能让玩家被困住。

### P1-3：建立设置中心和控制布局编辑器

首版至少覆盖：

- 视频：比例、整数缩放、滤镜（NONE/低成本/HQ4X）、刷新策略、帧率显示可选。
- 控制：B/A 布局预设、交换 A/B、按钮大小/间距/位置/透明度、摇杆大小/死区、触觉档位、恢复默认。
- 音频：开关、延迟/兼容策略、焦点行为。
- 系统：App 语言、游戏库来源、自动存档、导入/导出、许可和隐私。

设置必须版本化、跨重启持久化；旧值无效时安全回退。控制布局编辑器需要 safe-area 限制、碰撞提示和“试玩/确认”，避免用户把按钮拖到不可达区域。

### P1-4：把游戏库改成产品首页，而不是隐藏在 START 后面的工具页

**实测问题**

- 清空数据后的首次进入同时显示“共 1 个游戏”和“还没有游戏”，内置游戏实际上被隐藏。
- 一旦扫描过用户 ROM，唯一的“选择 ROM 目录”按钮随 empty state 消失，无法添加、换目录或重新扫描。
- 搜索 `zzzz` 后得到纯空白页，没有“无结果”和清除搜索动作。
- 内置条目出现 `From Below (内置) [内置]`，副标题又写“内置 ROM”。
- ZIP 只按扩展名加入库，启动时任意取第一个 `.nes`；权限/扫描异常又会被显示成“扫描完成：0 个游戏”。

**需要的库能力**

- 首次打开进入 Home/Library：最近玩、收藏、内置试玩、本地游戏和显著的“添加文件/目录”。
- 常驻“添加、刷新、管理来源”；支持多来源，或对替换单来源给出明确确认。
- 用结构化 `ScanResult` 区分成功、跳过、权限失效、损坏 ZIP 和 fatal error；fatal 时保留旧库。
- 以 ROM hash 为稳定 ID，维护最近、收藏、游玩次数和存档状态；同一内容的 `.nes`/zip 不重复。
- 搜索无结果、只有内置、权限失效、目录为空分别使用独立状态模型。

### P1-5：真正实现 i18n 与中文游戏标题能力

**当前证据**

- `res/values/strings.xml` 只有 `app_name`，没有 `values-zh-*`、localeConfig、plurals 或伪语言流程。
- Lint 本次直接报出 5 个 `SetTextI18n` 和 3 个 `DefaultLocale`；更多运行时拼接/Canvas 文本不一定会被 Lint 完整捕获。
- GameEntry 只有原始文件名；搜索只查原文件名；排序用 `String.compareTo`。
- `Popularity` 虽含少量中英别名，但只用来产生静态“热度分”，既不展示本地化标题，也不能用中文搜索。

**正确的数据模型**

- UI 文案：默认 `values` + `values-zh-rCN`/`zh-Hans`，需要时再加繁中；计数用 plurals，动态内容用格式资源；Android 13+ 声明 App language。
- 游戏元数据：`romId/hash`、原文件名、规范标题、localized title、aliases、region、revision、语言、用户自定义名。
- 中文检索：例如 `Contra (USA) (Rev A).nes` 可用“Contra”或“魂斗罗”搜索；fallback 为“本地化标题 → canonical 标题 → 规范化文件名 → 原文件名”。
- 搜索使用 Unicode normalization + locale-independent casefold；显示排序使用 ICU Collator，而不是默认字符串比较。

**验收**

- 声明语言资源覆盖率 100%，硬编码 UI 文案门禁为 0。
- en、zh-Hans、zh-Hant、tr-TR、`en-XA`、`ar-XB` 和 200% 字体自动截图/交互通过。
- 中文/英文别名均可搜索，同 hash 不因文件名或 ZIP 包装重复；用户始终可查看原文件名。

### P1-6：外部手柄、键盘与无障碍不是“以后再说”

- [实测] 注入 BUTTON_A、BUTTON_B 与 DPAD KeyEvent，App 没有产生 NES 输入；源码也没有 `KeyEvent`/`onGenericMotionEvent` 映射。因此 USB/蓝牙手柄和键盘目前不可用。
- [代码证实] 整个自绘手柄对 TalkBack 只是一个无名 Canvas View，没有 A/B/START/SELECT 节点、标签、状态、`performClick` 或替代操作。

**整改**

- 支持 Android 标准 Game Controller API、摇杆轴、D-pad、按键映射/重映射、断线重连和多控制器识别。
- 自绘控件优先拆成语义化子 View；否则用虚拟 accessibility node 暴露名称、边界、按下状态和 click action。
- TalkBack 能逐一访问控件；排序控件宣布“已选择”；扫描进度通过 live region 通知。

### P1-7：建立自动化质量门禁与可诊断性

当前 CI 只构建/测试 host C++ core，不编译、Lint 或测试 Android App。需要补齐：

- JVM：标题规范化、Locale、ROM ID、JSON/schema migration、ZIP/header parser、存档路径。
- Native：allocator fault、state/sram 隔离、帧 sequence、样本余数、sanitizer。
- Instrumentation：多点触控取消、START/暂停、SAF 权限撤销、60/90/120 Hz、生命周期、AudioTrack 失败。
- 设备矩阵：API 24/30/33/35/36，小屏/平板/折叠、手势/三键导航、60/90/120 Hz、en/zh/pseudolocale。
- 发布门禁：assemble、lint、unit、emulator smoke、真实源码 URL、无占位资产、内置内容许可证据。

## 6. P2：P0/P1 稳定后的增强项

- 手动存档槽、缩略图、导入/导出与冲突恢复。
- 更丰富的 GPU shader，但低成本滤镜必须始终可选；HQ4X 不应成为所有设备硬编码默认值。
- ROM 兼容性数据库、mapper/region 信息与问题报告导出。
- 用户可选的性能 HUD、帧 pacing 诊断和音频 underrun 诊断。
- 首次引导、控制校准、布局分享；不要在核心输入正确性完成前先做华丽动效。

## 7. 推荐路线图（按依赖关系排序）

### Now：v0.2 “可信可玩”（P0，约 4 个 L 级工作包）

| 工作包 | 关键交付 | 依赖/说明 |
|---|---|---|
| 输入系统 v2 | START/暂停拆分、互斥 hit map、CANCEL 全释放、安全区、B/A 预设、触觉开关 | 先完成命中模型，再做布局编辑器 |
| 帧发布 v2 | 单帧发布、双/三缓冲、sequence、GPU 呈现、Auto/60/90/120 | 高刷设置依赖发布协议，不可反过来 |
| 数据与生命周期 v2 | per-ROM autosave/SRAM、原子写、last-played、线程状态机、异步暂停保存 | ROM hash 是存档、收藏和标题库共同主键 |
| 错误/发布门禁 | 坏 ROM 回滚、HQ4X OOM 安全、AudioTrack 恢复、target 36、源码 URL、内置 ROM 授权 | 若不发布商店，合规可并行但不能遗漏 |

**Now 退出条件：** 没有输入卡死/重叠；START 正常；NTSC/PAL 独立帧率正确；A/B ROM 存档互不污染；1000 次暂停竞态测试通过；坏 ROM 与 OOM 不破坏当前会话。

### Next：v0.3 “产品化 Alpha”（P1，2–3 个 L + 若干 M）

- 设计 token、adaptive icon、统一组件、正确比例/安全区和自有暂停页。
- Home/Library 重构、来源管理、最近/收藏、结构化扫描反馈。
- i18n 资源管线、中文标题/别名索引、Locale 搜索与排序。
- 设置中心、控制布局编辑器、手柄/键盘映射、TalkBack。
- Android CI、截图回归、60/90/120 Hz 和生命周期设备测试。

**Next 退出条件：** 首装两步内开始试玩或导入；A/B 误触率 <1%；中英双语覆盖 100%；200% 字体可用；真实 60/90/120 Hz 手机连续 30 分钟通过帧 pacing、音频与热测试。

### Later：v0.4 “可发布候选”（P2 + 兼容性）

- 扩充合法 ROM/mapper 回归集，建立兼容率仪表板。
- 手动存档管理、导出/恢复、完整备份迁移。
- 高级 shader、性能档位和诊断报告。
- 商店物料、隐私/数据安全表、IARC、第三方内容清单、签名包可复现构建。

## 8. 不建议采用的“看似快捷修复”

- **只把窗口请求成 120 Hz**：会把当前重复帧和 1.875 MiB 拷贝翻倍，不能解决独立帧只有约 30/25 Hz。
- **只给 A/B 加不同振动**：命中区仍重叠时，振动只能在误触发生后提醒用户。
- **只换颜色/圆角**：START、存档、生命周期和比例错误仍会让新版看起来精致但不可信。
- **直接机器翻译 ROM 文件名**：无法稳定处理地区、版本、别名和同 ROM 不同包装，必须先有 hash 主键与标题元数据模型。
- **继续把游戏库藏在 START 菜单**：入口与 NES 输入冲突，且首次用户无法形成正确心智模型。

## 9. 本次实测证据索引

证据目录：`C:\Users\pippin\.codex\visualizations\2026\08\21\01a024cb-4fb1-7293-98e4-ce272f260092\flynes-audit`

| 文件 | 说明 |
|---|---|
| `01-main-launch.png` | 主游戏界面、错误 16:15 画幅、控制区与系统导航 |
| `02-pause-menu.png` | OEM AlertDialog 与游戏界面割裂 |
| `03-after-gameplay-input.png` | 方向/A/B 实际输入后的游戏画面 |
| `05-search-no-results.png` | 搜索无结果后整页空白 |
| `07-licenses.png` | 许可文本墙和源码占位 URL |
| `08-library-font-scale-2x.png` | 200% 字体下的库页面 |
| `10-first-run-library.png` | “共 1 个游戏”与“还没有游戏”同时出现 |
| `12-start-opens-app-menu.png` | 点击 NES START 同时弹 App 菜单 |
| `13-after-cancel-app-menu.png` | 取消 App 菜单后游戏已经进入下一画面 |
| `11-flynes-120hz-screenrecord.mp4` | 强制 120 Hz 场景录屏；仅作视觉佐证，不用于精确帧率判定 |

## 10. 关键源码位置

- 输入命中与 START 菜单耦合：`app/src/main/java/com/flynes/emu/GamepadView.java:99-130, 216-349`
- Surface 比例、Choreographer 和生命周期：`app/src/main/java/com/flynes/emu/MainActivity.java:54-103, 158-209, 232-319, 370-399`
- 两帧批处理与 AudioTrack：`app/src/main/java/com/flynes/emu/AudioThread.java:30-112`
- JNI 并发 blit：`app/src/main/cpp/nes_jni.cpp:7-14, 255-281`
- framebuffer/filter/state：`core/src/nes_core.cpp:219-220, 446-450, 853-926, 1104-1108`
- 游戏库状态与目录入口：`app/src/main/java/com/flynes/emu/GameLibraryActivity.java:76-399, 421-467`
- ROM 扫描/ZIP：`app/src/main/java/com/flynes/emu/RomScanner.java`、`RomLoader.java`
- i18n 基线：`app/src/main/res/values/strings.xml`
- 默认图标/窗口配置：`app/src/main/AndroidManifest.xml`
- 源码 URL：`app/src/main/java/com/flynes/emu/LicensesActivity.java:27-52`

---

### 最终建议

下一轮不要以“UI 美化 Sprint”命名，而应以 **“可信可玩基础设施”** 为目标：输入正确性、逐帧发布、per-ROM 数据安全和生命周期先并行完成；其后再用同一套 ROM ID、设计 token 和设置模型承载游戏库、中文标题、外部手柄与视觉升级。这样既能解决当前最刺眼的问题，也避免三个月后因底层模型错误而推倒重做。
