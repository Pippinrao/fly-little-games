> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby 双端非真机验收追踪（骨架） — 2026-09-15

**当前源树请读** [2026-09-17 Nearby 可玩进度台账](2026-09-17-nearby-playable-progress.md)。本文保留 2026-09-16 及更早的历史命令记录；**不得把历史 PASS 当成当前 dirty 树的复验结果**。

层级（与纠偏方案一致，不再混用「MVP-DUAL PASS」与同文 NOT_RUN）：

| 层级 | 含义 | 本文历史门禁如何读 |
|---|---|---|
| L1 | codec/scheduler + fake runtime / 进程内 LoopbackTransport | `DUAL-RUN` 600 帧、`MVP-DUAL` 假 runtime、`MVP-LOBBY` loopback 属 L1 |
| L2 | 两 V2 engine + 真 Quinn + 真 NES runtime | 历史未达到；crate 自测 + linkage 不能单独升到 L2 |
| L3 | 两原生应用实例从原稿界面完成一局 | 历史模拟器 Nearby 投影不是 L3 可玩 |
| L4 | 真机 / 物理 radio / 安全存储 / 时延 | 一律 NOT_RUN |

当前 worktree 若无绑定完整 SHA + dirty 指纹的新命令输出，对应门禁标 **未复验**，即使下表仍写着当时的 PASS。

本文是 Nearby 双端联机**非真机验收文档**（计划 Task 16）的需求追踪表骨架：
逐项登记总体方案 B01–B16、接口合同 IF01–IF16 与测试设计 81 个用例族，
**状态列一律留空 `—`**，等待 W0 依据本次可复核的实测证据填写。
本文不作任何通过/失败判断，也不重复已获批设计文档的正文。

条目来源（三份均为已获批文档，本 worktree 内）：

- `docs/superpowers/specs/2026-09-13-nearby-backend-repair-design.md` —— 总体方案 B01–B16（该文档以表格条目形式列出，无缺号）。
- `docs/superpowers/specs/2026-09-13-nearby-interface-design.md` —— 接口合同 IF01–IF16（以章节标题形式列出，无缺号）。
- `docs/superpowers/specs/2026-09-13-nearby-test-design.md` —— 用例族 API/PORT/PAIR/WIRE/GAME/CONTENT/MEDIA/REC/UX/PERF 共 81 条，与其 §10 自述的族数一致。

提取方式：各 ID 均按原文逐字摘出（B 取方案表第二列，IF 取章节标题，用例取表格的场景列；REC 取「事务族 + 决策前期望」）。
若后续设计与本文不一致，以已获批设计文档为准并回改本表。

## DUAL MVP 门禁状态（2026-09-16 实测）

以下每条状态都对应**本次可复核的命令输出**；`nearby_*` 全部在 WSL(Ubuntu-24.04) 构建并运行（Windows 上只编译不运行，原因见后文测试运行环境）。

| 门禁 | 状态 | 证据 |
|---|---|---|
| `BASE-0212` | PASS | public engine 测试断言两个 `0x0212` 持久化门禁（SecureStore KeyRef + exact 312 字节 ObjectStore 对象）缺一不可；全量回归 0 failure |
| `BASE-PARALLEL` | PASS | 三份获批文档 SHA-256 与源一致；共同基线提交 `e8703e4`；两份合同冻结 |
| `LINK-WIRE` | PASS | `flynes_link_hello_wire` / `flynes_link_ready_wire` PASS；golden 由**独立 oracle**（`gen_link_control_golden.py`，不调用被测 encoder）重算 |
| `LINK-SCHED` | PASS | `flynes_link_handshake_scheduler` / `flynes_link_handshake_races` PASS；`control_stream_loopback_closes_the_lobby()` 让两个公开 scheduler 在**零手工 `accept_peer_*` 注入**下闭合 |
| `DUAL-INPUT` | PASS | `nearby_canonical_input` / `nearby_input_window` PASS（毫秒级）；规范化解 dpad 冲突、拒绝错 owner / 旧 revision / 跨 branch / sequence 回退 / 溢出 |
| `DUAL-RUN` | PASS（**L1 历史**；当前树未复验） | `nearby_dual_run_scheduler` / `nearby_dual_run_races`；600 帧确定性 trace。**假 runtime / 非 NestopiaUE。不能记 L2 REAL-CORE。** |
| `E2E-HARNESS` | PASS（**证据已更正**） | 见下方「真实 loopback QUIC 证据更正」：Rust provider 必须 `FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON` 才有意义；本次实测 crate 真 loopback 测试 `cargo test --release` = **6 passed / 0 failed**（TLS1.3 + ALPN `flynes-nearby/2` + pin + exporter 一致 + stream/datagram 往返），`flynes_quic_provider_linkage` PASS 证明静态库真的被链接并调用。原先登记的「产品 crate 探针 `PASS=37 FAIL=0`」在本 worktree 的 CMake 里**不存在该目标**，已删除 |
| `E2E-SCENARIOS` | **未复验（ENG-01 收编中）** | 历史：W3 场景未在当时 W0 HEAD 跑。2026-09-17 已把 `test_two_engine_tamper_matrix` 与 fixture tamper 钩子收入 W0 dirty 树；**通过前不得写 PASS**。W3 dual_run/recovery 场景未复制（见进度台账） |
| `MVP-LOBBY` | **PASS（限定为进程内回环，见更正）** | 见下方 §MVP-LOBBY 证据 |
| `MVP-DUAL` | **L1 历史 PASS；当前树未复验；不是产品可玩** | 历史 `flynes_two_engine_dual_mvp`：catalog SELECT、START_DUAL、600 帧本机 digest、pause/disconnect freeze。runtime 为 fixture 假 DualRuntime，传输为 LoopbackTransport。**不得称为 L2/L3。** |

Harmony / iOS 非真机（本轮）：

