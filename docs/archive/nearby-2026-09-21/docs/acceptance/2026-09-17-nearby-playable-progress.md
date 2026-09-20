> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby 可玩首版进度台账 — 2026-09-17

当前产品集成 worktree（W0）：
`E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`

权威：原稿 `docs/superpowers/specs/assets/nearby-ui-parity-review.html`、获批 UX `docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md`。禁止改原稿迁就实现。

## 认领

| 时间 | 任务 | 状态 | Owner |
|---|---|---|---|
| 2026-09-17 | ENG-01 可复现基线 | **完成** `6ec6605de0f5275295fc08b5905fed3d329f37a4` | INTEGRATOR（本会话） |
| 2026-09-17 | TYPO-00.S 冻结文字角色 | **完成（未提交）** 合同+checker+8/8 单测 | INTEGRATOR（本会话） |
| 2026-09-17 | TYPO-01.H 鸿蒙角色+1.3 入口 | **完成有缺口（未提交）** HAP 绿 / Hypium NOT_RUN | INTEGRATOR（本会话） |
| 2026-09-18 | UX-00.S 页面状态合同及导航表 | **完成（未提交）** screen/action 投影 + PAIR 容器 | INTEGRATOR（本会话） |
| 2026-09-18 | ENG-02 冻结最小接缝 | **完成（未提交）** pending-config 尾追加 + seams + UX kind 映射 | INTEGRATOR（本会话） |
| 2026-09-18 | ENG-05 catalog pending config | **CP1 部分完成（未提交）** v2 解析+指纹仍绿；core id 现为 NestopiaUE `1.53.2`（`nes_core_version()`），不再接受 `nestopiaue` 名称哈希。profile/options 仍为 `default-2p`/`deterministic` 名称哈希。`DualContentRefV1` 仍只有 content_hash | INTEGRATOR（本会话） |
| 2026-09-18 | ENG-06 双确认开局屏障 | **CP2 完成（未提交）** 空 CONFIRM 经 Control `0x0218` 置 peer；双方 runtime-ready 屏障；L1 Fake DualRuntime 600 帧/暂停/REC04 已恢复为真实断言 | INTEGRATOR（本会话） |
| 2026-09-18 | CP0 目录 ABI 兼容复制 | **完成（未提交）** stride + 清零旧调用方按 R0 272 兼容 | INTEGRATOR（本会话） |
| 2026-09-18 | CP3 L2 NES DualRuntimePort | **进行中（未提交）** 两真实 NestopiaUE 端口 600 帧摘要一致且 P2 发散；尚未接入同一测试的产品 Quinn 双 SessionEngine | INTEGRATOR（本会话） |
| — | GATE-0 / ENG-10 前置 | CP2 L1 dual_mvp 已绿。L2 真核心+真 Quinn 同一测试、L3 Android 原生仍缺 | — |

## 1. Worktree 快照（ENG-01 开工读取）

`git worktree list --porcelain`：

| 路径 | HEAD | 分支 | dirty |
|---|---|---|---|
| `E:\workspace\codes\games\fly-little-games` | `5389585f8e3fa7ddc3e3dcd125d9b0a299c6c79c` | `main` | 获批 UX 文档有本地修改；多份 09-17 计划/审计未跟踪。**不在 main 写产品代码。** |
| `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes` | `c7bcd79e3e420268bfb79e9cc0627e5f743f294a` | `codex/nearby-ui-acceptance-fixes` | 是。W1/W2 已合入；大量未提交 DUAL/content/recovery 与三端 UX |
| `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-dual-link-control` | `66c048e21ac3f76d8ee57a2b9751f38c45b77572` | `codex/nearby-dual-link-control` | 干净。已并入 W0，不重做 |
| `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-dual-runtime` | `f0761db04239abd5c308065beb1e5436026cff27` | `codex/nearby-dual-runtime` | 干净。已并入 W0，不重做 |
| `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-dual-e2e-harness` | `5e8e73f2310dddb8cab353e8c6a0a8294c56322b` | `codex/nearby-dual-e2e-harness` | 仅 `.superpowers/sdd/*` 未跟踪。tip 不是 W0 祖先 |

