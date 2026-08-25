# FlyNES 显示质量与功耗模式设计

**日期：** 2026-08-26

**设计分支：** `codex/display-quality-modes`

**目标设备：** vivo X100 Pro（V2324A），同时保留通用 Android 能力探测与安全回退
**状态：** 设计冻结；已完成架构、执行与原型真实性交叉审查及响应式交互验收；本文件与高仿真原型不修改 Android 生产代码

## 1. 结论先行

当前版本已经能请求显示模式，并通过 OpenGL ES 放大 256×240 的 NES 画面，但没有动态插帧，也没有真正的多阶段像素画重建。把屏幕设成 120Hz 不会增加独立游戏帧：NTSC 源约 60fps，PAL 源应约 50fps（当前 PAL 动态申报仍待补）；持续在 120Hz 重绘还会消耗额外电量。因此新设计不再用一个含糊的“高帧率/高清”开关，而是将以下三条能力完全分离：

1. **屏幕刷新策略**：跟随系统、60Hz、90Hz 手动或 120Hz 低延迟扫描。
2. **时间处理**：原生帧或实验性运动补偿 60→120；“120Hz 低延迟”是“原生时间 + 120Hz 物理刷新”的组合，不伪装成独立时间算法。
3. **空间处理**：像素原貌、清晰缩放、兼容性验证后的 MMPX 2×、标准 ScaleFX 3×再最终合成；CRT 只作为独立风格效果。

面向普通用户提供“省电、均衡、极致、自定义”四档；面向高级用户展开上述三个维度。系统始终同时展示请求值、系统报告的活动显示模式和证据新鲜度；只有 Perfetto/SurfaceFlinger/高速录像等实验室证据才能标成“物理扫描已验证”。绝不把屏幕 120Hz 写成“游戏 120fps”，也不把边缘滤镜写成“AI 高清”。

推荐默认值是**均衡**：60Hz 原生时间、新帧驱动渲染，以及当前构建与设备已经验证的最佳“中低功耗”空间处理。当前构建尚无 MMPX，因此均衡直接展开为 Sharp Bilinear，并显示“完整生效”；卡片另行说明 MMPX 尚未随当前版本提供，不能把路线图缺失伪装成运行失败。未来只有完整的 `60Hz + Native + MMPX + 无后处理 + 当前分辨率/构建/GPU/驱动` 组合通过兼容性与功耗门禁后，均衡才在该组合上展开为 MMPX。需要更低扫描等待的用户可以选择“原生时间 + 120Hz”；需要运动更连续的用户只能在实验室签发的完整组合认证随 Release 发布后选择插帧。

## 2. 当前事实基线

### 2.1 已经存在的能力

- NES 核心按原生节拍输出：NTSC 约 60.0988fps；当前主路径尚未按 PAL 约 50fps 动态申报显示节拍。
- native 在 mutex 保护下把完整画面复制到两个循环复用的 DirectBuffer；同步消费者不会读到半帧，但不得跨后续 poll 长期持有 `PublishedFrame`，它不是生命周期永久不可变对象。
- OpenGL ES 2.0 只在收到新序列号时上传纹理，但 `GLSurfaceView` 仍连续 draw/swap。
- 当前 GPU shader 有最近邻、清晰双线性、轻量边缘增强和 CRT；它们不是插帧或超分辨率。
- 自动显示模式优先同分辨率 120Hz，再回退 60Hz；设置页和游戏内的模式解析尚未完全共用。

### 2.2 尚未存在的能力

- 不存在中间帧生成、光流或运动补偿。
- 不存在 MMPX、ScaleFX、xBR 等正式的像素画多阶段重建。
- 不存在源帧率、合成帧率、提交率、物理刷新率的联合实时统计。
- 不存在持续的温控、电量、GPU deadline 与显示模式回退状态机。
- 不存在 vivo X100 Pro 上插帧画质、功耗、温度和延迟的认证证据。

因此，原型必须把“当前构建”和“认证后目标”分成独立能力状态。当前构建中 MMPX、ScaleFX 与运动补偿均显示“尚未实现”且不可选；运行时系统回退或温控状态不能改变认证结果。

## 3. 方案比较与选择

### 3.1 交互架构

| 方案 | 优点 | 缺点 | 决策 |
|---|---|---|---|
| 只有多个技术开关 | 自由度高 | 普通用户不知道组合后果，容易制造互斥或高耗电配置 | 不采用 |
| 只有四个预设 | 简洁 | 无法满足用户对功耗、延迟和画质的细调 | 不采用 |
| 四个预设 + 高级自定义 | 默认易懂，仍可精确选择；能把能力与实测状态分开 | 需要统一解析器和较完整的状态模型 | **采用** |

### 3.2 时间处理

| 模式 | 独立游戏帧 | 物理屏幕 | 视觉收益 | 代价 | 产品定位 |
|---|---:|---:|---|---|---|
| 原生时间 + 60Hz | NTSC 约 60/s；PAL 约 50/s | 60Hz | 节奏真实、最低功耗 | NTSC 扫描机会约每 16.7ms 一次 | 正式默认 |
| 原生时间 + 120Hz 低延迟扫描 | NTSC 约 60/s；PAL 约 50/s | 请求 120Hz，应用提交率约等于实际源帧率 | 系统实际采用 120Hz 且新帧及时锁存时，可能缩短部分扫描等待 | 游戏运动本身不变，收益需实机测量，面板功耗更高 | 正式高级项 |
| 帧混合 60→120 | 约 60/s + 混合帧 | 120Hz | 看似平滑 | 明显拖影、文字发虚，不产生运动信息 | 不采用 |
| 运动补偿 60→120 | 约 60/s + 合成中间帧 | 120Hz | 横向卷轴和匀速运动更连续 | 至少约一帧 look-ahead、伪影、GPU 功耗 | 实验性、设备认证后开放 |
| 通用 AI 插帧 | 同上 | 120Hz | 理论上更强 | 模型体积、延迟、功耗和跨游戏稳定性不可控 | 本轮不做 |

### 3.3 空间处理

| 模式 | 方法 | 典型代价 | 视觉定位 | 产品定位 |
|---|---|---:|---|---|
| 像素原貌 | 整数倍最近邻 + 最终合成 | 最低 | 保留硬像素边缘 | 正式 |
| 清晰缩放 | sharp bilinear | 低 | 减少非整数缩放抖动 | 正式 |
| 像素重建 | MMPX 2× 的自研 GLES fragment port；用 CPU 参考实现做 fixture 对照 | 中低 | 改善斜线和小圆角，较少破坏字体 | 兼容性门禁后的默认候选 |
| 轮廓增强 | 标准 ScaleFX 3× 五 pass + 最终 sharp composite | 中高 | 连续轮廓更明显 | 目标设备认证后正式开放 |
| FSR1 | EASU + RCAS | 中 | 对硬像素画容易出现光晕和孤立像素 | 仅研究，不进入首版 |
| AI 超分 | 神经网络 | 高 | 可能改写文字、纹理和原画 | 不做 |

“高清化”在界面中统一改称“像素画重建”。帮助页明确说明：算法能改善缩放与轮廓，但不能恢复 ROM 中不存在的细节。

## 4. 四个用户模式

### 4.1 预设矩阵

| 模式 | 屏幕策略 | 时间处理 | 空间处理 | 预计功耗 | 可用状态 |
|---|---|---|---|---|---|
| 省电 | 请求 60Hz | 原生时间 | Nearest | 低 | 所有支持 GLES2 的设备 |
| 均衡（默认） | 请求 60Hz | 原生时间 | 当前构建 Sharp；完整组合通过门禁后为 MMPX 2× | 中低 | 始终可选；按设备资格展开为一个完整配置 |
| 极致 | 请求 120Hz | 运动补偿 60→120 | ScaleFX 3× + 最终合成 | 高 | **只有完整组合获得实验室认证才可选** |
| 自定义 | 用户选择 | 用户选择 | 用户选择 | 由配置计算 | 解析完整组合，不允许由单项资格做笛卡尔积 |

所有模式都受不可关闭的临界安全规则约束。“自适应保护”默认开启，只控制 Moderate/40°C/GPU 预算等普通降级；省电已经是最低画质配置，但仍会响应系统显示结果和 Critical/Severe 安全动作。

### 4.2 为什么均衡默认不是 120Hz

当前没有插帧时，120Hz 不会增加独立游戏帧；若应用还持续 draw/swap，就会额外唤醒 GPU。均衡档先把资源投入可见的像素重建，并使用新帧驱动渲染。120Hz 低延迟保留在自定义中，供重视输入扫描等待而愿意接受更高面板功耗的用户选择。

### 4.3 vivo X100 Pro 的目标配置

原型使用 60/72/90/120Hz 作为**模拟设备数据**；仓库尚无可追溯的 X100 Pro 模式枚举证据。实现时必须重新采集 build fingerprint、当前分辨率、modeId 和浮点 refreshRate，以下仅为待认证目标：

- 省电：60Hz + 原生时间 + Nearest。
- 均衡：60Hz + 原生 + MMPX 2×。
- 极致：120Hz + 像素感知运动补偿 + ScaleFX 3×再最终合成。
- 自定义推荐：
  - 动作/竞速：完整组合已验证时使用 120Hz 低延迟 + 原生时间 + MMPX 2×；否则跳过到已验证的 Sharp 组合。
  - RPG/策略：完整组合已验证时使用 60Hz + 原生时间 + ScaleFX 3×再最终合成；否则选择已验证的 MMPX 或 Sharp 组合。
  - 电量低于 20%：60Hz + 原生时间 + Nearest。

这些是待认证目标，不是当前已通过结论。

## 5. 高仿真设置页

### 5.1 页面结构

设置保持现有横屏主从结构：左侧分类，右侧内容。显示页不再使用一串简单下拉框，结构如下：

1. 页面标题和一句“不虚标”的说明。
2. 四张模式卡片，紧凑屏幕为横向滚动，常规横屏为 2×2；2.0 字体缩放改为单列。
3. 始终可见的“当前实际运行”状态卡。
4. 画面比例。
5. 选择“自定义”后展开高级控制。
6. “插帧与像素重建的区别”帮助区。

沿用克制的复古掌机视觉：石墨黑 `#121316`、表面层 `#1B1D22`、暖白 `#F4EFE6`、次级文字 `#BEB8AE`、信号珊瑚 `#FF6B5E`。模式状态必须用文字和图标共同表达，不能只用颜色。

HTML 原型的测试工具栏属于验收夹具，不随 1.0/1.3/2.0 应用字体一起缩放；应用画布在宽屏保持 2340:1080，在紧凑屏保持 16:9，并整体等比适配工具栏剩余区域，避免双滚动。侧栏和主内容各自只有一个可见纵向滚动容器。HTML 用于确认信息层级、状态和交互，不能直接把 CSS px 当作 Android dp 做 ±2dp 结论；Android 实现仍需在固定 AVD/密度上导出 PNG 与已冻结的逻辑画布基准并排验收。

