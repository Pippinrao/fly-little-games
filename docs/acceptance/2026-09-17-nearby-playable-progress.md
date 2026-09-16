# Nearby 可玩首版进度台账 — 2026-09-17

当前产品集成 worktree（W0）：
`E:\workspace\codes\games\fly-little-games\.worktrees\nearby-ui-acceptance-fixes`

权威：原稿 `docs/superpowers/specs/assets/nearby-ui-parity-review.html`、获批 UX `docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md`。禁止改原稿迁就实现。

## 认领

| 时间 | 任务 | 状态 | Owner |
|---|---|---|---|
| 2026-09-17 | ENG-01 可复现基线 | 验证完成，待基线提交 | INTEGRATOR（本会话） |
| — | UX-00.S | 未开始（依赖 ENG-01） | — |
| — | ENG-02 | 未开始（依赖 ENG-01、UX-00.S） | — |

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
| `flynes_two_engine_dual_mvp` | **Failed** | 600 帧/pause 段已打印；REC04 300ms 冻结 5 条 FAIL。假 runtime。**不标 PASS** |
| `flynes_runtime_pcm_contention` | Not Run | 既有 subobject-linkage，非本任务。未删测试 |
| `flynes_zip_payload_fixture_corpus_check` | Failed | 既有 Linux fixture 字节，非本任务。未删测试 |

### 5.3 未宣称

L2 REAL-CORE / REAL-NET、L3 原生可玩、L4 真机均为 NOT_RUN。`0 tests matched` 未发生。

## 6. 缺口与下一张卡

缺口：`dual_mvp` REC04 冻结未绿（M0 断线冻结仍有效需求，后续 ENG-10 复验）；content/recovery 源码在库中但产品传输/自动恢复后置；UX 合同未做。

下一张卡：`UX-00.S`（ENG-01 基线提交之后；ENG-02 依赖 UX-00.S）。