W0∩W3 merge-base = `08f2afdb884416e451260b4bfc9783b5a4ce996f`。

W3 独有提交（未在 W0 HEAD）：`34133b6` `7df4ec6` `3df0deb` `da3bac6` `c333a10` `697c4a1` `e8375c0` `e0c8bf4` `5e8e73f`。

W0 独有提交：`c7bcd79`（只读 content reference 端口尾追加）。

### 设计文件哈希

| 文件 | SHA-256 |
|---|---|
| 原稿 HTML（main 与 W0 相同） | `C69FF8F5CC3A346071DBF2BC6E0F2291232ECB6732A8F38970FFCE1126577C16`（与 UX 任务卡一致） |
| 获批 UX 工作副本（main dirty） | `00ED879C297CF81EBDFB0327D8B8474722318A289DD9F3511B2AC4ABC82D0E86`（任务卡记载） |
| 获批 UX（W0 HEAD 已提交副本） | `DDDAF28D122045FA0EA178CDAB1C7815FCB9FFFBA98323E9C195DA17B23658B5` |

差异：main 工作副本在 §1 多了三条后续后端设计文档链接。**未把该工作副本拷进 W0，也未改获批 UX 迁就实现。** 产品恢复仍以 HTML + W0 已提交 UX 正文 + 任务卡为准。

## 2. W0 未提交分类（不删除）

### 首版保留（M0 编译/假 runtime 双 engine 需要）

- `shared/src/session/dual/dual_session_controller.{hpp,cpp}`
- `shared/src/session/engine/session_engine.{hpp,cpp}` dirty（含 SELECT/START_DUAL 接线）
- `shared/src/session/ports/*`、`shared/include/flynes/flynes_session.h` dirty
- `shared/CMakeLists.txt` 将 dual 源编进 `flynes_session`
- `shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp`
- `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp` dirty（假 DualRuntime、content catalog、可调时钟）
- 合同测试 `test_session_v2_abi.c` / `test_provider_event_contract.cpp` 的配套增量

### 后置隔离（保留源码与编译，不记 M0 产品完成）

- `shared/src/session/content/*` 与 `test_content_offer_scheduler.cpp` / `test_content_transfer_controller.cpp`：文件传输后置；缺 ROM 只阻塞开局
- `shared/src/session/recovery/*` 与 `shared/tests/nearby/recovery/*`：自动重连/迁移/耐久 WAL 后置；**断线冻结 watchdog 仍留给 M0 使用**
- W3 `test_two_engine_dual_run.cpp` / `test_two_engine_recovery.cpp` / `loopback_quic_probe`：**不复制**。W0 已有更新的 dual_mvp；Quinn 双 engine 属 ENG-09；recovery 场景属后置

### 待 UX 卡串行（本卡不提交、不覆盖）

- Android/Harmony/iOS N00/N01–N03/G00 布局与字符串、对应测试：上一会话正在恢复原稿，归属 UX-01–07
- `shared/schema/nearby_ui_v1.json`、`nearby_ui_state.{hpp,cpp}`、`test_product_nearby_ui.cpp`：归属 UX-00.S

### 工作区杂项（不提交）

- `.superpowers/sdd/*`
- Harmony `.hvigor` / `.cxx` 构建缓存

## 3. W3 接收结果

已接入（3-way merge，冲突 0）：

- W3 默认关闭的 fixture tamper 钩子（`tamper_with` / GATT persist `remember_replacement` / join-params / exporter override）
- `shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp`（来自 `5e8e73f`）
- `shared/CMakeLists.txt` 目标 `flynes_two_engine_tamper_matrix`

未接入及原因：

| W3 资产 | 结论 |
|---|---|
| `test_two_engine_dual_run.cpp` | 待审。W0 已有 `test_two_engine_dual_mvp.cpp`（假 runtime 600 帧），不复制过时场景 |
| `test_two_engine_recovery.cpp` + crashable stores | 后置隔离 |
| `loopback_quic_fixture` / `loopback_quic_probe` | ENG-09；crate 自测不能冒充双 engine 真 Quinn |
| W3 `VERSION` / `app/build.gradle` / Harmony 版本 | **禁止**。W3 是 1.7 线，W0 是当前 MINOR |