### 5.2 模式卡文案

- **省电**：`60Hz 原生节奏 · 像素原貌 · 最低额外处理`
- **均衡（当前构建）**：`60Hz 原生节奏 · 清晰缩放 · 推荐`，并在次级能力说明中写 `MMPX 尚未随当前版本提供`。
- **均衡（完整组合已验证）**：`60Hz 原生节奏 · 像素重建 · 推荐`。
- **极致**：`120Hz 合成运动 · 轮廓增强 · 高功耗`
- **自定义**：`分别选择刷新率、时间处理与像素重建`

极致未认证时仍展示卡片，但锁定并朗读：`极致，尚未随当前版本发布本机认证。开发团队完成实机性能与伪影评审后才能启用。` 认证是随 Release 发布的离线白名单，不是用户手机自行跑一次 benchmark 后解锁，也不使用一个可以点击但实际退化成重复帧的假模式。

### 5.3 高级设置

#### 屏幕刷新

- 60Hz：较低显示功耗，接近 NTSC 约 60.0988fps 的原生节奏。
- 90Hz：只允许手动选择，并提示 60→90 无法均匀映射。
- 120Hz：实际采用且新帧及时锁存时，可能缩短部分显示扫描等待；是否产生合成帧由“时间处理”单独决定。
- 跟随系统：不主动锁定模式，测量前显示“等待实测”，不得预设成 60Hz。

只列当前分辨率真正支持的模式；从其他设备迁移来的不可用值会回退并给出一次性说明。

#### 时间处理

- 原生帧：不合成中间帧。与 120Hz 刷新组合时，状态必须绑定 core 确认的 `SourceTiming/sourceNominalFps`：NTSC 写“约 60 个独立帧与约 60 次新 buffer 提交”，PAL 写“约 50 个独立帧与约 50 次新 buffer 提交”，并另列“屏幕请求 120Hz”；UNKNOWN 只显示实测源帧率，不猜 60。
- 运动补偿 120（实验）：只有刷新率为 120Hz、设备/驱动/算法版本均认证且非省电/非过热时可选；第一次开启显示风险确认。选择它会显式把刷新策略切到 120Hz，刷新率离开 120Hz 时同步恢复原生帧并聚焦解释信息。

#### 像素重建

- 像素原貌（Nearest）。
- 清晰缩放（Sharp Bilinear）。
- 像素重建（MMPX 2× fragment port；与参考实现 fixture 对照）。
- 轮廓增强（ScaleFX 3×五 pass + 最终 sharp composite）。

CRT 是独立的“显示风格”开关，永远不计入高清增强。

### 5.4 首次开启插帧确认

标题：`申请实验性运动补偿？`

正文：

- 确认后先等待同 generation 的 120Hz 活动模式报告连续稳定 3 秒，再取得相邻帧预热；门禁完成前保持即时原生与 0ms 音频延迟。
- 真正启用后会等待下一帧来生成中间画面，增加约 16.7ms 基础延迟。
- 精灵闪烁、场景切换和快速遮挡可能出现短暂伪影。
- 预计功耗为高；温度或电量不满足时会自动关闭。
- 可随时在显示设置恢复“原生帧”。

操作：次级按钮 `保持原生`，主按钮 `了解风险并继续`。确认只允许保存偏好并启动资格检查，不能把按钮点击当成 Motion 已活动。确认状态绑定完整 `VideoConfigurationKey`、profile/evidence identity、算法实现 hash 和风险文案版本；其中任一项变化都自动失效并重新确认，不能只保存一个裸 boolean 或粗粒度“算法版本”。

## 6. 运行状态必须说真话

状态卡分成“用户请求”“当前生效”“系统报告”“实验室物理证据”“测量新鲜度”五层。`Display.getRefreshRate()` 或活动 `modeId` 只能称为“系统报告”，不能称为物理实测：

```text
用户模式       极致
核心产出       60.10 fps（NTSC）
唯一源帧上传   59.98 fps
合成时隙提交   59.91 fps
其中运动 warp  58.80 fps（其余安全保持）
应用 buffer 提交 119.89 fps
显示请求       120 Hz / modeId 7 / 2800×1260
系统报告活动模式 modeId 7 / 2800×1260 / 120.00 Hz
物理扫描证据   实验室已验证（证据 manifest abc…）/ 未验证
像素重建       ScaleFX 3× + 最终合成
时间管线       MOTION_COMPENSATING
视频/音频延迟  1 帧 / 16.7 ms
插帧安全回退   1.8%（场景切换/闪烁）
温控状态       正常
状态采样       0.6 秒前
```

如果系统拒绝 120Hz：

```text
部分生效
系统或设备策略未接受 120Hz 请求；系统报告活动模式为 60Hz。
运动补偿已停止，时间管线进入 BUFFERED_NATIVE_HOLD；ScaleFX 3× 继续生效。
音画仍保留约一帧对齐延迟，下一次暂停或安全场景切换再排空。
```

刷新率和空间算法按维度独立回退：显示模式被拒绝本身不会把 ScaleFX 偷换成 MMPX。若 120Hz 在插帧首次预热前就被拒绝，直接使用 `IMMEDIATE_NATIVE` 且音频延迟为 0；若运行中的插帧失去 120Hz，则先进入 `BUFFERED_NATIVE_HOLD`，避免音画时间线瞬跳。显式请求 60/90/120 与系统报告值相差超过 1Hz 即为“部分生效”；“跟随系统”没有数值请求，不因活动值为 60/90/120 而显示失败。

只有 Thermal API 或明确的电量策略提供可验证信号时，才能写“因温控/省电回退”；否则统一写“系统或设备策略”。“预计功耗”只显示低/中低/中/高，不显示未经标定的瓦数。

## 7. 时间插帧算法设计

### 7.1 目标

针对 256×240 像素画做轻量、可解释、可安全回退的 60→120 中点帧生成。不使用大模型，不改变游戏逻辑，不对虚拟按键和系统 UI 插帧。

### 7.2 流水线

```text
上一真实帧 A + 当前真实帧 B
  → 调色板/场景切换检测
  → 1/4 分辨率粗块匹配（8×8 block，搜索半径 8）
  → 全分辨率细化（4×4 block，搜索半径 2）
  → 前后向一致性与遮挡置信度
  → 0.5 时间点双向 warp
  → 仅从 A/B 实际出现的 RGB565 颜色中做 nearest sample
  → 低置信区域保留上一已显示真实帧 A，不做透明混合
  → 合成中间帧 M
```

时间顺序为 `A → M(A,B) → B`，但首个输出不能在尚未取得 B 时提前显示 A。明确的预热时序是：先取得 A、B，整体延迟一个源帧后提交 A，再在下一显示时隙提交 `M(A,B)`，随后提交 B；以后每取得一个新真实帧都沿同一延迟时间线推进。游戏核心和输入写入不延迟；“触摸到 core”以核心在下一模拟帧实际采样按键位的时间为终点，而不是 Java 写入时间。

运动补偿模式使用带时间戳的音频环形延迟线，把音频同步延迟到同一视频 presentation timeline；目标 A/V skew 绝对值小于 20ms。用户主动切换只允许在暂停状态完成，清空并重新预热视频/音频队列后再继续。运行中退出 Motion 时，先进入 `BUFFERED_NATIVE_HOLD`：停止**呈现**中间帧但保留同一帧视频/音频延迟，不跳时间线；只有下次暂停、场景切换或既定安全动作才排空缓冲。排空需要在一个已记录的视频 cadence adjustment 中移除一帧队列延迟，同时删除/消费对应约 16.7ms 音频块，并在删除边界做 8ms 等功率 crossfade；crossfade 只用于掩蔽边界，不能被描述为它自身“排空”了延迟。

这个时序是不允许绕过的统一不变量：显示失刷、伪影门限、source sequence 缺口/溢出、Swappy/ES3.1/运动 shader/context 失败、系统省电/低电量和普通自适应边，只要发生在活动 Motion 中，都由同一个 `TemporalTransition` 先进入 Hold，再在安全边界 drain。只影响空间 pass 的失败可以在不退出 Motion 的前提下原子切换到已资格化空间组合。用户已暂停或 Critical 强制暂停时允许在“音频已停、输入已释放”的暂停事务内清空双队列；Severe 使用已定义的强制 drain。`DisplayQualityResolver` 不能把一个活动 Motion 会话直接输出为 `IMMEDIATE_NATIVE + 0ms`。

临时 Surface/context 丢失有专门事务，不能借资源销毁绕过 Hold。若活动 Motion 的 `ANativeWindow` 消失，Session 在同一安全执行器上停止合成、暂停 core producer 与 AudioTrack、释放全部触控输入，把 CPU staging 中最后一对真实帧和相应音频延迟块冻结为 `SURFACE_SUSPENDED_HOLD`；不再 swap，但不丢弃或消费双队列。每次 Surface create/destroy 增加 `surfaceEpoch`，立即清空 active configuration id/key、撤销旧 display request generation 和 freshness deadline。新 Surface 到来后重新创建唯一 EGL owner、发出新 display generation，并保持 session 暂停；只有新 generation 的兼容 lease 连续 fresh 3 秒且完整资源重建成功，才在暂停事务中丢弃已冻结的旧呈现对、以新相邻帧重新 `PRIMING` 后恢复 Motion。若 ES3.1/Swappy 重建失败，则仍在音频已停、输入已释放的暂停事务内清空双队列并恢复 `IMMEDIATE_NATIVE`；绝不在失去 Surface 的运行线程里假装完成 drain。已由用户暂停或 Critical 暂停的 destroy 可以直接清队列，Native 模式则按普通生命周期清理。

### 7.3 像素画保护

- 不使用普通 alpha blend；warp 只做 nearest sample，最终颜色必须属于 A/B 两帧实际出现的 RGB565 颜色集合，避免产生新颜色和双边缘。
- 场景切换、全屏闪烁、sprite 闪烁、低置信遮挡块在合成时隙保留上一已显示真实帧。
- HUD/固定分数区通过低运动与边缘稳定性掩码锁定，不做 warp。
- 合成帧不能进入存档封面或游戏逻辑；封面只取真实帧。
- 每个实际 Motion 帧输出 `safetyFallbackRatio`，超过 25% 连续 2 秒时进入 `BUFFERED_NATIVE_HOLD`，保留音画延迟并在状态卡显示原因。Hold 中不拿停更的旧 ratio 作为恢复证据：温控/GPU/显示条件稳定满 120 秒且到达暂停或场景切换边界后，进入一个不提交到 Surface 的 2 秒/至少 120 对相邻帧 `PRIMING_SHADOW` 试算窗口；只有新窗口 ratio 全程低于 10%、无单帧超过 25%、sequence 连续且 GPU 预算通过，才重新预热并切回 Motion。失败则回到 Hold、保留延迟并重新开始 120 秒冷却；shadow 计算时间和功耗单独记账。

