> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 测试增强完整用例清单

日期：2026-09-13；基线：`661e84bc97023676882d5f8ffbbd97634d25e8b3`（main，1.1.12）。

本清单配套[总体计划](2026-09-13-full-functional-test-enhancement-plan.md)和[当前测试资产审计](2026-09-13-test-coverage-inventory.md)。**这是待实现/补强的完整清单，不是已执行通过报告。** 已有同义测试可以复用，但必须达到本表的操作与断言、接入runner并提交新SHA证据；只按名称相似不能标完成。

共 **239 条用例定义**：84 UT、21组件/系统集成、97模拟器E2E、22真机、15门禁。97条E2E展开为 **288个平台执行实例**；22条真机用例展开为 **65个平台计划实例**。真机按总体计划分为每候选包必测和轮换测试，不要求每个PR重复全部65项。

## 1. 执行合同

- `A/H/I` 分别为 Android/HarmonyOS NEXT/iOS；`S` 为共享主机层；`Hn` 为 Harmony native host；`T` 为工具；`ALL` 为跨层门禁。
- E/D用例执行编号包含平台前缀，例如 `A-E041`、`H-E041`、`I-E041`，三者独立记状态。本文明确列出所有展开ID。
- UT用例用纯逻辑或native API级别测试，允许真实core依赖；包含真实平台I/O/设备sink的C用例单列为集成，不能贡献UT专用覆盖率。既有大文件混合测试应拆label/target后计量。
- E2E默认从系统桌面/正式入口出发，使用真实app、存储、native core、renderer。正向准备经UI导入，结果至少包含可见行为；涉及持久化时必须销毁进程后重开，Activity重建不代替冷重启。
- 每条表内列出的所有参数/子步骤都要执行并逐项记录；任一失败则该ID失败，不用多数通过掩盖少数失败。可拆子ID，如 `A-E050-4_3`、`A-E050-square`、`A-E050-integer`，父ID仅在全部子ID成功时PASS。
- PASS必须包含预期结果证据。FAIL为断言失败；ERROR为执行器/报告错误；BLOCKED_ENV为缺设备/工具链/签名；BLOCKED_TOOLING为缺注入/测量能力；BLOCKED_IMPLEMENTATION为功能未实现。后三者均不是覆盖完成。
- 非P0用例不代表可永久不做。所有常用功能E2E都属于增强计划退出标准；P0先落地、P1紧随。
- 以下实现路径是精确落点：既有文件扩展，新文件按计划创建；本次尚未创建任何测试代码。

## 2. 共用前置与夹具

| 标识 | 固定准备方法与约束 |
|---|---|
| CLEAN | 新建本任务专用模拟器或独立测试设备用户；首次安装，无导入来源。不可清空用户正在使用的应用来满足条件 |
| PICKER | 系统文件提供器可见 `FlyNES-Test-v1/`；正向导入必须实际经过系统选择器、授权、扫描、启动，禁止直接写目录数据库 |
| DIAG / PLAYROM | 新建并归档可合法分发的 `input-av-diagnostic.nes`：画面显示逐帧计数、每个输入边沿序号/键位，发出对应声标；PLAYROM 可用仓库现有许可 fixture 做普通玩法验证 |
| DIAG2 / LIB2 | 两个不同完整 SHA-256 的自制诊断ROM，文件名 `Alpha-测试.nes` / `Beta-测试.nes`，各自有可区分画面；LIB2 经真实UI导入并记录分类/收藏/最近前态 |
| ZIP2 | `two-games.zip` 含按顺序排列的 `01-alpha.nes`、`02-beta.nes`；两者内容不同，明确期望启动第二条的画面/摘要 |
| DUP2 | `source-a/` 与 `source-b/` 含字节完全相同的 Alpha；来源身份不同，内容身份相同 |
| LARGE100 / LARGE2224 | 分别100/2224个合法自制变体，每个具有唯一内容摘要和可区分诊断标记；通过UI导入。不能只换文件名制造“100款不同内容” |
| LARGESCAN / SCANFAIL | 测试文件提供器或可控文件系统制造慢读/指定文件I/O失败；数据仍由正式扫描器读取，禁止直接返回假的扫描结果 |
| RANK | 固定manifest列出每个合法测试内容、可见标题、预期排名与tie-break；使用现有命名/排名规则，不改产品排行榜来通过测试 |
| UNKNOWN / BADFILES | 未登记hash、合法Unicode/空白外名；独立坏fixture包含截断ROM、坏CRC ZIP、文本文件。生成器先以通用parser验证分类 |
| COVERSEQ | 自制诊断ROM按固定帧阶段显示好帧→纯黑→纯白；用真实封面采样路径，禁止直接注入“好封面”结果 |
| BADSAVE / BADCATALOG / BADLAYOUT | 仅破坏本次run-id所属测试沙箱文件；保存原文件哈希及副本。负测允许准备持久化错误，不用于正向流程造假 |
| IOFAULT / RESOURCEFAULT | 在平台实际写入或资源读取边界一次性注入失败，记录触发次数；测试钩子不返回伪造UI状态、不进入发布包。普通真机不通过填满存储制造故障 |
| GPU | 明确记录模拟器GPU/Metal/EGL能力；支持路径跑真实呈现/金标，缺能力单独跑回退。缺能力不能等价于支持路径通过 |
| MULTITOUCH / SHORTTAP | 操作系统输入层可注入独立多指/短按，仍经过应用窗口分发；不能直接调用View、runtime或输入service。若所选SDK驱动无此能力，标BLOCKED_TOOLING并在工具批次补齐，不降低E2E定义 |
| AUDIOFOCUS / SYSTEMINTERRUPT | 独立测试媒体应用或系统能力触发真实focus/生命周期事件。平台不允许强制模拟的来电/路由转真机；模拟器仍测试可触发的本机策略路径 |
| SMALL / LARGETEXT | 专用小尺寸横屏、安全区配置；Android 200%字体，Harmony/iOS使用所测系统实际支持的最大无障碍字号并记录。结束恢复测试前设置 |
| UPGRADE | 同签名体系的上一兼容版本与当前候选版本；旧版本先通过UI创建来源/收藏/布局/设置/两游戏进度，然后覆盖安装。没有可安装旧包时BLOCKED_ENV |
| CANDIDATE / PHONE | 包来自同一固定SHA、版本、构建配置并记录hash；PHONE必须是明确serial和型号的实体设备，电量≥30%、初始温控正常；签名信息只记录类型/适用范围 |
| E025完成 | 本次同一测试独立执行E025的准备及动作，然后恢复其测试文件；不依赖另一个JUnit用例的执行顺序 |

夹具生成/分发落点：新增 `tools/quality/fixtures/generate_product_fixtures.py`、`tools/quality/fixtures/product-fixtures-v1.json`（预期内容/行为与许可清单）；平台stager写入ignored目录，iOS复用 `ios/scripts/stage_import_fixtures.py`。固定输出manifest包含case关联、完整ROM/ZIP SHA-256、大小、parser结果、诊断标记；坏fixture明确标注预期错误。补充fixture包括NROM/MMC1/UxROM的合法自制确定性轨迹、PAL声画诊断、有battery RAM的进度诊断以及不含BIOS的FDS结构负测。哈希由实际生成字节计算，不在本文编造。新增自制ROM完成前对应用例BLOCKED_ENV，不能借私人ROM充当CI夹具。

## 3. UT完整列表（84条）

