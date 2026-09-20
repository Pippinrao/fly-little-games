# 交接文档：Nearby DUAL 双端并行工作流

**日期：** 2026-09-16
**交接原因：** 本轮会话结束，需求尚有未达成项
**需求来源（三份获批文档，按优先级）：**

1. `docs/superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md`（1472 行，正文引用记作 `design:`）
2. `docs/superpowers/specs/2026-09-13-nearby-interface-design.md`（引用记作 `IF..`）、`2026-09-13-nearby-test-design.md`
3. `docs/superpowers/specs/2026-09-15-nearby-dual-parallel-worktree-design.md`（213 行，引用记作 `design-0915:`）
4. `docs/superpowers/plans/2026-09-15-nearby-dual-parallel-worktrees-plan.md`（661 行，Task 1–16）

---

## 0. 一句话现状

**共享层的 link 控制面与 DUAL 组件级确定性已经做完并实测通过；从"两个公开 engine"到"真能跑一局 DUAL"的那一段接线没做完**，卡在一个计划没覆盖的协议缺口上（`START_DUAL` 拿不到内容身份），另有 3 层篡改负例 fail-open。三端模拟器验收一次都没跑。

> 2026-09-16 补充：会话中发现的"Rust QUIC provider 被静默关闭以致 `E2E-HARNESS`/`MVP-LOBBY` 证据失真"这个验证缺陷**已修复并更正记录**（`08f2afd`），详见 §3.1。

---

## 1. 代码位置与分支

所有 nearby 工作**不在 `main`**，在 worktree 里。主仓工作目录 `E:\workspace\codes\games\fly-little-games` 是 `main`（VERSION 1.3.14），**不要在这里改 nearby**。

| worktree | 分支 | tip | VERSION | 作用 |
|---|---|---|---|---|
| `.worktrees/nearby-ui-acceptance-fixes` | `codex/nearby-ui-acceptance-fixes` | `c7bcd79` | 1.4.35 | **W0，集成主线，绝大多数工作在此** |
| `.worktrees/nearby-dual-e2e-harness` | `codex/nearby-dual-e2e-harness` | `697c4a1` | 1.7.5 | W3，双 engine 夹具 + 篡改矩阵 |
| `.worktrees/nearby-dual-link-control` | `codex/nearby-dual-link-control` | `66c048e` | — | W1，已并入 W0 |
| `.worktrees/nearby-dual-runtime` | `codex/nearby-dual-runtime` | `f0761db` | — | W2，已并入 W0 |

W0 提交链（新→旧）：

```
c7bcd79  content 只读端口前半（fly_session_content_port_v2 尾追加；端口目前惰性）
08f2afd  fix：Rust QUIC provider 被静默关闭（修复 + 重跑 + 更正验收记录）+ 修 link_control_contract 文档
11b3179  DUAL 运行 digest 上公开快照（尾追加 dual_state_digest/dual_frame_digest/dual_pcm_digest）
53f734f  DUAL runtime port 适配器 + 154 字节 CanonicalInputBundleV1 编解码
e87cbe3  STREAM 显式关闭（编译期钉住）
d3f1e2d  最小公开 DUAL action/snapshot ABI（纯尾追加）   ← W3 已合并到此
1bfabf6  Android frozen schema registry hash 重钉
2128e7f  记录 DUAL MVP 门禁状态与证据
0a2e5ca  step 5：QUIC bind + Control loopback 进双端 CONNECTED_LOBBY
dbb817b  fix：scheduler operation id 整块预留，不再重复发号
```

**`1bfabf6`、`2128e7f`、`d3f1e2d` 已被 W3 合并；`08f2afd`、`c7bcd79` 待 W3 合并。这些提交都不可 amend/rebase/改写历史——只能追加。**

> **ABI 冻结点尚未到达。** `c7bcd79` 之后**还差一处可预见的 ABI 追加**：content 查询的**完成 payload kind** —— 需要在 `fly_session_authenticated_operation_payload_kind_v2` 加枚举值 + `contract_for()` 条目 + `parse_provider_event_v2` 分支（provider 事件的 buffer 形是 `terminal = 0`，无法终结登记进 journal 的操作，所以必须走 hash 形）。除此之外 DUAL 一侧不再需要新 ABI：reducer、State Commit 流（复用既有 quic 端口 + `grant_read_credit`）、输入传输（既有 154 字节 `CanonicalInputBundleV1`）都落在现有 ABI 内。**三端平台 worktree 必须等这一处 + 引擎 reducer 同波落地后再分支。**

---

## 2. 门禁状态（`docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md`）

