# 附近联机 DUAL 首版并行 Worktree 拆分设计

**日期：** 2026-09-15  
**状态：** 已获用户确认，可据此编写各工作流实施计划  
**目标：** 先交付不依赖 STREAM 的基本可用多人联机版本，同时保留后续媒体串流的协议扩展位。

## 1. 范围与完成定义

首个可用版本只支持 `DUAL`：两端各自在本机运行同一游戏，网络只传输经过认证的控制、输入、时序、状态摘要和恢复证据。首版不传输视频或音频，不实现编码器、解码器、媒体拥塞控制或自动 `DUAL -> STREAM` 切换。

协议中的模式枚举和 capability 位继续保留，但在本版本中：

- 本机不得宣告 STREAM 可用。
- 收到仅支持 STREAM 的提议必须明确返回“不支持”，不能静默降级或进入半连接状态。
- 代码边界不得把 DUAL 输入通道与未来媒体通道耦合，后续可添加媒体数据面而无需重写配对、身份、QUIC 绑定和大厅状态机。

DUAL MVP 的非真机完成标准是：

1. 两个公开 `fly_session_v2_t` 实例能够通过真实 loopback QUIC 完成创建/加入、SAS、ChannelBind、双向 `LINK_HELLO` 和 `LINK_READY`，进入同一个空大厅。
2. 两端具备相同 ROM 时，能够锁定游戏配置、分配座位、开始 DUAL 会话、双向传递输入并得到一致的 canonical frame/state 摘要。
3. 暂停、退出、断链、超时、取消和旧 generation 回调不会把会话错误推进；断链必须冻结，不能自动变成单人游戏。
4. Android 单元测试及可用模拟器测试、HarmonyOS host/Hypium 及可用模拟器测试、iOS 可用 Mac 模拟器测试均有明确结果。当前没有环境的项目记为 `NOT_RUN`，不伪装为通过。
5. 真机安装、物理时延、功耗、温度、刷新率和九方向设备组合全部延期，等后端完成后统一执行。

三份获批后端文档中与 STREAM 专属媒体面相关的 B08/B09、MEDIA、STREAM 性能门禁本轮延期；配对、身份、链接、DUAL 输入、恢复、内容许可、UI 投影及非真机测试仍在范围内。

## 2. 截至 2026-09-15 的实际完成情况

以下状态以当前 `.worktrees/nearby-ui-acceptance-fixes` 工作目录为准。这里的“完成”表示已有生产代码、针对性测试和至少一次通过证据；“部分完成”不会用于宣称端到端可用。

三份 2026-09-13 获批后端文档当前只存在于主工作区 `E:/workspace/codes/games/fly-little-games/docs/superpowers/specs/`，且在主工作区仍为 untracked；当前 W0 worktree 内没有这些文件。建立基线时必须原样纳入 W0 并校验 SHA-256，不能让并行分支各读不同副本。