### 7.4 60.0988→120 的节拍

调度器以系统报告的实际活动模式浮点刷新率和 presentation timestamp 为时钟，不硬编码 120.000Hz。`2 × 60.0988 = 120.1976`；若实际模式恰为 120.000Hz，平均约每 5.06 秒少安排一个合成中点。相位误差必须有界，cadence adjustment 计数进入状态与 trace。

Motion 的启动门不是“已经发出 120Hz 请求”：只有同一 request generation、`FRESH`、modeId/分辨率一致且 milliHz 在 119–121Hz 门限内的 `DisplayObservation.systemReportedActiveMode` 已稳定 3 秒，才允许从 `IMMEDIATE_NATIVE` 进入 `PRIMING`；没有观测、观测过期或仍是 60/90Hz 时保持即时原生且音频延迟为 0。活动 Motion 只在同 generation、兼容且持续 `FRESH` 的显示观测 lease 下运行；收到不兼容观测，或 lease 变为 `STALE`/`UNKNOWN`、没有观测/超时，均立即停止合成并经统一 `TemporalTransition` 进入 `BUFFERED_NATIVE_HOLD`，原因分别记录为模式不兼容或 `DISPLAY_OBSERVATION_STALE`。3 秒只用于把持续不匹配标记成持久 fallback、停止重请求和更新 UI，不用于延迟安全切换。

`DisplayStatusMonitor` 在 Motion 被请求、预热或活动期间每 500ms 主动读取一次系统活动 mode，Activity/Display listener 事件只用于额外触发读取，不能替代 heartbeat。观测 lease 的 TTL 固定为 1500ms（允许缺失两个 500ms heartbeat；第三个截止点即失效），`StatusFreshness` 每次消费时都以 `nowElapsedRealtimeMs - observedAtElapsedRealtimeMs` 重新计算，不能把采样时的 `FRESH` boolean 冻结在记录中。独立的 `DisplayLeaseWatchdog` 由 session safety executor 持有；每次新观测用 `(surfaceEpoch, requestGeneration, observedAt + 1500ms)` 替换 deadline token，token 到期即使 poll/read 线程阻塞也主动提交 `DISPLAY_OBSERVATION_STALE` 并触发统一 Hold。Motion compute 每生成一个中间帧前、presenter 每次 Swappy swap 前还必须读取同一原子 lease 并复核 epoch/generation/兼容性/`now < deadline`，任一步失败都禁止本次合成或 swap。进程暂停、Display 移除、listener 注销、读取异常和单调时钟倒退都立即取消旧 token、发布 `UNKNOWN`/无 lease并触发上述安全退出。重新进入 Motion 仍需新的同 generation lease 连续兼容 3 秒，旧稳定时长不能沿用。

“真实帧优先”只表示应用队列在必须二选一时先提交真实帧，不承诺每个 core 帧都获得独立物理扫描：在恰好 60.000Hz 的屏幕上，60.0988fps 源平均约每 10.12 秒就有一个帧无法获得独立扫描机会。状态必须分别记录 core 产生、GPU 上传、应用提交和系统实际扫描证据。PAL 50fps 的运动补偿首版不开放；后续目标是 50→100，设备没有 100Hz 时保持原生呈现。

## 8. 空间重建管线

```text
256×240 真实/合成游戏层
  → overscan 阶段（本轮固定为 0/0/0/0，不做隐藏裁切）
  → MMPX 2× 或标准 ScaleFX 3×整数 FBO
  → 8:7 / 4:3 / 自定义像素宽高比校正
  → 最终 sharp composite
  → 可选 CRT 风格 pass
  → 独立合成虚拟按键和系统 UI
```

- MMPX 使用自研 GLES2 fragment port，并以 test-only CPU oracle `mmpx-cpu-oracle-v1` 的固定 fixtures 逐像素对照；oracle 必须先单独提交并把文件 hash 写入 implementation manifest，再开始 shader port。在所有 fixture 通过前只能称为“MMPX 候选 port”，不能称为参考等价。实现固定 `highp` 精度要求和论文/来源说明。标准 ScaleFX 固定到 `libretro/glsl-shaders@4f4eb801b2dbcaed0a9669a9deec1a098f3623d8` 的 `scalefx/shaders/scalefx-pass0.glsl` 至 `scalefx-pass4.glsl` 及该 revision 的许可证/notice，使用五 pass；前两 pass 先验证 half/float framebuffer 的可渲染性、可采样性和精度。两者都使用离屏纹理，严禁处理操控层和文字。
- shader 编译或 FBO 分配失败时以 `ScaleFX → MMPX → Sharp Bilinear → Nearest` 作为候选顺序，但 resolver 必须跳过未随构建提供、能力不满足或该完整组合未取得相应资格的候选；最终至少回到基础 Nearest，游戏不得崩溃。
- GL context 丢失后重建 pipeline；恢复期间短暂使用 Nearest。
- 旧的 CPU HQ4X 不进入实时路径，避免每帧大块 CPU 复制。

## 9. 新帧驱动与显示节拍

核心为每次完成的模拟帧返回 `FrameStepResult(framesRun, audioSamples, sequence)`，并在释放 framebuffer mutex 后发出带 sequence 的 frame-available signal。Native 模式可在 dispatcher 落后时明确选择“latest-frame coalescing”，但必须记录 skipped sequence，相关门禁只要求“每个已复制 sequence 至多上传/提交一次”，不能虚假要求每个 core 帧必被复制。Motion 模式使用无损有界 staging ring，禁止合并；sequence 出现缺口或 ring 溢出立即退出 Motion，进入 `BUFFERED_NATIVE_HOLD` 并记录原因。

在开放任何新空间/时间算法前，所有模式统一迁移到 `SurfaceView + 单一 native-owned EGL presenter`。当前 `GLSurfaceView` 只作为迁移前视觉基线，基线 Nearest/Sharp/legacy-edge/CRT parity 通过后删除；Java 与 native 不得同时拥有同一个 Surface。统一 presenter 从此负责 EGL context/surface、纹理上传、空间 pass 和唯一 swap 点。

非插帧低延迟 120 使用同分辨率 `preferredDisplayModeId`；Android 30+ 由唯一 `PresentationCoordinator` 在 native render thread 通过 `ANativeWindow_setFrameRate(sourceFps, DEFAULT)` 声明内容原生率，并以窗口高刷模式请求表达物理 120Hz 偏好。Java 层只提交 `preferredDisplayModeId` 请求，不得再对同一 Surface 调 `Surface.setFrameRate`。系统可拒绝，`DisplayObservation` 必须显示活动模式。120Hz 低延迟只在新源帧到达时提交新 buffer，由 SurfaceFlinger 保留最新 buffer 给中间扫描；应用提交率约等于 core 确认的源率（NTSC 约 60、PAL 约 50），而不是硬编码 60。

统一 presenter 在 Native/空间重建阶段直接使用 EGL swap。只有 Motion 真正需要约 120 次应用提交时，才在同一个 presenter 内接入 Swappy 或等价 presentation pacer；这不是第二套 presenter。Swappy 若选择回 60Hz、ES3.1 context 重建失败或约 120 次提交无法持续，Motion fail-closed。不得为了写上“使用 Swappy”而给 source-driven Native 路径增加重复 swap。

`PresentationCoordinator` 是每个 `surfaceEpoch` 唯一的 frame-rate vote 与 pacer owner；所有交接都在同一 render-thread 命令队列完成，后一步只有在前一步返回成功且插入 EGL fence 后才可执行：

| 时间状态 | 唯一 vote/pacer owner | 最后有效 vote | 提交方式 |
|---|---|---|---|
| `IMMEDIATE_NATIVE` | `PresentationCoordinator` | core 确认的 NTSC≈60/PAL≈50；FOLLOW_SYSTEM 时仍声明内容率 | 新 source frame 才 direct `eglSwapBuffers` |
| `PRIMING` | coordinator 先以 0 清除 Native vote，再把该 epoch 的 window 唯一交给 Swappy | Swappy 根据系统活动 119–121Hz mode 设置的目标 interval | A/B 未齐前不提交；齐备后只由 Swappy 提交 |
| `MOTION_COMPENSATING` | Swappy（coordinator 禁止任何并发 frame-rate 写入） | 与 fresh 活动 mode 对应的约 120Hz interval | 只调用 `SwappyGL_swap` |
| `BUFFERED_NATIVE_HOLD` | coordinator 在最后一次 Swappy swap/fence 后 detach/destroy Swappy，再写 source vote | NTSC≈60/PAL≈50 | 保持一帧延迟的真实帧 direct EGL |
| `PRIMING_SHADOW` | coordinator | source vote | direct EGL 呈现延迟真实帧；shadow 结果不提交 |
| `DRAINING` | coordinator | source vote | direct EGL，按记录的 cadence 事务移除一帧延迟 |
| `SURFACE_SUSPENDED_HOLD` | 无 window owner；旧 epoch token 全部失效 | 无 | 不提交；CPU 视频/音频延迟队列冻结 |

Surface 重建必须使用新 epoch，从“无 owner/无 vote”开始重新走表；禁止把旧 Swappy window、旧 source vote 或旧 deadline 带入。任一清票、attach、detach、vote 或 pacer 操作失败都清空 active configuration id/key，并交给统一 `TemporalTransition`/Surface 暂停事务处理，不能由两个调用者争夺“最后写者”。

## 10. 统一数据模型