| ID / 功能 | 平台 / 优先级 | 用例 | 前置 | 执行 | 必须断言 | 实现/复用文件 |
|---|---|---|---|---|---|---|
| U001 / F02 | S / P0 | 四分类过滤与空集合 | 内置、收藏、最近、普通各一条及空目录 | 逐次切换最近/收藏/全部/内置 | 只返回匹配条目；空输入返回空；无来源重复卡片 | `shared/tests/test_product_game_center.cpp` |
| U002 / F03 | S / P0 | 中英标题与别名搜索 | 同一内容有中英标题、别名和原文件名 | 分别输入中文、英文大小写、别名、原名及不存在词 | 全部合法查询命中同一 canonical id；不存在词为空 | `shared/tests/test_product_game_center.cpp` |
| U003 / F03 | S / P1 | 热门排序与并列稳定性 | 高低排名及相同排名的乱序条目 | 过滤全部目录并重复重排输入 | 按产品排名及稳定 tie-break 输出；无随机跳位 | `shared/tests/test_product_game_center.cpp` |
| U004 / F02 | S / P0 | 选中项失效后的导航 | 选中条目存在于当前分类 | 收藏取消或来源移除后重新筛选 | 不保留可启动的失效选中项；其余状态符合导航契约 | `shared/tests/test_product_game_center.cpp` |
| U005 / F03 | S / P0 | 完整 hash 标题身份 | 同内容不同名称、同名称不同 hash | 查询标题索引并切换中英 | 同 hash 稳定；异 hash 不串条目；未知 hash 不伪造翻译 | `shared/tests/test_game_title_index.cpp` |
| U006 / F06 | S / P0 | ROM 头边界识别 | 冻结 ROM fixture corpus | 遍历合法、截断、trainer、NES2、未知格式 fixture | 结果/长度/警告与 manifest 完全相同 | `shared/tests/test_rom_payload_parser.cpp` |
| U007 / F06 | S / P0 | ZIP 结构恶意边界 | 冻结 zip/open 夹具 | 遍历重复 EOCD、重叠、前缀、加密、未知压缩及目录边界 | 只接受唯一合法结构；稳定错误码；不越界 | `shared/tests/test_bounded_zip_archive_edges.cpp` |
| U008 / F06 | S / P0 | ZIP 解压预算与 CRC | zip/payload 合法及恶意 fixture | 解压选中条目；制造 CRC 错、尾数据、超限与不完整流 | 预算内正确字节；全部恶意输入拒绝；未选坏条目不污染合法项 | `shared/tests/test_bounded_zip_payload_edges.cpp` |
| U009 / F06 | S / P0 | unsupported payload 分类 | unsupported_payload 冻结语料 | 遍历每条并混合设置预算 | 返回精确分类；不把非 ROM 解释为可运行游戏 | `shared/tests/test_unsupported_payload_classifier.cpp` |
| U010 / F03 | S / P0 | 内容去重边界 | 同内容两来源、同名异内容、不同 ZIP entry | 计算 identity 并形成目录 | 仅完全相同内容合并；来源变体保持可追溯 | `shared/tests/test_content_identity.cpp` |
| U011 / F15 | S / P0 | 设置编解码默认值 | 空配置、完整合法配置、历史版本配置 | 加载、修改、写回并重新解析 | 默认值与当前契约一致；合法字段完整 round-trip | `shared/tests/test_settings.cpp` |
| U012 / F15 | S / P0 | 设置非法值拒绝与原子更新 | 已保存合法设置 | 提交未知版本、NaN、越界 enum、有效无效混合 batch | 非法 batch 不产生半更新；原快照不变 | `shared/tests/test_settings.cpp` |
| U013 / F19 | S / P0 | 布局 wire round-trip | 推荐布局和五控件修改布局 | encode/decode 多轮并对比 | 布局坐标、scale、opacity 和版本均保留 | `shared/tests/test_product_control_layout.cpp` |
| U014 / F19 | S / P0 | 损坏布局安全回退 | 缺字段、重复控件、NaN、无穷、未知版本布局 | 逐条解析 | 拒绝或回推荐值符合既有协议；永不产生非有限命中矩形 | `shared/tests/test_product_control_layout.cpp` |
| U015 / F12 | S / P0 | 命中优先级与安全区 | 五控件相交及多个屏幕/inset 尺寸 | 点击边缘、交叠、外部及 inset 边界 | 按钮优先级符合契约；越界不输入；PAUSE 与 START 分离 | `shared/tests/test_product_gamepad_hit_map.cpp` |
| U016 / F13 | S / P0 | 暂停动作集合 | 共享暂停动作定义 | 枚举动作和顺序 | 恰好继续、游戏中心、设置；不出现手动 Save/Load | `shared/tests/test_product_pause_actions.cpp` |
| U017 / F11 | S / P0 | 四端口帧输入 | 合法 runtime 与四个不同端口输入 | 逐端口按键并推进完整帧 | 各端口独立；frame index 单调；不隐式映射全部到 port0 | `shared/tests/test_runtime.cpp` |
| U018 / F14 | S / P0 | checkpoint 恢复确定性 | 已运行诊断 ROM 至固定帧 | 保存→推进 N 帧→恢复→重放相同输入 | 下一帧状态、画面和序号满足契约；旧 PCM 清理 | `shared/tests/test_runtime.cpp` |
| U019 / F14 | S / P0 | 坏 checkpoint 不污染运行态 | 合法状态快照和截断/错 ROM/错版本 checkpoint | 依次尝试恢复 | 明确失败；原 runtime 保持可运行且状态不被部分覆盖 | `shared/tests/test_runtime.cpp` |
| U020 / F11 | S / P0 | PCM 并发欠载 | 另一线程持有真实 core 锁 | 音频线程拉取 PCM 后释放锁再次拉取 | 不阻塞等待；返回安全静音；已有有效 sample 未被破坏 | `shared/tests/test_runtime_pcm_contention.cpp` |
| U021 / F11 | S / P0 | 帧序号与 epoch 一致性 | 帧 meta、RGB565、PCM 多次发布 | 制造旧 epoch 消费和不完整帧读取 | 帧/音频元数据一致；旧 epoch 不被当作新帧 | `shared/tests/test_runtime.cpp` |
| U022 / F25 | S / P0 | session 公共入口失败关闭 | 无认证 session、合法和非法裸帧 | 提交 event、receive stream/datagram | 未经认证始终拒绝；诊断识别不触发可信状态变化 | `shared/tests/test_session_receive_seam.cpp` |
| U023 / F25 | S / P0 | session framing 类型通道约束 | golden 与错类型/错通道/截断/尾数据 | 按 stream/datagram framing 解码 | 只识别被声明的对象；拒绝混淆和越界 | `shared/tests/test_app_frame.cpp` |
| U024 / F25 | S / P0 | 初始方案超时与迟到完成 | 已启动 60 秒 attempt、有 pending command | tick 到期、倒退时钟、重复完成、旧 generation 回调 | 失败关闭；过期不能恢复授权；已消费 effect 不重复执行 | `shared/tests/test_session_initial_plan.cpp` |
| U025 / F25 | S / P0 | 能力交集与建网角色分离 | 不对称能力和 owner/authority 不同组合 | 选择共同方案并执行 PLAN/ACK/FINAL | 选中 exact 共同 bytes/hash；角色不隐式绑定；无交集有明确原因 | `shared/tests/test_pair_capability.cpp` |
| U026 / F12 | A / P0 | 连续摇杆跟随与反向 | 固定/跟随两模式及有限坐标轨迹 | DOWN→拖到五倍半径→反向→UP | 无中途零输入；方向所有权保持；UP 立即归零 | `app/src/test/java/com/flynes/emu/input/DirectionSessionTest.java` |
| U027 / F12 | A / P0 | 多指 ID 重排与按钮叠加 | 方向指+A+B 三指 | 交换 pointer index；释放其中一指；取消最后一指 | 按 ID 而非 index 跟踪；其余按键不丢；终止无粘键 | `app/src/test/java/com/flynes/emu/input/GamepadInputStateTest.java` |
| U028 / F12 | A / P0 | 短按最小脉冲 | 小于一帧的 A 点击 | DOWN/UP 同时到达，随后 core 采样和 cancel | 合法短按至少一次被采样；cancel 不保留脉冲 | `app/src/test/java/com/flynes/emu/input/MinimumTapTest.java` |
| U029 / F18 | A / P1 | 方向触感去抖 | 方向激活、跨扇区及 80ms 边界 | 0/79/80/81ms 输入方向变化 | 只在合法激活/扇区改变触发；cooldown 边界正确 | `app/src/test/java/com/flynes/emu/DirectionFeedbackGateTest.java` |
| U030 / F18 | A / P1 | 触感等级与 A/B 区分 | OFF/LIGHT/STANDARD/STRONG 与 distinct 开关 | 请求 A/B/方向/预览 pattern | OFF 无振动；其余 pattern 与设定一致；关闭 distinct 后一致 | `app/src/test/java/com/flynes/emu/input/HapticPatternTest.java` |
| U031 / F15 | A / P0 | Android 原生字段映射 | 完整 AppSettings 与 native snapshot | 双向转换含自定义四轴、locale、autosave | 值无丢失或错 enum；未知值按显式策略处理 | `app/src/test/java/com/flynes/emu/settings/FlySettingsMapperTest.java` |
| U032 / F15 | A / P0 | 旧显示配置迁移幂等 | 每个支持的旧配置版本 | 迁移两次；模拟无效旧值 | 结果一致；旧字段不覆盖已合法新值 | `app/src/test/java/com/flynes/emu/settings/VideoSettingsMigrationTest.java` |
| U033 / F16 | A / P0 | 预设与四轴解析 | 省电/均衡/自定义/Extreme 和能力组合 | resolve 请求配置 | 请求、实际、回退原因分离；未认证 Extreme 不激活 | `app/src/test/java/com/flynes/emu/video/quality/DisplayQualityResolverBaselineTest.java` |
| U034 / F16 | A / P0 | 认证证据有效期与失配 | 正确证据及机型/驱动/版本/时间失配 | 执行资格判断与时钟回退 | 只有匹配有效证据可通过；失配/过期立即失效 | `app/src/test/java/com/flynes/emu/video/quality/EvidenceValidityPolicyTest.java` |
| U035 / F16 | A / P0 | GL 缺能力回退 | 无 highp、低纹理上限、缺 float target 的能力 | 请求 MMPX/ScaleFX | 精确理由回退；不得产生伪造像素或支持状态 | `app/src/test/java/com/flynes/emu/video/quality/DisplayQualityResolverQualificationTest.java` |
| U036 / F16 | A / P0 | 显示模式与 lease 撤销 | 60/90/120 与不同分辨率候选、前后台状态 | 选择模式；暂停/过期/失去 surface | 不选不兼容分辨率；lease 释放后无旧刷新请求 | `app/src/test/java/com/flynes/emu/video/DisplayRequestLifecycleTest.java` |
| U037 / F17 | A / P0 | 音频延迟排队与 drain | priming/motion/hold/drain 状态及有界 sample 队列 | 欠载、模式切换、暂停恢复 | 水位有上限；无重复音频；转换遵守状态机 | `app/src/test/java/com/flynes/emu/video/audio/TemporalAudioDelayTest.java` |
| U038 / F17 | A / P0 | A/V 时钟有效性 | 单调时间、偏移变化和异常 marker | 关联音频播放位置与帧 presentation | 不混用时钟域；无效样本显式拒绝；统计单位一致 | `app/src/test/java/com/flynes/emu/video/audio/AvSyncMonitorTest.java` |
| U039 / F27 | A / P0 | 温控与功耗降级状态 | normal/热/严重热、保护开关 | 升温→恢复→再升温 | 按策略降级或暂停；防抖；不无证据升到硬件模式 | `app/src/test/java/com/flynes/emu/video/power/AdaptiveQualityControllerTest.java` |
| U040 / F09 | A / P0 | 精确变体启动与异常归因 | 同 ROM 多来源、非首 ZIP entry、失效 locator | 请求指定 variant 并注入 provider 打开失败 | 只打开指定内容；明确错误不杀启动线程 | `app/src/test/java/com/flynes/emu/launch/ExactRomLoaderTest.java` |
| U041 / F03 | A / P0 | Android 目录投影健壮性 | 空白名、合法中英名、来源信息、重复内容 | 从 native rows 投影 | 单个坏显示名不毒化全目录；身份/来源正确 | `app/src/test/java/com/flynes/emu/catalog/android/NativeCatalogProjectorTest.java` |
| U042 / F08 | A / P0 | 删除与授权释放幂等 | 待释放 tombstone、失败重试、两个来源 | 重复执行 remove/retry | 仅删除目标来源；失败可重试；不提前释放他人授权 | `app/src/test/java/com/flynes/emu/catalog/source/SourceRegistryTest.java` |
| U043 / F19 | H / P1 | 布局历史上限与撤销 | 推荐布局、20次以上编辑 | push 21 个快照并连续 undo | 历史上限20；逐步恢复；空栈无破坏 | `harmony/entry/src/ohosTest/ets/test/ControlLayoutWarnings.test.ets` |
| U044 / F19 | H / P1 | 布局四类风险告警 | 过小、重叠、手势区、画面中央和推荐布局 | 逐一计算 warning | 每种命中对应 key；推荐布局无误报 | `harmony/entry/src/ohosTest/ets/test/ControlLayoutWarnings.test.ets` |
| U045 / F14 | H / P0 | 存档 key 隔离与安全路径 | 大小写、斜杠、Unicode、长 canonical id | 计算 key 并比较已知碰撞候选 | 确定、无路径穿越；不同 canonical id 不串档；碰撞一经复现需版本化修复 | `harmony/entry/src/ohosTest/ets/test/CheckpointStore.test.ets` |
| U046 / F03 | H / P0 | Harmony 目录合并保留排名 | 同 id 多来源、不同 id 同名 | uniqueGameCenterRows 与 title projection | 只按 id 去重；保留正确来源和最大排名；locale 优先级正确 | `harmony/entry/src/ohosTest/ets/test/CatalogProductService.test.ets` |
| U047 / F16 | H / P0 | Harmony viewport 三种比例 | 4:3/像素方形/整数倍与多种安全区 | 调用 viewport 纯函数 | 像素矩形正确、整数倍无小数 scale、不二次 letterbox | `harmony/entry/src/ohosTest/ets/test/SettingsHelpers.test.ets` |
| U048 / F25 | H / P0 | Harmony nearby 阶段与禁用态 | 每个首失败阶段、无 session、各 action-demanding 状态 | 生成 pipeline/lobby/banner/friend actions | 仅首失败说明；不可用控件有原因；无会话无游戏 banner | `harmony/entry/src/ohosTest/ets/test/NearbyService.test.ets` |
| U049 / F22 | H / P1 | 许可证查找与失败重试模型 | 已登记许可证、缺失资源 | 获取列表并读取 body | 列表与当前资源一致；缺失抛明确错误可重试 | `harmony/entry/src/ohosTest/ets/test/LicenseModel.test.ets` |
| U050 / F23 | Hn / P0 | Harmony render mailbox 陈旧帧 | 不同 generation/surface 的帧队列 | 替换 surface 并消费旧帧 | 旧帧丢弃；无悬空资源；队列容量受限 | `harmony/tests/render_mailbox_test.cpp` |
| U051 / F07 | Hn / P0 | Harmony 扫描作业取消与串行 | 两个扫描任务及取消请求 | 启动A→取消→启动B→注入A迟到结果 | 取消不发布完整成功；B不被A覆盖；UI线程不执行重扫描 | `harmony/tests/scan_job_executor_test.cpp` |
| U052 / F17 | Hn / P0 | Harmony PCM 队列有界 | 不同速率 producer/consumer 与欠载 | 持续 push/pop、暂停恢复、并发 | 无越界/乱序；上限及静音策略正确；Debug/Release断言均有效 | `harmony/tests/native_play_support_test.cpp` |
| U053 / F16 | Hn / P0 | Harmony 显示策略失败关闭 | 设备证据缺失、EGL失败、温控变化 | 请求不同 preset/refresh/temporal | 未认证不开放；理由明确；故障回退安全 | `harmony/tests/display_policy_test.cpp` |
| U054 / F19 | I / P0 | iOS 布局草稿独立与安全几何 | 真实 ControlLayoutDraft 推荐编码 | 复制修改、边界 scale、NaN 解码 | 草稿不改变已保存值；几何被约束；坏数据回退 | `ios/tests/ControlLayoutDraftTests.swift` |
| U055 / F12 | I / P0 | iOS frame input latch | 帧间短按和两个输入来源 | press/release/cancel/采样交错 | 至少一次采样合法短按；cancel清理；无跨帧粘键 | `ios/tests/frame_input_latch_test.cpp` |
| U056 / F12 | I / P0 | iOS direction session | 固定/跟随轨迹和多指竞争 | 拖出半径反向、抬起、重排 | 方向连续；持有者稳定；终止归零 | `ios/tests/direction_session_test.cpp` |
| U057 / F17 | I / P0 | iOS audio focus policy | PAUSE/DUCK/IGNORE、系统中断开始/结束 | 调用纯策略并切换输入状态 | 暂停/本机duck/忽略按策略；不会降低其他应用音量 | `ios/tests/test_playback_audio_focus.cpp` |
| U058 / F17 | I / P0 | iOS audio queue 上限 | 不同速率 PCM 与重启 | 排队、欠载、reset、切换 epoch | 队列有界、sample连续、旧epoch不播放 | `ios/tests/test_playback_audio_queue.cpp` |
| U059 / F11 | I / P0 | iOS 播放时钟与追帧预算 | NTSC/PAL、暂停间隔、大时间跳跃 | 逐次给 pacer 时间 | 帧预算正确且有上限；恢复不一次补跑后台所有帧 | `ios/tests/test_playback_clock.cpp` |
| U060 / F20 | I / P1 | iOS 本地化实时读取 | en/zh-Hans/zh-CN/en-US 与系统回退 | 运行现有 main 并切换保存语言 | 使用当前语言而非首次缓存；测试后恢复原设置 | `ios/tests/AppLocalizationTests.mm` |
| U061 / F10 | I / P1 | iOS 封面质量与采样 | 黑白坏帧、合格帧、四个采样偏移 | 执行 CoverCapturePolicy/GameCoverPolicy | 仅规定偏移采样；只用更优真实游戏帧；无 overlay | `ios/tests/game_cover_policy_test.cpp` |
| U062 / F09 | I / P0 | iOS ROM package 解包 | 原始 ROM、ZIP 多entry、非法包 | 按 locator 选择并解码 | 只返回所选内容；明确拒绝损坏及超限 | `ios/tests/rom_package_test.cpp` |
| U063 / F16 | I / P0 | iOS RGB565 上传边界 | 奇数宽、stride、空/短 buffer、真实合法帧 | 转换/上传源数据 | 无越界；合法颜色正确；错误输入拒绝 | `ios/tests/rgb565_upload_test.cpp` |
| U064 / F03 | I / P1 | iOS 标题展示优先级 | 可信索引、外部名、外层ZIP名、双语 | 逐个调用 presentation helper | 可信标题优先；不把外部名当翻译；别名保留可搜索 | `ios/tests/catalog_presentation_test.mm` |
| U065 / F26 | T / P0 | 标题生成与 Windows LF 检出 | 临时 Git 仓库 core.autocrlf=true | 提交真实属性/生成表→新clone→check | 检出为LF；字节等于生成器；改内容必须被check拒绝 | `tools/game_titles/test_game_titles.py` |
| U066 / F26 | T / P0 | 版本分配与提交同步 | 隔离Git仓库，major=1，已有minor | 并发分配worktree、提交一次、尝试越界 | minor唯一；patch只加一次；三端元数据一致；不授权major变更 | `tools/versioning/tests/Versioning.Tests.ps1` |
| U067 / F26 | T / P0 | schema/golden 再生成一致 | 当前schema及三端golden副本 | 执行现有check并单字节变异 | 未变内容一致；变异被拒绝；大小端/hash/版本不漂移 | `shared/tests/test_session_schema_registry.cpp` |
| U068 / F26 | T / P0 | 报告汇总不得零测试通过 | 0项、缺报告、失败、skip-only、错SHA报告 | 调用新增统一结果校验器 | 全部异常判失败/阻塞；不能由exit0或Failure0文字判PASS | `tools/quality/tests/test_test_result_gate.py` |
| U069 / F26 | T / P0 | 覆盖率分母完整 | 一个未加载生产文件、重复源路径、第三方文件 | 导入coverage和生产源码清单 | 未加载文件计0；canonical路径去重；第三方单列；UI不整目录排除 | `tools/quality/tests/test_coverage_gate.py` |
| U070 / F26 | T / P0 | 增量覆盖阈值 | 有/无新增可执行行、改名文件、低分支覆盖 | 比较base/head和UT专用报告 | 正确计算diff；无行标N/A；低于阈值失败；集成数据不能填充UT | `tools/quality/tests/test_coverage_gate.py` |
| U071 / F26 | T / P0 | 测试清单接入完整性 | 测试文件有声明但未注册；case缺平台实现 | 对比清单、discovery、报告 | 未接入、漏平台、重复ID、漏执行明确失败 | `tools/quality/tests/test_test_matrix.py` |
| U072 / F26 | T / P0 | 设备种类与包绑定 | 模拟器伪装Device、serial不符、包旧版本 | 验证preflight metadata | 拒绝目标混淆和错包；绝不将模拟器报告标真机 | `tools/quality/tests/test_device_gate.py` |
| U073 / F28 | S / P0 | core C ABI尺寸版本与空指针 | 合法cfg和短struct/未知version/null组合 | 逐个调用公开读取/配置接口 | 精确错误码；输出缓冲不越界；ABI头C/C++均独立编译 | `core/tests/test_core_abi_edges.cpp` |
| U074 / F28 | S / P0 | core句柄与load/power/reset/unload生命周期 | 合法NROM fixture | create→load→power→step→soft/hard reset→unload→destroy；错误顺序另测 | 生命周期结果明确；重复轮无泄漏；未就绪不会访问悬空ROM | `core/tests/test_core_lifecycle.cpp` |
| U075 / F28 | S / P0 | core视频格式与copy容量 | RGB565/RGB888/RGBA8888及各合法filter | copy查询尺寸→短buffer→足够buffer→if_new同序号 | 所需字节/stride正确；短buffer拒绝且不越界；无新帧不复制旧帧冒充新数据 | `core/tests/test_core_video.cpp` |
| U076 / F28 | S / P0 | core音频格式与采样累计 | 支持采样率及mono/stereo、NTSC/PAL | 设置格式→连续step固定源时间→切格式 | sample总量在整数舍入预算内；channel布局正确；非法格式拒绝 | `core/tests/test_core_audio.cpp` |
| U077 / F28 | S / P0 | core版本化输入与clear | 四端口和版本号 | 设置版本化输入→step→读取last_sample→clear | 采样版本与实际按键对应；越界port不污染；clear清四端口 | `core/tests/test_core_input.cpp` |
| U078 / F28 | S / P0 | core存档容量ROM匹配与损坏 | 两个不同ROM及合法savestate | size-query→短buffer→保存→异ROM/截断/篡改restore | 正确needed/written；错ROM和坏档拒绝；原状态不被部分覆盖 | `core/tests/test_core_state.cpp` |
| U079 / F28 | S / P0 | core回调重入与借用期限 | log/file/event/question真实回调 | 回调内尝试重入；返回后再次正常调用 | 受限重入返回NES_ERR_REENTRANT；数据/回调生命周期明确，无死锁 | `core/tests/test_core_callbacks.cpp` |
| U080 / F28 | S / P1 | core cheat API实际能力 | GameGenie/ProActionRocky已知合法与非法码 | encode/decode→add→count→remove→clear | 合法round-trip且count正确；非法码拒绝；不新增产品作弊UI | `core/tests/test_core_cheats.cpp` |
| U081 / F28 | S / P0 | core保留接口拒绝而不伪成功 | 当前未实现接口清单 | 调用patched ROM、battery_flush、mem_read/write、FDS系列 | 返回当前NOT_IMPLEMENTED；不写输出/状态；不声称产品支持 | `core/tests/test_core_unsupported.cpp` |
| U082 / F28 | S / P0 | core能力字段与产品可用性边界 | caps及当前公开ABI实现状态 | 查询caps并与产品资格映射对照 | 区分底层核心能力与可调用产品能力；未实现FDS/patch不会驱动可用UI | `core/tests/test_core_capabilities.cpp` |
| U083 / F11 | S / P0 | 区域与mapper固定轨迹 | 合法自制NROM/MMC1/UxROM、NTSC/PAL fixture | 每个运行固定输入轨迹10000帧并重复 | 同配置state/video/audio摘要确定；区域采样率正确；不以单款ROM证明所有mapper | `core/tests/test_core_conformance.cpp` |
| U084 / F14 | S / P0 | checkpoint包含电池RAM进度 | 合法自制有battery RAM的诊断ROM | 游戏内改变RAM→保存checkpoint→重建runtime→restore | 游戏内电池RAM状态正确恢复；不把独立battery_flush未实现当成通过 | `shared/tests/test_runtime.cpp` |