| 门禁 | 状态 | 证据 |
|---|---|---|
| Harmony host CTest | PASS | WSL `harmony/tests` 12/12 after GameCenterRow initializer fix |
| Harmony assembleHap / ohosTest HAP | PASS（unsigned install） | `assembleHap` BUILD SUCCESSFUL；uninstall `com.flynes.emu` 后 `entry-default-unsigned.hap` 与 `entry-ohosTest-unsigned.hap` 均 `install bundle successfully`。`signingConfigs` 仍为空，未做签名 HAP。 |
| Harmony Hypium on 127.0.0.1:5557 | PASS | `hdc shell aa start -a TestAbility -b com.flynes.emu -m entry_test` → `[Hypium]total cases:34;failure 0,error 0,pass 34`（含 NearbyService 10 条）。模拟器不认证物理 radio。 |
| iOS FlyNESRuntimeTests | PASS | 50 tests, 0 failures on iPhone 14 16.4 `988243AC-5704-45B6-9151-FF3A9B7AFD35`（本 worktree 专目录 `~/Developer/fly-little-games-nearby-w0`） |
| iOS FlyNESUITests | PASS | 17 tests, 0 failures。Nearby 4 + Product 10 + Import 3。根因：`nearby_multiplayer_filter` 的 AppStorage 未还原导致后续套件看不到单人内置卡；测试已还原并在 Import/Product setUp 关过滤。 |
| shared 全量 0 failure | **FAIL（2 项，均与本轮 Nearby 工作无关）** | Linux 全量（**provider ON**）`97/99`：`flynes_runtime_pcm_contention`（既有 `-Werror=subobject-linkage`，未触碰）与 `flynes_zip_payload_fixture_corpus_check`（既有 fixture 字节在 Linux 检出下不一致）。**`nearby_*` 子集 41/41 全绿** |
| Android unit/build 0 failure | **PASS** | `:app:testDebugUnitTest` BUILD SUCCESSFUL；`:app:assembleDebug` BUILD SUCCESSFUL；Nearby instrumentation 28/28 on emulator-5554 (AVD FlyNES_BuiltinContent). Physical radio/Keystore NOT_RUN. |
| STREAM provider/codec/media 调用次数为 0 | **PASS（结构证明 + 审计，见 §STREAM 证据）** | 公开 ABI 无任何 codec/media/encoder/decoder/sink provider 表，因此不存在可数的调用；以编译期结构钉住 + 全仓审计 `grep -rEn 'ports_?\.\w*(codec\|media\|encoder\|decoder\|sink)' shared/src/session \| wc -l` = 0 |
| 所有真机条目明确 NOT_RUN | PASS（记录形式） | 真机与物理性能项一律 `DEFERRED`/`NOT_RUN`，未做任何设备认证声明 |

### 真实 loopback QUIC 证据更正（2026-09-17）

**发现**：`out/nearby-host-linux/shared/build/CMakeCache.txt` 里
`FLYNES_CARGO_EXECUTABLE:FILEPATH=FLYNES_CARGO_EXECUTABLE-NOTFOUND`、
`FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=OFF`，且不存在任何真实 QUIC 探针二进制。
根因（本轮实测，与最初归因略有不同）：本 distro 里 **`cargo` 即使对 login shell 也不在 PATH**
（`bash -lc 'command -v cargo'` 为空，而 `/home/pippin/.cargo/bin/cargo` 存在），
`shared/CMakeLists.txt:352` 的 `find_program(FLYNES_CARGO_EXECUTABLE cargo)` 失败后
`:356` 把 `FLYNES_ENABLE_RUST_QUIC_PROVIDER` **FORCE 置 OFF**——于是「真实 Quinn」被静默移除。

**影响**：提交 `2128e7f` 记录的 `E2E-HARNESS`/`MVP-LOBBY` 证据里凡是「真实 loopback QUIC」的说法，
在那次构建下**都不可复核**。

**本轮更正与复验**（provider 确实 ON）：
- 两个构建脚本（`out/logs/wsl-test.sh`、`out/logs/task10-cycle.sh`）改为：显式把
  `$HOME/.cargo/bin` 加入 PATH、显式传 `-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=…`，
  并在 configure 之后**断言** `FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=ON` 且 cargo 不是 `-NOTFOUND`，
  否则立刻 `ABORT_PROVIDER_OFF`/`ABORT_CARGO_NOTFOUND` 退出（静默降级不再可能）。
- 复验结果：`cargo test --release`（`shared/nearby-quic-provider`）= **6 passed / 0 failed**；
  `flynes_quic_provider_linkage_test` PASS（"QUIC provider linkage tests passed"）；
  全量 CTest = `97/99`，两项失败与 provider 无关。
- 同时删除本表中不可复核的「探针 `PASS=37 FAIL=0`」条目——该目标在本 worktree 的 CMake 中不存在。

**两个**必须分开的**声明**（此前混为一谈）：
1. `flynes_two_engine_connected_lobby_e2e_test` 走的是**夹具自带的进程内 `LoopbackTransport`**（byte-accurate，
   零 injected class-2 事件），**不使用** Rust provider。它证明的是**引擎接线与协议闭合**。
2. 「真实 Quinn」由 crate 自测 + `flynes_quic_provider_linkage` 证明，是**另一条**声明。
§MVP-LOBBY 的 QUIC 端口计数因此只说明「引擎真的驱动了 QUIC 端口的全部原语」，不说明「跑在真 Quinn 上」。

### MVP-LOBBY 证据

命令：`./out/nearby-host-linux/shared/build/flynes_two_engine_connected_lobby_e2e_test`（跑前删除目标二进制，排除陈旧产物造成的假 PASS）

