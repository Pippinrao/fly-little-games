> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# 多人联机 UX：main 三端独立验收报告

日期：2026-09-14（Asia/Shanghai）。验收基线：`main` / `5389585f8e3fa7ddc3e3dcd125d9b0a299c6c79c`；UX 合并提交：`5d2ee7b`。

## 1. 验收结论

**不通过。当前 main 尚不满足两份依据文档规定的三端 UX 与完成标准。**

Android、HarmonyOS NEXT、iOS 均由独立子 agent 验收，主 agent 复核关键源码、Android 截图、构建/测试日志并运行共享测试和三端对比器。已发现可复现的产品/测试构建阻塞，以及布局、输入、筛选、邀请生命周期和真实状态接线缺口。公共策略或少量页面测试通过，不能替代这些要求。

本次只交付本报告；未修复产品代码、测试代码、协议、版本或签名配置。原始证据保存在 ignored [本次证据目录](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14)。本报告不是发布认证，也不认证实体双机联机或物理性能。

## 2. 依据、范围与证据规则

仅以下两份文档作为本次验收依据：

1. [附近联机三端统一 UX 设计](../superpowers/specs/2026-09-13-nearby-ui-parity-design.md)：U01–U11、N00–N12/G00、§6 尺寸合同、C01–C18。
2. [实现计划](../superpowers/plans/2026-09-13-nearby-ui-parity-implementation-plan.md)：P1–P8 及完成标准，含原生三端矩阵与实体配对闭环。

用户提供的旧 `.worktrees/main-test-repair` 已不在 `git worktree list`，对应文档已进入主工作区。本次没有引用后续后端修复设计的拟议 V2 接口或 wire 作为额外验收要求。

设计文档工作副本比 HEAD 多一行“后续后端设计链接”，不改变本次 UX 要求；`harmony/build-profile.json5` 有原有本地修改，验收保留其内容，前后 hash 一致。验收开始与结束均核对产品源码无本任务修改。基线与两份依据的 SHA-256 见 [baseline.json](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/baseline.json)。

状态含义：**失败**表示有源码或实际执行反例；**未验证/阻塞**表示缺少足够运行证据；**局部通过**只涵盖注明的样本/子断言，不表示整个跨端矩阵通过。没有把“未发生配对”当作“配对安全验收通过”，也没有把“页面能打开”当作“扫码已实现”。

[09-13 历史记录](2026-09-13-nearby-ui-parity.md)只作为线索。该记录对 iOS 存在“已经执行”与“未执行”的旧表述冲突，本报告以本次证据为准，不继承其通过计数。

## 3. 本次执行结果

| 范围 | 本次执行及结果 | 可证明的边界 |
|---|---|---|
| shared fresh build | VS 2022 / MSVC 19.44，独立 Release 构建目录；configure/build exit 0 | 当前源码可构建共享 host targets |
| shared 全量 CTest | **51/52 通过，exit 8**；唯一失败 `flynes_game_title_data_check`：`generated table is stale; run generate` | `nearby_product_ui`、`flynes_multiplayer_eligibility`、`flynes_invite_code_route`、公开 session path 等通过；全量门禁仍失败，未擅自重生成标题表 |
| 共享 UI 合同检查 | **exit 0，112 个字符串键覆盖三端，C01–C18 fixture 在位** | JSON/资源/纯合同检查，不证明原生页面实际使用这些键或共享状态 |
| Android JVM 单测 | **95 类、465 项，0 failures、0 errors** | 当前 JVM suite；包含部分能力筛选与本地邀请码策略 |
| Android 产品 APK | 单独 `testDebugUnitTest + assembleDebug` **exit 0**；当前 APK 安装、启动成功 | Android 15 / emulator-5570 的真实手工 UI 样本 |
| Android instrumentation | `assembleDebugAndroidTest` **exit 1**；当前 instrumentation **执行 0 项，阻塞** | 编译实参不匹配，未运行旧 test APK |
| Harmony private host | 独立 Debug configure/build/CTest 均 **exit 0，12/12 通过** | host C++ 适配层，不是 ArkUI/Hypium |
| Harmony 产品构建 | **exit -1，82 errors / 98 warnings** | ArkTS 语法错误导致级联编译失败，不是 82 个独立 UX 缺陷 |
| Harmony ohosTest 构建 | **exit -1，81 errors / 19 warnings**；Hypium **执行 0 项** | 当前包未生成；签名安装、Hypium、原生截图全部阻塞 |
| Harmony 辅助静态检查 | product contract exit 0；matcher **6/6 通过** | 这些文本检查未发现本次 ArkTS 编译错误，不替代编译 |
| iOS 当前源码产品构建 | Mac独立目录，当前commit archive + pinned Nestopia；**产品及两test bundle构建exit 0** | x86_64 iOS Simulator产品，不是portability probe |
| iOS 选定 XCTest UI | **4/4，exit 0**：NearbyUiParityTests三项及原大厅布局一项 | 验证入口存在、5→6位门禁及本地重新生成/取消；未运行完整UI suite，未认证真实邀请或C01–C18全覆盖 |
| iOS 选定 runtime/input/audio | **19/19，exit 0**：PlaybackRuntimeBridgeTests、GamepadOverlayTests、PlaybackAudioPlayerTests | 三个注册测试类；未运行完整runtime suite，也未验证双机输入/媒体同步 |
| P7 原生三端对比 | 既有目录及本次证据根目录分别运行，均 **exit 1，3 missing** | 缺Android/Harmony/iOS三份规定的native parity JSON，不能声明0/0/0 |

