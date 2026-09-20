> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby 当前进展与 UX 纠偏审计

日期：2026-09-17。结论：已有较多可复用的共享协议与调度实现，但尚无证据证明原生产品能够让两人按获批交互完成一局真实游戏。当前不能称为“只差真机验收”。

本轮交付文档，不修改产品、测试、版本或已有工作区内容。用户最新方向为“先做一个能玩的版本，复杂功能放后面”；STREAM 和真机验收继续延期。

## 1. 依据及证据边界

- UX 权威：[原始交互稿](../superpowers/specs/assets/nearby-ui-parity-review.html)、[获批 UX 设计](../superpowers/specs/2026-09-13-nearby-ui-parity-design.md)。后端设计不得改写其页面顺序、主次层级与双方授权。
- 历史反例：[09-14 main 验收](../acceptance/2026-09-14-nearby-ui-main-acceptance.md)。main HEAD 仍是该报告的基线；不能将 worktree 修复计入 main。
- 当前代码：读取 main 和全部 4 个 Git worktree 的 HEAD、状态、关键源码；检查 W0 未提交代码及 W3 独有提交。未重新构建、执行原生测试或双机测试。
- 历史运行结果：W0 `docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md` 及 `out/logs/`；它们是历史记录，本次不续签 PASS。
- 本地 HTML 浏览器预览被工具 URL 安全策略阻止。本次对交互稿的判断来自 HTML/CSS/事件源码和获批设计，**没有本次渲染截图，也没有本次原生视觉重验**。
- W0 是活动且不干净的工作区，核查中其文件状态发生变化。以下是本次读取时的事实；接手前必须再次固定 HEAD + dirty diff + untracked 文件清单，不能仅按 HEAD 复现。

## 2. 仓库和 worktree 实际位置

| 位置 | HEAD | 当前进展 | 集成判断 |
|---|---|---|---|
| main（仓库根） | `5389585` | 旧三端 UX 合并版；获批文档有本地修改，多份设计/验收文档未跟踪 | 09-14 验收不通过；未收到 W0 新后端 |
| `.worktrees/nearby-ui-acceptance-fixes`（W0） | `c7bcd79` | W1/W2 已并入；V2 协议、配对、link、输入调度；未提交 DUAL controller、content/recovery、平台 UX 修改 | 主要集成工作区；未提交部分不能由该 SHA 单独恢复 |
| `.worktrees/nearby-dual-link-control`（W1） | `66c048e` | LINK_HELLO/READY codec、纯握手 scheduler | 已并入 W0，不应重复开发 |
| `.worktrees/nearby-dual-runtime`（W2） | `f0761db` | canonical input、有界输入窗口、DUAL 调度/回滚/失步门禁 | 已并入 W0；不等于接好了真实 NES runtime |
| `.worktrees/nearby-dual-e2e-harness`（W3） | `5e8e73f` | 双 engine 场景及 pair 篡改负例修正 | 当前 tip 不是 W0 HEAD 的祖先；完整最新交付尚未集成 |

W0 的未提交实现包括 `shared/src/session/dual/dual_session_controller.*`、`content/`、`recovery/`、`test_two_engine_dual_mvp.cpp` 以及引擎约 780 行增量。不得通过清理、重置或整树覆盖“纠偏”；应逐项保留、审核、重新排序。

## 3. 哪些已做，哪些还不能算完成

