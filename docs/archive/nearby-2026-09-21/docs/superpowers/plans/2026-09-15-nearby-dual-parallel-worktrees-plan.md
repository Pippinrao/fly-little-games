> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby DUAL Parallel Worktrees Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 在不实现 STREAM、暂不做真机认证的前提下，通过一个集成主线和多个互不争抢中心文件的 worktree，先交付可运行的 DUAL 双人联机 MVP，再补齐三份获批文档中其余非 STREAM、非真机范围。

**Architecture:** W0 独占公共 ABI、`SessionEngine`、schema 和集中式构建；W1 交付 LINK_HELLO/READY 纯组件，W2 交付 DUAL 数据面纯组件，W3 交付真正的双公开 engine 测试夹具。共享合同稳定后，再按 Android、HarmonyOS、iOS 和内容/恢复领域建立第二、三波 worktree。

**Tech Stack:** C++17、C ABI V2、CTest、Rust Quinn 0.11/rustls 0.23、Android Java/JNI/Gradle、HarmonyOS ArkTS/N-API/Hypium、iOS Swift/ObjC++/XCTest。

---

## 0. 执行约束与状态基准

详细现状见 `docs/superpowers/specs/2026-09-15-nearby-dual-parallel-worktree-design.md` 第 2 节。执行者必须以以下事实开始：

- 当前 worktree 有大量未提交/未跟踪的附近联机实现，不存在可供新 worktree 继承的最新提交 SHA。
- 三份获批后端文档当前只在主工作区 `E:/workspace/codes/games/fly-little-games/docs/superpowers/specs/` 中存在并处于 untracked 状态；W0 worktree 内目前没有这三份文件。
- 此前完整 shared 回归曾为 84/84 PASS，但发生在最新 `SessionSigningKeyBindingV1(0x0212)` ObjectStore 改动之前。
- 最新可复核证据只有 `flynes_session_signing_scheduler` 1/1 PASS。
- `test_two_engine_empty_lobby.cpp` 当前并不是真正的两公开 engine 测试。
- `LINK_HELLO`、`LINK_READY` 和 DUAL 运行状态机尚未落地。
- STREAM 与全部真机认证按用户要求延期。

以下任何一项发生时不得宣称 MVP 完成：

- 仅有 codec/scheduler 单测，没有两个公开 engine 的 E2E。
- 由测试直接注入 `authenticated=true`、verified evidence 或 peer 已验证结果。
- 只建立 socket/QUIC，却没有完成双方 ChannelBind、HELLO 和 READY。
- 只传输入但未证明两端 canonical frame/state 一致。
- 断链后任一端继续独立 step、保存原 multiplayer branch 或自动切单人。
- 用模拟器结果代替硬件安全、真实 Bluetooth/Wi-Fi、物理延迟、功耗或温度认证。

## 1. Worktree 启动顺序

| 顺序 | 分支 | 能否并行启动 | 依赖 |
|---|---|---|---|
| 1 | W0 `codex/nearby-ui-acceptance-fixes` | 串行 | 当前工作目录 |
| 2 | W1 `codex/nearby-dual-link-control` | 与 W2/W3 并行 | Task 1、Task 2 的共同基线 SHA |
| 2 | W2 `codex/nearby-dual-runtime` | 与 W1/W3 并行 | Task 1、Task 2 的共同基线 SHA |
| 2 | W3 `codex/nearby-dual-e2e-harness` | 与 W1/W2 并行 | Task 1、Task 2 的共同基线 SHA |
| 3 | W0 集成 | 串行合并 W1→W2→W3 | 三分支各自绿色 |
| 4 | Android/HarmonyOS/iOS 平台分支 | 三端互相并行 | DUAL MVP 公共 ABI 冻结 |
| 5 | content/recovery/acceptance 分支 | 按依赖并行 | DUAL MVP 已集成 |

新 worktree 只能通过 `tools/versioning/New-VersionedWorktree.ps1` 创建。W0 没有交付共同基线 SHA 前，不启动 W1/W2/W3。

## Task 1（W0）：收尾当前 `0x0212` 持久化并恢复全量绿色 — ✅ 已完成，见 §19

**当前状态：** scheduler 生产代码和 focused 测试已经绿色；公开 engine 测试与全量回归未完成。

**Files:**

- Modify: `shared/tests/nearby/integration/test_two_engine_empty_lobby.cpp`
- Verify: `shared/src/session/link/session_signing_scheduler.hpp`
- Verify: `shared/src/session/link/session_signing_scheduler.cpp`
- Verify: `shared/src/session/engine/session_engine.cpp`
- Verify: `shared/src/session/wire/session_signing_binding.hpp`

- [ ] **Step 1: 补公开 engine 的失败断言**

在 SecureStore 返回 revision 后断言 link 仍为 `CONNECTING`，并检查紧随其后的 ObjectStore 请求：

- `object_kind == 0x0212`
- `value.size() == 312`
- `expected_hash == SHA256("flynes-session-signing-key-binding-hash-v1" || u32be(312) || exact_binding)`，并逐值等于 codec 产生的 binding hash
- SecureStore revision 单独不能使 scheduler ready

- [ ] **Step 2: 投递正确 ObjectStore 完成事件**

事件必须使用 `FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2`、非零 resource handle 和完全相同的 expected hash。完成后仍保持 `CONNECTING`，原因只能是等待 peer HELLO/READY，而不是本地材料未持久化。

- [ ] **Step 3: 增加错误完成负例**

分别投递零 resource、错误 hash、错误 payload kind、旧 token 和旧 generation。每个负例必须 fail-closed，不能发 LINK_HELLO、不能进入 connected lobby，并且取消走 ObjectStore port 而不是 Key/SecureStore port。

- [ ] **Step 4: 构建 focused targets**

```powershell
$cmakeBin = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestBin = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\ctest.exe'
& $cmakeBin --build out/acceptance-fix-baseline/shared/build --config Debug `
  --target flynes_session_signing_scheduler_test flynes_two_engine_empty_lobby_test --parallel 2