## 4. 组件与系统集成完整列表（21条）

这里允许直接调用真实service/bridge，验证跨层正确性；不称为用户E2E。磁盘原子性、音频队列、native/GPU和授权桥接是主要目标。

| ID / 功能 | 平台 / 优先级 | 用例 | 前置 | 执行 | 必须断言 | 实现/复用文件 |
|---|---|---|---|---|---|---|
| C001 / F06 | A / P0 | Android SAF provider 路径集成 | 隔离DocumentsProvider与真实resolver | 树URI→document URI→重启目录→打开非首ZIP entry | 读到目标ROM字节；非法拼接URI为可处理失败 | `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java` |
| C002 / F08 | A / P0 | Android持久授权丢失 | 注册来源后撤销测试授权 | 重启→scan/open→重新授权 | 首次准确报授权丢失；授权恢复后同来源可用 | `app/src/androidTest/java/com/flynes/emu/catalog/android/PersistedReadPermissionGatewayTest.java` |
| C003 / F07 | A / P0 | Android扫描崩溃与最后完整目录 | 已有完整目录，第二次扫描注入I/O错误 | 取消/进程终止→重启目录 | 保留最后合法快照；部分结果标stale而非成功 | `app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogRuntimeTest.java` |
| C004 / F14 | A / P0 | Android存档原子写失败 | 两个游戏各有checkpoint | 注入短写/rename失败并重读 | 旧有效存档保留；两游戏互不污染；错误可呈现 | `app/src/androidTest/java/com/flynes/emu/save/SaveRepositoryTest.java` |
| C005 / F24 | A / P0 | Android旧存档迁移重复执行 | 合法旧存档及迁移标志缺失/已存在 | 启动迁移两次并重启 | 只复制一次；保留原文件；不覆盖新进度 | `app/src/androidTest/java/com/flynes/emu/save/LegacySaveMigratorTest.java` |
| C006 / F10 | A / P1 | Android封面真实帧到缓存 | 运行合法fixture并有触控overlay | 等待规定采样→退库→加载封面 | 真实game-only图像、原子缓存、hash名称且可重载 | `app/src/androidTest/java/com/flynes/emu/cover/CoverCaptureIntegrationTest.java` |
| C007 / F16 | A / P0 | Android GPU 独立像素金标 | 支持GL的模拟器及冻结oracle | 执行Nearest2x/MMPX2x/ScaleFX3x | 按现有金标容差逐像素相符；缺能力明确BLOCKED | `app/src/androidTest/java/com/flynes/emu/SpatialFilterGoldenTest.java` |
| C008 / F23 | A / P0 | Android presenter surface 重建 | 真实native presenter已产帧 | 丢失surface→暂停→新surface恢复 | 只有一个owner；旧epoch拒绝；新frame恢复 | `app/src/androidTest/java/com/flynes/emu/NativePresenterIntegrationTest.java` |
| C009 / F14 | H / P0 | Harmony真实CheckpointStore读写 | 测试专属filesDir与两个checkpoint | write/read→短写/截断→quarantine→重读 | 真实文件round-trip；旧文件安全；坏档隔离；无跨ROM污染 | `harmony/entry/src/ohosTest/ets/test/CheckpointStoreIO.test.ets` |
| C010 / F08 | H / P0 | Harmony URI授权与NAPI FD生命周期 | 真实测试来源映射与borrowed FD | 注册→关闭调用方FD→重开→撤销授权 | 所有权明确；不泄漏FD；失效来源可诊断 | `harmony/entry/src/ohosTest/ets/test/SourcePermissionIO.test.ets` |
| C011 / F07 | H / P0 | Harmony NAPI扫描异步边界 | 多文件fixture与有界队列 | 从服务发起扫描/取消并注入迟到结果 | UI事件仍响应；单个作业结果不污染新作业 | `harmony/entry/src/ohosTest/ets/test/ScanLifecycle.test.ets` |
| C012 / F15 | H / P0 | Harmony设置/布局持久化桥接 | 真实native app独立根目录 | 写设置和布局→销毁重建app→读 | 值原子持久；不只比较页面变量 | `harmony/entry/src/ohosTest/ets/test/SettingsPersistenceIO.test.ets` |
| C013 / F09 | I / P0 | iOS bookmark失效与资源释放 | 测试文件/目录security-scoped bookmark | 导入→重启解析→移动/失效→取消访问 | 有效来源可重开；失效不猜路径；access成对释放 | `ios/tests/CatalogSourceImportTests.mm` |
| C014 / F06 | I / P0 | iOS重复导入与来源独立 | 同文件/文件夹重复、异来源同内容 | 重复导入→重启→只删一个来源 | 同来源幂等；不同来源保留；界面不集合修改崩溃 | `ios/tests/CatalogSourceImportTests.mm` |
| C015 / F14 | I / P0 | iOS checkpoint原子持久化 | 真实app沙箱内已有合法checkpoint | 注入write失败/损坏→重开运行表面 | 旧进度保留或错误明确；不静默恢复另一游戏 | `ios/tests/AutosavePersistenceTests.mm` |
| C016 / F17 | I / P0 | iOS真实音频输出 | 真实runtime与AVAudioEngine | 播放PCM→中断→route重建→恢复 | 运行中的sink消费有效PCM；队列有界；无重复旧音频 | `ios/tests/PlaybackAudioPlayerTests.mm` |
| C017 / F16 | I / P0 | iOS Metal金标与stride | 受支持Metal环境及冻结golden | 运行斜线/棋盘/奇数细线MMPX与ScaleFX | 逐像素满足现有容差；无CPU假结果代替GPU | `ios/tests/MetalPixelParityTests.mm` |
| C018 / F14 | S / P0 | shared目录/设置落盘崩溃原子性 | 独立临时根与有效快照 | 在写临时/flush/replace边界失败或终止后重开 | 只见旧或新完整版本；无半写快照；来源关系完整 | `shared/tests/test_catalog_persist.cpp` |
| C019 / F25 | S / P0 | 双进程session codec循环与攻击 | 真实两进程loopback与当前golden | 发送合法帧及乱序/重复/恶意长度/错channel | 编码接收行为一致；认证未实现仍拒绝入局；不等同无线认证 | `shared/tests/test_session_codec_loopback.cpp` |
| C020 / F27 | I / P1 | runtime加速耐久与泄漏 | 合法ROM、固定36000帧预算 | 循环step/pull/copy及重复建毁runtime | 帧/sample连续、内存与队列有界；报告真实耗时而非宣称30分钟实机 | `ios/tests/PlaybackSoakTests.mm` |
| C021 / F14 | A / P1 | Android独立battery仓库原子性 | 隔离SaveRepository与两个ROM identity | writeBattery/readBattery→写失败→重读 | 两ROM隔离；旧文件保留；仅证明仓库API，不声称产品已接入独立SRAM同步 | `app/src/androidTest/java/com/flynes/emu/save/SaveRepositoryTest.java` |