| 能力 | 状态 | 当前证据 | 仍缺什么 |
|---|---|---|---|
| 三端附近联机 UI 验收修复 | 部分完成 | shared/Android/Harmony/iOS 的页面、文案、动作和布局已有修改及自动化覆盖 | 真机按用户要求延期；平台 UI 仍需最终接入真实 engine snapshot/action 后重验 |
| Discovery/Bearer/ObjectStore 公共 ABI | 完成 | size-gated provider table、retain/release/cancel、ABI/缺函数/事件 payload 合同测试已实现 | 平台真实 adapter 尚未实现完 |
| typed provider event 与 operation fencing | 完成 | payload parser、token/scope/generation/operation 检查、BufferHandle 所有权测试存在 | 仍需在后续每个新增 operation 上复用并补 race 场景 |
| Quinn/rustls 产品 wrapper | 组件完成 | Rust wrapper、TLS 1.3/ALPN/pin/exporter/stream/datagram/cancel 测试此前通过 | 尚未形成两个公开 engine 之间的完整产品 E2E |
| PairContext→commit/reveal→签名→SAS→known/key-confirm→capability | 大部分完成 | exact codec、P-256/AEAD/HMAC、多个 scheduler 和公开 engine 单端路径已有测试 | initiator/responder 全公开双端闭环及完整负例矩阵仍未完成 |
| PairTranscriptV1 `0x0213` | 完成 | exact 880 bytes、角色顺序签名、object hash、persist-before-SAS/KEY_CONFIRM 测试；此前 focused 与全量回归通过 | 恢复时读取原对象的完整路径仍属恢复工作流 |
| SessionSigningKeyBindingV1 `0x0212` | 正在收尾 | exact 312 bytes codec；SecureStore KeyRef；ObjectStore 持久化 scheduler focused 测试于 2026-09-15 为 1/1 PASS | 公共 engine 大场景需补第二次 ObjectStore completion；最新变更后尚未重新跑 84 项全量回归 |
| bearer plan、credential、endpoint、QUIC ChannelBind | 大部分完成 | exact plan/endpoint/ChannelBind codec 和 scheduler；单端公开 engine 测试能推进到 ChannelBind 后创建 session signing key | 尚未由两个真实公开 engine 相互驱动；失败/重放矩阵未完全覆盖 |
| `LINK_HELLO` / `LINK_READY` | 未实现 | 现有 prebind gate 能表达“双方 READY 才开放” | 获批文档只冻结语义，没有冻结完整 exact byte layout；必须先完成 W0 合同冻结 |
| 真正的双公开 engine 空大厅 | 未完成 | 当前名为 `test_two_engine_empty_lobby.cpp` 的测试实际是单个公开 engine 加模拟 peer 输入 | 必须由两个 `fly_session_v2_t`、真实 serialization 和 loopback QUIC 完成 |
| DUAL 游戏数据面 | 未实现 | 老 schema/golden 中已有部分 input/state 对象，尚无本轮生产 scheduler 接线 | 游戏配置、座位、输入预留/传输、frame/state 一致性、冻结/退出均需实现 |
| 内容许可与 ROM 传输 | 未实现 | 设计与测试要求已写明 | 独立低优先级可靠 stream、用户授权、hash/格式校验和导入流程 |
| 崩溃恢复、换局、handoff/ledger | 未实现 | 设计文档有完整 REC01–REC09 要求 | ObjectStore root、WAL、sequence ledger、尾链修复和双端归并 |
| Android/HarmonyOS/iOS 安全、发现与 bearer adapter | 未完成 | 已有 UI shell、部分 native bridge/构建改动 | 各平台真实 provider 和非真机合同测试；物理 radio/path 证据延期 |
| STREAM | 明确延期 | capability 扩展位保留 | 不进入本轮实现与通过声明 |

最近一次完整 shared 基线曾达到 **84/84 PASS**；该结果发生在本表所述最新 `0x0212` ObjectStore 改动之前。当前只能准确声明最新 scheduler focused test **1/1 PASS**，必须完成 W0 波次 0 后才能建立新的 84/84 基线。

## 3. 为什么不能简单按平台拆

目前未完成工作的关键路径集中在共享 C++ 协议和状态机。若直接按 Android、HarmonyOS、iOS 分三路，每路都会重复推断协议语义，随后又要在 `flynes_session.h` 和 `SessionEngine` 上合并，返工风险高于并行收益。

采用“一个集成主线 + 三个独立实现 worktree”的结构：

- 集成主线独占公共 ABI、顶层状态机和集中式构建注册。
- 三个工作 worktree 只实现边界清晰的新组件和各自测试。
- 平台 worktree 等共享合同稳定后再启动。

这让第一波真正并行，并把高冲突文件留给单一所有者。

## 4. Worktree 与文件所有权

### 4.1 集成主线 W0

**现有 worktree：** `.worktrees/nearby-ui-acceptance-fixes`  
**现有分支：** `codex/nearby-ui-acceptance-fixes`

W0 永久独占以下文件；其他 worktree 如需改动，只提交“集成请求说明”和针对独立组件的测试，不直接编辑：

- `shared/include/flynes/flynes_session.h`
- `shared/src/session/engine/session_engine.hpp`
- `shared/src/session/engine/session_engine.cpp`
- `shared/src/session/flynes_session_v2.cpp`
- `shared/src/session/ports/session_ports.hpp`
- `shared/src/session/ports/provider_events.cpp`
- `shared/CMakeLists.txt` 及集中式测试注册文件
- `shared/schema/` 与三端 schema/golden 镜像清单
- `VERSION`、`VERSION_MAJOR` 和各平台版本元数据

W0 负责合同冻结、组件接线、冲突解决、跨分支回归和最终证据。

