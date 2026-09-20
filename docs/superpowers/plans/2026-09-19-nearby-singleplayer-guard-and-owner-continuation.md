# Nearby 单机保护与原生接线推进计划 / 执行 prompt

> **For agentic workers:** 使用 executing-plans 按检查点推进，行为修复使用 test-driven-development；完成声明使用 verification-before-completion。用户自行启动并行会话，本任务不自动派子代理。

**Goal:** 保住已有离线单机与原稿 UX，补齐现有 Android V2 owner 的真实执行链，再交付最小 DUAL 双人可玩闭环。

**Architecture:** 沿用共享 SessionEngine、NestopiaUE、Quinn/rustls 和现有原生游戏界面；联机不可用不影响单机。先修实际执行缺口，不重复创建 owner/Quinn 演示，不用测试替身证明产品接线完成。

**Tech Stack:** C++17/C ABI、Rust Quinn、Android Java/JNI；Harmony ArkTS、iOS Swift/ObjC++保持既有产品及接口一致性。

## 1. 最新审查与旧计划的覆盖关系

审查日期：2026-09-19。产品目录：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`（下称 W0）。读取时 HEAD 为 `6ec6605de0f5275295fc08b5905fed3d329f37a4`，大量最新代码未提交，且其他会话仍可能写入。

本文覆盖旧 `2026-09-19-nearby-real-quinn-continuation-prompt.md` 的开工顺序和“尚无 owner / 真 Quinn 测试”的状态描述；旧文 J0–J3 的验收要求仍保留，不从头重做。

| 当前事实 | 审查结论 / 下一步 |
|---|---|
| `shared/tests/nearby/integration/test_two_engine_real_quic.cpp` 已存在并注册 CTest | 已有双 engine＋真 NES＋产品 Quinn 的600帧用例；发现/GATT仍是明确标注的替身。先复跑和审查，不能继续派“新建同名测试” |
| `test_two_engine_nes_joint.cpp` 已补 PCM/样本范围等断言 | 不再按旧“未比较PCM”描述重复修；检查同帧对齐与负例 |
| `NearbySessionOwner.java`、`app/src/main/cpp/nearby/session_owner.{hpp,cpp}` 已存在 | V2 owner已创建，旧邀请码 facade已附着同一个owner；不等于原生可玩闭环 |
| `FlyNesApplication.onCreate()` 无条件 `NearbySessionOwner.create()` | create失败抛异常，单机启动仍被联机初始化牵连；风险仍在，只是入口从V1换成V2 |
| `SessionOwner::arm_timer()` 释放task后返回ACCEPTED；cancel无实际调度取消 | 定时任务未执行，不应验收为可工作的执行器 |
| Android `product_quic_port.hpp` 完成处理更新本地字段/计数后释放inbox，未见向engine投递完成事件 | socket计数/roundtrip测试不能证明SessionEngine收到握手、exporter或流数据 |
| Android owner启动注册clock/executor/platform/discovery/quic；discovery连接/GATT仍UNAVAILABLE | 不应把已存在owner称为已完成发现、鉴权、内容解析、真实NES运行和原生双人流程 |
| host测试适配器 `deliver_handshake()` 缺失TLS版本时补1.3，ALPN写死 | 真socket存在不等于握手事实转换严谨；必须保留provider真实事实，缺失/错误应拒绝，不能补成成功值 |
| shared schema发布hash为 `32b52…726f6b`，Android测试冻结值仍 `acc899…115219` | 先确认规范变更与生成链，再同步冻结基线；禁止简单删除冻结断言 |

证据口径：上一轮单机审查运行了12项host测试（通过）和Android全量unit（472项中469通过、1失败、2跳过），音频并发测试因GCC `-Werror=subobject-linkage` 编译失败。**这是上一轮源码证据，不是最新owner改动的回归结果。** 当前代码持续变化，执行者必须重新构建复跑，不复用旧数量。三端静态文案/字号通过也不等于原生截图或交互验收。

本轮新鲜验证：重新构建 `flynes_two_engine_real_quic_test` 和 `flynes_two_engine_nes_joint_test`，再运行本文件§3对应CTest，**2/2通过，退出码0**。真NES联合8.85秒，真Quinn联合120.33秒，总测试129.23秒。本轮只运行一次，没有完成稳定性重复验收；不是Android owner测试，也不是原生两App/真实无线验收。host适配器的握手事实转换缺口仍须补负例，不能因正向通过忽略。

## 2. 本轮顺序与完成边界

依赖顺序：S0复核 → S1单机隔离 → S2回归缺口 → A1执行器 → A2真实provider事件 → A3产品闭环。U1三端既有体验回归是贯穿门禁；本机缺Mac/签名时记录缺口，不阻塞不依赖它的host/Android工作，也不宣称三端验收完成。

### S0：固定当前事实，避免重复工作

- [ ] 读取W0的AGENTS.md、`docs/DEVELOPMENT.md`、最新seams/验收台账；核对HEAD、dirty和相关文件hash，不移动或覆盖别的会话文件。
- [ ] 复跑真NES联合和真Quinn联合测试。已过的只保留回归；失败的记录首个失败断言、超时及日志，不扩大timeout来掩盖死锁/丢事件。
- [ ] 台账上部旧结论与下部新证据有冲突时，以最新源码及新鲜执行为准，更新明确覆盖关系，不删历史证据。

### S1：联机初始化失败不得影响离线单机

涉及：`app/src/main/java/com/flynes/emu/FlyNesApplication.java`、`NearbySessionOwner.java`、`NearbySession.java`及其实际调用页；复用现有测试体系，新增 `app/src/test/java/com/flynes/emu/NearbyAvailabilityTest.java`（若已存在同职责测试则扩展）。

- [ ] 先加可注入的最小owner创建边界与失败用例：未进入联机时不创建联机engine/provider；模拟创建失败，游戏库/单机启动服务仍可用；进入联机得到真实不可用原因而非崩溃。
- [ ] 最小实现延迟初始化与可用性结果，处理已识别的创建失败，不用 `catch(Throwable)` 吞掉所有问题，不让调用方拿null后再崩溃。共享模拟器库本身缺失不能伪装成“单机仍可玩”。
- [ ] 正常路径只创建一个进程级V2 owner，邀请码 facade、页面和运行入口复用它；失败重试不泄漏句柄、不创建第二套V1 owner。
- [ ] 模拟器验证：离线冷启动→游戏库→启动ROM→操作→暂停→存读档→退出→第二款ROM；期间无联机创建。联机初始化失败后重复上述流程仍可用。

验收：上述失败注入用例先红后绿；正常复用、失败重试、单机不创建联机、不可用原因均有断言。不是“只移走onCreate一行”就完成。

### S2：修复已知回归门禁，并评估核心共用变化

涉及：`shared/tests/test_runtime_pcm_contention.cpp`、`shared/src/runtime/flynes_runtime.cpp`、`shared/CMakeLists.txt`；`app/src/test/java/com/flynes/emu/session/SessionSchemaRegistryTest.java`；`shared/schema/`及三端生成副本；`core/src/nes_core.cpp`、`core/tests/test_core.cpp`、`shared/tests/test_runtime.cpp`。

- [ ] 重现音频并发测试编译失败，修正测试编译/实现组织的根因，保持锁竞争、静音数据及metadata断言。不能删测试、全局关Werror、放宽实时路径断言。
- [ ] 审核schema变更是否符合已批准协议，再用仓库既有生成/同步机制修正副本与冻结hash；保留独立重算hash、跨端字节一致及旧包负例。不能将expected直接读actual而失去冻结保护。
- [ ] 检查 `RunningScope` 和 `SetRamPowerState(0)` 对单机的作用。先核实vendor原策略，不能凭推测认为旧值一定随机，也不能盲目回滚导致DUAL失去确定性。
- [ ] 补核心回归：单实例启动/输入/音画/存读档；两个实例输入及回调不串；销毁其中一个后另一个正常；卸载再加载；旧存档兼容（用合法已有fixture）。对明确的全局RAM策略变更提供兼容证据；必要时使用明确运行配置区分，禁止按Android/Harmony/iOS分叉模拟行为。
- [ ] 对manifest声明的7款合法内置游戏做基础启动、推进和存读档覆盖；测试从manifest读清单，不能在产品源码硬编码游戏名，不能承诺覆盖所有用户ROM。

验收：host相关测试、Android全量unit零失败；跳过项逐项解释。最新共同核心源码与三端构建来源可追踪。问题是已知旧问题也要清楚列出，不伪称全绿。

### A1：把Android owner的执行器变成真的

涉及：`app/src/main/cpp/nearby/session_owner.{hpp,cpp}`、`app/src/androidTest/java/com/flynes/emu/nearby/NearbySessionOwnerTest.java`及必要的独立执行器单元测试。

- [ ] 先写失败测试：到期任务恰好执行一次；未到期不执行；取消后不执行；同id重设遵循既有port合同；owner关闭后迟到task不再进入engine；task retain/release平衡。
- [ ] 使用已有调度设施/平台执行器提供串行pump与实际timer，不创建新的通用调度框架。确认即使没有页面点击/快照轮询，到期任务和QUIC完成仍被处理。
- [ ] 不阻塞UI线程等待socket；关闭和重开不产生悬空回调、双重释放。沿用token/generation取消规则。

验收：真实owner驱动的超时、取消、后台回调测试通过，不能以直接调用测试pump替代生产调度。

### A2：provider回调真正进入SessionEngine

涉及：`app/src/main/cpp/nearby/product_quic_port.hpp`、`session_owner.cpp`、`shared/tests/nearby/harness/product_quic_port.hpp`、`shared/tests/nearby/integration/test_two_engine_real_quic.cpp`；复用已有wire编码和port事件API。

- [ ] 先加失败断言：真实owner提交操作后，engine确实消费对应token的完成事件并改变公开snapshot/动作；仅bytes_written/read、handshake_inspected为真不算。
- [ ] 按既有port合同投递listen/connect、握手事实、exporter、open/read/write/credit/close及错误完成，携带原token、session/generation、resource和buffer，保证释放恰好一次；取消/关闭后的迟到结果不能改变新局。
- [ ] 严格校验provider事实长度/版本/ALPN长度及内容，TLS版本和ALPN来自返回数据，缺失、截断或不匹配必须失败；不填入期望值，不伪造pin/exporter已验证。
- [ ] 测试适配器与产品适配器都覆盖相同语义；产品不得include tests/harness。若抽共享纯转换代码只做最小提取，不为复用重构整个通信系统。
- [ ] 把“两个owner之间control往返”保留为组件回归，另验收公开engine动作驱动的完整握手及CONNECTED_LOBBY；不能直接改状态或调用诊断roundtrip冒充用户流程。

验收：同一联合测试仍有双确认、两个真NES、产品Quinn、600提交帧、同帧state/frame/PCM及双方冻结；新负例证明丢事件/错事实不能通过。发现/GATT替身必须继续标注。

### A3：补齐最小原生可玩链，不再增加孤立演示

- [ ] 清点现有owner仍未注册的鉴权/平台承载/内容解析/真实NES端口，按已有V2接口和seams逐个补；不创建第二套engine或第二个模拟步进拥有者。
- [ ] 先实现真实provider与平台接口代码及可在host/模拟器执行的测试。模拟器没有BLE/GATT能力时将物理发现段标记BLOCKED；不能长期以硬编码UNAVAILABLE代替实现。若采用调试专用发现注入，只允许测试入口、不得改正式UX或宣称真实无线通过。
- [ ] 用户流程保留原稿：附近入口→邀请码→房主接受→双方SAS确认→大厅选游戏→双方配置确认→原有游戏视图。主机/加入端各自本地运行、传输入/状态，不做STREAM。
- [ ] 两App/两模拟器各自拥有engine/runtime，P1/P2操作均对游戏有可观察效果，至少10分钟；暂停继续、结束回大厅、第二局重新确认，旧局输入无效；连接失败或断开不损坏单机存档/设置。
- [ ] 如仅用测试发现注入打通，交付称“模拟器注入发现的双App可玩验收”，不是完整附近无线E2E。已具备真实生产路径则用它，不额外造捷径。

### U1：三端既有UX与单机一致性，不重设计

依据（主仓绝对路径）：

- `E:/workspace/codes/games/fly-little-games/docs/superpowers/specs/assets/nearby-ui-parity-review.html`
- `E:/workspace/codes/games/fly-little-games/docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md`

- [ ] 分别验证普通游戏大厅、搜索/来源/设置/附近入口、游戏启动、暂停、存读档、返回。单机不能要求联网、附近权限或先初始化会话。
- [ ] 原稿对应屏幕做三端截图比对：相近逻辑视口、默认及1.3倍字体、横竖屏；保留必要的平台原生差异，但信息层级、操作顺序、可用状态一致。重点看鸿蒙字体比例、字符/emoji图标、分类滚动和按钮裁切；不要为了“统一”另画一套UI。
- [ ] Android跑instrumentation；Harmony编译＋host CTest＋可执行的Hypium；iOS在Mac用仓库脚本重新构建后跑Runtime/UI XCTest。没有环境标NOT_RUN/BLOCKED并给准确命令，静态检查不顶替。
- [ ] 真机安装、物理无线互通和功耗/温度/延迟/高刷资格继续DEFERRED，不启用硬件资格模式。

## 3. 起始命令与验收记录

W0对应WSL目录中，每条成功后再执行下一条：

```sh
cmake --build out/nearby-playable/shared-linux --target flynes_two_engine_real_quic_test flynes_two_engine_nes_joint_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R '^flynes_two_engine_(real_quic|nes_joint)$' --output-on-failure --no-tests=error
cmake --build out/nearby-playable/shared-linux --target flynes_runtime_pcm_contention_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R '^flynes_runtime_pcm_contention$' --output-on-failure --no-tests=error
```

Android在W0 PowerShell执行（按DEVELOPMENT配置JDK/SDK，不复制签名）：

```powershell
.\gradlew.bat :app:testDebugUnitTest --no-daemon --rerun-tasks
python tools/quality/check_nearby_ui_contract.py
python tools/quality/check_nearby_typography.py
python ios/tests/test_product_shell_contract.py
```

新增/既有单机instrumentation优先复用：`AndroidCatalogLaunchRegressionTest`、`StartAndPauseSeparationTest`、`SaveRepositoryTest`、`GamepadCancelTest`、`HomeContinuousLibraryTest`，按源码确认完整类名后用DEVELOPMENT固定模拟器serial执行。不得清除用户设备数据以方便测试。

记录：源码HEAD＋相关dirty文件hash、环境、命令、退出码、测试数/跳过数、实际runtime/provider、PASS/FAIL/NOT_RUN/BLOCKED/DEFERRED。日志在ignored `out/evidence/`。真网络测试需至少重复一次；零匹配、provider关闭、只编译均不算通过。

## 4. 可直接复制的执行 prompt

```text
继续实施 FlyNES Nearby 最小可玩DUAL，但先保护既有单机，再补原生owner真实接线。不要只审计或再产出大方案；完成当前计划里安全可执行的产品修复、回归与可运行交付。