## 5. 模拟器端到端完整列表（97条，288个平台实例）

E014只适用于当前提供单文件入口的Harmony/iOS；Android目录导入由E013覆盖。E095只适用于当前提供死区滑条的iOS；Android/Harmony的底层死区逻辑由输入UT覆盖，不新增产品入口。其他条目三端均有对应常用功能或安全阻塞态要求。GPU/物理音频限制不豁免页面、策略及存储E2E，物理效果另见真机表。

实现组与三个精确文件在§6给出。每个框架方法以同一逻辑ID命名：Android `testE001`、Harmony `it('H-E001', ...)`、iOS `testE001`。这些是新增实现规范，不是声称当前已存在的方法。

| 执行ID / 功能 | 优先级 | 用例 | 前置 | 用户/系统操作顺序 | 必须断言 | 实现组 |
|---|---|---|---|---|---|---|
| A-E001、H-E001、I-E001 / F01 | P0 | 全新安装冷启动 | CLEAN | 从系统桌面启动应用 | 进入游戏中心；内置目录可见；不自动进入对局或调试页 | LibraryJourney |
| A-E002、H-E002、I-E002 / F01 | P0 | 内置游戏完整启动 | CLEAN | 选内置卡→启动→等待真实画面→按START | 正确游戏产帧且输入生效；不是仅有游戏容器 | PlaybackJourney |
| A-E003、H-E003、I-E003 / F01 | P0 | 来源与设置返回路径 | CLEAN | 中心→来源→返回→设置→返回 | 每次回同一个游戏中心；无重复页面栈或丢失选中项 | LibraryJourney |
| A-E004、H-E004、I-E004 / F02 | P0 | 四分类切换 | LIB2 | 依次点全部/内置/收藏/最近 | 每类条目与fixture操作记录一致；空类有正确空态 | LibraryJourney |
| A-E005、H-E005、I-E005 / F02 | P0 | 卡片选中与详情启动联动 | LIB2 | 选择游戏A→选择B→启动 | 详情标题/收藏/启动状态都属于B；实际运行B而不是A | LibraryJourney |
| A-E006、H-E006、I-E006 / F03 | P0 | 中英别名搜索 | LIB2 | 依次搜索中文、英文大小写、别名、原文件名 | 命中同一已知内容；结果数/身份正确；无乱码 | LibraryJourney |
| A-E007、H-E007、I-E007 / F03 | P1 | 搜索无结果与清空 | LIB2 | 输入不存在词→清空→关闭搜索 | 显示空态；清空恢复当前分类；关闭不遗留键盘遮挡 | LibraryJourney |
| A-E008、H-E008、I-E008 / F02 | P0 | 搜索与选中冷重启恢复 | LIB2 | 选分类/搜索/卡片→终止进程→从桌面启动 | 恢复当前规定的导航状态；启动按钮仍对应有效条目 | LibraryJourney |
| A-E009、H-E009、I-E009 / F04 | P0 | 收藏增加取消与持久化 | LIB2 | 收藏A→切收藏→重启→取消收藏A | 收藏落盘；取消后退出列表；其他游戏收藏不变 | LibraryJourney |
| A-E010、H-E010、I-E010 / F04 | P0 | 最近游玩顺序 | LIB2 | 运行A退出→运行B退出→最近→重启 | B在A之前；真实游玩触发最近；仅浏览卡片不算游玩 | LibraryJourney |
| A-E011、H-E011、I-E011 / F03 | P1 | 热门排序和精确内容去重 | RANK | 通过UI导入已定义热门/同名异内容/同内容副本→全部 | 顺序符合固定fixture manifest；同内容一张卡，同名异内容两张 | LibraryJourney |
| A-E012、H-E012、I-E012 / F05 | P1 | 完整大目录横向浏览 | LARGE100 | 导入100条→滚到最后→搜索尾部→选择启动→返回 | 全100条可达而非首屏计数；无分页/竖向替代；尾部正确启动 | LibraryJourney |
| A-E013、H-E013、I-E013 / F06 | P0 | 系统目录选择器导入 | PICKER | 中心→添加来源→真实系统选择器选fixture目录→确认 | 取得真实权限；扫描完成；卡片出现；重启后可启动 | SourceJourney |
| H-E014、I-E014 / F06 | P0 | 系统单文件选择器导入 | PICKER | 来源→添加游戏文件→系统选择fixture.nes | 导入成为可启动来源；重启可读；Android当前无此入口不编造 | SourceJourney |
| A-E015、H-E015、I-E015 / F06 | P0 | 取消系统选择器 | LIB2 | 添加来源→打开系统选择器→取消 | 来源数/游戏数/收藏/原权限不变；无空来源或永久busy | SourceJourney |
| A-E016、H-E016、I-E016 / F09 | P0 | ZIP非首条目启动 | ZIP2 | 系统导入含两款合法游戏的ZIP→选第二款启动→重启再开 | 两次均运行第二款；标题和完整内容摘要匹配 | SourceJourney |
| A-E017、H-E017、I-E017 / F06 | P0 | 不支持与损坏文件可见错误 | BADFILES | 系统导入截断ROM/坏CRC ZIP/非ROM目录 | 错误或跳过统计正确；不生成可启动坏卡片；原目录保留 | SourceJourney |
| A-E018、H-E018、I-E018 / F06 | P0 | 重复添加同一来源 | PICKER | 同一文件夹添加两次→重启→来源列表 | 同来源幂等；只需移除一次；无重复卡片和权限泄漏 | SourceJourney |
| A-E019、H-E019、I-E019 / F08 | P0 | 不同来源同内容只删一个 | DUP2 | 通过UI导入两个不同来源的同内容→移除来源A | B仍存在且可启动；收藏与内容身份不丢；物理原文件不删除 | SourceJourney |
| A-E020、H-E020、I-E020 / F07 | P0 | 重扫新增删除与取消旧结果 | LIB2 | 在测试专属来源增删fixture→UI重扫→等待结束 | 新增出现、删除按目录规则更新；完整扫描状态真实 | SourceJourney |
| A-E021、H-E021、I-E021 / F07 | P0 | 扫描取消 | LARGESCAN | UI开始耗时扫描→点取消→再启动新扫描 | 第一次不报完整成功；busy释放；旧callback不覆盖新结果 | SourceJourney |
| A-E022、H-E022、I-E022 / F07 | P0 | 部分扫描失败与重试 | SCANFAIL | 扫描时使一个测试文件读取失败→恢复→UI重试 | 部分失败可见且旧数据安全；重试成功解除stale | SourceJourney |
| A-E023、H-E023、I-E023 / F08 | P0 | 移除来源与授权边界 | LIB2 | 移除fixture来源→确认→重启 | 只移除目标索引/授权；不删用户原ROM；内置源仍可运行 | SourceJourney |
| A-E024、H-E024、I-E024 / F08 | P0 | 授权丢失后重新授权 | LIB2 | 外部撤销测试来源授权→重启→尝试启动→UI重授权 | 失败留中心并有原因；重授权恢复同来源；不创建重复索引 | SourceJourney |
| A-E025、H-E025、I-E025 / F09 | P0 | 已索引文件消失启动失败 | LIB2 | 移走测试专属ROM→点启动 | 停留/返回游戏中心并说明无法打开；不崩溃、不偷偷启动内置ROM | SourceJourney |
| A-E026、H-E026、I-E026 / F09 | P0 | 修复来源后可再次启动 | E025完成 | 恢复fixture→重扫或重授权→启动 | 正确游戏运行；错误状态消失；不需重装应用 | SourceJourney |
| A-E027、H-E027、I-E027 / F03 | P1 | 未知标题与空白名称回退 | UNKNOWN | 导入未知hash且中文/空白外名fixture→中英切换 | 有可读回退标题；不伪造翻译；其他卡片正常 | LibraryJourney |
| A-E028、H-E028、I-E028 / F10 | P1 | 封面采样并冷重启重载 | PLAYROM | 真实游戏推进采样窗口→退出→重启 | 卡片有真实游戏封面；不含触控overlay；可从持久缓存重载 | CoverJourney |
| A-E029、H-E029、I-E029 / F10 | P1 | 黑白坏帧不替换好封面 | COVERSEQ | 先产生好画面→诊断ROM进入黑/白阶段→退出重开 | 已有高质量封面未被坏帧替换；写失败不破坏旧缓存 | CoverJourney |
| A-E030、H-E030、I-E030 / F11 | P0 | 无触摸仍持续模拟播放 | DIAG | 启动诊断游戏后至少10秒不触屏 | 帧序号及可见动画推进、音频产生；不依赖输入回调step | PlaybackJourney |
| A-E031、H-E031、I-E031 / F12 | P0 | NES方向与动作键实际生效 | DIAG | 通过屏幕分别按方向四向、A/B、SELECT/START | 诊断画面显示准确键位；抬起后释放；不只验证view回调 | InputJourney |
| A-E032、H-E032、I-E032 / F12 | P0 | 固定摇杆越界反向 | DIAG | 设置固定摇杆→开游戏→按住拖出五倍半径再反向 | 实际游戏方向连续、反向生效；终止归零 | InputJourney |
| A-E033、H-E033、I-E033 / F12 | P0 | 跟随摇杆与十字键切换 | DIAG | 设置跟随摇杆测试边缘起手；再设置十字键测试四向斜向 | 起手/死区与既有契约一致；切换后无旧方向残留 | InputJourney |
| A-E034、H-E034、I-E034 / F12 | P0 | 方向加A/B系统多指输入 | DIAG+MULTITOUCH | 系统级注入方向+A+B→先抬A→抬方向→抬B | 画面按真实组合响应；pointer重排无丢键；不得直接调app输入方法替代 | InputJourney |
| A-E035、H-E035、I-E035 / F12 | P0 | 系统短按与取消 | DIAG+SHORTTAP | 注入小于一帧点击；长按后系统取消 | 短按至少一次采样；取消清零；若平台驱动不支持则BLOCKED_TOOLING | InputJourney |
| A-E036、H-E036、I-E036 / F13 | P0 | START和独立暂停分离 | DIAG | 点击START→点击右上暂停 | START只传NES输入；只有暂停钮打开三动作侧栏 | PauseJourney |
| A-E037、H-E037、I-E037 / F13 | P0 | 暂停停止帧与音频 | DIAG | 游戏推进→暂停→观察2秒 | runtime不继续step；本机音频暂停；只显示约定三项 | PauseJourney |
| A-E038、H-E038、I-E038 / F13 | P0 | 继续按钮与遮罩恢复 | DIAG | 暂停→继续；再暂停→点遮罩 | 两条路径都恢复画面/音频；无重复runtime或卡住 | PauseJourney |
| A-E039、H-E039、I-E039 / F13 | P0 | 暂停进入设置后返回仍暂停 | DIAG | 暂停→设置→改布局/音频→返回 | 返回暂停态不偷偷推进；新布局重绘；手动继续后生效 | PauseJourney |
| A-E040、H-E040、I-E040 / F13 | P0 | 退出游戏中心释放运行资源 | DIAG | 暂停→游戏中心→等待→再开另一游戏 | 后台无残留音频/step；新游戏正确加载且输入为零 | PauseJourney |
| A-E041、H-E041、I-E041 / F14 | P0 | 自动存档开启后恢复进度 | DIAG | 开启autosave→运行到可识别进度→暂停退出→终止重启→继续 | 恢复对应已提交checkpoint；进度可由画面及状态摘要核对 | AutosaveJourney |
| A-E042、H-E042、I-E042 / F14 | P0 | 两游戏存档隔离 | DIAG2 | A推进保存→B推进保存→依次重启A/B | 各自恢复各自进度；不同来源同内容按既有身份契约处理 | AutosaveJourney |
| A-E043、H-E043、I-E043 / F14 | P0 | 自动存档关闭 | DIAG且有旧档 | 关闭autosave→推进新进度→暂停退出重启 | 不写新checkpoint；关闭时启动行为符合当前设置；旧有效档不被毁掉 | AutosaveJourney |
| A-E044、H-E044、I-E044 / F14 | P0 | 坏存档提示和安全继续 | BADSAVE | 在测试沙箱放损坏/错ROM档→UI启动 | 不崩溃、不加载错误进度；出现错误提示并能继续/返回 | AutosaveJourney |
| A-E045、H-E045、I-E045 / F14 | P0 | 保存失败不阻断退出 | IOFAULT | 在真实写边界注入磁盘写失败→暂停→继续或返回 | 用户看到未保存；旧档仍好；两个动作均可执行 | AutosaveJourney |
| A-E046、H-E046、I-E046 / F15 | P0 | 设置五分区全可达 | CLEAN | 从中心打开设置→依次点击显示/操作/音频/游戏语言/关于 | 五区顺序正确且内容匹配；无多余第六区 | SettingsJourney |
| A-E047、H-E047、I-E047 / F15 | P0 | 控制设置跨冷重启与整体重置 | CLEAN | 改方向/触感/AB区分/布局→重启→控制重置 | 先完整持久；重置恢复当前Android对齐契约全部控制字段 | SettingsJourney |
| A-E048、H-E048、I-E048 / F16 | P0 | 省电均衡自定义预设 | PLAYROM | 逐次UI选择省电/均衡/自定义→回游戏→重启 | 请求值持久；实际渲染与能力相符；不把请求冒充实际 | DisplayJourney |
| A-E049、H-E049、I-E049 / F16 | P0 | 自定义四轴与平台锁定态 | PLAYROM | 展开自定义→操作可用刷新/时间/空间/后效轴 | 可用值持久并真实生效；iOS固定60/native明确只读；不伪造高刷 | DisplayJourney |
| A-E050、H-E050、I-E050 / F16 | P0 | 三种宽高比实际画面 | DIAG | 依次选择4:3/方形像素/整数倍→回游戏 | 测实际viewport边界和像素比例；无拉伸或二次黑边错误 | DisplayJourney |
| A-E051、H-E051、I-E051 / F16 | P0 | Nearest实际渲染 | DIAG | 自定义选Nearest→回游戏→重启 | 真实画面最近邻呈现；设置与实际状态一致 | DisplayJourney |
| A-E052、H-E052、I-E052 / F16 | P0 | Sharp Bilinear实际渲染 | DIAG | 选择Sharp Bilinear→回游戏→重启 | 画面正常且选中路径真实提交；无黑屏 | DisplayJourney |
| A-E053、H-E053、I-E053 / F16 | P0 | MMPX实际渲染或明确回退 | DIAG+GPU | 选择MMPX→回游戏 | 支持环境实际生效且金标集成已绿；不支持明确原因回退 | DisplayJourney |
| A-E054、H-E054、I-E054 / F16 | P0 | ScaleFX实际渲染或明确回退 | DIAG+GPU | 选择ScaleFX→回游戏 | 支持环境真实生效；缺float能力明确回退；不静默显示成功 | DisplayJourney |
| A-E055、H-E055、I-E055 / F16 | P1 | CRT开关与持久化 | DIAG | 自定义CRT开→关→开→重启 | 画面可见变化符合参考；状态持久；基本画面不丢失 | DisplayJourney |
| A-E056、H-E056、I-E056 / F16 | P0 | Extreme与未认证高刷锁定 | 无设备认证证据 | 点击Extreme/检查刷新选项/重启 | 未认证始终不可激活且理由可读；UI和runtime一致 | DisplayJourney |
| A-E057、H-E057、I-E057 / F16 | P1 | 自适应保护偏好持久化 | PLAYROM | 关闭/开启保护→重启→回游戏 | 偏好正确；关闭不绕过硬件安全门禁；物理热效果另测 | DisplayJourney |
| A-E058、H-E058、I-E058 / F17 | P0 | 声音开关作用于真实播放 | DIAG有非零PCM | 游戏→设置声音关→继续→开→继续 | 本机sink静音/恢复；游戏继续；不只读开关值 | AudioJourney |
| A-E059、H-E059、I-E059 / F17 | P0 | 三种音频焦点策略 | DIAG+AUDIOFOCUS | 分别选择暂停/duck/忽略→测试音频应用获取焦点/结束 | PAUSE停步/音频；DUCK仅本机降低；IGNORE按系统许可继续；强制中断优先安全 | AudioJourney |
| A-E060、H-E060、I-E060 / F18 | P1 | 触感关闭与预览禁用 | CLEAN | 操作区选OFF→预览→进入游戏按键 | 预览不可用或无请求；触感请求为零；不把模拟器无马达当断言 | HapticsJourney |
| A-E061、H-E061、I-E061 / F18 | P1 | 触感三级与A后B预览 | CLEAN | 依次LIGHT/STANDARD/STRONG→点预览→重启 | 设置持久；真实haptic请求按级别；A/B预览顺序和间隔正确 | HapticsJourney |
| A-E062、H-E062、I-E062 / F18 | P1 | A/B区分开关 | DIAG | 开启区分按A/B→关闭再按→重启 | 底层请求区分按偏好变化；方向不会错误复用A/B规则 | HapticsJourney |
| A-E063、H-E063、I-E063 / F19 | P0 | 布局编辑唯一入口 | CLEAN | 设置操作→编辑布局→返回 | 编辑器可达；对局暂停仍只有原三动作，无新增布局捷径 | LayoutJourney |
| A-E064、H-E064、I-E064 / F19 | P0 | 五控件拖动保存后真实命中 | DIAG | 逐一拖动方向/A/B/SELECT/START→保存→开游戏按新位置 | 新位置输入生效、旧位置不误触；重启仍一致 | LayoutJourney |
| A-E065、H-E065、I-E065 / F19 | P1 | 控件scale上下限 | DIAG | 选A调0.5/1.8→保存→游戏；方向控件再测 | 画面与hit-map同步缩放；安全区夹取正确 | LayoutJourney |
| A-E066、H-E066、I-E066 / F19 | P1 | 透明度上下限 | DIAG | 设0.4/1.0→保存→游戏→重启 | 实际overlay透明度变化；游戏像素本身不变暗 | LayoutJourney |
| A-E067、H-E067、I-E067 / F19 | P1 | 编辑撤销 | 推荐布局 | 移动A→缩放B→改opacity→逐次撤销 | 按逆序恢复且控件/slider同步；空栈不可撤销 | LayoutJourney |
| A-E068、H-E068、I-E068 / F19 | P1 | 恢复推荐只修改草稿 | 已有自定义已保存布局 | 编辑→恢复推荐→先取消；再恢复推荐并保存 | 取消保留旧布局；保存才提交推荐布局 | LayoutJourney |
| A-E069、H-E069、I-E069 / F19 | P0 | 取消与返回不保存 | 已有布局 | 改位置/scale→取消或系统返回→重开 | 修改未落盘；继续游戏使用原布局 | LayoutJourney |
| A-E070、H-E070、I-E070 / F19 | P1 | 四类布局告警与二次确认 | 推荐布局 | 分别造过小/重叠/手势区/中心遮挡→保存→继续编辑/确认保存 | 正确告警；取消不提交；明确确认才保存风险布局 | LayoutJourney |
| A-E071、H-E071、I-E071 / F19 | P1 | 试用模式不移动布局 | 编辑器打开 | 开启试用→拖方向/按按钮→关闭试用再拖动 | 试用只输入反馈不改几何；关闭后可编辑；不写游戏进度 | LayoutJourney |
| A-E072、H-E072、I-E072 / F19 | P0 | 损坏已保存布局回退 | BADLAYOUT | 测试沙箱写非法布局→桌面启动→编辑/游戏 | 安全推荐布局；UI无NaN/越界；仍可保存新布局 | LayoutJourney |
| A-E073、H-E073、I-E073 / F20 | P0 | 中英与跟随系统语言 | CLEAN | app分别选中文/英文/跟随系统；切系统语言并重启 | 五分区、中心、弹窗使用正确语言；偏好不串码 | LocalizationJourney |
| A-E074、H-E074、I-E074 / F20 | P0 | 运行中语言切换保游戏标题 | PLAYROM | 开游戏→暂停设置换语言→返回→退出中心 | 仍同一游戏；运行标题与目录展示正确；不重载错误ROM | LocalizationJourney |
| A-E075、H-E075、I-E075 / F21 | P1 | 小横屏与安全区布局 | SMALL | 遍历中心/来源/设置/布局/暂停 | 主按钮可见可点；不被刘海/系统条遮挡；无横竖方向误布局 | AccessibilityJourney |
| A-E076、H-E076、I-E076 / F21 | P1 | 最大支持字号 | LARGETEXT | 设系统200%或平台最大支持档→遍历常用页面 | 分类/启动/保存取消仍可滚到并操作；文案不盖住按钮 | AccessibilityJourney |
| A-E077、H-E077、I-E077 / F21 | P1 | 无障碍控件语义 | CLEAN | 读取页面语义树并执行虚拟A、PAUSE及来源按钮 | label/role/enabled正确；焦点可达；虚拟控件触发真实输入 | AccessibilityJourney |
| A-E078、H-E078、I-E078 / F22 | P1 | 许可证列表阅读返回 | CLEAN | 关于→许可证→每个登记项→正文→返回 | 正文非空且对应项目；返回不丢设置；版面可滚动 | AboutJourney |
| A-E079、H-E079、I-E079 / F22 | P1 | 许可证读取失败与重试 | RESOURCEFAULT | 对一个资源读失败→进入→重试恢复 | 明确错误；重试后正确正文；非空假文案不能通过 | AboutJourney |
| A-E080、H-E080、I-E080 / F23 | P0 | 前后台暂停与恢复 | DIAG | 长按→系统Home/后台→返回→继续 | 后台按策略暂停、输入归零；无后台音频泄漏；恢复不重复step | LifecycleJourney |
| A-E081、H-E081、I-E081 / F23 | P0 | surface重建与横屏方向交换 | DIAG | 左右横屏交换/resize触发surface重建 | 正确画面恢复；旧surface不再提交；触控安全区更新 | LifecycleJourney |
| A-E082、H-E082、I-E082 / F23 | P0 | 系统中断后无粘键 | DIAG+SYSTEMINTERRUPT | 长按方向→系统弹窗/锁屏等受支持中断→恢复 | 方向/A/B全释放；会话不崩溃；只有合法恢复动作继续 | LifecycleJourney |
| A-E083、H-E083、I-E083 / F24 | P0 | 版本覆盖安装保留用户数据 | UPGRADE | 旧兼容版本通过UI创建库/收藏/布局/进度→覆盖安装新包→启动 | 数据完整、迁移幂等、进度可用；不通过卸载解决 | UpgradeJourney |
| A-E084、H-E084、I-E084 / F26 | P0 | 安装身份版本与冷启动 | CANDIDATE | 安装当前候选包→读取OS包信息→启动 | bundle/app id正确；版本来自VERSION并匹配hash报告；无调试默认页 | UpgradeJourney |
| A-E085、H-E085、I-E085 / F25 | P0 | 附近设备和好友空态 | CLEAN | 中心→附近联机→附近设备/好友切换 | 无假好友/设备；发现和扫码禁用且具体原因可见 | NearbyJourney |
| A-E086、H-E086、I-E086 / F25 | P0 | 配对页阶段阻塞 | CLEAN | 附近设备→配对入口→阅读六位码/Wi-Fi阶段 | 不能未认证确认；仅首失败解释；一次系统确认说明准确 | NearbyJourney |
| A-E087、H-E087、I-E087 / F25 | P0 | 两条好友管理入口 | CLEAN | 好友tab→管理；返回→设置关于→好友管理 | 两入口同页面；重命名/删除/拉黑/重置禁用并有原因；无第六设置分区 | NearbyJourney |
| A-E088、H-E088、I-E088 / F25 | P0 | 单机不显示联机状态 | DIAG | 正常单机运行→暂停→设置返回 | 没有会话就没有联机banner/状态行；不影响单机播放 | NearbyJourney |
| A-E089、H-E089、I-E089 / F08 | P0 | 目录损坏可恢复而不自动覆盖 | BADCATALOG | 破坏测试目录文件→启动→按恢复提示操作 | 可读错误/安全内置入口；不无声覆盖证据；用户可重新导入 | SourceJourney |
| A-E090、H-E090、I-E090 / F07 | P1 | 扫描中页面响应与重复操作 | LARGESCAN | 扫描中滚动/返回/再进；快速重复点扫描 | UI可响应；不重复并发作业；busy与实际状态一致 | SourceJourney |
| A-E091、H-E091、I-E091 / F23 | P0 | 进程死亡后输入和运行态清理 | DIAG且已保存 | 长按中终止app→桌面冷启动→继续游戏 | 不恢复按住状态；按已保存进度启动；无旧audio/surface | LifecycleJourney |
| A-E092、H-E092、I-E092 / F15 | P0 | 设置写失败可见且不半更新 | IOFAULT | 更改一个完整设置batch时失败→退出重开 | 显示保存失败；旧配置保留；UI不谎称已持久化 | SettingsJourney |
| A-E093、H-E093、I-E093 / F21 | P1 | 左右横屏和许可证方向 | CLEAN | 左右横屏依次看中心/许可证/游戏 | 允许的横屏方向正确；返回与持握位置合理；控件可达 | AccessibilityJourney |
| A-E094、H-E094、I-E094 / F01 | P0 | 断网下普通功能完整可用 | PICKER且互联网关闭 | 冷启动→导入本地→搜索→收藏→游戏→保存重开 | 单机完整流程不依赖外网；本地元数据与资源可用 | OfflineJourney |
| I-E095 / F12 | P1 | iOS摇杆死区滑条 | DIAG | 操作区设死区0.08/0.45→游戏小幅/大幅拖动→重启 | 阈值随设置真实变化并持久；十字键时滑条隐藏；Android/Harmony无现有入口不新增 | InputJourney |
| A-E096、H-E096、I-E096 / F11 | P0 | PAL游戏实际节奏与音频 | 自制PAL诊断ROM | 系统导入→启动→持续10秒观察并操作→暂停恢复 | 使用声明PAL源时序，未按60Hz硬编码；声音无速率漂移，恢复稳定 | PlaybackJourney |
| A-E097、H-E097、I-E097 / F09 | P0 | 缺BIOS或未实现格式的启动拒绝 | 合法可分发FDS结构测试fixture且无BIOS | 从系统来源导入→若被列出再尝试启动 | 导入或启动阶段明确缺BIOS/不支持原因，不崩溃、不静默运行其他ROM；不下载或索取私人BIOS | SourceJourney |