| 工作面 | 已有资产 | 当前缺口/结论 |
|---|---|---|
| 共享安全与连接 | V2 ABI、provider 事件、持久对象、配对及 link 控制面 | 有组件与 engine 夹具证据；三端真实 provider 组合未打通 |
| DUAL 输入与调度 | 输入 codec、有界窗口、scheduler、runtime C 端口适配 | 可复用；真实 NES runtime、实际画面/声音和产品输入接入仍缺 |
| 双 engine 运行 | W0 dirty 新增 select/start、600 帧、暂停/断线测试 | 测试 runtime 明确是假实现；不能叫真实游戏 E2E |
| 网络 | Quinn/rustls crate 及 linkage 测试 | 双 engine 主夹具使用进程内 LoopbackTransport，不是 Quinn；两个事实不能拼成“真网双机已通过” |
| UX 公共策略 | 状态/布局/字符串合同、三值能力与筛选 | 纯策略存在不证明页面消费了状态；新增 N00 常量函数不代替原生布局验证 |
| 原生首页 N00 | W0 Android/Harmony 有正在进行的原稿化修复；iOS 已有双栏骨架 | iOS 仍有阶段流水线、额外扫码/配对入口；三端未同步完成 |
| 配对 N01–N08 | 三端有入口、邀请码生命周期与部分旧 bridge | 扫码仍复用输入码块；没有原生 V2 engine 完整链路 |
| 本局配置 N09 | 三端可展示字段与禁用原因 | 仍是大量技术字段平铺；真实配置、双方确认和动作回调未闭环 |
| 内容与恢复 | W0 dirty 已写部分 controller、ledger/reducer | 不应继续作为首版前置条件；保留隔离资产，后置验收 |
| 测试 | 历史记录含 Android 28、Harmony 34、iOS UI 17/runtime 50 等结果 | 不能认证当前 dirty 树，更不证明双机联机；本次未重跑 |

## 4. 方向偏离清单

### D01：页面由用户任务变成了技术字段/阻塞原因展示

原稿 HTML 第 142 行 N00 是左侧邀请操作、右侧设备/好友；第 151/154/160 行才承载连接阶段和详情。W0 iOS `ios/app/NearbyFriendsView.swift` 的 `devicesTab` 仍包含 `ForEach(PairingStage.allCases)`、`nearby_scan_host_qr`、`nearby_open_pairing`。Android/Harmony 本次读取已在去除这些内容，不能笼统说三端完全没修。

原稿第 153 行 N09 把游戏、文件、主机、座位、声音、双方确认放首屏，技术字段放详情。Android `NearbyLobbyActivity.java:40` 的 `FIELDS` 与 iOS `NearbyLobbyView.swift:25` 的 `LobbyField.allCases` 仍将大量字段平铺，确认按钮禁用。底栏和颜色局部修复不等于恢复了信息层级。

影响：U06/U07/U11、N00/N09、C04/C17/C18。纠偏：逐页照原稿恢复信息顺序，阶段表仅放 N07/N10/详情，保留真实错误而非把工程待办放成产品主界面。

### D02：三端产品尚未接入新的 V2 engine

W0 三个入口桥文件：

- `app/src/main/cpp/flynes_app_jni.cpp:16`
- `harmony/entry/src/main/cpp/napi_init.cpp:30`
- `ios/app/bridge/FlyNesAppBridge.mm:21`

均有 `[[maybe_unused]] verify_nearby_v2_composition_contract()`，只构造 clock/executor/platform_state 端口并 `(void)ports`。在三端产品目录检索没有发现 V2 函数调用；存在的旧邀请码 bridge 不等于 V2 会话 owner、真实 discovery/crypto/bearer/QUIC/content/runtime 已装配。

影响：用户仍无法从按钮走到新后端。纠偏：下一阶段优先真实 session owner 与平台 provider，禁止继续以“ABI 编译检查存在”标记平台接入完成。

### D03：本地 START_DUAL 绕开了获批本局确认语义

W0 dirty `shared/src/session/engine/session_engine.cpp:379` 在有本地 selection 后发布 START_DUAL；`:6171` 分支调用 `dual_->start_dual(inputs)`。`dual_session_controller.cpp:176` 附近只校验本地 selection/runtime/connection，随后在约第 251 行置 `running_=true`；没有先完成两端相同 pending config 的独立确认。

同文件 `fill_snapshot()` 约第 596 行：`dual_seats_confirmed = selected_ ? 1u : 0u`。同时 seat/authority 由 initiator 固定映射，revision 多处为 1。这不能被 UI 当成原稿的“双方已确认”。这里只证明源码存在缺口，未执行恶意开局复现。