SAS：W3 诊断保留——公开 action 不携带 SAS 字节（IF05）；不在 ENG-01「修复」成把 SAS 塞进 ABI。

## 4. 历史验收纠偏

见 `2026-09-15-nearby-dual-nondevice-acceptance.md` 文首层级表。

冲突消除：

- 同文曾写 `MVP-DUAL PASS` 又写引擎 reducer 未做 / 假 runtime：现将 600 帧假 runtime 明确为 **L1 历史**，不是 L2/L3。
- `E2E-SCENARIOS` 曾 NOT_RUN：ENG-01 正在把篡改矩阵收进 W0；通过前不得写 PASS。
- `E2E-HARNESS` 的 crate 6/6 与 linkage 是 Quinn **组件**证据，不是两 engine 走产品 Quinn。
- 平台模拟器 Nearby 条数是历史 L1/布局投影，**未复验当前 dirty UX**。

## 5. ENG-01 验证

证据目录：`out/evidence/nearby-playable/ENG-01/`（gitignored `out/`）。

### 5.1 构建环境

| 项 | 值 |
|---|---|
| worktree | `E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes` |
| WSL path | `/mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes` |
| HEAD at claim | `c7bcd79e3e420268bfb79e9cc0627e5f743f294a` |
| dirty fingerprint at claim | `9378D0A7DD22C0BD43F8014F32D02499970ED7CC0B5CF551727E3F959C54BE59` |
| 构建目录 | `out/nearby-playable/shared-linux`（新目录，未复用 `out/nearby-host-linux`） |
| `FLYNES_ENABLE_RUST_QUIC_PROVIDER` | `BOOL=ON`（CMakeCache 已断言） |
| `FLYNES_CARGO_EXECUTABLE` | `/home/pippin/.cargo/bin/cargo` |
| provider 被测网络 | 篡改矩阵 = 进程内 LoopbackTransport（**L1**）。`flynes_quic_provider_linkage` = 组件链接，不是双 engine Quinn |
| runtime | dual_mvp = fixture 假 DualRuntime（L1）；真 NES = ENG-08 NOT_RUN |
| 物理/真机 | NOT_RUN |

WSL `git rev-parse` 需 `GIT_DIR=/mnt/e/.../.git/worktrees/nearby-ui-acceptance-fixes`，因为 worktree `.git` 文件写的是 Windows 路径。未改 git 内部文件。

`cmake --build` 全目标因既有 `flynes_runtime_pcm_contention`（`-Werror=subobject-linkage`）失败；随后对已产出目标跑 CTest。

### 5.2 CTest（本卡实测）

命令：`ctest --test-dir out/nearby-playable/shared-linux --output-on-failure --no-tests=error`

| 结果 | 值 |
|---|---|
| 用例数 | 105 |
| 通过 | 102 |
| 失败 | 3 |
| CTEST_EXIT | 8 |
| 日志 | `out/evidence/nearby-playable/ENG-01/host-ctest.utf8.log` |

失败分列：

| 测试 | 结果 | 口径 |
|---|---|---|
| `flynes_two_engine_tamper_matrix` | **Passed 0.36s** | W3 篡改负例已收编并在当前树跑过。运输=LoopbackTransport，**L1**。不得记 REAL-NET |
| `flynes_quic_provider_linkage` | Passed 0.01s | Quinn 静态库链接；不是双 engine 真 Quinn |
| `flynes_two_engine_dual_mvp` | **历史失效（2026-09-18 晚纠偏）** | 当时记「REC04 五断言已绿 / Passed 0.41s」。BOOLEAN 捷径删除后，600 帧、暂停断线、REC04 **已从通过路径移除**，现为 **NOT_RUN**（需已验证对端确认与 GAME_RUNNING）。不得继续引用本行当 L1 运行证据。 |
| `flynes_runtime_pcm_contention` | Not Run | 既有 subobject-linkage，非本任务。未删测试 |
| `flynes_zip_payload_fixture_corpus_check` | Failed | 既有 Linux fixture 字节，非本任务。未删测试 |

### 5.3 未宣称