```
link states: inviter=8 joiner=8                 # 8 = FLY_SESSION_LINK_CONNECTED_LOBBY_V2，两个 engine 分别断言
object kinds persisted: 两侧均含 0x0212 / 0x0213 / 0x0216 / 0x0217
exporter (link): 9e8fd3cd…（两侧交付值与之完全一致）
双向真实单位：0x0001/252B(bind 流) → 0x0212/318B → 0x0216/494B → 0x0217/438B ×2 (Control 流)
  # 318 = 6 字节 app-frame 头 + 312；494 = 6 + 488；438 = 6 + 432，与冻结尺寸逐一对上
**进程内**夹具回环上的 QUIC 端口两侧都被真实驱动（**不是** Rust Quinn，见上方更正）：
  inviter listen=1 connect=0 inspect=1 exporter=1 open=0 accept=2 write=6 read=7
  joiner  listen=0 connect=1 inspect=1 exporter=1 open=2 accept=0 write=6 read=6
单边 READY 负例：扣留一个方向的 0x0217 → 双方停在 7(CONNECTING)；释放 → 双方到 8
```

- **零 injected verified evidence**：跨端每个字节都 `std::equal` 于写入方自己的 `written_fragments`；测试**不自造** `DISCOVERY_CONNECTION`/`DISCOVERY_BYTES`/`QUIC_DATA`（这些「第 2 类」事件只由 `LoopbackTransport` 产生）；pump 只应答「第 1 类」操作终端，且逐 port 计数 1:1 恒等式成立。
- `nearby_*` 41/41 PASSED（provider ON；新增 `nearby_dual_stream_closed`、`nearby_dual_runtime_seam`）。

### STREAM 证据（结构证明 + 审计）

公开 ABI 里**不存在**任何 codec/media/encoder/decoder/sink provider 表，因此「调用次数」不是可测量——
本门禁以「不可能存在该调用」来证明，而不是数一个恒为 0 的计数器：
- 编译期：`fly_session_dual_runtime_port_v2` 以 `state_digest` 结尾、`fly_session_ports_v2` 以 DUAL runtime
  槽结尾（追加任何媒体入口即编译失败），见 `test_dual_stream_closed.cpp`；
- capability：本机宣告掩码 `== kLinkCapabilityDualV1`；STREAM-only/空提案 → `UnsupportedByThisRelease`；
  未知位 → `UnknownCriticalCapability`；`DualModeV1::HostStream` → `FLY_SESSION_V2_UNAVAILABLE`；
- 审计（本次实跑）：`grep -rEn 'ports_?\.\w*(codec|media|encoder|decoder|sink)' shared/src/session | wc -l` = **0**；
  `grep -cE 'fly_session_\w*(codec|media|encoder|decoder|sink)' shared/include/flynes/flynes_session.h` = **0**。

### MVP-DUAL 现状（仍 NOT_RUN）

已落地并绿的增量（每笔都以「删二进制重建 + `ctest -L nearby` 全绿」收尾）：

| commit | 内容 |
|---|---|
| `d3f1e2d` | 最小公开 DUAL action/snapshot ABI（尾追加 + 旧 prefix 兼容；`SELECT_CONTENT_V2=42`、`START_DUAL_V2=43`、`input.port_mask[4]`、快照 DUAL 运行状态、`fly_session_dual_runtime_port_v2` + `ports_v2.dual_runtime`） |
| `e87cbe3` | STREAM 显式关闭的测试与结构钉住 |
| `53f734f` | `dual/canonical_input_wire`（冻结 154 字节 `CanonicalInputBundleV1` 编解码）+ `dual/dual_runtime_adapter`（用公开 C 表实现冻结 `DualRuntimePort`） |
| `11b3179` | 快照发布 `dual_state_digest`/`dual_frame_digest`/`dual_pcm_digest`（60 帧收敛断言可观测的前提） |

**未做（因此 `MVP-DUAL` 不成立）**：引擎 DUAL reducer（动作仍落到 `session_engine.cpp:5642` 的 catch-all）、
State Commit 流、输入端到端传输、双 engine 600 帧 E2E。阻塞项与裁决请求见
`out/logs/task10-integration-notes.md`；其中内容引用来源已由 W0 裁决为「尾追加只读 content 端口」（未开始）。

**如实标注的一项**：`CONNECTED_LOBBY` 处 `pairing()` 子视图实测为 **EMPTY**（引擎只在 `AUTHENTICATING`/`PROVISIONING` 暴露该子视图，`session_engine.cpp:326-331`）。测试按**实测**钉住 EMPTY，**未伪造 CONFIRMED**。若平台 UI 需要在已连接状态展示配对信息，属后续平台波次的产品改动，本轮不改引擎。

### 这次 E2E 挖出的 4 个「只有两个真 engine 一起跑才暴露」的引擎缺陷

组件级测试永远测不到，因为每个组件单独测都是对的：

| 提交 | 缺陷 |
|---|---|
| `6ab2c7a` | 对端**早到的** `PairKnownStatus`/`Branch` 在本地 `pair_known_` 尚未建立时被判非法 → 改为有界 hold + 就绪后按序 replay（replay 走同一个 `accept_peer_envelope`，stage/counter/generation/HMAC 校验一条未绕过） |
| `970e484` | 非发起方 `mark_local_signature_sent()` 造出的 transcript persist effect **从不被派发**（写完成分支只处理失败路径）→ 引擎带 pending effect 空转、无任何 port 计数增长 |
| `dbb817b` | 调度器 operation id 未整块预留，同一 id 同时发给 `crypto.open` 与 GATT 写，随后被自己的去重判 `-15` |
| 顺带修复 | `wire/app_frame` 把「body 短于该 tag 的 fixed length」误报成 `Truncated`，与「记录没收到」不可区分 → 一个 6 字节头声称短 `0x0216` 的**敌意帧会让 attempt 永久挂起**而不是 fail-closed |

另有一处经查证**不是**缺陷：`kKnownBranchBodySizeV1 = 112`（112 字节 body + 64 字节 ECDSA `branch_proof`），与设计文档逐字节一致，维持现状。

## 证据分级约定

本套验收文档只使用下列五种状态，语义互不混用：