| 门禁 | 状态 | 说明 |
|---|---|---|
| `BASE-0212` | PASS | 两个 0x0212 持久化门禁缺一不可 |
| `BASE-PARALLEL` | PASS | 共同基线 `e8703e4`，两份合同冻结 |
| `LINK-WIRE` | PASS | 独立 oracle（`gen_link_control_golden.py`，不调用被测 encoder）重算 golden |
| `LINK-SCHED` | PASS | 两个公开 scheduler 零手工注入闭合 |
| `DUAL-INPUT` | PASS | 规范输入 golden + 权限/序号负例 |
| `DUAL-RUN` | PASS | 600 帧确定性 trace、12 帧回滚、失步 fail-frozen |
| `E2E-HARNESS` | PASS（provenance 已于 `08f2afd` 更正） | 不可复核的"探针 PASS=37 FAIL=0"已删除（该 worktree 的 CMake 无此目标）；provider 真 ON 下实测全量 97/99、`nearby_*` 41/41。**两条声明已拆开**：双 engine E2E 走夹具进程内 `LoopbackTransport`、**不用** Rust provider；「真实 Quinn」由 crate 自测（`cargo test --release` 6 passed / 0 failed）+ `flynes_quic_provider_linkage_test` 证明 |
| `E2E-SCENARIOS` | **未全绿** | 篡改 14 层中 10 层已验证、**3 层 fail-open**、1 层无注入面；DUAL 正向/故障场景被引擎接线阻塞 |
| `MVP-LOBBY` | PASS（QUIC 口径已于 `08f2afd` 更正） | 双端进 `CONNECTED_LOBBY`，单边 READY 不放行。QUIC 端口计数器只表示"引擎真的驱动了每个 QUIC port 原语"，**不代表跑在真实 Quinn 上** |
| `MVP-DUAL` | **NOT_RUN** | 见 §4 |
| Android unit/build 0 failure | **NOT_RUN（需复跑）** | 09-15 基线 97 类 / 468 tests / 0 failures；此后 ABI 与 schema 都动过 |

验收文档自己的规则（**必须遵守**）：0 tests matched 记 FAIL；只编译通过不算 PASS；不得把 BLOCKED/NOT_RUN 写成 PASS。

---

## 3. ⚠️ 已发现的"证据来源不可靠"问题（新接手者必须先修这条）

### 3.1 W0 构建里 Rust QUIC provider 被静默关闭 —— **已修（`08f2afd`）**，本节保留为教训

| | `FLYNES_ENABLE_RUST_QUIC_PROVIDER` | `FLYNES_CARGO_EXECUTABLE` | 探针二进制 |
|---|---|---|---|
| 修复前 W0 | **`OFF`** | **`-NOTFOUND`** | 不存在 |
| 修复后 W0（`08f2afd`） | `ON` | `/home/pippin/.cargo/bin/cargo` | 存在并通过 |

**根因（我原先写错了，以 `08f2afd` 的实际复现为准）：** cargo **即使对 login shell 也不在 PATH** —— `bash -lc 'command -v cargo'` 无输出，而 `/home/pippin/.cargo/bin/cargo` 确实存在。因此 `shared/CMakeLists.txt:352` 的 `find_program` 失败、`:356` **FORCE** 置 OFF。

> ⚠️ **只把 `wsl -e bash <script>` 换成 `-lc` 修不好这个问题**（本交接文档早先版本就是这么写的，**那条建议是错的**）。正确修法：显式把 `$HOME/.cargo/bin` 加进 PATH **且**显式传
> `-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=/home/pippin/.cargo/bin/cargo`，
> 然后 configure 之后**断言**缓存值。

**已做（`08f2afd`）：** `out/logs/wsl-test.sh` 与 `out/logs/task10-cycle.sh` 现在都会在 configure 后校验缓存，不满足即以 `ABORT_PROVIDER_OFF`(92) / `ABORT_CARGO_NOTFOUND`(93) 退出——**静默能力降级不再可能**。

**记录更正（同一 commit）：** 不可复核的"探针 `PASS=37 FAIL=0`"已删除（该 worktree 的 CMake 无此目标）；全量 `94/96→97/99`、`nearby_* 39/39→41/41`（provider 真 ON 下实测）；并**把两条被混为一谈的声明拆开**：`flynes_two_engine_connected_lobby_e2e_test` 走夹具**进程内 `LoopbackTransport`，不用 Rust provider**（只证明引擎接线与协议闭合），「真实 Quinn」由 crate 自测 + linkage 测试证明。`MVP-LOBBY` 的 QUIC 端口计数器口径也据此改写。

**给接手者的教训：** 一个"能力探测失败后静默降级"的构建配置，会让整套实测证据在不知情的情况下换掉被测对象。任何 `find_program` 型可选依赖都要在 configure 后断言，而不是信任默认值。

### 3.2 我此前口头汇报过的一段 Android 证据是错的

我曾说"468 tests / 0 failures"，但那两次 gradle 实际跑在**主仓 main**（`cmd /c gradlew.bat` / `Start-Process` 继承 harness 默认目录 = 主仓），而 W0 里 `app/build/test-results/testDebugUnitTest` **压根不存在**。

判别方法（看日志里的绝对路径）：
- `file:///E:/workspace/codes/games/fly-little-games/build/reports/...` → 主仓
- `E:\workspace\codes\games\fly-little-games\app\src\test\...` → 主仓