L2 REAL-CORE / REAL-NET、L3 原生可玩、L4 真机均为 NOT_RUN。`0 tests matched` 未发生。

## 6. 缺口与下一张卡

缺口（当前，不再把已落地的字体卡当成下一张）：

- CP2 L1 dual_mvp 现已 **PASS**（Control 确认 + runtime-ready + L1 600 帧）。L2 真 NES+Quinn 同一测试与 Android 原生仍缺。历史条目：曾记空 CONFIRM 2 FAIL / 600 帧 NOT_RUN，已被 §11 覆盖。
- UX-00.S **完成**：N00–N12/G00 容器表 + `apply_nearby_action`/`apply_nearby_session`。`nearby_product_ui` **Passed**；`check_nearby_ui_contract.py` OK。未实现平台 provider
- ENG-05 **CP1 完成（未提交）**：生产目录 version 2 解析 core/profile/options；v1/缺字段不可确认并带 `nearby_blocked_session_read`；非规范 profile 不可选（`nearby_blocked_profile_verify`）。指纹为 content+core+profile+options+DUAL+P1=0+P2=1，**不含**各端 local_seat。loopback catalog 已发 v2 规范身份。
- ENG-06 **CP2 未完成**：空 CONFIRM 只写 local。对端须走 Control `0x0218`；schema/codec/握手转发未落地。dual_mvp 新增负例。600 帧仍 **NOT_RUN**。
- CP0 **完成（未提交）**：旧布局 272 canary、capacity=2 stride、分页、新布局完整元数据、错误 version/size 无写入。`copy_game_choices_v2` 不再按新 sizeof 步进。
- N09 确认键仍 disabled：平台尚未读 pending-config 快照（ENG-03）
- 鸿蒙：产品/ohosTest `assembleHap` 可通过。字体验收改回比较缩放前后渲染高度 + `px2vp`≥48vp；Hypium **NOT_RUN**
- iOS XCTest（加入失败同页重试、N09 首屏）本机 Windows **NOT_RUN**

**下一收敛：Control `0x0218` PendingConfigConfirmV1（schema + codec + 握手转发）→ 双方 runtime-ready → 真核心/真网络 600 帧及断线冻结 → 原生界面闭环。** 不要假启用 N09。不要做 STREAM。不要把 NOT_RUN 运行场景计作通过。

## 10. CP0/CP1 本轮证据（2026-09-18，未提交）

HEAD：`6ec6605de0f5275295fc08b5905fed3d329f37a4`。产品目录 W0。未 commit。

命令（WSL `out/nearby-playable/shared-linux`）：

```
cmake --build out/nearby-playable/shared-linux --target flynes_session_v2_contract_test flynes_session_v2_abi_test flynes_game_config_consent_test flynes_two_engine_dual_mvp_test flynes_product_nearby_ui_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R 'flynes_game_config_consent|flynes_two_engine_dual_mvp|flynes_session_v2_abi|flynes_session_v2_contract|nearby_product_ui' --output-on-failure --no-tests=error
```

| 项 | 结果 | 证明边界 |
|---|---|---|
| CP0 `flynes_session_v2_contract` | **PASS** | 旧 272 canary、stride=2、分页、新布局 core/profile/options、错误 size/version 无写入。RED 曾 7 FAIL。 |
| CP0 `flynes_session_v2_abi` | **PASS** | R0=272、SIZE=368、目录 version 2 96-byte tail 静态断言 |
| CP1 `flynes_game_config_consent` | **PASS** 13 场景 | 含 v1 不可确认、v2 规范配置、不支持 profile、共享座位而非 local_seat |
| `nearby_product_ui` | **PASS** | 未改 UX 原稿 |
| CP2 `flynes_two_engine_dual_mvp` | **PASS**（见 §11） | 历史行曾记 2 FAIL；现 L1 Fake DualRuntime + LoopbackTransport 已绿 |
| 600 帧 / 暂停冻结 / REC04 | **NOT_RUN** | 依赖 CP2 GAME_RUNNING |
| CP3 真 NestopiaUE + 真 Quinn | **NOT_RUN** | 同一测试尚未存在 |
| CP4 Android 原生双人 | **NOT_RUN** | 未接 owner |
| 真机 | **NOT_RUN** | 本轮不做 |
| STREAM / ROM 传输 / 复杂恢复 | **DEFERRED** | 首版后置 |
| Hypium / iOS XCTest | **NOT_RUN** | 缺签名 / 非 Mac |
| CP2 `0x0218` schema/codec/握手转发 | **BLOCKED** 精确原因 | Control 流在 LINK_READY 后对未知帧 `PROTOCOL_VIOLATION`；`copy_game_choices` 已修。下一项必须把 kind `0x0218` 写入 `flynes_session_v1.schema`、`session_codec` FixedKind、`app_frame` 类型表与 Control allow-list，并让握手在 READY 之后把该帧转给 dual，而不是新编签名算法。 |

