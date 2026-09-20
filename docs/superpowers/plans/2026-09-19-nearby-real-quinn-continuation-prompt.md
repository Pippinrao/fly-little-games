# Nearby 真网络联合验收：进度与推进 prompt

> **后续审查已更新起点（2026-09-19）：** 真Quinn联合测试和Android V2 owner已出现；本轮重新构建联合测试2/2通过，但owner执行器/provider事件回投及单机隔离仍有缺口。下一轮优先执行 [单机保护与owner接线推进计划](2026-09-19-nearby-singleplayer-guard-and-owner-continuation.md)。本文保留历史证据和J0–J3验收要求，不再按“尚无owner/真Quinn测试”重建。

> **For agentic workers:** 使用 executing-plans 按检查点执行；用户自行启动并行会话，不自动派子代理。本文替代旧 prompt 的 CP2 起点，不改变原始 UX 和首版范围。

**Goal:** 在同一测试中让两个真实 SessionEngine 使用真实 NES 和产品 Quinn 完成双确认、600帧及故障验收，再接 Android 原生可玩闭环。

**Architecture:** 复用现有 V2 engine、NES runtime、Quinn/rustls 和原生界面；保留 Loopback 测试作为快速逻辑回归，另设真网络门禁。不以替身传输或测试事件证明生产网络通过。

**Tech Stack:** C++17/C ABI、NestopiaUE、Rust Quinn、Android Java/JNI；Harmony/iOS沿冻结接口扩展。

## 1. 本次实际进度（2026-09-19）