主仓那次是 `tests=465 failures=0`（95 个 XML），**465 ≠ 468**，而且主仓不含任何 nearby 改动。

**必须做：** 在 worktree 里显式设 `workdir` 重跑，并从 **worktree 自己** 的结果目录读数。四份错目录日志已被改名加 `-WRONG-TREE-main-repo.log` 后缀以防误用。**同一 worktree 同时只允许一个 gradle 进程**（并发跑曾产生 `BUILD SUCCESSFUL / 24 tasks executed` 但结果目录没重建的假象）。

---

## 4. 没做成的：四个缺口（这是接手者的核心工作）

### 缺口 A：`START_DUAL` 拿不到内容身份 —— 已裁决，方案已定

- 公开头文件里**没有 content 端口表**（IF11 列了 content 家族但从未冻结 C 形状）
- 引擎发布的 `game_choices` 恒为空（`session_view.cpp:95`），也无 catalog 接缝
- `CapabilitySummary`（`wire/pair_capability.hpp`）只有 bearer plan，没有内容身份
- `dual::DualContentRefV1.content_hash` 必须非零且两端一致 → `START_DUAL` 连 scheduler `begin()` 都进不去

**已裁决（用户 2026-09-16 批准）：补一个最小只读 content 端口。**
- 尾追加到 `fly_session_ports_v2`，`FLY_SESSION_PORTS_V2_R2_SIZE = offsetof(content)`；缺端口合法，依赖内容的动作 fail-closed
- 只读、最小、`struct_size` 门禁、C 兼容
- 引擎据此**填上 `game_choices`**
- `SELECT_CONTENT_V2` 只带 **IF05 合规的 typed choice reference，绝不带裸 32 字节 hash**
- **否掉的方案**：让 UI 在 submit 里带 32 字节 hash —— 违反已批准的 IF05（`2026-09-13-nearby-interface-design.md:151`「不要求 UI 在 submit 中重传这些 hash、revision 或证明」；`:167` PROPOSE_GAME 行是「选择公开 game choice 的 ContentRef/SourceChoiceRef」）
- **不做** offer/许可/导入/低优先级可靠流 —— 归 Task 11（`CONTENT-DUAL`）
- 无 content 端口时引擎行为必须与今天完全一致，`MVP-LOBBY` 不受影响

### 缺口 B：对端 digest 没有冻结的线上对象 —— 已裁决延后

- `design:460` 要求 State Commit 通道承载 digest；但我 `grep` 整份 design，**`[A-Za-z]*Digest[A-Za-z]*V[0-9]|DIGEST` 零命中**
- `app_frame.cpp:141-143` 自己写着 digest 家族「have no frozen wire object yet and therefore no entry」
- schema 里唯一的 `state_digest` 是 `END_PACKAGE_V1`（0x0306，Bulk）的字段，是结束存档包不是逐帧 digest
- 还有一整套 `ActivationFenceOfferV1(304)` / `AckV1(336)` / `FinalizedV1(136)`（`design:781`，走 Control 的 `*_ACTIVATE`）同样没进 schema

**已裁决（用户 2026-09-16 选 A）：digest + ActivationFence 系列整体延期到恢复波次（Task 12 `REC-DUAL`）。**
- 因此 `acknowledge_peer_digest()` 与**「暂停后恢复回 RUNNING」保持未实现、fail-closed、记 NOT_RUN**
- 绝不允许伪造对端 digest、合成 class-2 provider 事件、或注入 verified evidence
- MVP-DUAL 的 gate 收窄为：**进程内 600 帧 + 双端输入生效 + 两端本地观测 digest 逐 60 帧收敛 + 暂停/退出/断链冻结**

### 缺口 C：`SELECT_CONTENT` 之后引擎完全没有 DUAL 路径

（以下为代理带 file:line 的取证，**我未逐条复核**，接手者应自行确认）

- `session_engine.cpp:5642` 是动作归约 catch-all：`PROPOSE_GAME/CHANGE_AUTHORITY/CHANGE_SEATS/CONFIRM_GAME_CONFIG/RETURN_TO_LOBBY/PAUSE_GAME/RESUME_GAME/SAVE_AND_END/DISCONNECT_LINK` 全落它并返回 `REJECTED, INVALID_STATE`
- `publish_link_view_locked()`（`:272`，唯一 publish helper）把 `game_state` 硬编码 `NOT_STARTED`（`:322`），没有 game 视图发布路径
- `submit_input()`（`:2428`）前置要求 `scope.kind == SCOPE_GAME_V2`，而 view 永远发 `SCOPE_ENGINE_V2`（`session_view.cpp:66`）→ **构造上不可达**
- 引擎只开 bind 流 + **一条** Control 流（`:3932-3958`），Control 的 read/credit 循环在 `LinkHandshakeScheduler` 里（link 闭合即结束），**没有第三条 State Commit 流的归属者**
- `dual_runtime_contract.hpp` 的 `DualRuntimePort` **从未被引擎调用**