主要日志：[shared CTest](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/shared/ctest.log)、[共享合同](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/shared/contract.log)、[Android 构建/测试](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/build-unit.log)、[Android 产品复跑](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/unit-assemble-confirm.log)、[Harmony 产品构建](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/harmony/app-build.log)、[Harmony ohosTest](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/harmony/ohostest-build.log)、[Harmony host](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/harmony/host-ctest.log)。

iOS当前执行日志：[产品构建](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/product-build-lf.log)、[test bundle构建](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/test-bundles-build-enabled.log)、[UI 4项](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/ui-nearby.log)、[runtime 19项](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/runtime.log)、[完整runner命令/退出码](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/xctest-status.json)。两份xcresult保留在同一ignored证据树，分别对应上述UI和runtime运行。

P7本次结果见[parity-current.log](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/shared/parity-current.log)。测试注入隔离的有限静态检查也已完成：Harmony的host-fake仅编入private host test，Android产品CMake关闭shared测试构建，未发现产品UI可启用的fixture注入开关；这不证明产品本机生成的数字已经成为真实邀请。

## 4. 阻断项与 UX 缺陷

### F01 / P1：Harmony 当前产品和测试包无法构建

[CatalogProductService.ets:85](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/service/CatalogProductService.ets:85) 的注释缺起始符；[第90行](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/service/CatalogProductService.ets:90) 开始的筛选函数缺闭合与 return，后续类不能正常解析/导出。产品和 ohosTest 两次独立构建均复现。现场有 API 20 模拟器及 API 24 / Harmony 6.1 实体设备，阻塞来自当前源码编译，不能笼统记为“缺设备”。影响 P5/P7/P8。

### F02 / P1：真实邀请、扫码与连接状态未接入产品

三个产品都还没有 N01 的真实邀请二维码和 N03 的相机扫码链：Android [NearbyPairingActivity.java:94](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyPairingActivity.java:94)、Harmony [NearbyPairing.ets:94](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/NearbyPairing.ets:94)、iOS [NearbyPairingView.swift:49](E:/workspace/codes/games/fly-little-games/ios/app/NearbyPairingView.swift:49) 都将扫码入口导向输入码块或占位流程。真实 QR 编解码、签名邀请校验、房主接受链没有从这些原生入口贯通。iOS 还缺相机用途声明。

创建页使用产品本机状态生成数字：Android [NearbyInviteHostState.java:61](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyInviteHostState.java:61)，Harmony [NearbyPairing.ets:46](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/NearbyPairing.ets:46)，iOS [NearbyPairingView.swift:65](E:/workspace/codes/games/fly-little-games/ios/app/NearbyPairingView.swift:65)。这些数字未绑定公共层真实 invitation；iOS 首次进入还只显示占位点，点击重新生成才产生本机码。