& $ctestBin --test-dir out/acceptance-fix-baseline/shared/build -C Debug `
  --output-on-failure -R '^(flynes_session_signing_scheduler|flynes_two_engine_empty_lobby)$'
```

Expected: 2/2 PASS。

- [ ] **Step 5: 运行最新 shared 全量回归**

```powershell
& $cmakeBin --build out/acceptance-fix-baseline/shared/build --config Debug --parallel 2
& $ctestBin --test-dir out/acceptance-fix-baseline/shared/build -C Debug --output-on-failure
```

Expected: 当前注册的全部测试通过；按现有目录应为 84/84，若 CMake 合法新增测试则以新总数为准且 0 failure。

**Acceptance `BASE-0212`:** exact 312-byte binding 同时在 SecureStore 记录 KeyRef、在 ObjectStore 注册 `0x0212`；两个 durability gate 缺一不可继续。完整 shared 回归为 0 failure。

## Task 2（W0）：冻结并提交并行共同合同 — ✅ 已完成，见 §19

**Purpose:** W1/W2/W3 不得各自发明 HELLO/READY 或 DUAL seam。共同基线必须先冻结这些边界。

**Files:**

- Create from reviewed source bytes: `docs/superpowers/specs/2026-09-13-nearby-backend-repair-design.md`
- Create from reviewed source bytes: `docs/superpowers/specs/2026-09-13-nearby-interface-design.md`
- Create from reviewed source bytes: `docs/superpowers/specs/2026-09-13-nearby-test-design.md`
- Create: `shared/src/session/link/link_control_contract.hpp`
- Create: `shared/src/session/dual/dual_runtime_contract.hpp`
- Test: `shared/tests/nearby/contract/test_link_control_contract.cpp`
- Test: `shared/tests/nearby/contract/test_dual_runtime_contract.cpp`
- Modify: `shared/CMakeLists.txt`

- [ ] **Step 1: 把三份获批文档原样纳入 W0**

从主工作区 `E:/workspace/codes/games/fly-little-games/docs/superpowers/specs/` 读取三份 2026-09-13 文档，在 W0 的同名目录创建逐字节一致副本。复制前后分别运行 `Get-FileHash -Algorithm SHA256`，三对 hash 必须完全相同；主工作区原文件不得移动、删除或改名。

- [ ] **Step 2: 冻结 LINK_HELLO exact layout**

合同必须明确 version、reserved-zero、session/link/channel/generation、sender/receiver PairRole、PairTranscript hash、exact `0x0212` binding hash/ref、DUAL capability、协议版本和 critical-extension 处理。每个整数注明大端编码，每个对象注明 exact size、object kind、hash domain 和签名 domain。

- [ ] **Step 3: 冻结 LINK_READY/ACK exact layout**

READY 必须绑定双方已验证 HELLO hash、当前 ChannelBind hash/channel ID、协商结果 hash、当前 connection generation、sender/receiver role 和 phase。明确双方各自何时 durable、何时 send、何时 ACK、何时允许投影 `CONNECTED_LOBBY`。

- [ ] **Step 4: 冻结 DUAL 内部 seam**

至少定义：

- `DualRuntimePort::load/step/export_state/import_state/state_digest`
- 输入单位为四端口完整 mask，不传 UI 原始触控事件
- key 为 `(session_id, branch_id, timeline_epoch, frame_index, seat_revision)`
- mode 仅允许 DUAL；STREAM capability false
- 断链/暂停后禁止新的 `step`

- [ ] **Step 5: 写合同编译测试**

合同测试必须静态检查 enum 数值、结构大小、reserved 字段和 callback 签名；运行测试覆盖 STREAM capability=false、未知 critical bit 拒绝、checked monotonic generation/sequence 溢出拒绝。

- [ ] **Step 6: 完成共同基线提交**

先运行 Task 1 全量测试，再通过仓库 hook 正常提交。记录提交 SHA。不得把无法归属的用户改动一起提交；若中心文件与用户改动重叠，先逐块审核 staged diff。

**Acceptance `BASE-PARALLEL`:** 三份需求源文件已在 W0 受版本控制且初始 SHA-256 与获批源一致；有一个真实共同基线提交 SHA，W1/W2/W3 都从该 SHA 创建；合同文档不存在“拟议但未冻结”的 HELLO/READY 字段；shared 全量 0 failure。

## Task 3（W1）：实现 LINK_HELLO / LINK_READY exact wire codec

**Files:**

- Create: `shared/src/session/wire/link_hello.hpp`
- Create: `shared/src/session/wire/link_hello.cpp`
- Create: `shared/src/session/wire/link_ready.hpp`
- Create: `shared/src/session/wire/link_ready.cpp`
- Test: `shared/tests/nearby/protocol/test_link_hello.cpp`
- Test: `shared/tests/nearby/protocol/test_link_ready.cpp`

- [ ] **Step 1: 先写 legal golden 的失败测试**

分别构造 initiator/responder 固定向量；expected bytes 和 expected hash 必须由测试常量提供，不得调用被测 encoder 生成 expected。

- [ ] **Step 2: 写逐字段负例**

覆盖 truncate、trailing、reserved 非零、unknown enum、错 role、反射、错 session/link/channel/generation、错 PairTranscript hash、错 binding object kind/hash/ref、STREAM-only capability、unknown critical capability。

- [ ] **Step 3: 实现 codec**

codec 只负责 exact bytes、hash、字段合法性和 immutable decoded value；不启动 provider、不访问 UI、不修改 `SessionEngine`。

- [ ] **Step 4: 增加签名/身份验证向量**

HELLO 中的 `SessionSigningKeyBindingV1` 必须先用 PairTranscript 中对应角色的长期 identity public key 验证；随后普通 HELLO/READY 控制签名只能使用该 binding 认证的 session signing public key。double-hash、高 S、错 purpose 和交换角色均拒绝。

- [ ] **Step 5: focused 验收**

Expected: W1 的 protocol tests 全绿；ASan/TSan 仅在受支持环境运行，普通 Windows build 不冒充 sanitizer 证据。

**Acceptance `LINK-WIRE`:** 两端使用同一 exact codec；未知/错误字段无副作用；不包含 STREAM 媒体字段。