| 状态 | 含义 |
|---|---|
| `PASS` | 有本次可复核的命令输出证据 |
| `FAIL` | 实测不通过 |
| `BLOCKED` | 有明确环境/依赖阻塞，必须附阻塞原因 |
| `NOT_RUN` | 根本没有执行 |
| `DEFERRED` | 本轮明确延期（STREAM 专属 MEDIA/PERF，以及真机物理项） |

三条硬规则，不得违反：

1. **0 tests matched 视为 FAIL**。筛出的测试目标一个都没匹配上（空跑、目标改名、标签未注册）时记 `FAIL`，不是 `PASS`。
2. **只编译通过不算 PASS**。构建成功、类型检查通过、包能产出，都不能代替该条要求的实测断言结果。
3. **不得把 `BLOCKED`/`NOT_RUN` 写成 `PASS`**，也不得用 mock、另一平台的结果、模拟器或截图替代本条要求声明的证据形式。

补充约定：`证据/备注` 列当前只登记设计交叉引用（层级、C 编号/IF 编号对应），**不得当作状态证据**；实测命令、输出摘要与阻塞原因由 W0 追加。

## 追踪表

### 1. 总体方案 B01–B16

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| B01 | production 公共引擎不链接平台 UI/OS 头；所有系统访问经 ports；三端不存在复制的配对/游戏 reducer | PASS（结构 + host） | `core/` 平台无关门禁；session 经 ports；三端 Nearby UI 投影同一 identifier。未做全仓链接审计本轮复跑 |
| B02 | 完成回调重复不重做；旧代次回调不破坏新连接；相同 operation key 冲突结果可检测 | PASS（host） | LINK-SCHED / `dbb817b` operation id 预留；API04/05 类 host。真并发 OS 回调 L4 NOT_RUN |
| B03 | 无 ROM 完成码/QR 认证并进入同一空大厅；验证未完成绝不显示“双人联机中” | PASS（host loopback） | MVP-LOBBY 两 engine CONNECTED_LOBBY、无 ROM；START_DUAL 无选择 REJECTED。真码/QR L4 NOT_RUN |
| B04 | 错码、过期、刷新、并发两入口、匹配冲突、拒绝、退出、低 GATT MTU、错 pin/反射/重放/PSK 均安全结束当前尝试 | PASS（host 负例部分） | W3 篡改矩阵 fail-closed；单边 READY 停在 CONNECTING。真 BLE MTU/PSK L4 NOT_RUN |
| B05 | 对所有允许消息逐字节切分/拼接、FIN 半包、错流/type/channel、长度溢出、未知 critical、未绑定早到包验证无越权效果 | PASS（host WIRE） | `nearby_*` wire/app-frame；早到 hold+replay。模糊测试未扩到本轮 |
| B06 | 所有 platform-pair/authority/seat 组合的端口映射一致；连续 press/release 在丢包/乱序/repair 下不被吞掉 | PASS（host 默认座位） | START_DUAL inviter0/joiner1；canonical input 拒绝回退。换 authority 全矩阵 NOT_RUN |
| B07 | 截止已发 target 不平移；已 committed frame 不重写；raw/rollback 达界冻结；重放只发布最终画面 | PASS（host DualRun） | `nearby_dual_run_scheduler`；pause/disconnect freeze。STREAM 回放 DEFERRED |
| B08 | runtime 源 PCM 正确区分真实零样本与欠载；两消费者不重复 destructive pull；同 published sequence 的 authority/guest bytes/hash 一致 | DEFERRED | STREAM/媒体；`flynes_runtime_pcm_contention` 本轮未修 |
| B09 | 视频缺片/旧 generation/decoder 晚回调丢弃；IDR/config 前不显示；音频 FEC 单丢失恢复、多丢失记录静音，静音不停止 timeline | DEFERRED | STREAM |
| B10 | guest 本机 touch→最终成为 canonical committed 的对应输入真实 presentation：DUAL p95≤80 ms、STREAM p95≤150 ms；… | NOT_RUN | 模拟器不认证时延；真机 PERF DEFERRED |
| B11 | storage flush/replace 失败不推进 committed/replayable；active config/head/SRAM 原子一致；所有崩溃点无双 writer | PASS（host REC06） | BeforeFlush / AfterFlushBeforeCas 保留旧整组；STALE CAS read_root 重试 |
| B12 | 30 秒 deadline 不被本机 send/重启延期；recovery 未释放不能恢复 step；暂停中断重连后仍暂停 | PASS（host REC04/02） | watchdog 29.999/30/30+1ns；300ms freeze；pause 重连仍暂停。two-engine 30s 到期仅 unit（freeze 后 submit_input 拒 RUNNING） |
| B13 | 无 ROM 的 guest 只能保存不透明包；有完整 proof 才可接管；分区后用户新 branch 不覆盖另一未来 | PASS（host REC05 部分） | 无 end proof 不能 export；terminal writer 不复活。真分区 L4 NOT_RUN |
| B14 | 换游戏后新 branch/config，旧输入/媒体/确认/WAL 结果不生效；旧 grant 关闭、完整 ledger 交接、CAS/source release、双方不一致 link 摘要按 §4.4–§4.5 收敛；大厅类别/搜索/双人过滤不被连接改变 | PASS（host REC07–09 + iOS 过滤独立） | ledger 换局不归零；九行矩阵；`GameCenterMultiplayerOnly` 与连接无关。完整 ChildInputCloseV2 wire 仍精简 |
| B15 | 三端同 fixture 的 snapshot/动作/错误及逻辑尺寸 UI 相同；OS 权限和实际能力差异映射一致 | PASS（三端模拟器 Nearby 投影） | Android 28 Nearby + Harmony Hypium 34 + iOS UITests Nearby 套件同 identifier。逻辑宽 580dp 仅 Android 一条。真权限弹窗 L4 NOT_RUN |
| B16 | 真实离线已认证 bearer 任意支持组合可用，不能拿 Android↔Windows probe、路由器 LAN、模拟器帧率或 HTML 截图代替 | NOT_RUN | 本轮明确非真机；loopback/模拟器不作 bearer 认证 |