**已裁决（我，无需再问）：**
- State Commit 第三条流由**引擎新建组件**持有（不是 LinkHandshakeScheduler）。依据 `design:468`：Control 与 State Commit 是逻辑双向通道，实现用每方向独立 QUIC unidirectional stream
- `FLY_SESSION_PROVIDER_QUIC_DATA_V2` 是 `terminal = 0`，只能走 `parse_provider_event_v2`，**绝不能登记进 `ProviderOperationJournal`**
- `DUAL_RUN_FENCE_V1`(0x0201) **不加入** State Commit allow-list。依据 `design:777`：`DualRunFenceV1` 是**每端 active manifest 里的本地持久记录**（232 字节），不是线上消息；其线上形态是 `ActivationFence*` 三件套走 Control。硬加进 State Commit 是**违反 spec**，不是自由选择

### 缺口 D：耐久提交水位没有可挂的存储原语 —— 已裁决延后

- `shared/src/session` 内 grep：`manifest`/`flush`/`active_root`/`read_root`/`commit_watermark` **都不存在**
- `design:809` `AuthorityRecoveryHeadV1`、`design:812` 原子安装 active manifest 在本版本引擎中未实现

**已裁决（我）：延期到存档/WAL 波次（Task 12 本来就管 durable ledger）。MVP-DUAL 不实现 manifest/flush/commit_watermark。**

### 一个决定性的数学事实

单端只有本机 seat 输入时，`commit_frontier_` 永不推进，窗口在预测深度 10 冻结（`dual_runtime_contract.hpp:43` `kDualPredictionFreezeDepthV1 = 10`；`input_window.cpp:283-303`）。**所以"本地跑 600 帧"不存在，必须真的把对端输入送进 scheduler。** 这条决定了缺口 C 无法用本地测试绕开。

---

## 5. E2E-SCENARIOS 的真实状态（W3 交付，未全绿）

W3 已把 W0 的 tip 并入（`da3bac6`），删掉自己那份过时的 `two_engine_fixture.{hpp,cpp}` 与 `connected_lobby` 场景，保留 W0 的 `two_engine_loopback_fixture.*`、`loopback_quic_fixture.*`、`loopback_quic_probe.cpp`、两个 `crashable_*_store.hpp`。实测 101 个测试 98%，2 个失败是既有的非 nearby 项（`flynes_runtime_pcm_contention` 的 `-Werror=subobject-linkage`、`flynes_zip_payload_fixture_corpus_check` 的 Linux 行尾问题）。

### 篡改矩阵 14 层结果

**10 层已验证 fail-closed**（每层都断言"被检测到"`FAILED=9`，不只是"没进大厅"）：

| 层 | 注入方式 | inviter/joiner |
|---|---|---|
| role | QUIC relay，HELLO 帧 byte 14 XOR 1 | 7 / 9 |
| generation | HELLO 帧 byte 70 XOR 1 | 7 / 9 |
| capability | HELLO 帧 byte 91 XOR 2 → DUAL\|STREAM_VIDEO；**通过所有 stage-1 检查，只被记录尾部签名抓住** | 7 / 9 |
| binding | HELLO 帧 byte 194 XOR 1 | 7 / 9 |
| HELLO | HELLO 帧 byte 16 | 7 / 9 |
| READY | READY 帧 byte 17 | 7 / 9 |
| ChannelBind | bind 流记录长度 LSB XOR 1（248→249） | 7 / 9 |
| credential | `bearer.tamper_join_params` | 9 / 6 |
| TLS pin | `transport.present_unpinned_certificate`（MITM：verified=true，SPKI ≠ pin） | 7 / 9 |
| exporter | 一端开 `quic.use_exporter_override` | 9 / 7 |

对照组：不注入时 8/8 `CONNECTED_LOBBY`。

**3 层 fail-OPEN（必须修）**：`commit/reveal`、`pair signature`、`key-confirm`。注入生效（`tampered_logical_messages() > 0`）但两端仍进大厅。怀疑是签名/AEAD 末字节偏移猜错（PairCommit body 48 当 commitment；PairSignature/KeyConfirm 篡改最后一个 body 字节）。另有信号：key-confirm 那次钩子报 1 次 "reassembly-mismatch"（读不回重组记录自身长度）→ 那个 GATT 重组钩子对某些分组不成立。

**1 层无注入面**：SAS。`PairPipeline::approve_local(approval_kind, displayed_sas)`（`pair_pipeline.cpp:159-169`）有 `displayed_sas` 校验，但**引擎走的是单参数重载**（`session_engine.cpp:5598`），所以 `pair_pipeline.cpp:164` 的校验**不可达**。公开 action/choice 里没有携带 SAS 字节的位置。