## 6. E2E实现文件完整映射

所有文件新增；同义既有测试保留并逐步归并。Android优先复用现有Espresso/runner，Harmony接入Hypium及系统UI驱动，iOS纳入现有UI test bundle。三端用例都须从真实页面路径执行，不以新增深链跳过关键操作。

| 实现组 | Android | Harmony | iOS |
|---|---|---|---|
| LibraryJourney | `app/src/androidTest/java/com/flynes/emu/e2e/LibraryJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/LibraryJourney.test.ets` | `ios/tests/LibraryJourneyUITests.mm` |
| PlaybackJourney | `app/src/androidTest/java/com/flynes/emu/e2e/PlaybackJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/PlaybackJourney.test.ets` | `ios/tests/PlaybackJourneyUITests.mm` |
| SourceJourney | `app/src/androidTest/java/com/flynes/emu/e2e/SourceJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/SourceJourney.test.ets` | `ios/tests/SourceJourneyUITests.mm` |
| CoverJourney | `app/src/androidTest/java/com/flynes/emu/e2e/CoverJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/CoverJourney.test.ets` | `ios/tests/CoverJourneyUITests.mm` |
| InputJourney | `app/src/androidTest/java/com/flynes/emu/e2e/InputJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/InputJourney.test.ets` | `ios/tests/InputJourneyUITests.mm` |
| PauseJourney | `app/src/androidTest/java/com/flynes/emu/e2e/PauseJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/PauseJourney.test.ets` | `ios/tests/PauseJourneyUITests.mm` |
| AutosaveJourney | `app/src/androidTest/java/com/flynes/emu/e2e/AutosaveJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/AutosaveJourney.test.ets` | `ios/tests/AutosaveJourneyUITests.mm` |
| SettingsJourney | `app/src/androidTest/java/com/flynes/emu/e2e/SettingsJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/SettingsJourney.test.ets` | `ios/tests/SettingsJourneyUITests.mm` |
| DisplayJourney | `app/src/androidTest/java/com/flynes/emu/e2e/DisplayJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/DisplayJourney.test.ets` | `ios/tests/DisplayJourneyUITests.mm` |
| AudioJourney | `app/src/androidTest/java/com/flynes/emu/e2e/AudioJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/AudioJourney.test.ets` | `ios/tests/AudioJourneyUITests.mm` |
| HapticsJourney | `app/src/androidTest/java/com/flynes/emu/e2e/HapticsJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/HapticsJourney.test.ets` | `ios/tests/HapticsJourneyUITests.mm` |
| LayoutJourney | `app/src/androidTest/java/com/flynes/emu/e2e/LayoutJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/LayoutJourney.test.ets` | `ios/tests/LayoutJourneyUITests.mm` |
| LocalizationJourney | `app/src/androidTest/java/com/flynes/emu/e2e/LocalizationJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/LocalizationJourney.test.ets` | `ios/tests/LocalizationJourneyUITests.mm` |
| AccessibilityJourney | `app/src/androidTest/java/com/flynes/emu/e2e/AccessibilityJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/AccessibilityJourney.test.ets` | `ios/tests/AccessibilityJourneyUITests.mm` |
| AboutJourney | `app/src/androidTest/java/com/flynes/emu/e2e/AboutJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/AboutJourney.test.ets` | `ios/tests/AboutJourneyUITests.mm` |
| LifecycleJourney | `app/src/androidTest/java/com/flynes/emu/e2e/LifecycleJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/LifecycleJourney.test.ets` | `ios/tests/LifecycleJourneyUITests.mm` |
| UpgradeJourney | `app/src/androidTest/java/com/flynes/emu/e2e/UpgradeJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/UpgradeJourney.test.ets` | `ios/tests/UpgradeJourneyUITests.mm` |
| NearbyJourney | `app/src/androidTest/java/com/flynes/emu/e2e/NearbyJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/NearbyJourney.test.ets` | `ios/tests/NearbyJourneyUITests.mm` |
| OfflineJourney | `app/src/androidTest/java/com/flynes/emu/e2e/OfflineJourneyTest.java` | `harmony/entry/src/ohosTest/ets/e2e/OfflineJourney.test.ets` | `ios/tests/OfflineJourneyUITests.mm` |