Android 提交合法码只展示 discovery blocked（[NearbyPairingActivity.java:199](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyPairingActivity.java:199)）；其大厅用全默认 `NearbyFacts` 投影状态（[HomeActivity.java:180](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/HomeActivity.java:180)）。Harmony 大厅文案硬编码（[GameCenter.ets:242](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/GameCenter.ets:242)）；iOS bridge 无 nearby/session API。shared route/投影虽存在并通过单测，原生产品未消费完整真实状态链。

设计允许未支持路径显示明确不可用原因，因此这些 blocked 文案不是认证绕过证据；但不能据此通过 U05–U10、C06–C14 或 P8，也不能把尚未注册的本机数字认定为可用邀请。

### F03 / P2：iOS 缺少独立“支持双人”操作与可见连接文字

[CatalogLibraryView.swift:70](E:/workspace/codes/games/fly-little-games/ios/app/CatalogLibraryView.swift:70) 只声明 `@AppStorage multiplayerOnly`；实际数量行没有 Toggle/按钮，也没有该偏好变化的 UI 接线。用户无法执行“收藏 + 支持双人”或 C03 的关闭筛选动作。附近入口在 [第208行](E:/workspace/codes/games/fly-little-games/ios/app/CatalogLibraryView.swift:208) 仍是图片按钮与 accessibility label，未显示设计要求的“附近联机/双人联机中”可见文字。影响 U02/U04、C01–C03/C09。

三端能力 registry 也都只有本地空实例，未读取公共版本化 profile；Android [HomeActivity.java:332](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/HomeActivity.java:332)、Harmony [GameCenter.ets:36](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/GameCenter.ets:36)、iOS [CatalogLibraryView.swift:57](E:/workspace/codes/games/fly-little-games/ios/app/CatalogLibraryView.swift:57)。UNKNOWN 被排除本身符合策略，但永久缺能力数据使产品无法呈现真实 SUPPORTED 集合；这与测试中手动填 registry 的通过结果不同。

### F04 / P1：Android 完整 instrumentation 门禁被测试编译错误阻塞

[AndroidCatalogLaunchRegressionTest.java:112](E:/workspace/codes/games/fly-little-games/app/src/androidTest/java/com/flynes/emu/catalog/android/AndroidCatalogLaunchRegressionTest.java:112) 调用 `NativeCatalogProjector.project` 只有 7 个实参，当前签名要求第 8 个 `BuiltinGames`。这是当前 main 测试门禁问题，不等于已定位一个附近联机运行时崩溃；但 P4 要求的完整 instrumentation 无法执行。465 项 JVM 单测和旧 test APK 均不能替代。

### F05 / P2：三端首页未实现统一横屏双栏与视觉规则

设计 §6 要求可用宽度 >580 时左 224、间距 18、右侧剩余宽度；三端当前仍是纵向页面：Android [activity_nearby_friends.xml:39](E:/workspace/codes/games/fly-little-games/app/src/main/res/layout/activity_nearby_friends.xml:39)，Harmony [NearbyFriends.ets:55](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/NearbyFriends.ets:55)，iOS [NearbyFriendsView.swift:40](E:/workspace/codes/games/fly-little-games/ios/app/NearbyFriendsView.swift:40) 仍用 List。

Android 本次在约 802.91dp 可用宽度下实测为全行按钮单列，见 [N00截图](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C04-nearby.png)。Harmony 七个阶段排在三动作之前，常用操作在 Scroll 内，好友页签不保留三动作；iOS 同样把三动作放在 devices 页签。Harmony 仍使用旧蓝色令牌、44vp 返回命中区；iOS 使用系统 List 风格。N09 的确认操作也未形成设计要求的独立固定底栏。已证明布局合同不符；八宽度、全部页面/弹窗的横向溢出矩阵仍另列未验证。

### F06 / P2：Android 七位码被静默截断并可提交

复现：进入输入码页，输入 `0123456`。实际字段变为 `012345`，提交按钮启用。预期是保留原输入并拒绝七位数据，不能静默截断成另一个合法 locator。