## Task 4（W1）：实现 link handshake scheduler

**Files:**

- Create: `shared/src/session/link/link_handshake_scheduler.hpp`
- Create: `shared/src/session/link/link_handshake_scheduler.cpp`
- Test: `shared/tests/nearby/integration/test_link_handshake_scheduler.cpp`
- Test: `shared/tests/nearby/race/test_link_handshake_races.cpp`

- [ ] **Step 1: 写状态序列 RED 测试**

合法序列固定为：本地 binding durable → send HELLO → receive/verify peer HELLO → durable negotiated result → send READY → receive/verify peer READY → durable local ready → send/receive required ACK → `connected_lobby=true`。

- [ ] **Step 2: 写 persist-before-send 断言**

HELLO、协商结果、READY 的本机 durable 写失败时不得 send；对端消息必须先验证和 durable 再 ACK。

- [ ] **Step 3: 写 race/取消矩阵**

覆盖单边 READY、READY 先于 HELLO、旧 generation、重复相同 bytes、相同 token 不同 bytes、取消后晚到、QUIC terminal、shutdown、ObjectStore/SecureStore failure。旧资源允许释放但不得推进新 link。

- [ ] **Step 4: 实现纯 scheduler**

scheduler 通过 effect/value 驱动，不直接持有平台对象；每个 effect 使用 monotonic operation ID 和期望 payload kind。

**Acceptance `LINK-SCHED`:** 组件只有在双方身份、能力、ChannelBind 和 READY/ACK 全部闭合时返回 connected；所有失败路径可重复运行且无资源泄漏。

## Task 5（W2）：实现 canonical DUAL 输入与序号合同

**Files:**

- Create: `shared/src/session/dual/canonical_input.hpp`
- Create: `shared/src/session/dual/canonical_input.cpp`
- Create: `shared/src/session/dual/input_window.hpp`
- Create: `shared/src/session/dual/input_window.cpp`
- Test: `shared/tests/nearby/protocol/test_canonical_input.cpp`
- Test: `shared/tests/nearby/protocol/test_input_window.cpp`

- [ ] **Step 1: 写 exact input golden RED 测试**

输入必须绑定 session/branch/term/epoch/frame、全局 seat revision、logical seat、owner session signing key、单调 input sequence 和规范化完整 mask。上下/左右冲突在发送前归零为中立。

- [ ] **Step 2: 写权限和重放负例**

拒绝错 seat owner、旧 seat revision、跨 branch/epoch、sequence=0、sequence 回退、相同 sequence 不同 bytes、已 committed 历史改写和 checked overflow。重复相同 bytes 只能幂等，不重复应用。

- [ ] **Step 3: 实现有界输入窗口**

按获批规范支持最多 12 帧回滚；缺失输入预测为该 seat 最后已知完整状态；超过窗口的未来包、太旧包和越过 committed 水位的冲突包拒绝。

**Acceptance `DUAL-INPUT`:** 对任意消息到达顺序，两端生成相同 canonical bundle；权限、版本或序号错误不会进入 runtime。

## Task 6（W2）：实现 DUAL simulation scheduler 与一致性门禁

**Files:**

- Create: `shared/src/session/dual/dual_run_scheduler.hpp`
- Create: `shared/src/session/dual/dual_run_scheduler.cpp`
- Create: `shared/src/session/dual/dual_state_digest.hpp`
- Create: `shared/src/session/dual/dual_state_digest.cpp`
- Test: `shared/tests/nearby/integration/test_dual_run_scheduler.cpp`
- Test: `shared/tests/nearby/race/test_dual_run_races.cpp`

- [ ] **Step 1: 写双副本确定性 RED 测试**

两个独立 fake runtime 从同一初始 state 开始，交换双方输入并推进至少 600 帧。每 60 帧比较相同 committed frame 的 state digest，并证明 `state_verified_through` 只在匹配时前进。

- [ ] **Step 2: 写 rollback 测试**

在 1–12 帧内迟到远端真实输入时回滚并重放；预测帧不作为最终 canonical 成功依据。迟到输入改变已 committed 历史必须冻结并报协议/失步错误。

- [ ] **Step 3: 写生命周期冻结测试**

暂停、surface/audio/network 不可用、300 ms 无认证活动、QUIC terminal、shutdown 后都不得继续 step、present 或接受新用户输入。恢复只能从双方确认的安全点继续。

- [ ] **Step 4: 写失步测试**

digest 不一致时双方在 committed actual frame 冻结。首版 STREAM 不可用，因此不得自动切换；允许动作只有重同步、重连、保存结束或明确失败。

**Acceptance `DUAL-RUN`:** 600 帧正常 trace 的 frame/state digest 全等；12 帧内回滚收敛；失步和断链均 fail-frozen；未实现 STREAM 路径不可被选择。

## Task 7（W3）：建立真正的 two-engine harness

**Files:**

- Create: `shared/tests/nearby/harness/two_engine_fixture.hpp`
- Create: `shared/tests/nearby/harness/two_engine_fixture.cpp`
- Create: `shared/tests/nearby/harness/loopback_quic_fixture.hpp`
- Create: `shared/tests/nearby/harness/loopback_quic_fixture.cpp`
- Create: `shared/tests/nearby/harness/crashable_object_store.hpp`
- Create: `shared/tests/nearby/harness/crashable_secure_store.hpp`

- [ ] **Step 1: 创建两个公开 engine**

fixture 必须分别调用 `fly_session_create_v2`，提供不同 engine instance ID、独立 provider context、独立持久存储和独立资源句柄空间。

- [ ] **Step 2: 转发真实边界数据**

Discovery/GATT 只转发已编码 raw fragments；QUIC 使用产品 Quinn wrapper 的 loopback 连接；测试不得直接调用对方 reducer 或注入 verified evidence。

- [ ] **Step 3: 建确定性调度器**

支持逐事件运行、指定端暂停、事件重排、复制、丢弃、取消、时钟推进和进程重建。每个回调保留原 token/scope/generation。

- [ ] **Step 4: 建资源泄漏 oracle**