影响：U10、N09、C11/C13，是首版阻断项。纠偏：真实配置协商和双确认门禁必须在可玩版之前完成；简化主机/座位选项可以显式只读，但不能把选择等同授权。

### D04：测试层的进展被扩大成产品可玩进展

W0 `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp:1647` 直接注明 `Deterministic fake DualRuntimePort`，load/step/export 操作测试状态。`test_two_engine_dual_mvp.cpp` 的 600 帧使用此 fixture；两个 engine 是真实对象，但 runtime、内容及传输仍有替身。

因此已证明的边界是“特定测试环境中的 engine 接线和确定性”，缺少“真实 ROM → NES core → P1/P2 输入 → 两端画面和音频”的证据。保留这些测试，但重命名验收层级。

### D05：首版关键路径膨胀

两端已有同一游戏即可验证基础 DUAL；当前 W0 却在平台会话接线和真实运行时未完之前增加内容传输、导入、恢复账本。完整后端设计确实要求这些，不是无依据开发；偏离在于没有随用户“先能玩”的新优先级重新排序。

纠偏：文件补齐、自动重连恢复、接管/主机迁移、耐久事务和 STREAM 后置。身份验证、双方开局授权、ROM/核心兼容、断链冻结不可削减。

### D06：验收台账和测试目标不同步

W0 验收文档上表写 `MVP-DUAL PASS（进程内 loopback host）`，下方又写“仍 NOT_RUN / 引擎 reducer 未做”；W3 修过的篡改用例也未反映到同一集成状态。`final3-*` 日志实际为 39 项 nearby / 96 项全量，而另一轮记录为 41 / 99，不能混用目录和日期。

纠偏：每项证据绑定 worktree、SHA、dirty 指纹、命令、provider、runtime、用例数和日志。当前快照无有效对应证据时写“未复验”，而非沿用历史 PASS。不得靠删除旧结构断言就宣布新 UX 合格，应改为原稿语义与几何断言。

## 5. 距离原始设计还差多少

不按行数或提交数报百分比；完整体验还有六条主链需要收口：

1. 原生操作到真实邀请/房主接受/身份认证/无游戏连接大厅。
2. 回原大厅选游戏到两端同一配置、双确认、真正开局。
3. 真实 ROM 和 core、跨端输入、本地画面/声音、持续同步。
4. 暂停/继续、正常结束/换游戏、断链冻结与明确退出。
5. 三端逐页视觉及动作一致，真实原生测试而非仅公共字符串/fixture。
6. 原设计复杂部分：QR 全链、好友完整管理、文件补齐、恢复/迁移及 STREAM。

首个可玩交付聚焦 1–4 的受限能力集合；三端推广完成 5；6 分期交付。首版与完整原设计必须分别记状态，不宣称本轮可全部验收。

## 6. 后续文档

- [可玩首版纠偏方案](../superpowers/specs/2026-09-17-nearby-playable-mvp-correction-design.md)：原交互不变项、受限功能、架构接线、验收层级。
- [继续完成与并行任务计划](../superpowers/plans/2026-09-17-nearby-playable-mvp-correction-plan.md)：顺序、文件责任、明确任务、验收和后置清单。

本轮不自动创建 worktree、启动并行代理、提交或合并；用户自行启动并行的约定继续有效。

## 7. 后续同日核查补注

任务细化时再次读取 W0：iOS `NearbyFriendsView.swift` 已有原稿 headline，先前 `PairingStage`/重复扫码/配对快捷块已移除；Android/Harmony 也有持续 UX 修改。本报告 D01 保留最初读取时点的发现，不应拿旧反例要求执行者重复重写。N01–N03 混页及 N09 字段平铺/旧测试仍需按[逐页任务卡](../superpowers/plans/2026-09-17-nearby-ux-restoration-task-cards.md)复核；本次仍未给任何新增原生 PASS。