### 4.2 链路控制 W1

**建议分支：** `codex/nearby-dual-link-control`  
**建议目录：** `.worktrees/nearby-dual-link-control`

独占职责：

- 精确编码/解码 `LINK_HELLO`、`LINK_READY` 和必要的 ACK。
- 验证 PairTranscript、SessionSigningKeyBinding、角色、session/link/channel/generation 和 capability。
- 只有双方 HELLO 与 READY 经过认证且完全匹配时才给出 `connected_lobby` 结果。
- 重放、错角色、错 generation、错 object hash、未知 critical capability 全部 fail-closed。

建议拥有的新文件：

- `shared/src/session/wire/link_hello.hpp`
- `shared/src/session/wire/link_hello.cpp`
- `shared/src/session/wire/link_ready.hpp`
- `shared/src/session/wire/link_ready.cpp`
- `shared/src/session/link/link_handshake_scheduler.hpp`
- `shared/src/session/link/link_handshake_scheduler.cpp`
- `shared/tests/nearby/protocol/test_link_hello.cpp`
- `shared/tests/nearby/protocol/test_link_ready.cpp`
- `shared/tests/nearby/integration/test_link_handshake_scheduler.cpp`

交付边界：W1 返回纯组件和测试，不直接把它接入 `SessionEngine`。

### 4.3 DUAL 数据面 W2

**建议分支：** `codex/nearby-dual-runtime`  
**建议目录：** `.worktrees/nearby-dual-runtime`

独占职责：

- canonical input bundle、输入序号、帧号和座位归属。
- 双端输入合并、缺失输入的有界等待、重复/乱序拒绝。
- canonical state/frame digest 对比，以及不一致时冻结而非继续漂移。
- 暂停、退出和断线时停止接受新运行输入。
- 数据面接口保留独立通道编号，使未来 STREAM 媒体通道可追加而不修改 DUAL 编码。

建议拥有的新文件：

- `shared/src/session/dual/canonical_input.hpp`
- `shared/src/session/dual/canonical_input.cpp`
- `shared/src/session/dual/dual_run_scheduler.hpp`
- `shared/src/session/dual/dual_run_scheduler.cpp`
- `shared/src/session/dual/dual_state_digest.hpp`
- `shared/src/session/dual/dual_state_digest.cpp`
- `shared/tests/nearby/protocol/test_canonical_input.cpp`
- `shared/tests/nearby/integration/test_dual_run_scheduler.cpp`
- `shared/tests/nearby/race/test_dual_run_races.cpp`

交付边界：W2 只依赖冻结的 wire/port 合同；不修改平台 UI 或 `SessionEngine`。

### 4.4 双引擎验收 W3

**建议分支：** `codex/nearby-dual-e2e-harness`  
**建议目录：** `.worktrees/nearby-dual-e2e-harness`

独占职责：

- 从当前单引擎模拟对端测试升级为两个真实公开 engine 实例。
- 建立确定性 executor、双向 Discovery/GATT 转发、真实 loopback Quinn QUIC 和 crashable stores。
- 提供按 token/generation 路由的受控 Key/Crypto/TLS/SecureStore/ObjectStore provider。
- 建立 create/join、SAS、空大厅、DUAL 开局、双方输入、暂停/退出、断链恢复的场景 oracle。
- 注入取消、超时、乱序、重复、晚到回调、损坏 hash、单边 READY 和 provider 失败。

建议拥有的新文件：

- `shared/tests/nearby/harness/two_engine_fixture.hpp`
- `shared/tests/nearby/harness/two_engine_fixture.cpp`
- `shared/tests/nearby/harness/loopback_quic_fixture.hpp`
- `shared/tests/nearby/harness/loopback_quic_fixture.cpp`
- `shared/tests/nearby/scenarios/test_two_engine_connected_lobby.cpp`
- `shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp`
- `shared/tests/nearby/scenarios/test_two_engine_recovery.cpp`

W3 可以先针对组件接口编译；最终公开 engine 接线断言由 W0 合并后启用。W3 不修改现有巨型 `test_two_engine_empty_lobby.cpp`，避免与 W0 冲突。

## 5. 依赖图与执行波次