B09/B10 在同一设计文档正文另有媒体门禁补充，需一并验收：guest 有效帧率不低于声明源帧率 92%（NTSC 至少 55 fps、PAL 至少 46 fps）；不得出现连续 100 ms 及以上音频欠载；未静音 10 分钟窗口内 gain-before 非 canonical/插入静音总量 ≤200 ms 且 ≤0.1%，单次 correction ≤200 ms。精确采样与统计见测试设计 §8。

### 2. 接口合同 IF01–IF16

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| IF01 | 分层与 ABI 选择：保留 `shared/include/flynes/flynes_session.h` 作为 session 公共入口，追加 V2 类型和函数，V1 大小/字段/枚举/拒绝语义不变 | PASS（host ABI） | `test_session_v2_abi` / STREAM 结构钉住；尾追加 SELECT=42 START_DUAL=43 CONTENT_CHOICE=290 |
| IF02 | 基础类型与结果：类型、字段/值域/所有权 | PASS（host） | ABI size/reserved；EMPTY 同步 query 返回 |
| IF03 | Session 公共 API 签名与交互形状 | PASS（host） | acquire_view/submit SELECT/START_DUAL/PAUSE；2 槽队列既有测试 |
| IF04 | View 读取 | PASS（host） | lobby/game_choices/dual snapshot digests |
| IF05 | UI action 合同：action_kind 完整首版业务集合；公共层绑定上下文/用户选择，UI 不在 submit 重传 hash/revision | PASS（host） | SELECT_CONTENT 用 16-byte source_choice_ref；SAS 不进 action（W3） |
| IF06 | 公共 product policy：与 UX 逐页对齐 | PASS（三端投影） | 同 identifier；大厅字段 blocked_session_read |
| IF07 | 所有 port 的公共模式：异步方法 `start(OpToken, Request) → ACCEPTED \| error`，完成经 `deliver(inbox, Event)`，ACCEPTED 才接管引用 | PASS（host） | content/QUIC GrantRead/Write；token 碰撞已修 |
| IF08 | 时钟、executor、权限、扫码和平台事实 | PASS（host 时钟） | `g_loopback_clock_ns` + engine `on_clock`。真相机/权限 L4 NOT_RUN |
| IF09 | BLE 与 bearer | NOT_RUN | 模拟器无 BLE；loopback 替代 |
| IF10 | QUIC | PASS（crate + linkage；引擎 loopback） | `cargo test --release` 6/6；`flynes_quic_provider_linkage`；两 engine 用 LoopbackTransport 非 Quinn |
| IF11 | key、crypto、secure store、object store 和 content | PASS（host） | 0x0212 312B；content hash CONTENT_CHOICE；durable root CAS |
| IF12 | Runtime：默认 RuntimePort 包装公共 `fly_runtime`，不搬模拟算法；除 capability/只读元数据外仅 simulation worker 串行调用，active 与 scratch 用不同 handle | PASS（host adapter） | dual_runtime_adapter；600 帧 fake runtime |
| IF13 | Codec 与 sinks | DEFERRED | STREAM；公开 ABI 无 codec/media 表 |
| IF14 | 装配和资源释放次序：composition root 创建时钟/executor/系统 provider 并调用 create，引擎持引用 | PASS（host fixture） | LobbyPair 装配。平台 composition 真机 NOT_RUN |
| IF15 | 错误到 UX 的映射 | PASS（三端投影） | 首失败 stage + reason；join 无 bearer 显示 discovery reason |
| IF16 | 接口完成与 UX 对齐检查 | PASS（本轮非真机范围） | 追踪表已按证据填；L4/STREAM 显式 NOT_RUN/DEFERRED |

### 3. 测试条目

用例族共 81 条，与测试设计 §10 自述数量一致（API15、PORT8、PAIR6、WIRE3、GAME7、CONTENT3、MEDIA8、REC9、UX18、PERF4）。
本表 3.1–3.8 列 API/PORT/PAIR/WIRE/GAME/CONTENT/REC/UX；MEDIA/PERF 见第 4 节（预填 `DEFERRED`）。

#### 3.1 API（API01–API15，15 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| API01 | C/C++ 最小消费者只包含公共头；V1/V2 各自 size；未知 version/非零 reserved | PASS（host ABI） | `test_session_v2_abi` / STREAM 结构钉住 |
| API02 | 缺 Clock/Executor/认证 provider；缺 Camera；缺 encoder | NOT_RUN | 缺 encoder 合法（STREAM 关）；本轮未单列复跑缺 Clock 用例 |
| API03 | action 入队后更换 config，再消费旧 CONFIRM；同 request_id 重复；另测仅倒计时/view 刷新 | NOT_RUN | 本轮未单列 |
| API04 | start 返回 error 后违规回调；ACCEPTED 后重复相同 terminal 和不同 terminal | NOT_RUN | 本轮未单列 |
| API05 | generation g 取消后创建 g+1，再送 g 的 Wi-Fi/QR/codec/storage 结果 | NOT_RUN | 本轮未单列 |
| API06 | 2 槽 action 队列和结果槽填满；再投普通动作、terminal、shutdown | NOT_RUN | 本轮未单列 |
| API07 | bytes 提交后调用者立即改写/释放；接受/拒绝/取消每种结果 | NOT_RUN | 本轮未单列 ASan |
| API08 | UI 持 view r；engine 产生 r+1 并 destroy；旧 view 分页读取再 release | NOT_RUN | 本轮未单列 ASan |
| API09 | ports 在 start 内直接投递；同时任意线程回调 | NOT_RUN | 本轮未单列 TSan |
| API10 | begin_shutdown 后持续送 callback；某 port 永不 terminal | NOT_RUN | 本轮未单列 |
| API11 | source/媒体 sequence 从 0 开始、uint64 上界、大小乘法溢出 | DEFERRED | 媒体 sequence 属 STREAM |
| API12 | release 产品链接/启动配置、测试 provider 参数注入尝试 | NOT_RUN | 本轮未单列 |
| API13 | 独立 C 消费者仅经 acquire_view/copy_actions/copy_game_choices … | NOT_RUN | 本轮未单列独立 C 消费者 |
| API14 | 保持 engine/connection/game scope 相同，仅推进 authority_term … | NOT_RUN | 本轮未单列 |
| API15 | retain 公开 ApprovalToken 后 release 原 view … | NOT_RUN | 本轮未单列 ASan |