> **⚠️ 这条要当成可能的引擎安全缺陷查，不只是测试限制。** 如果引擎永远到不了 `displayed_sas` 校验，它可能**根本没校验 SAS**（MITM 相关）。接手者应查清：引擎 confirm 路径实际走到哪个重载、校验了什么；live 路径上还有没有别处比较 SAS；能不能驱动一端去确认它从没见过的 SAS。**只做诊断取证，不要改 `session_engine.cpp`/`pair_pipeline.cpp`（W0 拥有）。**

### W3 另外发现的两件事

- **夹具真 bug（已修）**：relay 循环用正在被改写的 watermark 去偏移篡改副本，导致分组第一分片被重投。
- **夹具改动（default-off、纯增量，待 W0 review）**：`two_engine_loopback_fixture.hpp` 增加 `tamper_with` / `tamper_logical_with` / `present_unpinned_certificate` / `ClockFixtureV1` + `Exporter`/`Bearer`/`Quic` 旋钮；`relay_one_direction` 增加一个 `LoopbackTransport&` 参数。
- **文档缺陷（已在 `08f2afd` 修复）**：`link_control_contract.hpp:185` 说 LINK_READY「exactly 368 bytes」，而它自己的表和 `kLinkSizeV1` 都是 **432**（368 pretag + 64 签名）。
- **未解释的分歧**：W3 的夹具形状下，撤销中断后**回不到 lobby**，收端以 `CONTRACT_VIOLATION(-15)` 拒绝重试的 QUIC 单元（W3 归因于 W0 夹具头里记录的 operation-id 缺陷）；而 W0 自己的单边 READY 测试**能**恢复。两个夹具形状行为不同，未查清。

---

## 6. 已裁决的决定汇总（接手者不要再重新讨论）

| # | 决定 | 依据 |
|---|---|---|
| 1 | 补最小只读 content 端口；`SELECT_CONTENT_V2` 只带 typed choice reference，**不带裸 hash** | 用户 2026-09-16 批准；IF05:151/167 |
| 2 | digest + ActivationFence 系列延期到 Task 12 恢复波次；`acknowledge_peer_digest` 与 resume-to-RUNNING 记 NOT_RUN | 用户 2026-09-16 选 A |
| 3 | State Commit 第三条流由引擎新建组件持有 | `design:468` |
| 4 | `DUAL_RUN_FENCE_V1`(0x0201) 不进 State Commit allow-list | `design:777`（本地持久记录，非线上消息）、`design:781`（线上形态是 ActivationFence*） |
| 5 | 耐久提交水位 / manifest / flush 延期到 Task 12 | `shared/src/session` 无此原语；`design:809/812` 未实现 |
| 6 | content 的 offer/许可/导入/低优先级流归 Task 11 | 计划 Task 11 `CONTENT-DUAL` |
| 7 | 后端 ABI 一律**尾追加** + `struct_size` 门禁 + old-prefix 兼容，必须有测试证明 | 本仓既有 ABI 约定 |

---

## 7. 三端验收环境（本次已全部打通，可直接用）

详细手册见 `.worktrees/nearby-ui-acceptance-fixes/out/logs/three-platform-acceptance-env-20260916.md`。摘录：

**Android** —— 3 台模拟器已 booted、动画已关（回读 0/0/0）

```
adb   = C:\Users\pippin\AppData\Local\Android\Sdk\platform-tools\adb.exe
emu   = C:\Users\pippin\AppData\Local\Android\Sdk\emulator\emulator.exe
emulator-5554 = FlyNES_BuiltinContent
emulator-5564 = FlyNES_Loading_20260913_5564
emulator-5570 = MIT_Phone_API35
```
启动参数：`-avd <name> -port <p> -no-snapshot-save -no-boot-anim -gpu swiftshader_indirect`。
UTP/Espresso 前必须关三个 animation scale。**有真机时先设 `ANDROID_SERIAL`。**

**HarmonyOS** —— 模拟器本轮才第一次真正启动过（之前 `hdc list targets` 是 `[Empty]`）

```
hdc      = D:\soft\DevEco Studio\sdk\default\openharmony\toolchains\hdc.exe   (Ver 3.2.0b)
hvigorw  = D:\soft\DevEco Studio\tools\hvigor\bin\hvigorw.bat
Emulator = D:\soft\DevEco Studio\tools\emulator\Emulator.exe                  (6.0.0.502)
实例 "Pura 70 Pro"  HarmonyOS 6.0.0(20) API 20 x86
镜像根   = %LOCALAPPDATA%\Huawei\Sdk
```
**启动坑（实测）：** `-path` 要传**父目录** `...\Emulator\deployed`（它会把 `-hvd` 名字拼在后面；传实例目录会报 `is not found`）；参数必须带引号（`Start-Process -ArgumentList` 传数组不加引号会被空格切开，报 `Invalid command: please attach - or --`）。
可用命令：
```powershell
$argLine = '-hvd "Pura 70 Pro" -path "C:/Users/pippin/AppData/Local/Huawei/Emulator/deployed" -imageRoot "C:/Users/pippin/AppData/Local/Huawei/Sdk"'
Start-Process -FilePath 'D:\soft\DevEco Studio\tools\emulator\Emulator.exe' -ArgumentList $argLine
```
成功判据（约 20–40s）：stdout 出现 `Windows Hypervisor Platform accelerator is operational`，且 `hdc list targets` 返回 `127.0.0.1:5557`。stderr 的 `dshow ... Could not enumerate video devices` 无害。
Hypium：`hdc shell aa test -b com.flynes.emu -m entry_test -s unittest OpenHarmonyTestRunner`
宿主 CTest 用 Android SDK 的 cmake 3.22.1，`-C Debug --parallel 1`（部分套件用裸 assert，Release 会被 NDEBUG 抹掉）。