结束时检查 buffer retain/release、key/secret/material/stream/path handle、inbox/context 和 pending operations 全部平衡；stale completion 只能回收旧资源。

**Acceptance `E2E-HARNESS`:** fixture 能证明消息确实跨越 public ABI、wire serialization 和真实 loopback QUIC；关闭两个 engine 后资源计数归零。

## Task 8（W3）：编写双 engine 场景与独立 oracle

**Files:**

- Create: `shared/tests/nearby/scenarios/test_two_engine_connected_lobby.cpp`
- Create: `shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp`
- Create: `shared/tests/nearby/scenarios/test_two_engine_recovery.cpp`

- [ ] **Step 1: 空大厅正向场景**

创建邀请、输入 6 位码、匿名 host 接受、双方 SAS 确认、plan/bearer/QUIC/ChannelBind/HELLO/READY 完成；没有 ROM 也必须进入 `CONNECTED_LOBBY`。

- [ ] **Step 2: 空大厅认证负例**

对每一层分别篡改 role、generation、commit/reveal、pair signature、SAS、key-confirm、capability、credential、TLS pin/exporter、ChannelBind、binding、HELLO 和 READY。任一例不得进入大厅。

- [ ] **Step 3: DUAL 正向场景**

加载公开测试 ROM fixture 或可重复 deterministic runtime fixture，双方确认 authority/P1/P2/DUAL，至少运行 600 帧；P1 和 P2 各产生至少 100 个可辨认 input edge，两端最终 frame/state/input root 相同。

- [ ] **Step 4: DUAL 故障场景**

覆盖重复/乱序/丢包、12 帧内迟到、越窗迟到、digest mismatch、300 ms 无认证活动、2 秒链路中断、暂停和退出。所有故障必须进入文档指定的冻结/恢复/结束状态，不能自动单人或 STREAM。

**Acceptance `E2E-SCENARIOS`:** 两个公开 engine 语义状态序列一致；正向路径完成；所有篡改点 fail-closed；无 injected verified evidence。

## Task 9（W0）：集成 W1，完成双 engine 空大厅

**Files:**

- Modify: `shared/src/session/engine/session_engine.hpp`
- Modify: `shared/src/session/engine/session_engine.cpp`
- Modify: `shared/src/session/ports/session_ports.hpp`（仅合同确需时）
- Modify: `shared/CMakeLists.txt`
- Integrate tests from W1/W3

- [ ] **Step 1: 只接入 W1 组件，不重写 codec/scheduler**

ChannelBind 完成后启动 SessionSigning；`0x0212` durable 后启动 HELLO；scheduler effect 通过现有 QUIC/SecureStore/ObjectStore ports 派发。

- [ ] **Step 2: 投影正确状态**

HELLO/READY 过程中保持 `CONNECTING`；只有双方完整 READY/ACK 才发布 `CONNECTED_LOBBY` 和“双人联机中”。失败发布统一 reason key 并释放资源。

- [ ] **Step 3: 合入 W3 空大厅场景**

旧的单 engine `test_two_engine_empty_lobby.cpp` 可保留为长链路组件回归，但不得继续承担 E2E 名义；新场景必须注册 `nearby_integration` 标签。

**Acceptance `MVP-LOBBY`:** 两个公开 engine、真实 loopback Quinn、无 ROM、零 injected verified evidence，双方进入 `CONNECTED_LOBBY`；单边 READY 不通过。

## Task 10（W0）：集成 W2，完成 DUAL MVP

**Files:**

- Modify: `shared/include/flynes/flynes_session.h`
- Modify: `shared/src/session/engine/session_engine.hpp`
- Modify: `shared/src/session/engine/session_engine.cpp`
- Modify: `shared/src/session/view/` 中对应 snapshot/action projection
- Modify: `shared/CMakeLists.txt`
- Integrate tests from W2/W3

- [ ] **Step 1: 增加最小公开 action/snapshot**

只增加选择内容、确认 config/seat/authority、开始 DUAL、提交本机完整输入、暂停/恢复/退出和读取运行状态所必需的字段。保持 struct-size 尾追加与旧 prefix ABI 兼容。

- [ ] **Step 2: 接入 runtime port 和 DUAL scheduler**

engine 仍是唯一 reducer owner；UI、runtime、QUIC callback 只提交事件。每个 provider completion 经过 journal/fence 才进入 scheduler。

- [ ] **Step 3: 显式关闭 STREAM**

本机 capability 不宣告 STREAM；peer 只给 STREAM 时返回 unsupported reason；没有任何 codec/media provider 调用。

- [ ] **Step 4: 运行 E2E 与全量回归**

除 focused tests 外，运行完整 shared CTest、Android unit 和 ABI/schema mirror 检查。

**Acceptance `MVP-DUAL`:** 两个公开 engine 在相同内容上完成座位/配置确认并运行 600 帧；双方输入生效；frame/state digest 收敛；暂停/退出/断链行为正确；STREAM 调用次数为 0。

## Task 11（独立 content worktree）：实现内容许可与 ROM 补齐

**建议分支：** `codex/nearby-dual-content-transfer`

**Files:**

- Create shared content-offer/transfer codec、scheduler 和 tests under `shared/src/session/content/` and `shared/tests/nearby/`
- Modify platform catalog/import bridges only in later integration commits

- [ ] **Step 1: 实现 offer/check/permission 状态机**

发送、接收、仅验证和导入许可互相独立；保存好友不自动授权。双方没有同一 ROM 时可以留在大厅，不得伪装为可运行 DUAL。

- [ ] **Step 2: 实现低优先级可靠 stream**

ROM bytes 只走独立 bulk stream，不混入 input/control；支持 8 MiB 上限、声明长度、payload SHA-256、NES 格式/单 payload archive 校验、10 秒无进度超时和 staging 清理。

- [ ] **Step 3: 接入通用 scanner/import**

成功后原子移入受管库并重新执行 content/determinism gate；失败不发布 catalog generation；日志不得输出 ROM hash、路径或内容。

**Acceptance `CONTENT-DUAL`:** 许可拒绝不传字节；篡改/超限/多 payload/磁盘满不入库；合法内容导入后两端可重新协商 DUAL。因为 STREAM 延期，拒绝或失败时显示 DUAL 不可用，而不是回退串流。