根因：[activity_nearby_pairing.xml:52](E:/workspace/codes/games/fly-little-games/app/src/main/res/layout/activity_nearby_pairing.xml:52) 的 `maxLength=6` 与 `digits` 过滤先于完整格式校验。证据：[截图](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C05-seven-digits.png)、[节点树](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C05-seven-digits.xml)。Harmony [NearbyPairing.ets:203](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/NearbyPairing.ets:203) 的 `maxLength(6)` 有同类静态风险，未把它记为已实机复现。影响 C05；normalize 纯函数通过不能覆盖输入控件先行改写。

### F07 / P2：邀请过期 UI 未撤销；配对码与 SAS 仍混称

Android 在 00:13:26 创建本机邀请码，00:14:39 同一码仍显示，倒计时为 0s，重新生成/取消仍启用。ticker 只调用 `renderValidity()`，没有调用状态对象的过期处理，见 [NearbyPairingActivity.java:125](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyPairingActivity.java:125)。证据：[过期截图](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C16-expired-invite.png)、[节点树](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C16-expired-invite.xml)。此反例证明本地页面过期投影不完整，不声称已证明远端接受过期码。

Harmony/iOS 用 `Date.now()/Date` 和本地 generation 维护邀请码，缺少公共 continuous-clock 到期和真实请求失效接线；取消又把 generation 归零。Android 实际 SAS 控件仍引用“六位码/确认六位码”（[中文资源:292](E:/workspace/codes/games/fly-little-games/app/src/main/res/values-zh-rCN/strings.xml:292)），而不是批准的“身份校验码”。影响 U08、C07/C16。

### F08 / P1：本局配置、文件、朋友管理和中断流程仍是禁用占位

Android [NearbyLobbyActivity.java:37](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyLobbyActivity.java:37)、Harmony [NearbyLobby.ets:101](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/NearbyLobby.ets:101)、iOS `NearbyLobbyView` 的配置字段/确认动作仍不可用；没有实际主机/座位/fingerprint 变更及双端确认失效闭环。文件发送、接收、校验、导入许可和朋友管理同样没有真实命令链。

Android [NearbyInGameStatus.java:86](E:/workspace/codes/games/fly-little-games/app/src/main/java/com/flynes/emu/NearbyInGameStatus.java:86) 恒定无 session；Harmony [RunGame.ets:401](E:/workspace/codes/games/fly-little-games/harmony/entry/src/main/ets/pages/RunGame.ets:401) 同样如此。不能证明 C11–C15 或“退出换游戏保留联机”；原单人路径存在不等于这个联机路径已完成。

## 5. C01–C18 三端判定矩阵

Android 的“局部通过”包括英文环境的原生手工样本及注明的纯 fixture；Harmony 因当前产品编译失败，失败项均为源码/编译证据，未验证项没有冒充原生运行。iOS 运行补充另列于执行表；静态失败不会因少量 XCTest 通过而转为通过。