```java
enum VideoQualityPreset {
    POWER_SAVER, BALANCED, EXTREME, CUSTOM
}

enum PhysicalRefreshPolicy {
    FOLLOW_SYSTEM, LEGACY_AUTO_INTEGER_MULTIPLE, HZ_60, HZ_90, HZ_120
}

enum TemporalMode {
    NATIVE, MOTION_COMPENSATED_2X
}

enum RuntimeTemporalState {
    IMMEDIATE_NATIVE,
    PRIMING,
    PRIMING_SHADOW,
    MOTION_COMPENSATING,
    BUFFERED_NATIVE_HOLD,
    SURFACE_SUSPENDED_HOLD,
    DRAINING
}

enum SessionSafetyDirective {
    NONE,
    PAUSE_FOR_CRITICAL_THERMAL
}

enum SpatialMode {
    NEAREST, SHARP_BILINEAR, LEGACY_EDGE_ENHANCED, MMPX_2X, SCALE_FX_3X
}

enum PostEffect {
    NONE, CRT
}

record CustomVideoSettings(
    PhysicalRefreshPolicy refreshPolicy,
    TemporalMode temporalMode,
    SpatialMode spatialMode,
    PostEffect postEffect
) {}

record VideoPreferences(
    VideoQualityPreset preset,
    CustomVideoSettings customSettings, // 只有 CUSTOM 使用；切换预设时仍保留
    boolean adaptiveProtection
) {}

record DisplayModeCapability(
    int modeId,
    int width,
    int height,
    int refreshMilliHz // Android float 进入模型时 Math.round(hz * 1000)
) {}

enum SourceTiming {
    NTSC_60_0988,
    PAL_50,
    UNKNOWN
}

record GlCapabilities(
    int activeContextMajorVersion,
    int activeContextMinorVersion,
    String vendor,
    String renderer,
    String version,
    Set<String> extensions,
    int maxTextureSize,
    boolean fragmentHighp,
    boolean halfFloatColorFramebufferRenderable,
    boolean halfFloatColorFramebufferFilterable,
    boolean floatColorFramebufferRenderable,
    boolean disjointTimerQuery,
    boolean supportsEs31Compute,
    boolean canCreateOwnedEs31Presenter
) {}

record DisplayCapabilities(
    int currentWidth,
    int currentHeight,
    List<DisplayModeCapability> sameResolutionModes,
    GlCapabilities gl
) {}

record BuildAlgorithmAvailability(
    boolean mmpxIncluded,
    boolean scaleFxIncluded,
    boolean motionCompensationIncluded,
    int mmpxOracleVersion,
    String mmpxImplementationHash,
    String scaleFxUpstreamCommit,
    String scaleFxImplementationHash,
    int motionAlgorithmVersion,
    String motionImplementationHash
) {}

record VideoConfigurationKey(
    SourceTiming sourceTiming,
    int width,
    int height,
    int displayModeId,
    int refreshMilliHz,
    TemporalMode temporalMode,
    SpatialMode spatialMode,
    PostEffect postEffect,
    AspectMode aspectMode
) {}

enum EvidenceLevel {
    COMPATIBILITY_AND_POWER,
    DEVICE_LAB
}

enum EvidenceClockTrust {
    TRUSTED,
    TIME_UNTRUSTED
}

enum PersistedEvidenceClockState {
    NONE,
    COMPLETE,
    CORRUPT
}

enum AuthenticatedTimeSource {
    ANDROID_NETWORK_TIME,
    SIGNED_CI,
    RFC3161,
    EQUIVALENT_INDEPENDENT_TIMESTAMP
}

enum AuthenticatedReleaseTimeRole {
    CANDIDATE_BUILD,
    PROFILE_RELEASE,
    RELEASE_COMPLETION
}

enum PhysicalScanEvidenceState {
    VERIFIED,
    UNVERIFIED
}

enum EvidenceInvalidReason {
    NONE,
    CERTIFICATE_MISSING,
    PROFILE_MISMATCH,
    CONFIGURATION_MISMATCH,
    BUILD_OR_DRIVER_MISMATCH,
    IMPLEMENTATION_MISMATCH,
    MANIFEST_MISMATCH,
    EVIDENCE_LEVEL_INSUFFICIENT,
    INVALID_VALIDITY_RANGE,
    VALIDITY_TOO_LONG,
    CLOCK_STATE_CORRUPT,
    EVIDENCE_TOMBSTONED,
    TIME_BOOTSTRAP_REQUIRED,
    TIME_UNTRUSTED,
    FUTURE_ISSUED,
    EVIDENCE_EXPIRED
}

record CertifiedVideoConfiguration(
    String configurationId,
    VideoConfigurationKey key,
    EvidenceLevel evidenceLevel,
    String buildImplementationHash,
    String algorithmImplementationHash,
    long certifiedAtEpochMs,
    long validUntilEpochMs,
    String evidenceManifestSha256
) {}

record BootSessionIdentity(
    Integer androidBootCount,       // 不可读时为 null
    String kernelBootIdSha256,      // 不可读时为 null
    String canonicalIdentitySha256  // 由平台 provider 计算，调用方不能传 boolean 替代
) {}

record AuthenticatedTimeSample(
    AuthenticatedTimeSource source,
    long sampledEpochMs,
    long sampledAtElapsedRealtimeMs,
    String bootSessionIdentitySha256,
    String provenanceSha256
) {}

record EvidenceExpiryTombstone(
    String certificateIdentitySha256,
    String profileId,
    String configurationId,
    String evidenceManifestSha256,
    long validUntilEpochMs,
    long firstObservedExpiredAtEpochMs
) {}

record PersistedEvidenceClockAnchor(
    String bootSessionIdentitySha256,
    AuthenticatedTimeSample bootstrapAuthenticatedTime,
    long anchorEvaluatedAtEpochMs,
    long anchorSystemWallEpochMs,
    long anchorElapsedRealtimeMs,
    long persistedLastSeenValidatedEpochMs,
    List<EvidenceExpiryTombstone> expiryTombstones,
    String authenticatedPayloadMac
) {}

record TrustedEvidenceClockSnapshot(
    BootSessionIdentity currentBootSession,
    PersistedEvidenceClockState persistedState,
    PersistedEvidenceClockAnchor persistedAnchor, // NONE/CORRUPT 时为 null；COMPLETE 时必须非 null
    AuthenticatedTimeSample authenticatedNetworkTime, // 无当前 boot 平台认证时间时为 null
    long systemWallEpochMs,
    long elapsedRealtimeMs,
    long releaseBuildEpochMs,
    Long authenticatedNetworkNowEpochMs,
    Long monotonicFloorEpochMs,
    long evaluatedAtEpochMs,
    EvidenceClockTrust trust
) {}

record AuthenticatedReleaseTime(
    AuthenticatedReleaseTimeRole role,
    AuthenticatedTimeSource source, // 只允许 SIGNED_CI/RFC3161/EQUIVALENT_INDEPENDENT_TIMESTAMP
    long evaluatedAtEpochMs,
    String requestNonceSha256,
    String authorityIdentitySha256,
    String boundCandidateRecordSha256,
    String boundCandidateManifestSha256,
    String boundReleaseRecordSha256,
    String boundReleaseGateSetSha256,
    String proofSha256
) {}

record AdaptiveTransition(
    String fromConfigurationId,
    String toConfigurationId,
    AdaptiveTriggerClass triggerClass
) {}

record AdaptiveQualityPolicy(
    List<AdaptiveTransition> downgradeTransitions,
    Map<String, Float> gpuP95BudgetMsByConfigurationId,
    float batteryDowngradeCelsius,
    float batterySafeCelsius,
    float batteryRecoveryCelsius,
    long recoveryStableMs
) {}

record DisplayObservation(
    long requestGeneration,
    PhysicalRefreshPolicy requestedPolicy,
    DisplayModeCapability requestedMode,
    DisplayModeCapability systemReportedActiveMode,
    long observedAtElapsedRealtimeMs,
    long stableForMs
) {}

record DeviceQualityProfile(
    String profileId,
    String manufacturer,
    String model,
    String buildFingerprint,
    String gpuVendor,
    String gpuRenderer,
    String gpuVersion,
    String driverFingerprint,
    String buildImplementationHash,
    List<CertifiedVideoConfiguration> certifiedConfigurations,
    AdaptiveQualityPolicy adaptivePolicy
) {}

record RuntimeConstraints(
    SourceTiming sourceTiming,
    DisplayObservation displayObservation,
    boolean systemBatterySaver,
    int batteryPercent,
    float batteryTemperatureCelsius,
    ThermalBand thermalBand,
    boolean optionalAdaptiveProtectionEnabled,
    Set<RuntimeFailure> runtimeFailures,
    RuntimeTemporalState currentTemporalState
) {}

record EffectiveVideoConfig(
    VideoPreferences requested,
    String resolvedConfigurationId,
    VideoConfigurationKey resolvedConfigurationKey,
    PhysicalRefreshPolicy effectiveRefresh,
    DisplayModeCapability requestedDisplayMode,
    DisplayModeCapability systemReportedActiveMode,
    TemporalMode effectiveTemporal,
    RuntimeTemporalState runtimeTemporalState,
    SpatialMode effectiveSpatial,
    PostEffect effectivePostEffect,
    int videoDelayFrames,
    float audioDelayMs,
    SessionSafetyDirective sessionSafetyDirective,
    List<FallbackReason> fallbacks
) {}

record MotionRiskConsent(
    String profileId,
    String configurationId,
    VideoConfigurationKey configuration,
    String evidenceManifestSha256,
    long evidenceCertifiedAtEpochMs,
    long evidenceValidUntilEpochMs,
    long evidenceEvaluatedAtEpochMs,
    String buildImplementationHash,
    String algorithmImplementationHash,
    int riskCopyVersion,
    long acceptedAtEpochMs
) {}

record VideoRuntimeStatus(
    long surfaceEpoch,
    long displayRequestGeneration,
    String activeConfigurationId,
    VideoConfigurationKey activeConfigurationKey,
    float sourceNominalFps,
    float coreProducedFps,
    float copiedUniqueSourceFps,
    float textureUploadFps,
    long sourceFramesNotCopied,
    PhysicalRefreshPolicy requestedPolicy,
    float requestedDisplayHz, // FOLLOW_SYSTEM 时为 NaN
    DisplayModeCapability requestedMode,
    DisplayModeCapability systemReportedActiveMode,
    float synthesizedSlotSubmitFps,
    float motionWarpedSlotFps,
    float appBufferSubmitFps,
    float safetyFallbackRatio,
    RuntimeTemporalState runtimeTemporalState,
    int videoDelayFrames,
    float audioDelayMs,
    int videoQueueDepth,
    int audioQueueDepthSamples,
    TemporalTransition temporalTransition,
    long cadenceAdjustmentCount,
    ThermalBand thermalBand,
    StatusFreshness freshness,
    long capturedAtElapsedRealtimeMs,
    List<FallbackReason> fallbacks
) {}

record PhysicalScanEvidence(
    String profileId,
    String configurationId,
    VideoConfigurationKey configuration,
    String buildFingerprint,
    String driverFingerprint,
    String buildImplementationHash,
    String algorithmImplementationHash,
    PhysicalScanEvidenceState state,
    EvidenceInvalidReason invalidReason,
    String evidenceManifestSha256,
    long capturedAtEpochMs,
    long certifiedAtEpochMs,
    long validUntilEpochMs,
    long evaluatedAtEpochMs,
    EvidenceClockTrust clockTrust
) {}
```