## Task 12（独立 recovery worktree）：实现断链、重连与 durable ledger

**建议分支：** `codex/nearby-dual-recovery`

**Files:**

- Create recovery/link-root/ledger/WAL components under `shared/src/session/recovery/`
- Create `shared/tests/nearby/scenarios/test_two_engine_recovery.cpp`
- Create crash/cas/property tests under `shared/tests/nearby/recovery/`

- [ ] **Step 1: 实现 300 ms 失联冻结和 30 秒不延长 deadline**

只有验证过的 inbound peer activity 或证明 peer 收到本端消息的 authenticated ACK 可刷新 activity。重启、发送完成、transport ACK 和排队回调不得延长 deadline。

- [ ] **Step 2: 实现 input sequence ledger**

整个 parent link 内 seat 序号单调；换游戏、取消、崩溃和恢复不能归零或复用烧掉区间。CAS 冲突必须重新读取完整 root 后重算。

- [ ] **Step 3: 实现 ObjectStore root/WAL 原子切换**

先写 immutable children，再以 guarded replace 切换 manifest；失败保留旧整组。禁止 revisionB 与 ref/hashA 的半新半旧组合。

- [ ] **Step 4: 实现 REC01–REC09**

按获批测试设计逐项覆盖 reconnect、TAIL_STATUS prelude、pending handoff/start/end、prestart abort、source writer release、child NONE/ACTIVE/ENDED 归并和冲突 branch 冻结。

**Acceptance `REC-DUAL`:** crash 注入覆盖每次 flush/CAS/send/ACK 前后；重启只从 durable graph 恢复；旧 writer 不复活；旧 generation 不推进；无法证明唯一历史时保持 repair-blocked。

## Task 13（Android worktree）：接入真实 engine 与 Android providers

**建议分支：** `codex/nearby-dual-android`

- [ ] 实现 Android Key/Crypto/TLS/SecureStore/ObjectStore provider，私钥不可导出，metadata 放 no-backup storage。
- [ ] 实现 Android Discovery/Bearer adapter；模拟器不支持的真实 radio/path 返回 unavailable，不伪造 LAN 成认证路径。
- [ ] UI 只从公开 snapshot/action 投影连接、SAS、大厅、配置和运行状态；不保留第二套伪 stage。
- [ ] 运行 `:app:testDebugUnitTest`、`:app:assembleDebug` 和可用 emulator 的 Nearby instrumentation。

**Acceptance `ANDROID-NONDEVICE`:** 单元/构建/模拟器有效用例通过；硬件 Keystore 属性、Bluetooth/Wi-Fi Direct/LocalOnlyHotspot 和物理性能标为 NOT_RUN。

## Task 14（HarmonyOS worktree）：接入真实 engine 与 HarmonyOS providers

**建议分支：** `codex/nearby-dual-harmony`

- [ ] 实现 HUKS Key/Crypto/TLS/SecureStore/ObjectStore provider，区分 LOCKED/UNAVAILABLE/NOT_FOUND/REVOKED。
- [ ] 实现 Discovery/Bearer adapter 并保持 role 与认证 plan 一致。
- [ ] ArkTS/N-API UI bridge 只消费公开 descriptor/token/revision/snapshot。
- [ ] 运行 host CTest、Hypium、assembleHap；可用模拟器范围内运行测试。

**Acceptance `HARMONY-NONDEVICE`:** host/Hypium/build 通过；无签名真机安装、物理 HUKS/无线/性能证据明确 NOT_RUN。

## Task 15（iOS worktree）：接入真实 engine 与 Apple providers

**建议分支：** `codex/nearby-dual-ios`

- [ ] 实现 Security/Keychain/Network.framework/CoreBluetooth provider；ThisDeviceOnly、non-synchronizable、backup exclusion 合同不降级。
- [ ] simulator 缺硬件能力时明确 unavailable，不生成“测试成功”身份替代物。
- [ ] Swift/ObjC++ UI bridge 只消费公开 engine projection。
- [ ] 在 Mac 上先 build simulator，再用仓库 runner 执行 RuntimeTests/UITests。

**Acceptance `IOS-NONDEVICE`:** 有 Mac 时 simulator suites 通过；当前 Windows 无 Mac 执行环境时记录 NOT_RUN，不能由编译推断通过。

## Task 16（W0/acceptance worktree）：最终非真机验收与追踪

**Files:**

- Create: `docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md`
- Modify: `docs/superpowers/plans/2026-09-14-nearby-backend-r2-plan.md`
- Modify: 本计划的任务勾选和实际提交 SHA

- [ ] **Step 1: 建需求追踪表**

逐项映射总体方案 B01–B16、接口 IF01–IF16、测试 API/PORT/PAIR/WIRE/GAME/CONTENT/REC/UX。STREAM 专属 MEDIA/PERF 标为 DEFERRED；真机 L4 标为 NOT_RUN；其他项只能是 PASS/FAIL/BLOCKED，不能空白。

- [ ] **Step 2: 记录精确证据**

记录 revision、命令、测试总数、失败数、toolchain、artifact hash、模拟器范围和资源泄漏结果。0 tests matched 视为失败。

- [ ] **Step 3: 执行最终负向审计**

全仓搜索 STREAM capability/codec/media 调用，证明首版未宣告/启动 STREAM；搜索测试中的 injected verified evidence，证明 E2E 不依赖注入；审核日志无 ROM/密钥/SSID/BSSID/MAC/IP 泄漏。

**Acceptance `FINAL-NONDEVICE`:** DUAL MVP 所有硬门禁 PASS；三份文档中非 STREAM、非真机条目有逐项证据；没有把 BLOCKED/NOT_RUN 描述成 PASS。

## 17. 总体验收门禁

### DUAL MVP 必须全部满足