所有Harmony文件加入 `harmony/entry/src/ohosTest/ets/test/List.test.ets`；所有iOS文件加入 `ios/app/CMakeLists.txt` 的对应UI bundle。Android新类在 `e2e` 包中独立选择，现有instrumentation组件测试不改标签冒充E2E。

## 7. 真机完整列表（22条，65个平台计划实例）

真机复用对应E2E旅程和夹具，另加真实硬件、权限、签名与测量断言。自动操作不能充分覆盖的触感/真实多指/来电由执行者按步骤操作并录制证据；不能把人工判断包装为自动断言。D013沿用现有认证runner的显式授权参数，任何未认证路径都维持锁定。

| 执行ID / 功能 | 优先级 | 用例 | 前置 | 实机操作顺序 | 必须断言 | 真机执行组 |
|---|---|---|---|---|---|---|
| A-D001、H-D001、I-D001 / F01 | P0 | 真机候选包安装与单机冒烟 | PHONE+CANDIDATE | 安装保留数据→桌面启动→内置游戏→暂停返回 | 包身份/hash/版本正确；画面、真实输入、声音和退出均正常 | DeviceSmoke |
| A-D002、H-D002、I-D002 / F06 | P0 | 真实系统文件授权持久化 | PHONE+PICKER | 真实FilePicker/SAF/Files选目录→重启app与设备→开游戏 | 授权长期有效；对应内容可打开；拒绝授权有正确提示 | DeviceStorage |
| A-D003、H-D003、I-D003 / F24 | P0 | 真机覆盖升级与旧进度 | PHONE+UPGRADE | 旧包创建数据→新候选包覆盖安装→恢复A/B进度 | 来源/收藏/布局/设置/进度保留；签名不兼容明确BLOCKED_ENV | DeviceUpgrade |
| A-D004、H-D004、I-D004 / F27 | P0 | 30分钟真实时钟播放 | PHONE+DIAG | 正常温控电量≥30%→连续实际播放30分钟并每5分钟操作 | 无crash/ANR/OOM/持续内存增长；记录帧率/耗电/温度/音频欠载 | DeviceSoak |
| A-D005、H-D005、I-D005 / F12 | P0 | 真手指双手多指输入 | PHONE+DIAG | 方向+A+B并按→交替抬指→斜向→短按→取消 | 各角色键位正确且无粘键；高速摄像/诊断画面留证 | DeviceInput |
| A-D006、H-D006、I-D006 / F21 | P1 | 屏幕边缘与系统手势冲突 | PHONE+DIAG | 靠刘海/手势区长拖方向、系统返回、左右横屏 | 系统手势按平台规则可用；游戏不抢走全屏；取消归零 | DeviceInput |
| A-D007、H-D007、I-D007 / F18 | P1 | 马达触感真实效果 | PHONE含马达 | OFF/LIGHT/STANDARD/STRONG和A/B区分预览 | OFF确无振动；等级/顺序可辨；实际支持限制明确，不伪造成功 | DeviceHaptics |
| A-D008、H-D008、I-D008 / F17 | P0 | 真实焦点抢占与来电中断 | PHONE+DIAG | 外部媒体抢焦点/受控来电或系统中断；三策略各一轮 | 暂停/本机duck/忽略按系统允许行为；恢复无双音频/粘键 | DeviceAudio |
| A-D009、H-D009、I-D009 / F17 | P1 | 扬声器有线蓝牙路由切换 | PHONE+可用耳机 | 播放中扬声器→有线或USB→蓝牙→断开 | sink重建、无持久静音/崩溃；记录设备路由额外延迟 | DeviceAudio |
| A-D010、H-D010、I-D010 / F14 | P0 | 两游戏真机自动存档 | PHONE+DIAG2 | 各玩不同进度→暂停退出→杀进程→重启恢复→关闭autosave再试 | 进度隔离；开关语义正确；已存档不被错误覆盖 | DeviceStorage |
| A-D011、H-D011、I-D011 / F23 | P0 | 锁屏前后台与系统回收 | PHONE+DIAG | 长按时后台/锁屏→恢复；独立轮终止进程→重启 | 后台资源释放；按键归零；仅恢复已持久进度 | DeviceLifecycle |
| A-D012、H-D012、I-D012 / F16 | P0 | 真实GPU基础滤镜与回退 | PHONE+DIAG | Nearest/Sharp/MMPX/ScaleFX/CRT逐一播放与暂停恢复 | 支持项真实呈现；缺能力明确回退；记录GPU/driver/图像证据 | DeviceDisplay |
| A-D013、H-D013 / F16 | P0 | 实际刷新率与证据门禁 | PHONE支持高刷且有匹配资格证据 | 默认基线→按既有certification流程请求支持刷新→暂停撤销 | 测物理呈现而非API声明；无证据保持锁定；不为测试解锁产品 | DeviceCertification |
| A-D014、H-D014、I-D014 / F27 | P1 | 温控降级与电量记录 | PHONE正常环境 | 在正常安全使用条件连续高负载并记录温控状态/降级 | 若达到系统严重热态则降视频或暂停；未达到记条件未覆盖，不伪造通过 | DeviceSoak |
| A-D015、H-D015、I-D015 / F05 | P1 | 真实大目录响应 | PHONE+LARGE100及LARGE2224 | UI导入→冷启动→滑到尾部→搜索→重复导航20次 | 不卡死/集合修改崩溃；记录p50/p95启动/搜索/导航及内存 | DeviceLibrary |
| A-D016、H-D016、I-D016 / F21 | P1 | 真实屏幕最大字号和读屏 | PHONE | 系统最大支持字号/读屏→中心/设置/布局/暂停/许可证 | 关键操作可完成；语义正确；保存取消不被裁切 | DeviceAccessibility |
| A-D017、H-D017、I-D017 / F08 | P1 | 真机授权撤销与重新授权 | PHONE+测试专属来源 | 系统撤权/文件移动→启动失败→UI重新授权 | 错误可理解；恢复后同来源可用；其他来源不受影响 | DeviceStorage |
| A-D018、H-D018、I-D018 / F13 | P1 | 反复暂停设置返回耐久 | PHONE+DIAG | 重复暂停→设置→继续→退出共50轮 | 无资源持续增长、重复音频或旧surface；周期输入归零 | DeviceLifecycle |
| A-D019、H-D019、I-D019 / F14 | P0 | 真机写入失败可恢复 | PHONE+IOFAULT | 只对测试文件写边界注入失败→暂停保存→重试恢复 | 错误可见；旧进度保留；不通过填满用户设备制造故障 | DeviceStorage |
| A-D020、H-D020、I-D020 / F25 | P0 | 真机联机入口诚实阻塞 | PHONE当前联机未实现包 | 附近设备/配对/好友管理→单机对局 | 不能假装发现/认证；原因真实；单机无联机banner和回归 | DeviceSmoke |
| A-D021、H-D021、I-D021 / F17 | P1 | 本机真实A/V同步抽样 | PHONE+声画诊断fixture | 同时录制已知画面/音频标记≥100次并采集本机时间 | 报告本机A/V分布和测量误差；新增单机目标abs p95≤50ms，未达需调查 | DeviceAudio |
| A-D022、H-D022、I-D022 / F26 | P0 | 发布包隔离测试钩子 | PHONE+CANDIDATE release配置 | 尝试访问测试seed/fault/instrumentation入口并正常启动 | 测试专用入口不可用；常规功能正常；不含诊断凭据/私有ROM | DeviceRelease |