#### 3.2 PORT（PORT01–PORT08，8 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| PORT01 | 两端不同 raw clock；暂停/后台 10 秒；clock generation 变化 | PASS（host 时钟部分） | `g_loopback_clock_ns` + 300ms freeze。后台 10 秒 L4 NOT_RUN |
| PORT02 | GATT MTU23、分片背压、换 MTU、central 与 protocol role 反向 | NOT_RUN | 无 BLE 模拟器 |
| PORT03 | 空认证 plan 交集、错误 creator/confirmation role、拒绝系统弹窗 | NOT_RUN | L4 系统弹窗 |
| PORT04 | full TLS 正确/错误 pin、恢复 ticket、exporter context/长度错误 | PASS（crate Quinn） | `cargo test --release` 6/6 TLS1.3+ALPN+pin+exporter。恢复 ticket 未单列 |
| PORT05 | ObjectStore flush/replace 失败；SecureStore revision 冲突；短读取 | PASS（host REC06） | crash points + STALE CAS |
| PORT06 | codec 请求 profile 与实际输出不符、flush 后迟到帧 | DEFERRED | STREAM |
| PORT07 | 同一音频块向双 sink 提交，host mute/guest 慢消费/route 变化 | DEFERRED | STREAM |
| PORT08 | 真实 provider 创建独立 Identity/SessionSigning/TLS 材料；错 purpose 句柄互换；材料 durable 前后/manifest 切换前后 kill；换 child、原 link 合法恢复、link 终结再新建 | NOT_RUN | 真 crypto/Keystore 物理 NOT_RUN |

#### 3.3 PAIR（PAIR01–PAIR06，6 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| PAIR01 | 创建 60 秒邀请；查询 `012345`；无匹配/多 context 匹配/限流边界 | PASS（UI 输入；匹配引擎部分） | 三端 join 码 `012345`；无 discovery bearer 显示错误。60s 邀请/限流 L2 未单列 |
| PAIR02 | 同一 UI generation 的 code/QR child 并发请求；房主先接受其一 | NOT_RUN | 需两端匹配 |
| PAIR03 | join 提交 commit 但房主未 accept，之后拒绝或页面退出 | NOT_RUN | 需两端匹配 |
| PAIR04 | 真实 BLE/SAS 双端握手，单端确认、码不同、双方确认 | NOT_RUN | L4 SAS |
| PAIR05 | 合法 QR、签名/commitment 替换、反射/重放/到期；接受丢失 | NOT_RUN | L4 QR |
| PAIR06 | 使用总设计唯一 V2 GATT 注册：type27 REQUEST body32 bytes、type28 MATCH body136 bytes，完整 logical 分别 72/176 bytes；交换 outer/body version、方向、type、长度及 nonce/context；注入并行草案 V1 type28/29、0x214/0x215 | PASS（host golden） | `flynes_link_hello_wire` / `flynes_link_ready_wire`；W3 篡改负例 |

#### 3.4 WIRE（WIRE01–WIRE03，3 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| WIRE01 | 所有合法 record 切分/粘合；两 stream 交织；FIN 半包/重复 bind 流 | PASS（host） | LINK-WIRE / app-frame 切分；Control+StateCommit 两 bidi |
| WIRE02 | 类型跨 channel、unknown critical、reserved、计数/长度越界、旧 scope/generation | PASS（host） | 短 body≠Truncated；unknown critical fail-closed；W3 篡改 |
| WIRE03 | bind FIN 之前早到合法 Control；TLS 仅 connected；HELLO 版本不兼容 | PASS（host） | 早到 hold+replay；capability Dual-only |

#### 3.5 GAME（GAME01–GAME07，7 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| GAME01 | 两端无 ROM 建立 link；随后从原大厅提议支持双人的 fixture | PASS（host two-engine） | `flynes_two_engine_dual_mvp`：无 content port 仍 CONNECTED_LOBBY；有 catalog 后 SELECT 同一 fixture。真 BLE 双端 L4 NOT_RUN |
| GAME02 | authority=P1/P2、换 authority/seat/content/profile；本机 mute/查询改变 | NOT_RUN | START_DUAL 默认 inviter seat0/joiner seat1；换 authority/mute 未测 |
| GAME03 | seq100 按 A 于 frame42，seq101 释放于 43；丢/乱序/repair | PASS（host DUAL-INPUT / 600 帧） | `nearby_canonical_input` / `nearby_input_window`；600-frame dual mvp 两侧 ≥100 edges。真丢包 L4 NOT_RUN |
| GAME04 | 相同 input key 同 bytes 重复、不同 mask 或 target；已 committed 冲突 | PASS（host） | DUAL-INPUT 拒绝错 owner / 旧 revision / 跨 branch / sequence 回退 |
| GAME05 | raw=47→48、模拟差 9→10、ring 硬界 12；guest 超前 4→5；追帧量子 | PASS（host DualRunScheduler） | `nearby_dual_run_scheduler` / races |
| GAME06 | D=2/3/4 与 PAL/NTSC；120 中立帧门禁通过/摘要不一致 | NOT_RUN | 600 帧本机 digest 每 60 帧比对；PAL/NTSC D 矩阵未跑 |
| GAME07 | 准备期返回大厅、运行期 RETURN_TO_LOBBY、换新游戏后旧输入/媒体/确认 | PASS（host 部分） | pause/disconnect freeze；RETURN_TO_LOBBY / 换局旧输入失效未单独立项。媒体 DEFERRED |