- [ ] `BASE-0212`
- [ ] `BASE-PARALLEL`
- [ ] `LINK-WIRE`
- [ ] `LINK-SCHED`
- [ ] `DUAL-INPUT`
- [ ] `DUAL-RUN`
- [ ] `E2E-HARNESS`
- [ ] `E2E-SCENARIOS`
- [ ] `MVP-LOBBY`
- [ ] `MVP-DUAL`
- [ ] shared 全量 0 failure
- [ ] Android unit/build 0 failure
- [ ] STREAM provider/codec/media 调用次数为 0
- [ ] 所有真机条目明确 NOT_RUN

### 三份文档非延期范围完成还必须满足

- [ ] `CONTENT-DUAL`
- [ ] `REC-DUAL`
- [ ] `ANDROID-NONDEVICE`
- [ ] `HARMONY-NONDEVICE`
- [ ] `IOS-NONDEVICE` 为 PASS 或因无 Mac 明确 NOT_RUN
- [ ] `FINAL-NONDEVICE`

## 18. 明确延期项

下列内容不应分配给本轮 worktree：

- H.264/PCM STREAM 编码、传输、解码、媒体背压和 A/V 性能。
- DUAL→STREAM 自动事务和 STREAM→DUAL（原设计也不允许同局自动升回）。
- 真机 Bluetooth/GATT/Wi-Fi bearer、硬件密钥性质、跨设备 QUIC。
- 物理触控到屏幕延迟、功耗、温度、刷新率和九方向/18 seat 设备矩阵。

延期不代表删除：mode/capability/channel/object kind 继续保留扩展性，未实现路径必须显式 unsupported/fail-closed。

---

## 19. 执行记录：W0 波次 0（2026-09-15 完成）

**共同基线提交 SHA：`e8703e4fdb598277a25d42c515486bea720ea757`**（`e8703e4`，336 个文件，hook 自动将 VERSION 由 1.4.0 递增为 1.4.1）。提交后工作区干净。

### Task 1 证据

| 步骤 | 状态 | 证据 |
|---|---|---|
| Step 1–3 0x0212 公开 engine 断言与负例 | ✅ | `test_two_engine_empty_lobby.cpp` 3910 → 4150 行；新增 `drive_session_signing_persistence`、`SessionSigningTail` 与两个测试函数（`test_session_signing_binding_object_hash_mismatch_fails_closed`、`test_session_signing_binding_shutdown_cancels_through_object_store`），二者均被 `main` 调用，非空跑 |
| Step 4 focused | ✅ | `flynes_two_engine_empty_lobby` PASSED (0.14 s) |
| Step 5 全量回归 | ✅ | `ctest` **86/86 PASSED，0 failed**，24.77 s（基线 84 + 本轮 2 个新合同测试） |

`nearby_*` 标签门禁非空：`nearby_protocol` 23、`nearby_auth` 12、`nearby_ports` 11、`nearby_abi` 5、`nearby_integration` 1、`nearby_race` 1。

**Acceptance `BASE-0212`：✅** exact 312 字节 binding 同时在 SecureStore 记 KeyRef、在 ObjectStore 注册 `0x0212`，两个 durability gate 缺一不可继续；全量回归 0 failure。

### Task 2 证据

| 步骤 | 状态 | 证据 |
|---|---|---|
| Step 1 三份获批文档纳入 W0 | ✅ | 复制前后 `Get-FileHash -Algorithm SHA256` 三对完全一致（`52CFC416…`、`633FDDE1…`、`98EBA20A…`）；主工作区原件仍为 untracked，未移动/改名 |
| Step 2 LINK_HELLO exact layout 冻结 | ✅ | `shared/src/session/link/link_control_contract.hpp`：kind `0x0216`、tag `0xFF06`、exact 480 字节（pretag 416 + 签名 64）、大端、reserved 全零、domain 字符串、DSL |
| Step 3 LINK_READY/ACK exact layout 冻结 | ✅ | 同文件：kind `0x0217`、tag `0xFF07`、exact 384 字节（pretag 320 + 签名 64）；phase 必须为 RECONCILE；双方 durable → send → verify → ACK → 才投影 `CONNECTED_LOBBY` |
| Step 4 DUAL 内部 seam 冻结 | ✅ | `shared/src/session/dual/dual_runtime_contract.hpp`：`DualRuntimePort`（load/step/export_state/import_state/state_digest）、四端口完整 mask、输入主键元组、12 槽回滚环、预测 10 帧冻结、mode 仅 DUAL |
| Step 5 合同编译测试 | ✅ | `flynes_link_control_contract`、`flynes_dual_runtime_contract` 均 PASSED；静态检查 enum 数值、结构大小、reserved 字段与 callback 签名；运行期覆盖 STREAM capability=false、未知 critical bit 拒绝、checked 单调 generation/sequence 溢出拒绝 |
| Step 6 共同基线提交 | ✅ | `e8703e4` |

**Acceptance `BASE-PARALLEL`：✅** 三份需求源文件已受版本控制且 SHA-256 与获批源一致；存在真实共同基线提交 SHA；合同不存在"拟议但未冻结"的 HELLO/READY 字段；shared 全量 0 failure。

### 执行中发现并处理的计划外问题

1. **`.gitignore` 漏掉 Rust 构建产物。** `git status --porcelain` 默认折叠未跟踪目录只显示 64 项，实际用 `--untracked-files=all` 展开是 **2024 项**，其中 `shared/nearby-quic-provider/target/` 占 **1865 项 / 1.36 GB** 且没有任何 ignore 规则。直接 `git add -A` 会把整个 cargo 构建树提交进基线。已追加 `/shared/nearby-quic-provider/target/`，untracked 降至 159 项且全部可归属 nearby 工作。
2. **三份获批文档并未冻结 HELLO/READY 的字节布局。** 它们只冻结语义与门禁。exact 布局、object kind、message tag、domain 字符串由 W0 按 Task 2 Step 2/3 设计并冻结于上述两份合同头；object kind 分配依据是仓库现有 registry（`0x0210`–`0x0213` 已占用，`0x0214/0x0215` 被 invite-code amendment 占用）。**注册进 `shared/schema/` 与三端镜像仍属 Task 9，本轮合同头中不注册。**
3. **构建环境 `MSB6001` 代理变量坑。** PowerShell `env:` 提供程序按大小写折叠重复项，常规"分组删重复"删不掉。已提供包装器 `out/dsh-exec.ps1`（解析 `cmd /c set` 原始环境块、按大写去重后重建 `ProcessStartInfo`）。另注意该包装器的参数名是 `$Rest`，`-R`/`-C` 会被 PowerShell 当作 `-Rest` 缩写，**ctest 一律使用长选项** `--tests-regex` / `--build-config`。