| Case | Android | HarmonyOS NEXT | iOS | 主要依据/缺口 |
|---|---|---|---|---|
| C01 原大厅与未连接入口 | 局部通过 | 未验证 | 失败（源码） | Android原生结构保留；iOS没有可见状态文字；Harmony不能启动当前包 |
| C02 分类×双人×搜索×连接 | 失败 | 失败 | 失败 | 空能力registry；iOS无开关；真实连接事件未接线 |
| C03 内置零结果与关闭筛选 | 局部通过 | 未验证 | 失败 | Android保持BUILTIN的手工样本通过，真实UI数据为UNKNOWN；仅UNSUPPORTED条件由纯fixture覆盖；iOS无操作 |
| C04 无游戏三入口 | 局部通过 | 失败 | 失败 | Android所测尺寸三入口可见；Harmony阶段先于操作；iOS默认页存在性测试通过，但切好友页签失去三入口且扫码不进N03；双栏另见F05 |
| C05 六位输入与错误 | 失败 | 失败 | 未验证 | Android七位截断实测；Harmony截断静态问题；iOS只实测5→6位门禁，完整向量/过期查找未完成 |
| C06 匿名等待房主许可 | 未验证 | 未验证 | 未验证 | 缺真实请求/凭据释放运行链，不能据“没有发包”证明完整安全语义 |
| C07 单端SAS/不一致 | 失败 | 失败 | 未验证 | 控件禁用，真实双方确认/取消未贯通 |
| C08 QR合法/篡改/重放/过期 | 失败 | 失败 | 失败 | 扫码路由是输入表单/占位，无真实相机与QR链 |
| C09 建网与成功状态 | 失败 | 失败 | 失败 | 固定未连接/blocked状态，无成功投影及好友保存 |
| C10 相机/Wi-Fi拒绝 | 未验证 | 失败 | 失败 | Android未实测拒绝；Harmony/iOS缺实际扫码权限/建网链 |
| C11 配置变更使确认失效 | 失败 | 失败 | 失败 | 配置页占位；未绑定真实pending config |
| C12 文件双方许可/导入 | 失败 | 失败 | 失败 | 许可、传输、校验、导入真实动作缺失 |
| C13 换游戏保留连接 | 未验证 | 失败 | 未验证 | 单机导航/偏好可见，真实朋友连接与换局链缺失 |
| C14 断线中断状态 | 失败 | 失败 | 未验证 | Android/Harmony有恒定无session的具体实现；iOS无法建立连接后制造断线，未认证完整场景 |
| C15 好友管理/身份重置 | 失败 | 失败 | 失败 | store未接入，管理动作禁用 |
| C16 取消/刷新/到期迟到结果 | 失败 | 失败 | 失败 | Android到期投影反例；本地generation不等于真实请求隔离 |
| C17 八宽度×页面/弹窗 | 未验证 | 未验证 | 未验证 | 没有完整原生bounds矩阵；已知双栏合同失败独立列F05 |
| C18 横屏/大字/安全区/键盘 | 未验证 | 失败 | 未验证 | Android只采样部分尺寸/字体；Harmony正文内主动作违约；三端完整矩阵缺失 |

Android 局部通过样本：[C01大厅](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C01-home.png)、[C03筛选](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C03-builtin-filter.png)、[关闭筛选](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/C03-filter-disabled.png)。横屏/大字采样包括约 `802.91×392.73dp`、`640×360dp`，English，fontScale 1/2；不覆盖全部页面、中文、1.3倍率、键盘与所有安全区组合。

## 6. P7 验收工具与证据缺口

现有 `check_nearby_ui_parity.py` 对缺文件返回失败，符合“缺端不得跳过”的一部分要求。但还不能独立证明 P7 完整通过：它只比较收到的 group，没有强制 C01–C18、八宽度/C18矩阵、必要的尺寸/字体/bounds/截图来源完整性。

本次做了单独的**检查器负例自测**：隔离目录只放三份合成 C01 最小记录，刻意缺 C02–C18、contentSize、fontScale、bounds、原生截图及其来源。预期拒绝，实际 **exit 0，1 case group，0/0/0**。源码位置：[必需字段:56](E:/workspace/codes/games/fly-little-games/tools/quality/check_nearby_ui_parity.py:56)、[仅遍历输入组:134](E:/workspace/codes/games/fly-little-games/tools/quality/check_nearby_ui_parity.py:134)、[bounds交集:152](E:/workspace/codes/games/fly-little-games/tools/quality/check_nearby_ui_parity.py:152)。这是 **P2 验收工具缺口**，不能用其绿色退出码单独宣布“三端完全一致”。

负例位于 [checker-negative-control](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/shared/checker-negative-control/README.txt)，有明确 SYNTHETIC/非原生证据标识，不进入真实设备验收矩阵；[result.log](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/shared/checker-negative-control/result.log)只证明检查器漏检。没有生成或伪造产品通过证据。

此外，Harmony 计划中的 `NearbyUiParity.test.ets` 未交付，现有测试没有从 `nearby_ui_v1/cases.json` 导出完整原生结果。三个平台都没有齐备的 P7 native parity JSON。Android 图片/XML 和 iOS XCTest 各自有价值，但不是同条件、同case的三端完整对比矩阵。

## 7. 设备、安装与实体配对范围