Android/iOS 已消费冻结文字角色（脏工作区）；鸿蒙 TYPO-01.H 已接线。字体改善 ≠ 原稿恢复 ≠ 可玩。


## 7. TYPO-00.S 交付（未提交）

Worktree：`E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`  
HEAD：`6ec6605de0f5275295fc08b5905fed3d329f37a4`（本卡未 commit）

新增：

- `shared/schema/nearby_typography_v1.json` — 10 个独立角色（pageTitle/paneTitle/sectionTitle/body/muted/action/primaryAction/kicker/inviteCode/codeInput）及原稿选择器覆盖
- `tools/quality/check_nearby_typography.py`
- `tools/quality/tests/test_nearby_typography.py`

TDD：先红（`ModuleNotFoundError: check_nearby_typography`，checker 尚不存在）后绿。

控制器复跑：

```
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
→ Ran 8 tests, OK
python tools/quality/check_nearby_typography.py .
→ OK: nearby typography contract valid; 10 roles frozen at CSS px default scale; no native measurements claimed.
python tools/quality/check_nearby_ui_contract.py .
→ OK: nearby UI contract valid; 137 strings covered on android/harmony/ios; C01-C18 fixture present
```

负例：muted 14、paneTitle 24、action 16、inviteCode tracking `.12`/`.12em`、缺文件、抄 JSON 常量当原生测量 → FAIL。

规格审查 PASS；质量审查 APPROVE（Minor：`format_tracking` 科学计数法、bool 走 `numbers_equal`，未阻塞）。未宣称原生截图或 L3。未改原稿、未改 `nearby_ui_v1.json`、未改三端页面。

## 8. TYPO-01.H 交付（未提交，有缺口）

鸿蒙消费冻结角色。产品页真实引用 `NEARBY_TYPE`：主标题 21/600、说明 12、按钮/页签 14；邀请码 29/600、字距 `.17em`→4.93；kicker `.13em`→1.43。debug Want 允许 `{1, 1.3, 1.5, 2}`，非法值打 `rejected=`，release 忽略覆盖，`followSystem` 未关。按钮 `minHeight 48` 可随大字增高；Nearby 页加垂直 Scroll。

Host：`test_nearby_harmony_typography.py` 当时 4/4 OK（控制器复跑）。**历史失效（2026-09-18 晚纠偏）**：该 4/4 含对已删除 `getTextSize` 的静态要求，且未比较 1→1.3 渲染字高、把像素≥48 当成 48vp。现静态合同已改为 `getBounds`+`px2vp`+`headline13 > headline1`。HAP 编译通过仍 **不等于** Hypium 已跑。`127.0.0.1:5557` 确认为 emulator。**Hypium NOT_RUN**。

规格审查 PASS。质量审查两条 Important（Manage 调色板、`onWindowStageCreate` 未 await）均为本卡前已有问题，未纳入 TYPO-01.H 顺带改色/改窗口 API。

## 9. 验收纠偏（未提交，2026-09-18）

针对：鸿蒙验收未测页面且 `tap2 > 48` 会红；加入码防重复修成失败后无法重试；N09 仍平铺技术字段；台账仍派 TYPO-02.A。

已改（证据；未宣称 L2/L3/L4 / 鸿蒙 Hypium）：