### 并行 worktree（已创建，均由 `e8703e4` 分出）

| worker | 分支 | 目录 | 版本 | 任务 |
|---|---|---|---|---|
| W1 链路控制 | `codex/nearby-dual-link-control` | `.worktrees/nearby-dual-link-control` | 1.5.0 | Task 3 + 4 |
| W2 DUAL 数据面 | `codex/nearby-dual-runtime` | `.worktrees/nearby-dual-runtime` | 1.6.0 | Task 5 + 6 |
| W3 双引擎夹具 | `codex/nearby-dual-e2e-harness` | `.worktrees/nearby-dual-e2e-harness` | 1.7.0 | Task 7 + 8 |

三个 worker 已并行启动。交接约定与构建手册见 `out/logs/w1w2w3-conventions.md`（忽略目录，不入版本控制）。

**已知需要 W0 在集成阶段处理的开放点：**

- W1/W2 的测试需要注册进 `shared/CMakeLists.txt`（W0 独占文件），本轮允许各 worker **仅追加**自己带标记的注册块，W0 顺序合并。
- W3 的 `CONNECTED_LOBBY` 正向断言在 W0 把 HELLO/READY 接进 `SessionEngine`（Task 9）之前不可能成立；W3 须如实标注待启用断言，不得伪造。
- 真实 loopback QUIC 能否在本机 Windows 上构建并链接（`shared/nearby-quic-provider` Rust crate）尚未验证，W3 正在确认；若不可用须记 BLOCKED 而非 PASS。
- 平台验收环境已确认可用（用户 2026-09-15 告知）：Windows 本机 Android/HarmonyOS 模拟器 + `ssh apple` 远程 Mac，因此 Task 13–15 的平台验收项应真实执行，**不再默认记 NOT_RUN**。

### 波次 0 之后的 W0 增量（`e22cbff`）

**真实 loopback QUIC 已打通 —— 这是本轮解除的最大计划风险。**

此前 `shared/CMakeLists.txt` **完全没有引用** `shared/nearby-quic-provider`，因此没有任何 CTest 套件执行过真实 provider 代码，所有传输测试用的都是假实现。这会让 Task 7/8 的 `E2E-HARNESS` 与 Task 9 的 `MVP-LOBBY`（要求「真实 loopback Quinn」）无法成立。

已落地的接线：

- `FLYNES_ENABLE_RUST_QUIC_PROVIDER`（默认 ON；找不到 cargo 时打印 status 并降级）
- 自定义目标 `flynes_nearby_quic_provider_build` 触发 `cargo build --release`（crate 源码与 Cargo.toml 列为 DEPENDS），实测 13.45 秒完成
- IMPORTED STATIC 目标 `flynes_nearby_quic_provider`，带 C ABI 头目录与 Rust 依赖需要的 MSVC 系统库
- 新增 `shared/tests/nearby/contract/test_quic_provider_linkage.cpp`

**证据：** `flynes_quic_provider_linkage` PASSED。该测试不是"加了一行链接就算通过"——它调用 provider 自己的参数校验与引用计数（`struct_size`/`abi_version`/三个回调非空校验、null 句柄 release 无副作用、最终 release 恰好释放上下文一次），能通过只可能是真实 Rust 实现执行了。全量 `ctest` **87/87 PASSED，0 failed**（16.27 s）。

### Android unit 基线（§17「Android unit/build 0 failure」）

**证据：** `gradlew.bat :app:testDebugUnitTest` → `BUILD SUCCESSFUL in 22s`；测试结果 XML 汇总 **97 个测试类 / 468 tests / 0 failures / 0 errors / 2 skipped**，其中包含 nearby 相关类 `NearbyInviteHostStateTest`、`NearbyInviteTickerTest`、`SessionSchemaRegistryTest`、`BuiltinGamesTest`。**不是 0 tests matched。**

### 平台验收环境（2026-09-15 实测探测，非仅用户口述）

| 平台 | 实测命令 | 结果 |
|---|---|---|
| Android | `adb devices` | `emulator-5554` / `emulator-5564` / `emulator-5570` 均 `device` |
| HarmonyOS | `hdc list targets` | `127.0.0.1:5557` |
| iOS | `ssh -o BatchMode=yes apple 'xcrun simctl list devices booted'` | iPhone 14 / iOS 16.4 **Booted**（UDID `988243AC-5704-45B6-9151-FF3A9B7AFD35`） |

### Task 9 集成点已定位

`shared/src/session/engine/session_engine.cpp:4467`：`session_signing_->ready()` 成立（exact 312 字节 `0x0212` 的两个持久化门禁都过）时，当前只 `publish_link_view_locked(FLY_SESSION_LINK_CONNECTING_V2)`，注释写明「LINK_HELLO is the next protocol gate」。需照抄 `session_signing_` 的 8 处接线（effect kind 映射、cancel、start、事件归属判定、effect 派发、完成分支、shutdown 忙判定、状态投影）已逐条记入 `out/logs/task9-integration-notes.md`（忽略目录）。

---

## 20. 执行记录：并行波次与集成（2026-09-15 进行中）

### 20.1 测试运行环境改为 WSL（用户 2026-09-15 明确要求）

**原因：** 在 Windows 上运行失败的 MSVC Debug 测试会弹出 "Debug Assertion Failed" 对话框打扰用户。因此约定：**C++ 测试只在 WSL(Ubuntu-24.04) 构建并运行；Windows 上只允许 `cmake --build`。**

WSL 环境实测：`cmake 3.28.3`(/usr/bin)、`g++ 13.3`、`make 4.3`、`openssl 3.0.13`、`cargo 1.96`、`zlib.h`、32 核；仓库经 `/mnt/e/...` 可达。配方：