真机执行清单新增 `tools/quality/device/device_cases.json`，每条以平台展开ID为key，记录自动旅程selector、人工步骤和采样要求；Android/Harmony入口新增 `tools/quality/device/run_device_cases.ps1`，iOS入口新增 `ios/scripts/run_device_cases.py`。DeviceCertification复用 `BaseVideoCertificationTest.java`、`NativeTime120CertificationTest.java` 与 `tools/quality/capture_display_evidence.ps1`，不另建绕过授权的认证入口。其他执行组以本表名称写入同一device manifest，并关联§6测试文件。

## 8. 门禁与测试设施完整列表（15条）

| ID / 功能 | 优先级 | 用例 | 前置 | 执行 | 必须断言 |
|---|---|---|---|---|---|
| G001 / F26 | P0 | 全量test discovery对账 | 全新检出且依赖就绪 | 枚举JUnit/CTest/XCTest/Hypium→与test-matrix比对 | 每个required id均注册且唯一；未接入文件明确失败 |
| G002 / F26 | P0 | 零测试与skip-only拒绝 | 故意选不存在过滤器或全skip | 运行真实runner及汇总 | 0执行/全skip不得PASS；保留原因和过滤器 |
| G003 / F26 | P0 | 错误码及日志截断传播 | 一个真实失败断言和截断报告 | 执行runner→聚合→CI退出 | 任何层保留失败；缺结束标记/计数不完整为ERROR |
| G004 / F26 | P0 | 同SHA包与报告一致性 | A提交的包与B提交的结果 | 尝试合并结果；再用同SHA同variant结果 | 错配拒绝；正确结果携包hash/源码dirty/toolchain信息 |
| G005 / F26 | P0 | UT独立覆盖率门禁 | UT-only和integration-only两套coverage | 先仅集成高覆盖/UT低覆盖→再补UT | UT低覆盖仍失败；分层报告不互相填充 |
| G006 / F26 | P0 | 漏生产文件分母防护 | 新增一个未被测试加载的生产文件 | 跑coverage并生成模块报告 | 新增文件以0覆盖入分母；不得因未加载消失 |
| G007 / F26 | P0 | 新检出LF与生成物检查 | Windows autocrlf=true 与Linux checkout | 不generate直接执行全部check | check成功；修改JSON未生成则失败；禁用自动修复掩盖 |
| G008 / F26 | P0 | native Release断言存活 | Debug/Release独立构建 | 在临时测试副本注入必失败断言 | 两个配置都失败；正式代码撤销变异后全绿 |
| G009 / F26 | P0 | 模拟器与真机目标隔离 | 同时在线多目标 | 传明确target运行；再故意错类型/缺target | 只操作目标；身份不符拒绝；证据类型不能串用 |
| G010 / F26 | P0 | 专用夹具与数据保全 | 测试数据和非测试来源并存 | preflight→运行→成功清理/失败留证 | 拒绝污染用户数据；只清理run-id所属资源；失败证据保留 |
| G011 / F26 | P1 | 测试顺序独立与冷启动重复 | 平台完整E2E及独立fixture | 新环境连续3轮；nightly随机顺序10轮 | 无顺序依赖；首次失败保留；重跑不能覆盖红灯 |
| G012 / F26 | P0 | coverage与性能分离 | 同SHA coverage包和非插桩性能包 | 分别运行coverage与性能作业 | 性能报告明确无coverage开销；结果不混合比较 |
| G013 / F26 | P0 | 缺SDK缺设备退出状态 | 工具链或设备故意缺失 | 运行相应stage | BLOCKED_ENV非PASS；不把编译/UT成功替代设备执行 |
| G014 / F26 | P0 | 增量变更触发正确测试集合 | 只改shared、只改UI、只改fixture三种diff | 计算CI受影响任务 | shared触发三端回归；UI至少对应E2E；fixture触发消费者；required门禁不被路径过滤漏掉 |
| G015 / F26 | P1 | 失败证据完整性与脱敏 | 真实失败截图/日志包含测试专属敏感哨兵 | 导出report bundle并校验 | 有步骤/树/截图/日志/结果/版本；敏感哨兵脱敏；原始证据受控保存 |