`AspectMode` 独立保存在 `AppSettings`，不属于预设，但会进入完整组合的性能证据 key。`BuildAlgorithmAvailability`、运行时 GL 能力和 `CertifiedVideoConfiguration` 是三个相互独立的门：APK 中有代码不等于 GPU 可运行，GPU 可运行也不等于完整组合已通过功耗/伪影门禁。认证匹配的是完整组合，绝不能把“Motion 已认证”和“ScaleFX 已认证”两个集合做笛卡尔积后自动解锁 `Motion + ScaleFX + CRT`。

Resolver 必须在一个不可变结果中原子输出 `resolvedConfigurationId + resolvedConfigurationKey`；两者表示**准备应用的完整稳定配置**，必须一起为空或一起非空。统一 presenter 只能在同一个 render-thread 事务中完成 display generation、时间状态、空间 pipeline、后处理、比例、frame-rate vote/pacer 与音画延迟的实际切换，成功后才原子发布 `activeConfigurationId + activeConfigurationKey + surfaceEpoch + displayRequestGeneration`。`VideoStatusAccumulator` 禁止从多个异步字段事后拼接 active key。`PRIMING/PRIMING_SHADOW/BUFFERED_NATIVE_HOLD/SURFACE_SUSPENDED_HOLD/DRAINING`、紧急保护帧、资源重建和 resolved/active 任一不匹配期间，active id/key 必须一起置空并让资格样本立即 `INVALID`；稳定 Native/Motion 只有 active id/key 与 resolver 输出完整相等时才可计入驻留。

`configurationId` 只是稳定 tuple 的身份，不等于实验室认证结论。内置 Nearest/Sharp 基线用 `builtin:<canonical-key-sha256>` 生成确定 id；需要证据的 MMPX/ScaleFX/Motion 使用证书中的 id。是否认证仍只由 `CertifiedVideoConfiguration`/`PhysicalScanEvidence` 判断，不能因为 active id 非空就显示“已验证”。

均衡不是“固定请求 MMPX 再失败回退”的配置：解析器根据完整资格把均衡原子展开为 `60 + Native + MMPX` 或当前正式的 `60 + Native + Sharp`，二者都可显示“完整生效”。只有系统拒绝、温控、电量、shader/FBO/context 等真实运行约束才产生 fallback。MMPX 组合至少需要 `COMPATIBILITY_AND_POWER`；ScaleFX、Motion 及任何包含 Motion 的组合需要 `DEVICE_LAB`。所有页面、renderer 和保护控制器都只消费同一个 `DisplayQualityResolver` 输出；shader/FBO 失败必须回灌 `RuntimeFailure` 后重算，renderer 不得自己形成第二套“有效模式”。用户偏好与当前有效配置分开保存；自动回退不得覆盖用户选择。认证由开发团队基于 Release 证据签发并随 APK 发布，运行时不能由系统回退状态、深链或用户 benchmark 自行提升级别。

GL 能力在没有活动游戏 Surface 时通过短生命周期 EGL pbuffer probe 采集；在 probe 完成前状态是 `UNKNOWN`，只允许 Nearest/Sharp 等基础组合。`supportsEs31Compute` 表示硬件/API 声明，`canCreateOwnedEs31Presenter` 必须实际创建并销毁独立 ES3.1 context 才能为真。GL context 丢失、驱动更新或 Surface 重建后重新验证运行资源；旧快照不能跨进程重启冒充新鲜能力。

`SourceTiming` 只能来自 core 已确认的帧元数据，不能从 ROM 文件名推断。首版 Motion 的完整配置 key 必须是 `NTSC_60_0988`；`PAL_50` 和 `UNKNOWN` 对 Motion fail-closed。显示模式使用 `modeId + width + height + canonical refreshMilliHz`，不拿 Java `float` 做 record 精确相等；运行观测与资格 key 的 milliHz 允许明确的 ±1000mHz 系统误差门限，但 modeId/分辨率仍需准确匹配。

`DisplayObservation` 是 resolver 的必需输入。每次显示请求增加 generation；过期或旧 generation 的观测不能影响新请求。记录本身不保存 freshness；resolver 接受当前 `elapsedRealtime`，按 1500ms TTL 派生 `FRESH/STALE/UNKNOWN`，因此缓存中的旧 `FRESH` 值绝无机会被继续信任。Motion 启动必须等待同 generation 的实际 120Hz fresh lease 稳定 3 秒；活动 Motion 对任何不兼容观测或 freshness lease 失效立即 fail-safe，不能等待 3 秒。稳定门限只决定持久状态与重请求抑制。输出同时保留“应用请求的 mode”和“系统报告活动 mode”。安全策略只能改变请求，永远不能把“已请求 60Hz”写成“系统实际 60Hz”；系统报告结果始终是事实约束。

`PhysicalScanEvidence` 不是独立可复用的“已验证”缓存，只能由**当前全量匹配**的 `DeviceQualityProfile + CertifiedVideoConfiguration` 即时派生。profileId、configurationId/完整 key、系统 build fingerprint、driver fingerprint、build/算法 implementation hash、evidence manifest 或证书时间区间任一失配、缺失或失效，状态立即为 `UNVERIFIED` 并填写唯一 `invalidReason`；持久层只保存随 APK 发布的 profile/certificate、可信时钟锚和原始 evidence identity，不单独持久化 `VERIFIED` DTO。

`PhysicalScanEvidence` 的构造不变量固定为：`state == VERIFIED` 当且仅当 `invalidReason == NONE`、`clockTrust == TRUSTED` 且该 DTO 的全部身份/时间字段与本次验证输入逐字段相等；其他所有情况必须是 `state == UNVERIFIED && invalidReason != NONE`。禁止构造 `VERIFIED + 非 NONE`、`UNVERIFIED + NONE` 或把上一次 VERIFIED 状态复制到新 profile/新时钟快照。

### 10.1 证据有效期、可信时钟与 bootstrap

证书有效区间统一采用半开区间 `[certifiedAtEpochMs, validUntilEpochMs)`。`COMPATIBILITY_AND_POWER` 最长有效期为 365 天（31,536,000,000ms），`DEVICE_LAB` 最长有效期为 180 天（15,552,000,000ms）；`validUntilEpochMs <= certifiedAtEpochMs`、持续时间超过对应上限、非正时间戳或 checked arithmetic 溢出均为不可恢复的证书格式错误。内置 `builtin:` Nearest/Sharp 基线不需要证书，也不受这两个 TTL/时钟 bootstrap 限制；MMPX、ScaleFX、Motion 和任何其他高级组合一律需要当前有效证书，不能因为离线或时钟不可用而“宽限”开放。

`BootSessionIdentityProvider` 由平台层读取 Android `BOOT_COUNT`，并在内核 boot id 可读时一并纳入 canonical SHA-256；两个来源至少一个必须可用，值与来源标记一起 canonicalize。当前 boot 是否匹配只能比较 provider 产出的 `canonicalIdentitySha256`，禁止由 UI、Resolver、测试入口或调用方传入 `sameBoot=true` 之类 boolean。两种来源都不可用时高级 bootstrap 失败为 `TIME_BOOTSTRAP_REQUIRED`。进程重启不会改变 boot identity；设备重启必然产生新 identity。

`PersistedEvidenceClockAnchor` 是**可空的、应用私有的完整记录**，不承诺跨清数据或卸载保存。读取器先确定唯一状态：记录完全不存在为 `NONE`；所有必填字段、范围、唯一 tombstone 和 Android Keystore MAC 都有效为 `COMPLETE`；部分字段、重复 tombstone、MAC/key 不可验证、反序列化或算术检查失败均为 `CORRUPT`。旧版本的零散时间字段不能拼成 COMPLETE。重装后即使外部残留旧文件也没有原 Keystore key，只能得到 NONE 或 CORRUPT，绝不能称为跨卸载防回拨。

应用运行时的高级证据 bootstrap 规则是 fail-closed 的：状态为 NONE/CORRUPT、COMPLETE anchor 的 boot identity 与当前 boot 不同，或当前 boot 尚无可信 anchor 时，必须通过平台 `SystemClock.currentNetworkTimeClock().millis()`（不支持或抛错即不可用）取得 `ANDROID_NETWORK_TIME` 样本，把采样 epoch、同次 `elapsedRealtime` 与当前 boot identity 原子绑定，并把 API/source、系统 build 和样本 identity 的 provenance hash 写入记录后原子生成新的 COMPLETE anchor；从 MAC 有效但 boot 不同的旧 COMPLETE anchor 重建时必须原样携带其全部 tombstone，NONE/CORRUPT 则不能假装恢复其中内容。网络时间不可用、来源不是该平台认证源或提交失败时，不得使用本地墙钟、Release build 时间或旧 boot anchor 解锁高级模式，分别返回 `TIME_BOOTSTRAP_REQUIRED` 或 `CLOCK_STATE_CORRUPT`。因此 fresh install、清数据、重装和每次设备重启在没有新鲜平台认证时间时都只允许基础模式；同一 boot 内的进程重启可继续使用 COMPLETE anchor。

Release 离线校验器不走上述 app bootstrap，也不能把构建机墙钟当证明。每个 attempt 必须显式接收三张不可互换的 `AuthenticatedReleaseTime`：

1. `CANDIDATE_BUILD` 在候选 APK 构建前获取，四个 bound-context 字段必须全为 null；候选 APK/record/manifest 绑定它。
2. 候选证据和 candidate manifest 冻结后再获取 `PROFILE_RELEASE`；它必须绑定 candidate-record/manifest 的 canonical SHA-256，两个 bound-release 字段为 null，且 epoch 不早于 candidate manifest 中任一 `capturedAtEpochMs`。profile 的 `certifiedAtEpochMs` 必须等于这张 receipt 的 epoch，最终 APK 的 `releaseBuildEpochMs` 和签名 profile 绑定它。
3. 所有 Release 自动化、视觉、无障碍、安装和试玩 receipt 已生成且四类 gate record 已 finalise 后，再获取 `RELEASE_COMPLETION`；它必须同时绑定 candidate-record/manifest、Release record 和由 base/profile/automation/experience gate-record canonical hashes 得到的 `boundReleaseGateSetSha256`，epoch 不早于任一 Release capture/sign-off。Release manifest 和最终完成结论绑定这张 receipt；最终验证必须以 `completionEpoch` 重算所有证书并要求 `certifiedAt <= completionEpoch < validUntil`。

三张 receipt 的 source 都只允许签名 CI 时间、RFC3161 时间戳或经审计的等价独立时间证明，nonce、authority identity、role、context 和 proof hash 全部进入对应 manifest/record。缺失、角色/context/nonce/签名无效、replay、完成时间早于任一绑定证据，或 source 为 `ANDROID_NETWORK_TIME` 的 Release 验证立即非零退出。旧的 PROFILE_RELEASE epoch 不能替代 RELEASE_COMPLETION；因此验收拖过 `validUntil` 时 Release 必须失败并重新认证，不能用早先仍有效的时间宣告完成。