```bash
cmake -S shared -B out/nearby-host-linux/shared/build -DFLYNES_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/nearby-host-linux/shared/build -j 24
# 直接跑二进制；用 ctest 时必须用长选项（--test-dir/--build-config/--tests-regex）
```

新 worktree 首次 configure 前需 `git submodule update --init core/vendor/nestopiaue`。

### 20.2 Linux 上暴露并已修复的既有缺陷（`9826703`）

MSVC 全部接受、GCC 加 `-Werror` 全部拒绝；这些只有换到 Linux 才暴露。**其中一处是真实未定义行为**，不是单纯的告警：

| 文件 | 问题 | 性质 |
|---|---|---|
| `test_initial_quic_bind_scheduler.cpp:249` | 同一实参列表里两次 `next_resource++`（未定序） | **真实 UB**，GCC 正确拒绝、MSVC 只默默选一个顺序 |
| `test_zip_payload_fixture_loader.cpp:266` | `const std::string&` 绑定由 `const char*` 构造的临时对象 | 可移植性缺陷 |
| `initial_bearer_scheduler.cpp` / `pair_signature_scheduler.cpp` | 两个匿名 namespace 死函数 | 死代码，已删 |
| `test_two_engine_empty_lobby.cpp`（30 处）/ `test_real_pair_pipeline.cpp`（2 处） | 未使用的 `unavailable_*` 端口桩与 CNG-only 辅助 | 标 `[[maybe_unused]]`，保留桩族以记录完整端口面 |

Windows 侧仍然干净：`cmake --build` exit 0、**0 个 `error C`**（131 个 warning 全部来自 nestopia 既有代码）。**Linux 上 `nearby_*` 标签全部通过。**

**仍存在、本轮未处理的 2 个既有 Linux 失败**（与本轮 Nearby 工作无关，且不在 DUAL 门禁内，仅记录）：
- `flynes_runtime_pcm_contention` 编译失败：`shared/src/runtime/flynes_runtime.cpp:292` 的 `-Werror=subobject-linkage`（匿名声明的 `Snapshot` / `NesDeleter` 被外部链接的 `fly_runtime_handle` 当作成员）——真实缺陷，需结构性改动。
- `flynes_zip_payload_fixture_corpus_check` 失败：4 个 fixture 文件在 Linux 检出下的字节与生成结果不一致（`deflate_large_signed_descriptor_exact_limits.zip`、`directory_payload_not_rejected.zip`、`truncated_deflate_incomplete.zip`、`manifest.tsv`），疑似行尾/`.gitattributes` 差异。

### 20.3 W1 已合并（`fe91ff1`）

- W1 交付 `32b0d1e`（Task 3 codec）+ `66c048e`（Task 4 scheduler）。
- 合并冲突面只有 4 个文件：**`shared/CMakeLists.txt` 自动合并成功**（已核对 W0 的 QUIC 接线三个定义各 1 次、W1 的 4 个测试注册块都在、0 处冲突标记）；3 个版本元数据文件按设计文档处理——取 W0 侧版本后跑 `tools/versioning/Sync-Version.ps1` 重新同步三端（`synchronized: 1.4.6`），**未手改 versionName/versionCode**。
- **合并后验证绿色**（设计文档要求"合并后的全量测试失败必须先恢复绿色，再接收下一个分支"）：WSL 上 **90 个测试 / 98% 通过**，W1 的 `link_hello_wire` / `link_ready_wire` / `link_handshake_scheduler` / `link_handshake_races` 全部通过；仅剩 §20.2 记录的 2 个既有非 Nearby 失败。
- W1 的 golden 向量经**独立手工核对**：按合同偏移逐字段比对 `kHelloInitiatorBytes`（480 字节）——version/reserved/角色互反/phase/session_id/link_id/channel_id/两个 generation/wire 版本/**capability 仅含 DUAL 位**/critical_extension_mask=0/四个 32 字节 hash 的偏移/identity_verifier_ref 内 version=1 与 reserved/其 `identity_key_id` 与单独声明的常量逐字节相等/公钥首字节 `0x04`——全部正确，排除了"脚本与实现从同一处误读而一起出错"的风险。
- W1 留有一个临时静态库 `flynes_link_control_w1`，按 Task 9 Step 3「集中式 CMake」要求需收编进 `flynes_session_codec` / `flynes_initial_plan_lock` 后删除。

### 20.4 W3 真实 loopback QUIC 探针（`3df0deb`）——`E2E-HARNESS` 的硬门禁已有硬证据

此前 W3 报 BLOCKED，根因是缺少能生成 91 字节 canonical P-256 DER-SPKI 证书与 low-S 签名的工具。WSL 有 `openssl 3.0.13` 后打通，**卡点正是 low-S 归一**：`ECDSA_do_sign` 的原始输出约一半是 high-S，必须 `s = min(s, n-s)` 再 `i2d_ECDSA_SIG` 重编码，否则 provider 用 ring 验证会正确拒绝。

WSL 实测输出（提交 `3df0deb`）：

```
PROBE register-tls-material-accepted: PASS
PROBE loopback-handshake-completed: PASS
PROBE connector/listener-handshake-facts: PASS  (TLS1.3 + ALPN flynes-nearby/2 + pin + peer_certificate_verified + peer_der_spki_hash==pin)
PROBE exporters-agree: PASS        PROBE stream-payload-round-trip: PASS
PROBE datagram-round-trip: PASS    PROBE all-resources-closed: PASS
PROBE signer-context-balanced: PASS   PROBE_EXIT=0   PASS=37  FAIL=0
```

Windows 降级路径已验证：同时具备 cargo 与 OpenSSL 才注册该探针，否则打印 `W3 loopback QUIC probe: skipped (needs cargo and OpenSSL libcrypto)`。

**验收记录须注明**：`two_engine_fixture.hpp`（2235 行）**不是手写**，而是从 `test_two_engine_empty_lobby.cpp` 逐行 carve（生成器在 git-ignored 的 `out/tools/`，不受版本控制），三处显式 delta 为 per-engine clock、可换 QUIC port、engine ordinal+ledger。