- Android：使用 Android 15 的 emulator-5570，当前 APK 正常安装；此前该模拟器无本应用，未卸载或清除用户数据。APK SHA-256 为 `E165657A97586566F6D1A5DCD034C6270D548C299D8F11E01F0C29B561626E07`，使用 debug 测试签名。实体 Android 及另两台模拟器未操作。采样后 size/density/fontScale 恢复原值，筛选关闭，测试应用留在模拟器。见 [环境恢复记录](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/android/environment-restored.txt)。
- Harmony：现场 API 20 模拟器、API 24 / Harmony 6.1 实体机均存在。新产品和 ohosTest 构建失败，因此没有进行本revision签名安装、Hypium或原生截图；不使用旧 HAP 替代。原有签名配置未更改、未输出内容。
- iOS：已连接文档注明的 `ssh apple`，macOS 13.7.8 / Xcode 14.3.1 / iOS 16.4。旧目录不是可验证的同revision checkout，已弃用；使用独立 `flynes-acceptance-5389585-20260914` 导出目录，当前main与Nestopia `4470a2e99199d8010322eef4bf680fb3760f6eda` 两份archive的传输hash一致。新建专用iPhone 14模拟器执行测试，结束后shutdown，未操作原有模拟器数据。官方产品脚本首次因CRLF失败exit 1，随后仅在内存规范化脚本换行后成功执行；默认XCTest target未开启的首次构建exit 65，按仓库已有选项在独立build cache开启后构建成功。上述中间失败日志也保留，未改产品源码。实际[横屏键盘样本](../../../../../out/evidence/nearby-ui-acceptance-2026-09-14/ios/native/ui-during-xctest.png)不代表全尺寸矩阵。

| P8 实体配对组合 | 本次结果 |
|---|---|
| Android ↔ Harmony | 未验证：Harmony当前包编译失败，真实邀请/会话入口未贯通 |
| Android ↔ iOS | 未验证：未执行实体双机、双入口及角色交换闭环 |
| Harmony ↔ iOS | 未验证：Harmony当前包编译失败，未执行实体闭环 |
| Android ↔ Android | 未验证：没有两台实体Android的完整流程证据 |
| Harmony ↔ Harmony | 未验证：当前包编译失败，没有双实体证据 |
| iOS ↔ iOS | 未验证：没有两台实体iPhone的完整流程证据 |

以上均没有完成两种加入入口、邀请者/加入者及候选主机交换、P1/P2、声音独立、文件拒绝/补齐、换游戏保留连接与重连的完整组合。模拟器和纯策略测试不代替这些项目。

## 8. 复验门槛与交付边界

| 实现计划 | 本次阶段判断 |
|---|---|
| P1 合同与公共投影 | 局部通过：合同、资源、纯策略在位；原生消费与完整布局验收未完成 |
| P2 邀请路由及无游戏连接 | 不通过完成标准：shared route/public path测试通过，三端真实入口和无游戏认证连接未贯通 |
| P3 大厅筛选/状态 | 部分策略通过；产品能力数据接线缺失，iOS缺操作控件，连接状态投影未接入 |
| P4 Android | 不通过：产品可运行，存在已实测UI反例，完整instrumentation编译阻塞 |
| P5 Harmony | 不通过：当前产品及ohosTest均编译失败，原生验收阻塞 |
| P6 iOS | 不通过完整UX：当前产品和选定XCTest通过，完整页面/筛选/双栏及真实状态仍缺失 |
| P7 三端一致性 | 不通过：native矩阵缺失，对比器也有完整性漏检 |
| P8 实体闭环 | 未验证：六组实体组合、双入口与角色交换未完成 |

复验至少需要：当前 main 能构建三端产品及测试包；修复上述可复现输入/到期/布局/筛选缺陷；将原生动作与状态接到公共真实会话；按 C01–C18 导出同条件三端截图/节点/语义证据，并让 P7 严格校验完整矩阵；最后执行计划 P8 的实体配对组合，或明确收缩支持范围并重新评审。

本轮没有执行这些修复。现有测试结果证明了一部分公共模型和可打开页面；**不能据此批准“三端多人联机 UX 已完整实现并验收通过”。**

报告核验：主agent已检查源码定位、Android/iOS原始截图与测试日志；Harmony和iOS子agent再次只读复核本报告的计数、环境及判定边界，未发现重大误述。C01–C18编号完整，证据链接在本机可访问；原始日志和截图保持在ignored目录，不作为仓库生成包提交。