#### 3.6 CONTENT（CONTENT01–CONTENT03，3 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| CONTENT01 | 只批准 send；再批准 receive；完成 hash 校验但未批准 import | PASS（host） | `flynes_two_engine_dual_mvp` send-only 零 Rom；both-consents 打开 stream_kind 5 并等到 APPROVE_IMPORT |
| CONTENT02 | 错 declared hash、>8MiB、offset 越界、10 秒无进展、取消 | PASS（host） | `flynes_content_offer_scheduler` CONTENT02 族 |
| CONTENT03 | 缺 ROM 选择 STREAM 且拒绝补齐；以后获准导入 | PASS（host，STREAM 延期） | 缺 ROM 留大厅；拒绝不回退 STREAM；导入后 SELECT 可重新协商 |

#### 3.7 REC（REC01–REC09，9 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| REC01 | 初始开始/Source handoff：source writer 与候选准备门禁一致；未允许时无 game step | PASS（host reducer） | `flynes_recovery_rec` |
| REC02 | pause/resume/seat/input-delay/resync：冻结并按各自允许的 abort/恢复路径；不越过 input close/prime | PASS（host reducer） | pause 重连后仍暂停；resume 仍 fail-closed |
| REC03 | DUAL→STREAM：pre_safe 可读、旧 branch 冻结，失败不恢复已判无效 DUAL | PASS（host，STREAM 延期） | `request_stream` = UNAVAILABLE；冻结 DUAL 不复活 |
| REC04 | reconnect/release 尾链：原 deadline 不可滑动；30s 边界和 5s 限定 grace 只授权合法 phase | PASS（host） | watchdog 29.999/30/30+1ns；two-engine 300ms freeze；本机 send 不延期 |
| REC05 | save/end/takeover/fork：无合法 proof 不生成可用 user save/接管；保持未决包 | PASS（host reducer） | 无 end proof 不能 export；terminal writer 不复活 |
| REC06 | object GC/存储损坏：先保持旧 root，不能把缺失新 candidate 当成功 | PASS（host） | BeforeFlush / AfterFlushBeforeCas 保留旧整组 |
| REC07 | 空大厅 link 恢复与换局：NONE game 用 link 专用基线，不造 GENESIS 游戏 | PASS（host reducer） | NONE 不 step；换局不把 ledger 归零 |
| REC08 | parent ledger 跨 child 交接：old child 仍 active+FROZEN 时先修复/关闭 reservation；逐 phase 故障覆盖 ChildInputCloseV2、LedgerHandoffV2、PrestartAbortTombstoneV2 及 read_root/CAS | PASS（host 精简） | grant 单调、handoff +1 gen 继承 hash、CAS STALE 后 read_root 重算；完整 wire 对象仍待平台 |
| REC09 | reconnect 混合 child 状态：先交换 phase=ROUTE_ONLY 的 LinkResumeSummaryV2；旧 TAIL_STATUS prelude 完成后 read_root 重读并交换 RECONCILE；覆盖 §4.5 九行并交换方向、故障注入 | PASS（host 九行矩阵） | 硬编码 expected，不按 term/revision 选赢；冲突 repair-blocked |

#### 3.8 UX（UX01–UX18，18 条）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| UX01 | 无 link 进入 G00 | PASS（Harmony 投影；Android/iOS 入口页） | Hypium disconnected→`nearby.open`；Android Nearby 28/28；iOS `testNearbyEntryOpensThePageOnTheDevicesTabWithDisabledDiscovery`。L4 真机 NOT_RUN |
| UX02 | 4 分类×2 开关×空/命中/不命中查询×未连/连接中/已连/中断 | NOT_RUN（全矩阵） | 分类/筛选 host `flynes_product_game_center` PASS；连接态四格仅 Harmony `projectNearbyEntry` 四态。未跑 4×2×查询全矩阵 |
| UX03 | BUILTIN 只有单人；开双人筛选再关 | PASS（iOS 模拟器隔离） | `NearbyUiParityTests` 拨动 `nearby_multiplayer_filter` 后**还原**；Import/Product 在 setUp 关闭过滤后 17/17。过滤 ON 只保留 SUPPORTED，内置卡从网格消失（本轮失败即该证据）。 |
| UX04 | 无选中游戏打开 N00，分别点三动作 | PASS（三端模拟器） | Android `NearbyUiParityTest`；iOS `testN00ShowsTheThreePrimaryActionsInContractOrder`；Harmony 同 identifier 投影。 |
| UX05 | `012345`、首尾空白、空/5/7 位/字母/全角数字/过期 | PASS（三端模拟器规范化） | Android `NearbyInviteCodeTest`；Hypium `invite_code_input_keeps_leading_zeros_and_rejects_the_rest`；iOS `testJoinCodeFormNeverSubmitsIncompleteInput`。过期码 L4 NOT_RUN |
| UX06 | N04/N05 等待 host，拒绝/接受 | NOT_RUN | 需两端真实匹配；模拟器无 discovery bearer |
| UX07 | 一端 SAS 确认、两端不同、两端同 transcript 确认 | NOT_RUN | L4/SAS 视觉比对；IF05 不把 SAS 放进 action。W3 篡改矩阵 host 已过，非 UX 页面 |
| UX08 | 合法 QR/篡改/重放/过期 | NOT_RUN | L4 相机/QR |
| UX09 | socket 成功/ChannelBind 未完/单端 ready/全部 link 门禁 | PASS（host 引擎；UX 投影） | MVP-LOBBY two-engine CONNECTED_LOBBY；Hypium 七段 pipeline。真 socket/Wi-Fi NOT_RUN |
| UX10 | camera 拒绝、Wi-Fi 拒绝、版本失败、codec 未开始 | PASS（Harmony 投影） | `marks_only_the_first_failing_stage_and_never_annotates_later_rows`；无 session stage 时回落 permission。真系统弹窗 L4 NOT_RUN |
| UX11 | N09 某端确认后变 authority/seat/content/plan；另测 mute/展开详情 | NOT_RUN | Harmony `nearbyLobbyRows` 15 字段均 `nearby_blocked_session_read`（尚无活 session 读） |
| UX12 | send-only、send+receive、verified-only、approve-import | PASS（host CONTENT01–03） | 引擎同意链；平台 CONTENT 页未在模拟器走完导入同意 |
| UX13 | 从 N09 回 G00 再换游戏；运行中退出；单人游戏可浏览 | NOT_RUN | 需活 session；host GAME07 未单独立项 |
| UX14 | 已连断线、后台恢复、暂停时断线 | PASS（host REC；Harmony banner 投影） | two-engine pause/disconnect freeze + 300ms watchdog；Hypium frozen/reconnect/timeout banner。真后台 L4 NOT_RUN |
| UX15 | rename/delete/block/reset；匿名发现再次出现 | PASS（三端禁用态） | Android `NearbyFriendsManageTest.allActionsArePresentAndDisabledWithAReason`；Hypium `keeps_every_friends_management_action_disabled_on_the_friend_store`。活 friend store / 匿名再发现 NOT_RUN |
| UX16 | cancel/refresh/timeout 后晚到 QR、SAS、Wi-Fi、codec、确认 | NOT_RUN | Android invite regenerate/cancel 有部分；晚到 QR/SAS L4 |
| UX17 | 所有 screen/弹窗×宽 320/375/550/580/640/736/900/1024 | PASS（Android 580dp 一条） | `NearbyUiParityTest.wideLayoutUsesTheShared224By18Split`（>580dp → 224/18）。其余宽度 NOT_RUN |
| UX18 | 640×360/736×414/844×390×1.0/1.3/2.0 字倍×安全区×键盘 | NOT_RUN | 未跑字倍/安全区矩阵 |