同一 current boot 内，COMPLETE anchor 的单调下界和平台认证网络时间现值分别按 checked arithmetic 计算：

```text
monotonicFloorEpochMs =
  anchor.anchorEvaluatedAtEpochMs
  + (elapsedRealtimeMs - anchor.anchorElapsedRealtimeMs)

authenticatedNetworkNowEpochMs =
  sample.sampledEpochMs
  + (elapsedRealtimeMs - sample.sampledAtElapsedRealtimeMs) // 有当前 boot 样本时

evaluatedAtEpochMs = max(
  authenticatedNetworkNowEpochMs if available,
  systemWallEpochMs,
  releaseBuildEpochMs,
  anchor.persistedLastSeenValidatedEpochMs,
  monotonicFloorEpochMs
)
```

若没有同 boot COMPLETE anchor，也没有成功提交当前 boot 的认证网络 bootstrap，则上式即使能从 wall/build 算出数值也**不具有高级证据信任资格**。每次准备发布 `TRUSTED + VERIFIED` 前，以保留全部 tombstone 的单事务把 `anchorEvaluatedAtEpochMs = evaluatedAtEpochMs`、`anchorSystemWallEpochMs = systemWallEpochMs`、`anchorElapsedRealtimeMs = elapsedRealtimeMs` 和 `persistedLastSeenValidatedEpochMs = max(old, evaluatedAtEpochMs)` 写回；事务成功后才能发布 VERIFIED，任一字段不能单独推进，写入失败按 CORRUPT 锁定。

`EVIDENCE_CLOCK_ANOMALY_TOLERANCE_MS` 固定为 86,400,000ms（24 小时）。同 boot 时，`(systemWallEpochMs - anchorSystemWallEpochMs)` 与 `(elapsedRealtimeMs - anchorElapsedRealtimeMs)` 的差值绝对值超过 24 小时、墙钟相对 `monotonicFloorEpochMs` 前后偏离超过 24 小时、认证网络现值与 monotonic floor 矛盾超过 24 小时、时间/差值溢出或 elapsed 倒退，均把 clock trust 设为 `TIME_UNTRUSTED`。证书签发时间晚于 evaluated 值一律 `FUTURE_ISSUED`；相差超过 24 小时还同时构成 `TIME_UNTRUSTED`。反复把墙钟每次回拨 23 小时也不能降低 evaluated 值，因为 monotonic floor 和 persisted last-seen 始终参加 max。

每张证书在当前 boot 的单调过期 deadline 只允许提前：

```text
candidateDeadlineElapsedMs = elapsedRealtimeMs
  + max(0, validUntilEpochMs - evaluatedAtEpochMs)
scheduledDeadlineElapsedMs = min(previousScheduledDeadlineElapsedMs, candidateDeadlineElapsedMs)
```

首次没有 previous deadline 时取 candidate；所有减法/加法使用 checked arithmetic，异常即 `TIME_UNTRUSTED`。`certificateIdentitySha256` 是 `profileId + configurationId + 完整 VideoConfigurationKey + manifest + certifiedAt/validUntil + build/算法 hash` 的 canonical hash。任何后续墙钟、网络样本、anchor 刷新或进程恢复都不得把同一 identity 的 deadline 向后移动；进程恢复从已提交 anchor 重算出的 candidate 也必须不晚于此前数学下界。跨 boot 不比较已重置的 elapsed 数字，而由 fresh authenticated network time 重算到同一个绝对 `validUntilEpochMs`，绝不能延长证书的 epoch 有效期。`EvidenceValidityWatchdog` 在进程启动、Activity resume、profile/configuration 变化、每次 Resolver 使用前及至少每 60 秒重验，并在 deadline 触发时先原子写入 `EvidenceExpiryTombstone` 再发布失效状态；观察到 `evaluatedAt >= validUntil` 时也执行同一事务。tombstone 对该精确证书永久阻止其在本次安装中复活，写入失败则立即 `CLOCK_STATE_CORRUPT`。应用数据丢失会同时丢失 tombstone，因此恢复路径只能是 fresh authenticated network bootstrap；绝不能退回本地 wall 来“复活”。

到期、tombstone、profile 被替换、bootstrap 丢失或时钟失信时，高级选项立即显示对应锁定原因。若 Motion 正在运行，不直接跳到零延迟 Native，而是提交统一 `TemporalTransition` 进入 `BUFFERED_NATIVE_HOLD` 并在既定安全边界 drain；非 Motion 高级空间组合通过资格图原子回退到内置基线。

### 10.2 `invalidReason` 确定性优先级

验证器计算所有谓词，但只返回下表中**最先为真**的一行；这张表是完整真值决策表，任何多个失败同时出现也不得由 Map 遍历顺序、异常先后或调用入口改变结果。成功的 network bootstrap/repair 必须先原子提交为 COMPLETE，再重新从第 1 行验证，不能在表中临时跳过 CLOCK 行。

| 优先级 | 为真条件 | 唯一 `invalidReason` |
|---:|---|---|
| 1 | 对当前完整 key/profile 不能找到恰好一张证书，或证书对象缺失 | `CERTIFICATE_MISSING` |
| 2 | 证书 profile identity 与当前 profile 不同 | `PROFILE_MISMATCH` |
| 3 | configurationId 或 `VideoConfigurationKey` 任一不同 | `CONFIGURATION_MISMATCH` |
| 4 | build fingerprint、driver fingerprint 或设备 identity 不同 | `BUILD_OR_DRIVER_MISMATCH` |
| 5 | build implementation hash 或 algorithm implementation hash 不同 | `IMPLEMENTATION_MISMATCH` |
| 6 | evidence manifest 缺失、hash/字段签名不符或 capture identity 不符 | `MANIFEST_MISMATCH` |
| 7 | EvidenceLevel 低于该组合要求 | `EVIDENCE_LEVEL_INSUFFICIENT` |
| 8 | 时间戳非正、`validUntil <= certifiedAt`、`capturedAt > certifiedAt` 或任一时间运算溢出 | `INVALID_VALIDITY_RANGE` |
| 9 | `validUntil - certifiedAt` 超过该 EvidenceLevel 上限 | `VALIDITY_TOO_LONG` |
| 10 | anchor 为 CORRUPT 且本轮未成功原子 repair | `CLOCK_STATE_CORRUPT` |
| 11 | COMPLETE anchor 中存在该精确证书 identity 的 expiry tombstone | `EVIDENCE_TOMBSTONED` |
| 12 | anchor 为 NONE、boot identity 不同或无当前 boot anchor，且未成功取得并提交平台认证时间 | `TIME_BOOTSTRAP_REQUIRED` |
| 13 | bootstrap 后仍发生 24 小时墙钟/network/monotonic 异常、认证时间 provenance 无效、elapsed 倒退或其他时钟不变量失败；仅“证书在未来”由第 14 行处理 | `TIME_UNTRUSTED` |
| 14 | `certifiedAtEpochMs > evaluatedAtEpochMs` | `FUTURE_ISSUED` |
| 15 | `evaluatedAtEpochMs >= validUntilEpochMs` | `EVIDENCE_EXPIRED` |
| 16 | 上述 1–15 全部为假 | `NONE` |

状态真值因此只有两类：第 16 行命中时构造 `VERIFIED/NONE/TRUSTED`；第 1–15 任一行命中时构造 `UNVERIFIED/<该行原因>`。其中第 10、12、13 行的 `clockTrust` 必须为 `TIME_UNTRUSTED`；第 14 行仍固定返回 `FUTURE_ISSUED`，但未来差值超过 24 小时时其独立 `clockTrust` 也必须为 `TIME_UNTRUSTED`。其他失败保留独立时钟计算出的 trust，但无论为哪一值都不能变成 VERIFIED。未知枚举、未知 EvidenceLevel 或新增但未加入表的失败条件一律映射为 `UNVERIFIED/TIME_UNTRUSTED`，并让测试失败以迫使更新表。

### 10.3 Motion 风险同意的提交时刻

Motion 首次风险确认不是一个裸 boolean。弹窗打开时的 profile/certificate/clock 只用于展示，不能直接写入 consent。用户点击“接受”时，`MotionRiskConsentGate.accept()` 必须重新读取一份 `TrustedEvidenceClockSnapshot`，重新获取当前 profile generation 与证书，并从上表第 1 行完整验证；只有结果仍是 `VERIFIED/NONE/TRUSTED`、弹窗展示的 profile/certificate identity 与当前值精确相等，才把 `acceptedAtEpochMs` 和 `evidenceEvaluatedAtEpochMs` **同时设为这次点击快照的 `evaluatedAtEpochMs`**，再以 profile generation 的 compare-and-set 原子保存。

点击重验期间跨过 `validUntil`、deadline/tombstone 触发、profile/configuration/manifest 被替换或 compare-and-set 失败，均拒绝本次点击且不写 consent；界面关闭或刷新为新的锁定/确认状态，不能沿用弹窗打开时间。每次使用 Motion 前仍须重新走证据表并要求 `certifiedAt <= acceptedAt == evidenceEvaluatedAt <= currentEvaluatedAt < validUntil`，同时精确匹配 profileId、configurationId/key、manifest、证书起止、build/算法 hash 与风险文案版本；任何变化自动忽略旧 consent。`TIME_BOOTSTRAP_REQUIRED/CLOCK_STATE_CORRUPT/TIME_UNTRUSTED` 时不能确认。UI、深链、旧偏好和实验室控制入口都经过同一 gate。Critical 输出 `PAUSE_FOR_CRITICAL_THERMAL`，Session 层执行暂停、停止音频、释放全部输入和恢复页；resolver 本身不直接操纵 Activity。

## 11. 动态功耗与温控保护

### 11.1 降级顺序

降级不是若干独立开关的顺序，而是 `configurationId → configurationId` 的原子有向图。每条边的目标必须是一套已提供、可运行且资格足够的完整配置；一个普通触发只走一条边。若证据表明必须同时关闭 Motion 并降低空间算法，应把二者定义成同一条原子边，而不是在控制器里临时执行两次。控制器记录实际走过的边作为恢复栈，恢复只按反向栈一次回退一条边，不能重新猜测路径。

未标定设备生成资格图时采用稳定配置候选链：`Motion120+ScaleFX → Native120+ScaleFX → Native120+MMPX → Native120+Sharp → Native60+Sharp → Native60+Nearest`，跳过任何未资格化节点。`BUFFERED_NATIVE_HOLD` 与 `SURFACE_SUSPENDED_HOLD` 是离开/暂停 Motion 时由 `TemporalTransition` 管理的短期运行状态，不是可签发、可恢复的 `configurationId`；达到安全边界或完成 Surface 恢复事务后才进入目标稳定节点。目标设备完成 paired A/B 后，`DeviceQualityProfile.adaptivePolicy` 携带该设备的明确边、每个稳定 `configurationId` 的 GPU p95 预算和恢复阈值，不能凭直觉固定为所有手机通用。