| 波次 | 可并行工作 | 前置条件 | 汇合门禁 |
|---|---|---|---|
| 0 | W0 稳定当前工作成果 | 完成 SessionSigningKeyBinding 对象持久化；全量 shared 测试通过 | 形成所有 worktree 共用的基线提交 SHA |
| 1A | W1 精确 HELLO/READY；W2 DUAL 纯数据面；W3 双引擎夹具骨架 | W0 冻结所需内部接口和 exact wire 字段 | 各分支独立测试全绿且未改 W0 独占文件 |
| 1B | W0 依次集成 W1、W2、W3 | 各分支交付可审查提交 | 两个公开 engine 完成 loopback 空大厅和 DUAL 运行 |
| 2 | Android、HarmonyOS、iOS 分平台 worktree | 公共 ABI 和 UI projection 冻结 | 各平台非真机测试通过或明确 NOT_RUN |
| 3 | 内容许可/传输、恢复矩阵和验收证据补齐 | DUAL MVP 主路径稳定 | 三份设计文档除 STREAM 与真机条目外完成追踪 |

第一波可同时占用三个工作 worker，W0 留给集成者。任何依赖 W0 尚未冻结字段的工作不得自行发明 ABI；应输出最小接口请求，由 W0 统一落地后再继续。

## 6. 基线建立与 worktree 创建

当前 W0 包含大量未提交成果。Git worktree 只能从提交继承，所以并行前必须完成以下顺序：

1. 完成正在进行的 SessionSigningKeyBinding `0x0212` ObjectStore 持久化红绿循环。
2. 运行 shared 全量构建和 CTest，保留失败/通过输出。
3. 审核 `git diff`，区分本轮附近联机变更与用户已有的其他修改；不得清理、stash 或覆盖用户文件。
4. 把可归属的附近联机成果形成基线提交，记录 SHA 为 `$baseCommit`。
5. 只使用仓库分配器创建 worktree，不直接运行 `git worktree add`：

```powershell
$baseCommit = (& git rev-parse HEAD).Trim()
if ((& git status --porcelain).Count -ne 0) {
  throw '共同基线提交后工作区仍有未审核修改，停止创建 worktree。'
}
.\tools\versioning\New-VersionedWorktree.ps1 `
  -Path .worktrees/nearby-dual-link-control `
  -Branch codex/nearby-dual-link-control `
  -StartPoint $baseCommit
.\tools\versioning\New-VersionedWorktree.ps1 `
  -Path .worktrees/nearby-dual-runtime `
  -Branch codex/nearby-dual-runtime `
  -StartPoint $baseCommit
.\tools\versioning\New-VersionedWorktree.ps1 `
  -Path .worktrees/nearby-dual-e2e-harness `
  -Branch codex/nearby-dual-e2e-harness `
  -StartPoint $baseCommit