**iOS** —— 经 `ssh apple` 远程 Mac，工具链本轮复核完好

```
cmake    = ~/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake
SDK      = xcrun --sdk iphonesimulator --show-sdk-version → 16.4
测试入口 = /Users/apple/Developer/fly-little-games-ios/ios/scripts/run_simulator_tests.py
模拟器   = iPhone 14  988243AC-5704-45B6-9151-FF3A9B7AFD35  (Booted)
磁盘     = /Users/apple 剩余 72Gi
```
configure：`-DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 -DFLYNES_IOS_BUILD_XCTESTS=ON`
测试：`python3 ios/scripts/run_simulator_tests.py <udid> [FlyNESRuntimeTests|FlyNESUITests]`
传源码用 `git archive` 后**必须另传 nestopia 子模块**，否则 core 编不过。
限制：MacBookPro14,1 2017 Intel / macOS 13.0.1 / Xcode 14.3.1，无法建 iPhone 16 Pro Max 模拟器（需 Xcode 16 + macOS 14.5+），也无法直连安装真机。

---

## 8. 会浪费时间的已知陷阱

1. **C++ 测试只在 WSL 跑**（Ubuntu-24.04，cmake 3.28.3，g++ 13.3，cargo 1.96，32 核）。Windows 只编译不运行——MSVC Debug 运行会弹 CRT 断言对话框。
2. **Rust QUIC provider 会被静默关掉**（见 §3.1）：本 distro 里 cargo **连 login shell 都不在 PATH**，所以 `-lc` **修不好**。必须显式导出 `$HOME/.cargo/bin` **且**显式传 `-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=/home/pippin/.cargo/bin/cargo`，并在 configure 后断言。W0 的两个脚本现在会以 exit 92/93 中止而不是静默降级。
3. **ctest 短选项在 pwsh wrapper 下会坏**（`-R` 绑到 `-Rest`）：只能用 `--tests-regex/--build-config/--test-dir/--label-regex`。
4. **gradle 会跑在错目录**：`cmd /c gradlew.bat` 继承 harness 默认目录 = 主仓。必须显式 `workdir`。同一 worktree 同时只允许一个 gradle 进程。
5. **MSVC 陷阱**：`std::copy(hex_bytes<N>(kX).begin(), hex_bytes<N>(kX).end(), dest)` 用两个临时 array 会 abort（"array iterators incompatible"），Linux 却静默正常——必须先绑具名局部变量。
6. **Linux `-Werror`**：同表达式两次 `next_resource++`（真 UB）、`-Wrange-loop-construct`、`-Wsubobject-linkage` 都会挂。
7. **MSBuild `MSB6001 ... NO_PROXY/no_proxy`**：DSH pwsh 把代理变量按大小写重复导出，.NET 的 EnvironmentVariables 是大小写不敏感字典。用 `out/dsh-exec.ps1` 包一层（它解析 `cmd /c set` 原始环境块后重建 ProcessStartInfo）。`Get-ChildItem env:` + `Remove-Item` 删不干净。
8. **WSL 里的 git 对 worktree 不可用**（`.git` 文件里是 `E:/...` 路径 → `fatal: not a git repository`）。所有 git 操作在 Windows 侧做。
9. **新 worktree 首次 configure 前**要 `git submodule update --init core/vendor/nestopiaue`。
10. **合并冲突**：`shared/CMakeLists.txt` 一般能自动合；只有 `VERSION`/`app/build.gradle`/`harmony/AppScope/app.json5` 会冲突 → 取 ours，跑 `tools/versioning/Sync-Version.ps1`，验证绿了再接受下一支。**build 文件不能整份取一边**（取分支版 `app/build.gradle` 会丢主干新增的 generatePopularity 代码生成任务）。
11. **两个既有的非 nearby 失败必须保留、不要"顺手修"**：`flynes_runtime_pcm_contention`（`shared/src/runtime/flynes_runtime.cpp:292` 的 `-Werror=subobject-linkage`）和 `flynes_zip_payload_fixture_corpus_check`（Linux 行尾）。
12. **pre-commit hook 每次提交自动 +PATCH** 并同步 Android/HarmonyOS 元数据。不要手改平台 `versionName`/`versionCode`。
13. 建 worktree 只能用 `tools/versioning/New-VersionedWorktree.ps1`，不要直接 `git worktree add`。