### 11.2 触发条件

- Thermal `CRITICAL`：强制暂停游戏、停止音频、释放所有输入并显示过热恢复页。
- Thermal `SEVERE` 或电池温度持续 ≥42°C：强制请求 `60Hz + Native + Nearest`；若此前在插帧，按已定义的排空时序离开缓冲。界面仍以 `DisplayObservation.systemReportedActiveMode` 展示系统事实，不能仅因已请求 60Hz 就声称实际为 60Hz。
- 系统拒绝显示模式持续 3 秒：把结果标记为持久 fallback、接受系统报告结果并停止循环抢模式；若当时 Motion 已活动，首个新鲜不兼容观测早已立即触发 Hold，不能等满 3 秒才停止合成。
- 系统省电开启或电量 ≤15%：强制应用省电有效配置；分别记录 `SYSTEM_BATTERY_SAVER` 或 `LOW_BATTERY`。
- 仅当“自适应保护”开启时：Thermal `MODERATE` 持续 10 秒、电池温度持续 ≥40°C，或 GPU p95 超过当前 `configurationId` 的预算连续 5 秒，沿资格图走一条原子降级边。
- 恢复需 Thermal 不高于 `LIGHT`、电量 >20%、电池温度 <38°C 且当前节点的 GPU 预算连续稳定 120 秒；稳定计时从所有恢复条件首次同时满足时开始，每次只弹出恢复栈的一条边，并只在暂停或安全场景切换边界重新预热插帧。

安全动作优先级固定为 `CRITICAL 暂停 > SEVERE/42°C 安全请求 > 系统省电/低电量 > 可关闭的普通自适应`；`DisplayObservation` 不参加“谁覆盖谁”，它始终是最终扫描事实约束，任何请求之后都必须重新核验。关闭“自适应保护”只关闭 Moderate、40°C 普通降级和性能型降级；不能关闭 Critical/Severe、42°C、电量 ≤15%、系统省电或系统拒绝显示模式。Thermal headroom 最多每 10 秒采样一次，并处理 API 不支持、NaN 和厂商始终返回 `NONE`。电池温度可作为运行时信号；实验室机背热点只能用热像仪或贴片温度计测量，不能由应用推算。

## 12. 设置迁移与可访问性

### 12.1 旧值迁移

迁移必须一次读取旧的完整 tuple：`aspect + filter + refresh`，逐字段保留现有效果，不能把一个旧滤镜替换成尚未实现的新算法：

- `NEAREST → CUSTOM + NATIVE + NEAREST`
- `SHARP_BILINEAR → CUSTOM + NATIVE + SHARP_BILINEAR`
- `EDGE_ENHANCED → CUSTOM + NATIVE + LEGACY_EDGE_ENHANCED`
- `CRT → CUSTOM + NATIVE + SHARP_BILINEAR + CRT`，并用迁移 golden 验证与旧 shader 的可见差异在门限内。
- 旧 `HZ_60/HZ_90/HZ_120` 原值保留。
- 旧 `AUTO` 迁移为可见的 `LEGACY_AUTO_INTEGER_MULTIPLE`，保持当前 120→60 行为并显示一次迁移说明；用户可随后选择新的“跟随系统”或固定值。

迁移只执行一次，旧设置保留备份键一个版本。新算法通过门禁后只向用户推荐，不静默改写迁移值。

### 12.2 无障碍

- 卡片和开关点击目标不小于 48×48dp。
- “推荐、实验、尚未认证、部分生效、高功耗”均有文字，不只靠颜色。
- TalkBack 顺序：标题 → 模式 → 当前实际运行 → 比例 → 高级控制 → 帮助。
- 运行数字最多每秒更新一次；只有状态级别变化时使用 polite live region 播报。
- 1.3 和 2.0 字体无裁切；2.0 字体模式卡改为单列。
- 中文、英文、伪语言、正反横屏、16:9 与 20:9 均纳入截图和语义测试。
- 支持减少动态效果；预览动画可以暂停。

## 13. 分阶段开放规则

### 阶段 A：真实性与省电基础

- 修复显示能力解析和错误回退文案。
- 增加完整运行状态。
- 先把所有模式迁移到 `SurfaceView + 单一 native-owned EGL presenter`，完成现有 Nearest/Sharp/legacy-edge/CRT 的视觉与生命周期 parity 后删除旧 `GLSurfaceView` 所有权；再改为新帧驱动渲染，避免重复 framebuffer copy/draw/swap。
- 正式开放省电档；均衡以 Sharp Bilinear 作为当前完整配置，不能显示为失败回退。

### 阶段 B：正式像素重建

- 实现 MMPX 2×、整数 FBO 和 golden image；为每个目标分辨率/构建/GPU/驱动组合生成 `COMPATIBILITY_AND_POWER` 资格。
- 资格通过后，该完整组合上的均衡从 Sharp 原子升级为 MMPX；未通过的设备仍保持 Sharp 的完整均衡配置。
- 实现标准 ScaleFX 3×五 pass 与最终 sharp composite；只有完整组合得到 `DEVICE_LAB` 证据后才开放，不能拿 MMPX 的资格代替。

### 阶段 C：低延迟 120Hz

- 在阶段 A 已统一的 native EGL presenter 中保持新帧驱动且提交率约等于 core 确认的源率，通过 Surface/Window 模式请求让系统选择物理 120Hz；此阶段不接入 Swappy，也不重复提交旧 buffer。
- 用系统报告、SurfaceFlinger/Perfetto 和高速录像共同证明活动模式 120Hz、源率对应的独立帧/新提交（NTSC 约 60、PAL 约 50）和显示系统保留旧 buffer 的 cadence；证据与 `SourceTiming` 一起记录，若阶段 C 只跑 NTSC 样本必须明确限制结论范围。
- 正式开放“低延迟 120”，不称作插帧。

### 阶段 D：实验运动补偿

- 在同一个 native EGL presenter 内加入 ES3.1 Motion pipeline、presentation timestamp 和 Swappy/等价 pacer，为真正约 120 次合成提交服务；不创建第二套 Surface/presenter。
- 插帧算法、场景保护和逐帧指标完成。
- 只对白名单中的完整 `VideoConfigurationKey（SourceTiming + 分辨率/显示 mode + 时间/空间/后处理/比例）+ V2324A + build/GPU/驱动指纹 + 构建/算法 hash + 证据 hash` 开启认证入口；每个 key 单独签发，NTSC 证据不能复用于 PAL/UNKNOWN。
- 通过 30 分钟 Release 实机与人工伪影评审后，才允许极致模式解锁。

认证采用两阶段构建，避免“未认证所以无法测试、加入 profile 后 APK 又变化”的闭环：内部候选 build type 必须 `initWith(release)`，仅允许实验室签名/applicationId、候选 profile overlay 和隔离 driver 三类差异；Java/NDK 优化、R8/resource shrink、ABI、CMake/编译链接参数、依赖图和打包选项生成 resolved variant parity hash，候选与 Release 必须一致。候选包只能加载候选完整配置且不进入发布渠道；通过后对算法源码、shader、编译参数和 presenter/audio 关键文件生成 canonical implementation manifest 及 hash，签发正式 profile；再从已提交的干净生产输入生成最终 Release，并重新核验三类 hash、签名、APK hash、profile 匹配和关键 30 分钟回归。profile 受 APK 签名保护，本轮不称“独立签名 profile”；若未来增加 detached signature 再改变术语。任何 canonical manifest 或 resolved variant 内输入变化都会使资格 fail-closed。

## 14. 验收门禁

### 14.1 真实性

- 设置页、状态页、日志中的源帧、合成帧、应用提交率、系统报告的活动模式和实验室物理扫描证据互不混淆；单次 `Display.getRefreshRate()` 不能标成面板实测。
- 状态为 `APPLIED` 时，固定 60/90/120 请求与系统报告的活动模式误差不超过 1Hz；状态为 `FALLBACK` 时允许偏差，但必须记录实际值和可验证原因；`FOLLOW_SYSTEM` 没有数值请求，不参与误差门禁。
- 没有插帧时合成帧率必须是 0。
- `IMMEDIATE_NATIVE/PRIMING/PRIMING_SHADOW/MOTION_COMPENSATING/BUFFERED_NATIVE_HOLD/SURFACE_SUSPENDED_HOLD/DRAINING`、surface epoch、display generation、active configuration id/key、音频延迟、视频队列深度和 cadence adjustment 与 trace 一致。
- 极致完整组合未认证时无法从 UI、深链、旧偏好或单项资格拼接绕过门禁。
- 证据有效期必须用参数化边界测试逐项覆盖：`evaluated = certifiedAt - 1` 为 `FUTURE_ISSUED`，`evaluated = certifiedAt` 有效，`evaluated = validUntil - 1` 有效，`evaluated = validUntil` 与 `validUntil + 1` 均为 `EVIDENCE_EXPIRED`；`validUntil == certifiedAt`、负时间戳和 long 溢出均为 `INVALID_VALIDITY_RANGE`。
- TTL 测试分别覆盖 `DEVICE_LAB` 的 180 天与 `COMPATIBILITY_AND_POWER` 的 365 天：恰好上限有效，上限加 1ms 为 `VALIDITY_TOO_LONG`；低等级不能代替高等级，内置 Nearest/Sharp 在没有证书与 `TIME_UNTRUSTED` 时仍可用，高级组合必须锁定。
- 可信时钟算术测试必须精确断言 `monotonicFloor = anchorEvaluated + (elapsedNow - anchorElapsed)` 与五项 `max`，覆盖 release build/last-seen/network/monotonic 各自成为最大值、checked overflow、elapsed 倒退、墙钟刚好偏离 24 小时与 24 小时加 1ms；再连续多次每次回拨墙钟 23 小时，证明 evaluated/last-seen 不下降且同证书 deadline 从不变晚。任何刷新后的 deadline 必须 `<=` 刷新前值，跨进程恢复也必须保持该不变量。
- bootstrap 状态矩阵必须覆盖 fresh install 的 NONE、合法 COMPLETE、缺字段/MAC 失败/重复 tombstone 的 CORRUPT，并分别组合当前 boot 平台认证网络时间可用/不可用；NONE 或新 boot 无认证时间得到 `TIME_BOOTSTRAP_REQUIRED`，CORRUPT 未 repair 得到 `CLOCK_STATE_CORRUPT`，基础模式始终可用而高级模式始终锁定。Release verifier 另测 SIGNED_CI/RFC3161/等价证明成功，以及缺证明、伪造证明、Android app network source 或构建机 wall source 非零失败；还必须排列交换三张 receipt 的 role、修改 nonce/authority/四个 context 字段、让 PROFILE_RELEASE 早于一条 candidate capture、让 RELEASE_COMPLETION 早于一条 Release capture/sign-off、篡改任一 gate hash、重放另一 attempt 的 proof，并逐项证明 candidate、profile/final APK 与 completion manifest 只能接受各自的角色和 hash。把 completion epoch 分别置于 `validUntil - 1`、`validUntil`、`validUntil + 1`，只有第一项可完成 Release。
- 生命周期测试必须覆盖同 boot 进程重启继续使用 monotonic anchor、设备重启因 boot identity 改变而要求 fresh bootstrap、清应用数据与重装回到 NONE、旧外部残留因 Keystore key 不符为 CORRUPT；本地墙钟无论调到证书有效区间何处都不能让上述状态解锁。已观察过期必须先写 exact tombstone，随后 wall 回拨、进程重启、profile reload 都保持 `EVIDENCE_TOMBSTONED`；模拟 anchor/tombstone 全部丢失后也必须先取得 fresh authenticated network time，不能宣称跨卸载保存 tombstone。
- `invalidReason` 测试须对表中第 1–15 行各做单失败用例，并对任意两行同时为真做 pairwise 组合，结果严格为优先级较高者；同时穷举断言只存在 `VERIFIED/NONE/TRUSTED` 或 `UNVERIFIED/非 NONE`，未知枚举/新增失败默认 fail-closed。证书未来 1ms 为 `FUTURE_ISSUED`，未来 24 小时加 1ms 还必须使 clock trust 为 `TIME_UNTRUSTED`。
- `MotionRiskConsent` 变异测试必须逐字段翻转 profileId、configurationId/完整 key、manifest、证书起止时间、build/算法 hash 与文案版本；点击接受前推进 clock 穿过 expiry、在弹窗期间热替换 profile/certificate、触发 tombstone 或让 compare-and-set 失败时均不得写记录。成功用例必须证明 `acceptedAtEpochMs == evidenceEvaluatedAtEpochMs == 点击时新快照.evaluatedAtEpochMs`，不是弹窗打开值；失效时若 Motion 活动则通过 Hold/drain 安全退出，不能直接切为零延迟 Native。