产品目录 W0：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`。

HEAD 仍为 `6ec6605de0f5275295fc08b5905fed3d329f37a4`；最新内容仍有大量未提交修改。本次只检查、构建测试及编写交接，未修改产品或安装真机。

与上一轮相比：

| 项目 | 本次源码/测试证据 | 下一步不要误判 |
|---|---|---|
| CP2确认/就绪 | 原有L1确认、运行、冻结回归通过；READY路径增加session/branch/epoch/seat_revision比较，新增错session、错branch、旧epoch负例 | 不再从“确认消息未实现”开始，不重复实现0x0218 |
| CP1配置身份 | profile/options已从名称哈希改成规范字段字节哈希；旧名称哈希拒绝用例通过 | 台账仍有旧描述；还须证明实际runtime加载采用同一配置，不能仅换一组常量便称生产绑定完成 |
| PCM | 测试NES端口从`fly_runtime_pull_pcm`提取样本构造摘要，不再把checkpoint计数当PCM | 联合测试尚只比较state/frame；真网络门禁要比较真实PCM并按同仿真帧对齐 |
| 两真核心 | `flynes_two_nes_schedulers`通过 | 它自身不是两个SessionEngine的网络测试 |
| 两engine＋真NES | 新增`flynes_two_engine_nes_joint`，本轮通过600帧，真实NES端口步进，假runtime步数为零 | **仍是LoopbackTransport，不是Quinn**；暂停/断开只检查发起端冻结，不能说双方生命周期全部验收 |
| 两engine＋真NES＋产品Quinn | 尚未找到已落地的同一测试 | 这是当前主目标，不是继续造一个独立Quinn演示 |
| Android原生双人 | 未见真实session owner闭环交付 | 联合host通过后还需原生接线和模拟器验收 |

### 本轮新鲜验证

重新构建下列7个目标，再运行相应CTest：**7/7 PASS，退出码0，14.77秒（测试阶段）**。

- `flynes_session_v2_abi`
- `flynes_session_v2_contract`
- `nearby_dual_run_scheduler`
- `flynes_two_engine_dual_mvp`
- `flynes_game_config_consent`
- `flynes_two_nes_schedulers`
- `flynes_two_engine_nes_joint`

本轮不是全仓回归；未重跑Android instrumentation、Hypium或iOS XCTest。不能把上一轮12/12和本轮7/7累加成一次19项验收。

## 2. 下一阶段只做四个检查点

### J0：巩固现有联合测试的证据，不重写已有链路

文件：W0 `shared/tests/nearby/integration/test_two_engine_nes_joint.cpp`、`shared/tests/nearby/harness/nes_dual_runtime_port.hpp`、`shared/tests/nearby/integration/test_game_config_consent.cpp`。

- [ ] 保留原600帧用例、加载次数和假runtime步数为零断言；先补失败断言证明比较的是两端同一已提交仿真帧，且摘要不是未产生状态的零值。
- [ ] 联合测试补实际PCM摘要比较和样本数量/时间线证据；不把“摘要非零/不等于计数哈希”单独当PCM正确性验收。
- [ ] P1/P2输入分别有可观察作用，不能只统计发出的输入种类；复用真核心端口已有差异测试。
- [ ] 暂停/断开后检查双方状态及帧号不再推进。健康暂停继续、结束第二局若尚未实现，明确列入J3，不能将FROZEN等同完整暂停继续。
- [ ] READY增加错seat_revision及拒绝旧包后合法包仍可处理的回归；已有session/branch/epoch负例保持。只修可复现问题，不扩展恢复系统。

### J1：把产品Quinn接入同一双engine测试

复用文件：W0 `shared/nearby-quic-provider/include/flynes_quic_provider.h`、`shared/nearby-quic-provider/src/{ffi,runtime,tls}.rs`、`shared/nearby-quic-provider/tests/ffi.rs`、现有session provider接口及联合NES端口。

拟新增：`shared/tests/nearby/integration/test_two_engine_real_quic.cpp`、必要的`shared/tests/nearby/harness/two_engine_real_quic_fixture.{hpp,cpp}`；先搜索同职责实现，有则复用。构建注册由同一集成人修改`shared/CMakeLists.txt`。不复制一套新的NES核心/握手/密码实现。

- [ ] 测试构建显式要求`FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON`并检查最终cache。当前CMake在缺Cargo时可能关闭provider，不能接受该回退；无目标/零匹配不是通过。
- [ ] 测试中两个SessionEngine的QUIC端口必须实际调用产品provider的listen/connect、握手事实、exporter、流open/read/write/credit/close；记录两端socket地址、字节计数、真实provider类型。
- [ ] 连接后Control和承载输入的StateCommit流必须实际经过Quinn socket。禁止在一边调用write后由fixture直接把应用字节转送另一engine。
- [ ] 若发现/GATT/系统bearer因host限制使用替身，精确标注哪些阶段是替身；QUIC证书pin/exporter/channel bind事实不得用伪造已验证事件替代。localhost真socket仍不等于物理无线认证。
- [ ] 回调必须保留session/generation/token和buffer生命周期，读写credit、取消、关闭有测试。provider胶水缺失是实施工作，不能只报“缺adapter”并停止。
- [ ] 第一关是两engine通过公开动作到CONNECTED_LOBBY，完成实际配置确认；不直接写peer_confirmed/connected，不恢复BOOLEAN捷径，不让pump隐式批准游戏配置。

### J2：同一测试的真核心＋真网络600帧门禁

- [ ] J1同一进程/测试中使用两个真实NES实例，双方独立确认和加载就绪后才进入GAME_RUNNING；唯一步进拥有者仍是scheduler。
- [ ] 连续至少600个已提交仿真帧，两路真实输入有效；比较同一帧的state/frame/PCM，日志记录帧号、core版本、ROM SHA-256、实际配置、provider、样本范围和摘要。
- [ ] 检查实际runtime设置与规范profile/options相符。`DualContentRefV1`/C ABI目前仍主要传内容身份，必要绑定由集成人按既有ABI政策最小追加或复用读口；不把测试固定fixture路径带进生产，也不靠标题猜测双人能力。
- [ ] 网络断流/关闭时双方冻结或按合同终止，无继续单人/STREAM；单边未确认/加载失败、旧局输入、错连接、迟到回调拒绝。
- [ ] 至少一次重跑证明无随机竞态；旧L1、真核心端口、codec/握手/篡改回归保持。真实时间网络测试不把测试时钟的300ms声明成物理时延证据。
- [ ] J2未过，不把“Quinn crate通过＋NES测试通过”相加写成端到端通过。

### J3：Android首平台可玩闭环

J2之后按既有ENG-03.A、ENG-04各子卡、ENG-10、ENG-11.A、ENG-12继续；不另写大方案。

- [ ] 生产session owner＋provider＋NES适配器接入原生界面；正式代码不得include测试harness，不能用页面本地bool模拟连接/确认。
- [ ] 原稿入口→输入码→房主接受→双SAS→连接→原大厅选游戏→双确认→既有游戏视图；保留原稿布局、文案、返回和不可用原因。
- [ ] 两App两路输入至少10分钟，暂停继续、结束回大厅、第二局重新确认；旧局消息不得影响新局；单人大厅/导入/播放/存档回归。
- [ ] host、Android unit及模拟器instrumentation按`docs/DEVELOPMENT.md`执行并固定模拟器serial。没有BLE等能力时该生产无线段明确BLOCKED，不冒充完整原生联机通过。
- [ ] 首平台M0有可运行结果即交付；三端后续按同一接缝扩展，不以STREAM/完整恢复/高级好友拖延交付。

## 3. 开工回归命令

在W0对应WSL目录执行，构建成功后才运行测试，每条检查退出码：

```sh
cmake --build out/nearby-playable/shared-linux --target flynes_game_config_consent_test flynes_two_engine_dual_mvp_test flynes_two_nes_schedulers_test flynes_two_engine_nes_joint_test flynes_session_v2_abi_test flynes_session_v2_contract_test flynes_nearby_dual_run_scheduler_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R 'flynes_game_config_consent|flynes_two_engine_dual_mvp|flynes_two_nes_schedulers|flynes_two_engine_nes_joint|flynes_session_v2_abi|flynes_session_v2_contract|nearby_dual_run_scheduler' --output-on-failure --no-tests=error
```

预期当前7项。新增真Quinn目标后单列新结果，不复用旧数字；协议或核心变更须追加对应完整回归及Android unit。日志放ignored `out/evidence/`，台账记录新鲜源码/dirty范围，旧证据保留但标明覆盖关系。

## 4. 可复制推进 prompt

```text
继续实施 FlyNES Nearby 最小可玩 DUAL，当前主目标是“两个真实SessionEngine＋真实NES＋产品Quinn在同一测试中联合通过”，不是再做独立组件演示或只写审计。