---

## 9. 建议的接手顺序

1. ~~修 §3.1 的 Rust provider 静默降级~~ —— **已在 `08f2afd` 完成**（修复 + 重跑 + 记录更正 + 两条声明拆分 + 文档缺陷修正）。接手者只需复核该 commit 的 diff 与验收文档措辞是否诚实。
2. **修 §3.2 的 gradle 错目录**，在 worktree 里显式设 `workdir` 重跑 Android unit，落实"Android unit/build 0 failure"。**（仍待做，`MVP-LOBBY` 之后唯一一个纯验证缺口）**
3. **W3 第二轮**：修 3 层 fail-open 篡改（从真实记录布局与 decoder 反推偏移，不要猜末字节）；修 GATT 重组钩子的长度回读 bug；把 SAS 那条当**引擎安全缺陷**取证。
4. **Task 10 下一轮**（缺口 A+B 已裁决，可开工）：content 只读端口 → 引擎 DUAL reducer（干掉 `:5642` catch-all、补 game/config/seat/authority 状态、真 game 视图发布、修 `SCOPE_GAME_V2` vs `SCOPE_ENGINE_V2` 死路、发送前 `normalize_dual_port_mask_v1`）→ State Commit 第三条流 → 154 字节输入端到端 → **600 帧双 engine E2E + 逐 60 帧 digest 收敛**。
5. **ABI 冻结**，然后才并行下发三端平台 worktree（计划 §1 明确把 Android/HarmonyOS/iOS 排在"DUAL MVP 公共 ABI 冻结"之后；现在分叉就是明知要返工）。环境已全部待命。
6. Task 11 `CONTENT-DUAL`、Task 12 `REC-DUAL`（含被延后的 digest/fence）。
7. Task 16 `FINAL-NONDEVICE` 追踪表逐项填证据 → **独立盲审**（用户明确要求：先模拟器三端测试全过，再做独立盲审）。

---

## 10. 交接时刻仍在运行的工作

本会话结束时有**两个后台代理正在 W0 与 W3 里自主提交**：

- **W0**（`codex/nearby-ui-acceptance-fixes`，tip `08f2afd`）：§9.1 已交付（provider 修复 + 记录更正）。**第四轮已下达**：content 只读端口（第一件）→ 引擎 DUAL reducer → State Commit 流 → 输入端到端 → 600 帧 E2E。
- **W3**（`codex/nearby-dual-e2e-harness`）：正在做 §9.3 的三层 fail-open 修复 + SAS 取证；已通知其在本轮收尾于干净提交点后合并 W0 的 `08f2afd`。

**接手者必须先检查这两个 worktree 的实际 tip 与工作区状态**（`git -C <worktree> log --oneline -5` / `status --porcelain -uall`），不要相信本文档记录的 tip（`08f2afd` / W3 的 `697c4a1`）仍然是最新——它们是本文书写时刻的值。若代理仍在跑，等它们落地或先中断，避免并发写同一 worktree。

---

## 11. 风险与未决问题

- **`MVP-DUAL` 与 `E2E-SCENARIOS` 都还不是 PASS**，而它们是 §17「DUAL MVP 必须全部满足」的成员。在它们变绿之前，`FINAL-NONDEVICE` 不可能 PASS。
- **content 端口的 C 形状是本次新冻结的**，三份获批文档里没有；`SELECT_CONTENT_V2` 的 typed choice reference 具体编码也需要在使用中固化。落地时建议补一份契约测试 + golden。
- **SAS 不可达校验**可能是真安全缺陷，见 §5。
- **两个夹具形状在"中断后能否恢复"上行为不同**，未解释，见 §5。
- 本次会话未能做到"三端模拟器实测"与"独立盲审"——这是用户明确的完成标准（**仅编译/打包通过不算完成**），接手者不要用编译结论替代。

---

## 12. 第五轮设计（已确定，直接可执行）

W0 代理在 `c7bcd79` 干净收工（上下文预算耗尽），交回了下述设计。**原文只存在于 `out/logs/task10-integration-notes.md`，而 `out/` 是 gitignored**（见 §13），所以在此固化。

### 12.1 最后一块 ABI：content 查询的完成 payload kind

- 新增 **`FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2`** 到 `fly_session_authenticated_operation_payload_kind_v2`
  —— 取该枚举**下一个空闲值**；`101/102` 与 `201/202+` 段已被占用。
- 形状必须是 **hash 形**：`hash[32] = SHA256("flynes-content-choice-v1" || u32be(len) || exact record)`，
  `resource` 携带 choice index，**`terminal = 1`**。
  理由：provider 事件的 buffer 形是 `terminal = 0`，**无法终结一个登记进 journal 的操作** —— 这正是对象读当初改用 hash 形的原因。
- 在 `shared/src/session/ports/provider_events.cpp` 加 `contract_for()` 条目，并加 `parse_provider_event_v2` 分支。

### 12.2 引擎侧接线