### 14.2 帧与延迟

- NTSC 唯一源帧 59.9–60.2fps；PAL 49.9–50.1fps。
- 30 分钟 core sequence 不倒退；由 60.0988 与实际扫描率差异产生的 cadence adjustment 必须计数，不能误报为 core 丢帧。
- 新帧驱动 Native 模式下，每个已复制 unique sequence 至多 texture upload/swap 一次；若 latest-frame coalescing 跳过 sequence，状态和门禁按 `coreProduced/copied/uploaded/skipped` 分别记账。Motion 认证样本不允许 sequence 缺口。
- 120Hz 低延迟的应用新 buffer 提交仍约等于 core 确认的源帧率（NTSC 59.9–60.2fps；PAL 49.9–50.1fps）；系统活动模式与 SurfaceFlinger/Perfetto 证据为 119–121Hz，否则明确回退。
- 运动补偿合成帧 59.5–60.5fps，总提交 119–121fps。
- GPU 时间覆盖 texture upload、运动估计/warp、全部空间 FBO pass、final composite 和 CRT：省电 <2ms、均衡 <3ms、低延迟 120 <3ms、极致 <5ms。优先用 `EXT_disjoint_timer_query`；不支持或发生 disjoint 时该样本作废，并用 Perfetto/AGI GPU counter 作为替代证据。
- 触摸事件携带 Java `elapsedRealtimeNanos`；core 在实际读取 pad bits 时写 native monotonic timestamp 和 input generation。JNI `ClockDomainCalibrator` 用成对样本估计两时钟的 offset/uncertainty，只有 uncertainty <1ms 的窗口进入 touch-to-core p95；不能把“Java 写入按键位”当终点。目标 p95 ≤16ms；非插帧触摸到可见响应 p95 <50ms。
- A/V 监视器把 `AudioTrack.getTimestamp()` 的实际播放头与 presenter/Swappy presentation timeline 映射到同一校准时钟，记录 skew 和测量不确定度；队列长度估算不能代替实际播放 timestamp。插帧延迟基线必须使用相同空间滤镜、相同 120Hz 物理模式和相同音频配置；新增延迟 ≤20ms，绝对 p95 <70ms，A/V skew 绝对值 <20ms。

### 14.3 画质

- 固定测试帧覆盖文字、细线、棋盘格、sprite 闪烁、横纵卷轴、场景切换和调色板闪烁。
- MMPX/ScaleFX 与基线图有可解释差异，不能只是整体锐化。
- 颜色并集断言只作用于 256×240、空间重建/最终 sharp composite/CRT/操控 UI **之前** 的 temporal intermediate `M(A,B)`；该层每个 RGB565 颜色必须来自相邻 A/B 帧的并集，安全回退块不得产生透明拖影。ScaleFX 和最终合成之后的 Surface 输出允许产生滤波颜色，不应用此断言。
- 五款固定 ROM 由至少两人逐场景 A/B；任何连续可见拉丝、HUD 扭曲或闪烁丢失都阻止插帧认证。

### 14.4 vivo X100 Pro 功耗与温控

固定亮度约 200nit、室温 23±2°C、同一存档与场景，预热 10 分钟后测 30 分钟。每档至少做 3 次与 60Hz+Nearest 基线交错顺序的 paired A/B；固定电池区间、音量、网络、充电状态、后台进程和 Game Mode，报告均值、p95 与 95% CI：

| 档位 | 相对 60Hz + Nearest 的平均增量目标 | 电池温度 | 机背热点 |
|---|---:|---:|---:|
| 省电 | ≤0.3W | <38°C | <40°C |
| 均衡 | ≤0.8W | <40°C | <42°C |
| 低延迟 120 | ≤1.2W | <40°C | <42°C |
| 极致 | ≤2.0W | <40°C | <43°C |

每个 30 分钟目标样本必须在每个计时采样点原子报告同一个目标 `activeConfigurationId + activeConfigurationKey`，其中 full key 包含 core-confirmed `SourceTiming`；id/key 必须与 resolver 的目标完整相等并保持同一 `surfaceEpoch + displayRequestGeneration`。任一过渡/紧急帧的空 id/key、epoch/generation 变化或字段不匹配立即使样本 `INVALID`，不能通过异步字段拼接补回。系统报告活动模式、Motion/Native 稳定状态、空间算法、后处理均与 key 一致，fallback 数为 0，Motion sequence 缺口为 0；Motion 样本的同 generation 显示观测 lease 还必须在整个计时窗口持续 `FRESH`，任一 1500ms deadline 超时即使旧记录仍显示 120Hz也将该次样本标记为 `INVALID`。任何自动降级、显示拒绝、观测失鲜、shader fallback 或安全切换都使该次样本 `INVALID`，不能用降级后的低功耗平均值通过原目标门禁。为了测量原始上限，可以在实验室 characterization 中关闭**可选**自适应，但强制安全永远开启；此外还必须用默认保护开启做独立 30 分钟回归。默认极致在 40°C 已会普通降级，因此正式极致电池门限统一为 <40°C。

优先采集可用的 power rails；不可用时用 BatteryManager 长窗口采样与外置功耗仪交叉验证。功耗门禁只有在“平均增量的 95% CI 上界不超过目标”时通过；CI 跨越门限时增加 interleaved paired runs，若仍无法分辨 0.3W 级差异则以外置功耗仪的同样配对统计为准，否则标记 `UNVERIFIED`。机背热点用热像仪或贴片温度计测量，不能由 BatteryManager 推算。UI 在没有设备标定数据时只显示定性功耗等级。

### 14.5 证据包

- Git commit、Release APK SHA-256、签名信息。
- 设备型号、系统构建、GPU 驱动、支持模式列表。
- 每个证书的 profile/configuration identity、EvidenceLevel、`capturedAtEpochMs/certifiedAtEpochMs/validUntilEpochMs`、可信时钟求值和 evidence manifest hash；Release 包还必须包含三张角色/context 完整的独立 `AuthenticatedReleaseTime` 及 source/proof hash。校验器以 `RELEASE_COMPLETION` epoch 做最终有效期判断，拒绝缺失/无效/重放时间证明、完成时间早于证据、签发时间在未来、完成时已过期或 TTL 超上限的 profile，不能以较早 receipt、构建机 wall 或 app bootstrap 代替。
- Perfetto、SurfaceFlinger、应用帧日志和 thermal/power CSV。
- 每档 30 分钟摘要、失败与复测记录。
- 五款固定 ROM 的原生/重建/插帧并排截图与屏幕录像。
- 高速摄影的触控到可见响应。

缺少任一关键证据时，只能写“未验证”或“未认证”，不能写“通过”。

## 15. 主要风险与停止条件

- 若运动补偿在 3 款以上固定样本中持续出现无法可靠屏蔽的 sprite/HUD 伪影，则不发布插帧，极致改为“原生时间 + 120Hz 低延迟扫描 + ScaleFX 3×最终合成”。
- 若 ScaleFX 相对 MMPX 的盲测偏好低于 60%，或功耗超过门限，则不进默认档，仅保留高级选项。
- 若 vivo 系统长期拒绝应用 120Hz 请求，接受实际模式并展示原因，不反复强制切换。
- 若统一 native EGL presenter 的基础生命周期、录屏或 Surface 恢复 parity 不稳定，阶段 A 失败，回到上一可用 Release，B/C/D 均不得建立在旧双所有权路径上。若只有 ES3.1 context 升级或 Swappy 扩展不稳定，则只锁定 Motion；已经通过门禁的统一 presenter 基础模式和非插帧低延迟 120 可独立发布。

## 16. 参考资料

- Android Frame Pacing：<https://developer.android.com/games/sdk/frame-pacing>
- OpenGL Swappy 集成：<https://developer.android.com/games/sdk/frame-pacing/opengl/add-functions>
- Android 游戏刷新率指南：<https://developer.android.com/games/optimize/display-refresh-rate-change>
- `Surface.setFrameRate`：<https://developer.android.com/reference/android/view/Surface>
- Android Thermal API：<https://developer.android.com/games/optimize/adpf/thermal>
- Android 游戏功耗优化：<https://developer.android.com/games/optimize/power>
- Perfetto FrameTimeline：<https://perfetto.dev/docs/data-sources/frametimeline>
- MMPX：<https://www.jcgt.org/published/0010/02/04/paper.pdf>
- ScaleFX：<https://github.com/libretro/glsl-shaders/tree/master/scalefx>
- RIFE：<https://www.ecva.net/papers/eccv_2022/papers_ECCV/papers/136740608.pdf>