先完整阅读：
E:/workspace/codes/games/fly-little-games/docs/superpowers/plans/2026-09-19-nearby-real-quinn-continuation-prompt.md
按J0→J1→J2→J3执行；既有ENG/UX卡及原始HTML/获批UX仍是验收依据。旧CP2 prompt已不是开工起点。

产品目录：E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes
先读AGENTS.md、DEVELOPMENT.md、最新seams和台账，核对HEAD与dirty。HEAD尚未包含全部最新实现，保留所有他人改动和证据。不在main改产品、不reset/stash/clean、不自动派代理、不擅自提交/合并全部未提交文件。

已验证基线：7/7定向测试通过。新增test_two_engine_nes_joint已经通过双engine＋真实NES的600帧，但使用LoopbackTransport；不是Quinn。READY已校验session/branch/epoch/seat_revision；PCM已来自实际样本；profile/options已改为规范字节哈希。不要重复做旧修复，也不要照台账过时段落派任务。

先巩固联合测试同帧state/frame/PCM、两路输入效果、双方冻结断言，再把产品Quinn C ABI接入两个engine端口，让Control和输入流真正走socket。保留Loopback作快速回归，另立真网络门禁。复用现有NES端口/Quinn/rustls，不重写通信栈或密码算法。adapter缺失是要实现的任务，不是外部阻塞。

真网络验收必须在同一测试同时满足：真实session、真实核心、真实provider、双确认与双方ready、600个提交帧、同帧摘要一致、断流/错连接/旧包安全处理。检查实际runtime配置与profile/options相符。发现替身要逐项标注，不能伪造TLS/pin/exporter已验证事实，不能把crate自测和独立NES测试相加称E2E。

通过后继续Android原生owner和游戏视图接线，完成双人输入、暂停继续、结束第二局、10分钟交互及单人回归。产品不能依赖测试harness或fixture ROM路径。原始UX不改，不假启用N09、不另造connected/confirmed布尔。

按失败测试→最小实现→回归→证据推进，有安全可执行下一步就继续，不停在更新文档或一个新测试变绿。真机暂不做，host/模拟器可以继续；STREAM、ROM传输、复杂恢复/迁移及高级好友后置。25%停止线已撤销。

每个检查点交付文件、源码/dirty范围、命令/退出码/数量、实际provider/runtime、PASS/FAIL/NOT_RUN/BLOCKED/DEFERRED。不得删负例、放宽断言、关闭provider或用零匹配通过。需要外部权限或选择时明确请求，不擅自扩大范围。
```

## 5. 本次关键源码指纹

这不是全工作区指纹，只用于识别本次读取的实现；执行前重新核对。

| W0文件 | SHA-256 |
|---|---|
| `shared/tests/nearby/integration/test_two_engine_nes_joint.cpp` | `0AC7285BEB2D9C1B76A2C66761AC26158061FE77F50194DABD7577E334194D73` |
| `shared/src/session/dual/dual_session_controller.cpp` | `1EA00D4E577E7C97BAADEA4A4D401C3907857F65632F849BA4A01DF9C2CB6874` |
| `shared/tests/nearby/harness/nes_dual_runtime_port.hpp` | `E9AD42D7B5B7CB4DC1D1B3317ACCC9F684B41F1F71DE3633C610BDA46EF060E4` |