完整阅读并执行：
E:/workspace/codes/games/fly-little-games/docs/superpowers/plans/2026-09-19-nearby-singleplayer-guard-and-owner-continuation.md

产品工作区：
E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes

先读AGENTS.md和docs/DEVELOPMENT.md，核对HEAD、dirty、最新seams及验收台账。其他会话仍可能写入；不要覆盖、reset、stash、clean、合并或提交别人的改动。不在main改产品，不自动派代理；用户自己启动并行。原始HTML和获批UX不可重新设计。

最新代码已有test_two_engine_real_quic、增强后的真NES联合测试、Android NearbySessionOwner和V2邀请码facade，不要再从零创建。旧文“尚无owner/真Quinn测试”已过时。先用新鲜构建确认现状，已完成项只回归，不重复开发。

按S0→S1→S2→A1→A2→A3推进，U1贯穿：
1. 联机engine/provider延迟初始化并隔离创建失败；未进入联机时不创建联机资源，失败不能导致普通大厅或单机崩溃。正常路径仍只有一个V2 owner，页面显示准确不可用原因。
2. 修音频并发测试编译根因、审核并同步schema冻结基线，保留负例和严格断言。验证共享核心RunningScope/RAM策略对单机、双实例隔离和旧存档的影响，不盲目回滚也不臆测兼容。
3. 修Android owner arm_timer释放任务却返回ACCEPTED的问题，真实到期执行/取消/关闭生命周期有测试，不能依赖点击页面才pump。
4. 修产品Quinn适配器只更新计数却不把完成事件投递给engine的问题。token/generation、资源/buffer生命周期与失败事件遵循现有合同。握手事实必须来自provider真实返回，拒绝缺失/截断/错TLS或ALPN，不补成预期成功值。
5. 复用并补齐现有owner缺失的生产ports，走公开动作接通双确认、真实NES与原有游戏视图。验收两App各自本地模拟+传输入/状态，P1/P2有效、10分钟、暂停继续、结束和第二局，单机回归保持。

验收分层：host真Quinn+真NES600帧不等于两App可玩；socket往返计数不等于engine收到事件；发现/GATT替身不等于真实无线。模拟器调试发现注入仅限测试且明确标注，禁止假connected/confirmed、硬编码fixture ROM进入产品或从产品include测试harness。

先失败测试→最小修复→相关回归→证据；继续到当前可执行工作完成，不停在一个组件演示或更新台账。缺Mac/签名/模拟器能力只阻塞对应原生验收，不阻塞其他可做部分，也不能据此宣称三端已通过。

三端保持原稿的信息层级、操作顺序、字号角色、返回与不可用状态；补普通单机流程和默认/1.3倍字体截图验收。STREAM、ROM传输、复杂恢复/迁移和高级好友继续后置。真机不做，host/模拟器继续；25%停止线已撤销。

交付必须包含实际修改、源码/dirty范围、新鲜命令/退出码/测试数、实际provider/runtime、完成项与剩余阻塞。不得删除失败测试、放宽断言、关闭provider、扩大超时掩盖丢事件或用零匹配冒充通过。需要新权限或改变产品范围时明确请求。
```