- **query effect**（token + index），journal 的 expect/accept 与对象读完全同法（hash 形才可登记 journal）。
- **answer handler**：把返回的 hash 变成已发布的 `fly_session_game_choice_v2`，字段来自规范记录的固定偏移：

  | 目标字段 | 来源 |
  |---|---|
  | `content_id` | record offset **20** |
  | `source_choice_ref` | record offset **4** |
  | `display_name_size` | record offset **52** |
  | name | record offset **56**（1..64 字节） |

- 引擎本来就需要一条真正的 **game 视图发布路径**（第 1b 项也要用）。
- **`SELECT_CONTENT_V2`**：把到达的 `choice.choice_kind == FLY_SESSION_CHOICE_REFERENCE_V2` 且 `choice_id[16]` 与已发布的 `source_choice_ref` 匹配，然后把解析出的 32 字节 content id **冻结进 DUAL content reference**。
- 端口槽位的 old-prefix 兼容已由 `FLY_SESSION_PORTS_V2_R2_SIZE` 的相关测试证明；新工作只需要补 **payload-kind 的测试**。

### 12.3 然后是 1b–5（顺序不变）

1b 引擎 DUAL reducer（干掉 `session_engine.cpp:5642` catch-all、game/config/seat/authority 状态、真 game 视图发布替换 `:322` 的硬编码 `NOT_STARTED`、修 `:2428` vs `session_view.cpp:66` 的 scope 死路、发送前 `normalize_dual_port_mask_v1`）
→ 2 State Commit 流（引擎新建组件持有；每方向 read/credit；入站 `app_frame` 路由；`FLY_SESSION_PROVIDER_QUIC_DATA_V2` 走 `parse_provider_event_v2`、**永不进 `ProviderOperationJournal`**；`0x0201` 不进 allow-list）
→ 3 输入端到端（154 字节 `CanonicalInputBundleV1`）
→ 4 **600 帧双 engine E2E**（第三条流 relay；双端确认座位/配置；每 seat ≥100 个可辨认 input edge；双方输入生效；两端 `dual_state_digest`/`dual_frame_digest` **逐 60 帧边界相等，且由快照观测**）
→ 5 回归。

不动的东西：`acknowledge_peer_digest`/恢复成功保持 fail-closed 记 NOT_RUN（Task 12）；不实现 manifest/flush/commit_watermark；Android unit 门禁除非以 worktree 为显式工作目录否则 NOT VERIFIED。

### 12.4 必须继承的两条纪律

1. **构建驱动脚本必须保留 QUIC 断言**：`wsl-test.sh` 与 `task10-cycle.sh` 在缓存不是 ON / cargo NOTFOUND 时以 `ABORT_PROVIDER_OFF`(92) / `ABORT_CARGO_NOTFOUND`(93) 中止。任何后继脚本都要照做，否则真实 QUIC 证据会再次悄悄变成"无法支撑"（见 §3.1）。
2. **任何 "Not Run" 一律当 FAIL 并复跑**（验收文档自己的规则）。W0 见到的那次来自它自己"删二进制再重建"的窗口，复跑得 42/42。

### 12.5 W0 交接时的实测状态

- 从 `2128e7f` 到 `c7bcd79` 共 8 笔提交，各自在自己的门禁上全绿；工作树干净；无任何断言被弱化。
- `ctest -L nearby` = **42/42**（构建脚本断言 Rust provider 为 `ON`）
- 全量 shared CTest = **97/99**，2 个失败仍是那两项既有非 nearby 问题
- **`MVP-DUAL` 仍为 NOT_RUN**，剩余工作就是 §12.1–12.3

---

## 13. ⚠️ `out/` 是 gitignored —— 所有设计档案都不在版本控制里

以下文件**只存在于本机磁盘**，不在任何提交里，worktree 一旦清理或换机器就全部丢失：

```
.worktrees/nearby-ui-acceptance-fixes/out/logs/task10-integration-notes.md   ← 四个缺口的逐条 file:line 证据 + 第五轮设计
.worktrees/nearby-ui-acceptance-fixes/out/logs/three-platform-acceptance-env-20260916.md
.worktrees/nearby-ui-acceptance-fixes/out/logs/w3-merge-checklist.md
.worktrees/nearby-ui-acceptance-fixes/out/logs/final-acceptance-runbook.md
.worktrees/nearby-ui-acceptance-fixes/out/logs/w1w2w3-conventions.md
.worktrees/nearby-ui-acceptance-fixes/out/logs/wsl-test.sh / task10-cycle.sh   ← 含 QUIC 断言的两个驱动脚本
.worktrees/nearby-dual-e2e-harness/out/w3-verify.sh
```

**接手者第一步：把这些拷到版本控制里**（或至少拷到本交接文档旁边）。本文档 §12 已经固化第五轮设计，但四个缺口的完整 file:line dossier、以及两个构建驱动脚本的 QUIC 断言逻辑，仍然只在 `out/` 里。