实现落点：`tools/quality/test-matrix.json`、`coverage-policy.json`、`run_test_plan.py`、`check_test_matrix.py`、`check_test_results.py`、`check_coverage.py`、`check_device_target.py`；它们分别由U068–U072中的测试保护。仓库现有 `run_harmony_completion_gate.ps1`、`ios/scripts/run_simulator_tests.py` 与Android runner接入这些检查，不重复发明另一套结果格式。

## 9. 当前联机未实现功能的处理

当前本清单对附近联机执行E085–E088、D020及session UT/组件负测，证明入口和阻塞态真实。真正BLE/QR身份、双机输入/媒体、DUAL/STREAM、断线事务属于尚未实现能力，不能用假的session让这些正向用例通过。后续启用前，必须同时纳入[多人联机完整实机方案](2026-09-13-main-status-nearby-device-test-plan.md)§5.4的PAIR/NET/LOBBY/PLAY/AUDIO/LIFE/REC/SAVE/FILE/FRIEND/RADIO/FRAG/PERF全部条目，以及九方向至少18角色配置；这是功能上线前置，而不是当前单机覆盖率中可勾PASS的项目。

## 10. 每条用例的交付结果

实施时在 `test-matrix.json` 登记本表全部239条及适用平台执行ID，维护 `feature_id, layer, platform, priority, fixture_id, test_file, framework_selector, environment, automation_kind, status, evidence`。新增文件路径和未注册测试必须作为待完成项被校验器识别，不得填现成PASS。

结果写入 `out/evidence/test-plan/<sha>/<run-id>/`：测试开始前的fixture/hash和包信息，实际步骤日志、首个失败截图与UI树、JUnit/Hypium/xcresult、coverage文件、持久化前后摘要、设备指标。没有真实执行的用例写NOT_RUN；缺能力写具体BLOCKED及负责实施批次。本次计划不创建测试结果、不声称任何新用例通过。