```

`$baseCommit` 必须在共同基线提交和全量测试完成后取得；不能在当前 dirty 状态提前运行这段命令。

每个新 worktree 会获得独立 MINOR 版本线。合并回 W0 时，功能提交与版本文件冲突分开处理：保留 W0 的目标版本，通过 `tools/versioning/Sync-Version.ps1` 重新同步元数据，不手改 `versionName` 或 `versionCode`，也不改变受保护 major。

## 7. 每个 worker 的统一交付合同

每个 worker 必须：

1. 先提交失败测试和失败证据，再提交最小实现。
2. 只修改分配给自己的文件；确需改 W0 独占文件时，提交一份包含符号名、调用方向和测试需求的集成说明。
3. 每个 provider 操作都保持 token、scope、generation、payload kind 和所有权检查。
4. 对取消、失败、重复和晚到完成事件给出测试，不能只覆盖 happy path。
5. 不添加 STREAM 媒体实现，不宣告 STREAM capability。
6. 返回提交 SHA、修改文件列表、运行命令、实际结果和仍未验证事项。

禁止 worker 通过复制旧测试中的伪对端字节来宣称双引擎端到端完成。最终 E2E 必须由两个公开 engine、真实序列化边界和真实 loopback QUIC 产生。

## 8. 集成与合并顺序

W0 按以下顺序集成：

1. W1 wire codec 与 link scheduler。
2. W0 将 W1 接入 `SessionEngine`，验证空大厅双引擎路径。
3. W2 canonical input 与 DUAL scheduler。
4. W0 将 W2 接入公开 action/snapshot/runtime 边界。
5. W3 harness 与场景测试。
6. W0 补齐集中式 CMake、schema/golden 和 ABI 测试。
7. 运行 shared 全量回归，再启动第二波平台 worktree。

每次合并只处理一个分支。出现冲突时由 W0 根据冻结合同重写胶水，不允许把冲突标记整体选择为某一侧。合并后的全量测试失败必须先恢复绿色，再接收下一个分支。

## 9. 第二波平台拆分

共享公开 ABI、投影视图和 action 语义冻结后，可以新建三个 versioned worktree：

| 分支 | 独占目录 | 非真机验收 |
|---|---|---|
| `codex/nearby-dual-android` | `app/` | Android unit、assembleDebug、可用 emulator instrumentation |
| `codex/nearby-dual-harmony` | `harmony/` | host CTest、Hypium、可用 emulator；签名真机延期 |
| `codex/nearby-dual-ios` | `ios/` | Mac 上 RuntimeTests/UITests/simulator；无 Mac 时记 NOT_RUN |

平台分支不得复制 shared 协议实现；只实现 provider adapter、native bridge、UI action 和 snapshot projection。平台需要的新公共能力先由 W0 增加到 ABI，再同步三个分支。

## 10. 测试与验收命令

W1/W2/W3 每次提交至少运行自己的 focused target。合并到 W0 后执行：

```powershell
cmake --build build --config Debug --parallel 2
ctest --test-dir build -C Debug --output-on-failure
.\gradlew.bat :app:testDebugUnitTest
```

集中门禁还必须确认 `nearby_` 标签真实匹配测试而不是 0 tests：

```powershell
ctest --test-dir build -C Debug --output-on-failure -L nearby_
```

Android UI、HarmonyOS 和 iOS 的具体命令沿用 `docs/DEVELOPMENT.md`。证据必须区分 `PASS`、`FAIL`、环境导致的 `BLOCKED` 和没有执行的 `NOT_RUN`。真机项统一记录为“按用户要求延期”，不影响本轮非真机代码推进，但不能写成设备认证通过。

## 11. 里程碑与粗略工期

以下是基于现有代码量的墙钟时间估算，不是承诺日期：

| 里程碑 | 并行后估算 | 主要不确定性 |
|---|---:|---|
| 建立可分支基线 | 0.5–1 个工作日 | 当前 dirty diff 的归属和回归 |
| 空大厅双引擎认证完成 | 1.5–3 个工作日 | HELLO/READY exact wire 冻结与真实 Quinn 对接 |
| DUAL 双向输入基本运行 | 2–4 个工作日 | emulator runtime 接口、帧/状态确定性 |
| 非真机 DUAL MVP 集成验收 | 4–7 个工作日 | 跨分支合并、平台环境可用性 |
| 三文档除 STREAM/真机外的剩余矩阵 | 7–12 个工作日 | 内容传输、崩溃恢复和三端 adapter 完整度 |

相比单线推进，预期可把可独立组件阶段缩短约 35%–50%；顶层状态机接线、真实双引擎验收和跨平台合同仍必须串行收口。

## 12. 风险控制

- **未提交基线风险：** 不从工作目录复制文件到新 worktree；先验证并提交共同基线。
- **中心文件冲突：** ABI、engine、CMake、schema 仅 W0 修改。
- **协议漂移：** exact bytes、domain、object kind 和 hash 先由 W0 冻结，worker 不自行增加近似版本。
- **测试假阳性：** 单引擎模拟对端只算组件测试，不能替代两个公开 engine 的 E2E。
- **范围膨胀：** STREAM 专属实现保持延期；未知 STREAM capability 明确拒绝。
- **版本冲突：** 只用版本脚本创建 worktree；合并时保留集成线版本并重新同步。
- **用户数据风险：** 不 clean、reset、stash、移动或删除现有未跟踪证据和其他 worktree 文件。
- **资源预算：** 每个新批次开始前检查套餐余量；低于 25% 时立即停止所有运行中和排队工作。

## 13. 下一步

文档获复核后，先由 W0 完成波次 0。基线提交可用后创建 W1/W2/W3 三个 versioned worktree，并为每个 worktree 分别生成可独立执行的 TDD 实施计划。只有在三份计划通过文件所有权冲突检查后才同时启动 worker。