### 4. MEDIA / PERF（本轮预填 DEFERRED）

| ID | 要求摘要 | 状态 | 证据/备注 |
|---|---|---|---|
| MEDIA01 | step_exact 输出真实零 PCM、source first=0；容量不足；旧 pull 欠载 | DEFERRED | STREAM 延期 |
| MEDIA02 | 两帧 staging 内回滚；发布后深回滚 | DEFERRED | STREAM 延期 |
| MEDIA03 | 四块 960-byte 固定模式，逐个丢其中 1 块；再丢 2 块；parity 迟到 | DEFERRED | STREAM 延期 |
| MEDIA04 | payload budget 恰好够 DATA/FEC 和少 1 byte；运行中预算下降 | DEFERRED | STREAM 延期 |
| MEDIA05 | AU 乱序/缺片/重复片/冲突片、3→4 在途、256KiB 边界、新 generation | DEFERRED | STREAM 延期 |
| MEDIA06 | encoder queue 满、慢 decoder、慢 video sink、audio 欠载/静音 | DEFERRED | STREAM 延期 |
| MEDIA07 | 两端 clock offset 差 5 秒、±100ppm drift、播放设备路由切换 | DEFERRED | STREAM 延期 |
| MEDIA08 | DUAL 连续两次 deep audio correction 在 30 秒内、持续失步 | DEFERRED | STREAM 延期 |
| PERF01 | 已认证正常近场条件，每设备配置/模式 3 次独立会话；每次稳定运行至少 10 分钟、至少 1000 个可辨认 touch 动作 | DEFERRED | STREAM 延期 |
| PERF02 | 同一运行记录 source/media/video 因果及本机 audio playout 位置；统计 gain 前 sample status，静音仍计游标 | DEFERRED | STREAM 延期 |
| PERF03 | 至少 30 分钟持续运行、背景/恢复、视频拥塞、2s 链路断开、温控/资源事件 | DEFERRED | STREAM 延期 |
| PERF04 | L2 固定测量反例：同一 edge 在 40ms 呈现预测帧，迟到远端输入使其回滚，100ms 才呈现纠正后的 canonical 帧；另有一 edge 在 1 秒内无 canonical 呈现 | DEFERRED | STREAM 延期 |

## 实测环境与命令

- Worktree: `.worktrees/nearby-ui-acceptance-fixes`（`codex/nearby-ui-acceptance-fixes`），非 main。
- Host Nearby：WSL Ubuntu-24.04，`out/nearby-host-linux/shared/build`，`FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON`。`flynes_two_engine_dual_mvp` / `flynes_recovery_rec` / `flynes_content_offer_scheduler` / `flynes_content_transfer_controller` / `flynes_link_activity_watchdog` 本轮均为 EXIT 0。
- Android：`:app:testDebugUnitTest`、`:app:assembleDebug`、Nearby instrumentation 28/28 on emulator-5554（AVD FlyNES_BuiltinContent）。物理 radio/Keystore NOT_RUN。
- HarmonyOS：host CTest 12/12；unsigned HAP 已打包并在卸载旧包后成功安装到 hdc `127.0.0.1:5557`。Hypium TestAbility `34/34 pass`（`failure 0,error 0`）。`signingConfigs` 仍为空。模拟器不认证物理 radio。
- iOS：本 worktree 同步到 `~/Developer/fly-little-games-nearby-w0`（排除 rust target）。simulator iPhone 14 16.4 UDID `988243AC-5704-45B6-9151-FF3A9B7AFD35`。RuntimeTests **50/50 PASS**。UITests **17/17 PASS**（`TEST EXECUTE SUCCEEDED`）。未覆盖旧 `~/Developer/fly-little-games-ios`。
- 模拟器不认证物理刷新率、功耗、温度、时延。STREAM 仍为 0。未 commit。

## 资源泄漏与负向审计

待填。