- 鸿蒙 `NearbyUxRestoration.test.ets`：当时写 `@ohos.uitest`/`getTextSize`。**历史失效**：SDK 无 `getTextSize`；纠偏后改为 `@ohos.UiTest` + `getBounds`/`px2vp`，并保留 `headline13 > headline1` 与 48vp。Hypium 仍因 unsigned HAP **NOT_RUN**。
- 加入码：`NearbyJoinSubmitState` 飞行中只发一次，失败后同页恢复提交/输入/取消。Android `submit.post`、iOS `DispatchQueue.main.async`、鸿蒙 `setTimeout(0)`。失败后错误文案仍是 discovery，不是 invalidFormat。
- Android host：`:app:testDebugUnitTest --tests com.flynes.emu.NearbyJoinSubmitStateTest` → **4/4**（`TEST-com.flynes.emu.NearbyJoinSubmitStateTest.xml` failures=0）。
- Android emulator `emulator-5554` / FlyNES_BuiltinContent API 15：`NearbyInviteCodeTest,NearbyLobbyTest` **13/13**，0 failed（含 `submitFailureAllowsModifyRetryAndCancelOnSamePage` 与 N09 首屏）。
- N09 三端首屏改为游戏/主机/座位 + 底部确认；指纹等技术字段进详情。确认键保持 disabled + `nearby_blocked_session_read`。iOS XCTest 已写，本机 Windows **NOT_RUN**。
- 台账 §6 下一张卡改为 GATE-0，不再派 TYPO-02.A。

## 11. CP2/CP3 续作证据（2026-09-18 夜，未提交）

HEAD 仍为 `6ec6605de0f5275295fc08b5905fed3d329f37a4`。产品目录 W0。未 commit。未改 main。

命令（WSL `out/nearby-playable/shared-linux`）：

```
ctest --test-dir out/nearby-playable/shared-linux -R 'flynes_game_config_consent|flynes_two_engine_dual_mvp|flynes_session_v2_abi|flynes_session_v2_contract|nearby_product_ui|flynes_session_schema_registry|flynes_session_codec$|flynes_session_codec_loopback|flynes_session_app_frame$|flynes_session_app_frame_consistency|flynes_link_handshake_scheduler|nearby_dual_run_scheduler|flynes_two_nes_schedulers' --output-on-failure --no-tests=error
```

CTEST_EXIT=0；13/13 PASS；19.65s。

`cargo test --offline --test ffi ffi_real_connection` in `shared/nearby-quic-provider`：1 passed；CARGO_EXIT=0。

| 项 | 结果 | provider / runtime | 证明边界 |
|---|---|---|---|
| CP2 `flynes_two_engine_dual_mvp` | **PASS** | LoopbackTransport / Fake DualRuntime | 空 CONFIRM 经 Control `0x0218`；双方 runtime-ready；L1 600 帧/暂停/REC04。不是真 NES、不是 Quinn |
| CP2 codec/schema/handshake | **PASS** | n/a | 含 `flynes_session_codec_loopback` 15.74s |
| CP0 旧 copy 清零头 | **PASS** | n/a | 零 header 按 R0 272 |
| CP1 core id | **PASS 部分** | catalog 合同 | core=`1.53.2`=`nes_core_version()`；`nestopiaue` 不可选。profile/options 仍是名称哈希 |
| CP3 `flynes_two_nes_schedulers` | **PASS** | 无网络 / 两 `fly_runtime` NestopiaUE 1.53.2 | 600 帧摘要一致、P2 发散、拒绝 nestopiaue 当 ROM 身份。不是双 SessionEngine，不是 Quinn |
| CP3 双 engine + NES + Quinn 同一测试 | **NOT_RUN** | — | 尚未存在。Quinn `ffi_real_connection` 只证明 crate localhost |
| CP4 Android 原生双人 | **NOT_RUN** | — | 未接 owner |
| 真机 / Hypium / iOS XCTest | **NOT_RUN** | — | 本轮不做 |
| STREAM / ROM 传输 / 复杂恢复 | **DEFERRED** | — | 首版后置 |

核心接线：`nes_core.cpp` 用 `g_running_ctx` 让同一进程两个 `nes_t` 各自拉自己的 pad；`nes_load_rom` 在 Power 前 `SetRamPowerState(0)`。摘要忽略 checkpoint 里的 `last_produced_time_ns`。

