# Review package W3 tamper round 2
Base: 697c4a1
Head: 5e8e73f

## Commits
5e8e73f test(nearby): assert pair-layer tampers crossed and the receiver rejected AEAD e0c8bf4 Merge commit '08f2afd' into codex/nearby-dual-e2e-harness e8375c0 test(nearby): close the three pair-layer tamper cases and record the SAS diagnosis 08f2afd docs(nearby): correct the real-QUIC provenance and the LINK_READY size comment 11b3179 feat(nearby): publish the DUAL run digest through the snapshot 53f734f feat(nearby): add the DUAL runtime port adapter and the canonical input wire codec e87cbe3 test(nearby): pin the DUAL gate's explicit STREAM closure

## Diff stat
 .superpowers/sdd/w3-tamper-report.md               |  86 ++++  VERSION                                            |   2 +-  app/build.gradle                                   |   4 +-  .../2026-09-15-nearby-dual-nondevice-acceptance.md |  71 ++-  harmony/AppScope/app.json5                         |   4 +-  shared/CMakeLists.txt                              |  51 +++  shared/include/flynes/flynes_session.h             |  27 ++  shared/src/session/dual/canonical_input_wire.cpp   | 208 +++++++++  shared/src/session/dual/canonical_input_wire.hpp   | 116 +++++  shared/src/session/dual/dual_runtime_adapter.cpp   | 149 +++++++  shared/src/session/dual/dual_runtime_adapter.hpp   |  76 ++++  shared/src/session/link/link_control_contract.hpp  |   5 +-  .../nearby/contract/test_dual_stream_closed.cpp    | 193 ++++++++  shared/tests/nearby/contract/test_session_v2_abi.c |   9 +  .../nearby/contract/test_session_v2_contract.cpp   |  17 +  .../nearby/harness/two_engine_loopback_fixture.hpp | 117 ++++-  .../nearby/integration/test_dual_runtime_seam.cpp  | 488 +++++++++++++++++++++  .../nearby/scenarios/test_two_engine_dual_run.cpp  |   8 +-  .../scenarios/test_two_engine_tamper_matrix.cpp    | 251 ++++++++---  19 files changed, 1776 insertions(+), 106 deletions(-)

## Diff -U10
diff --git a/.superpowers/sdd/w3-tamper-report.md b/.superpowers/sdd/w3-tamper-report.md
new file mode 100644
index 0000000..adcd556
--- /dev/null
+++ b/.superpowers/sdd/w3-tamper-report.md
@@ -0,0 +1,86 @@
+# W3 round 2 — tamper matrix fail-open + SAS diagnosis
+
+Worktree: `nearby-dual-e2e-harness`
+Branch: `codex/nearby-dual-e2e-harness`
+
+## Status
+
+GREEN. Round-1 "fail-open" on commit/reveal, pair signature, and key-confirm was a harness bug (tampered GATT copies discarded on BACKPRESSURE). Persist + prefix-only rewrite already lived on this branch; this round added the assertions that would have caught that bug, watched them fail with persist disabled, restored persist, and re-ran green.
+
+W0-owned files were not touched. `flynes_runtime_pcm_contention` and zip corpus were not fixed.
+
+## What this round added
+
+- Fail-closed assertions for the three previously fail-open pair-secure layers:
+  injection landed, `tampered_logical_delivered >= 1`, neither engine
+  `CONNECTED_LOBBY`, documented named states with CONNECTING or FAILED on at
+  least one end, and `aead-open-fail` or `verify-fail` on the receiver.
+- SAS diagnosis recorded in the test comment (and below): live engine does not
+  compare SAS bytes; two-arg `PairPipeline::approve_local` is unused; public
+  action has no SAS field (IF05). Not "fixed" by adding SAS bytes to the action.
+- GATT persist (`RelayDirectionState::remember_replacement`) and prefix-only
+  rewrite already present; reassembly mismatch is counted, not treated as a
+  product failure.
+
+## TDD evidence
+
+### RED
+
+Persist (`remember_replacement`) temporarily disabled. WSL:
+
+```
+export PATH="$HOME/.cargo/bin:$PATH"
+cmake --build out/w3-linux/shared/build --target flynes_two_engine_tamper_matrix_test -j 24
+ctest --test-dir out/w3-linux/shared/build --output-on-failure --tests-regex flynes_two_engine_tamper_matrix
+```
+
+Observed (exit 8):
+
+```
+  commit/reveal          landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
+FAIL: a tampered commit/reveal actually CROSSED (observed 0)
+FAIL: a tampered commit/reveal is rejected by the RECEIVER's own provider (aead-open-fail=0/0 verify-fail=0/0)
+FAIL: a tampered commit/reveal NEVER reaches FLY_SESSION_LINK_CONNECTED_LOBBY_V2 (inviter=8 joiner=8)
+  pair signature         landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
+  key-confirm            landed=yes crossed=0 inviter=8(-) joiner=8(-) aead-open-fail=0/0
+18 failure(s)
+```
+
+Landed-without-crossed plus both ends in lobby (8) is the round-1 harness bug.
+
+### GREEN
+
+Persist restored. Same ctest: `1/1 Test #91: flynes_two_engine_tamper_matrix ... Passed`. Direct binary:
+
+```
+  CONTROL (no tamper)    landed=yes crossed=0 inviter=8 joiner=8 aead-open-fail=0/0
+  ... 10 other layers fail-closed, at least one end FAILED ...
+  commit/reveal          landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
+  pair signature         landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
+  key-confirm            landed=yes crossed=1 inviter=5 joiner=9 aead-open-fail=0/1
+  SAS layer: confirm offered=yes, inviter stage=1 joiner stage=1, SAS agrees=yes
+flynes_two_engine_tamper_matrix passed
+```
+
+`flynes_two_engine_dual_run` also Passed (0.07 s). LoopbackTransport is the fixture's in-process transport, not Quinn.
+
+## Pair-secure body layout (verbatim)
+
+```
+body[0]=version 1, [1..3]=reserved 0, [4..11]=message_counter u64be nonzero,
+body[12..]=ciphertext || 16-byte AEAD tag
+body_size = pair_secure_inner_size_v1(type) + 28
+```
+
+AEAD tag is the last 16 body bytes. `kLastBodyByte` xor therefore hits the tag; the GATT trailing domain hash is recomputed so the transport cannot eat the tamper.
+
+## SAS diagnosis
+
+The live engine at `session_engine.cpp:5598` calls `PairSignatureScheduler::approve_local(kind)`, which does not compare SAS bytes — it only forwards the approval kind. Two-arg `PairPipeline::approve_local(kind, displayed_sas)` (the only compare of `displayed_sas != *expected`) is unused; `PairPipeline` is never instantiated by the engine. Single-arg `PairPipeline::approve_local(kind)` reject-path is `entry_mode==1 && !known_path` (`pair_pipeline.cpp:174`), and even that overload is not on the live confirm branch. The public action has no SAS field (IF05): `fly_session_action_choice_v2` is boolean / invite-code / reference only, and `CONFIRM_SAS` is submitted with `choice_size == 0`; the confirm guard rejects any choice payload before apply. This is not exploitable through the public ABI and is not a "confirms a SAS it was never shown" defect: the engine derives SAS from the transcript and the human compares the two displayed codes. Do not "fix" by adding SAS bytes to the public action. Residual design observation: if confirmation was intended to be cryptographically bound to the displayed SAS, that binding is dead code (`PairPipeline`), not the shipped engine.
+
+## Concerns
+
+- Pair-layer sender stays `AUTHENTICATING` (5) because it never observes the peer's AEAD rejection; the receiver is `FAILED` (9). The named-state check is CONNECTING or FAILED on at least one end, not both. Tightening to "both CONNECTING or FAILED" would fail this honest asymmetry.
+- GATT reassembly groups may start mid-message after BACKPRESSURE; mismatches are counted (`logical_tamper_reassembly_mismatches`) and are not treated as product failure. Only a complete record prefix is tampered.
+- Full `ctest` in `out/w3-linux` still reports `flynes_runtime_pcm_contention` (Not Run / subobject-linkage `-Werror`) and `flynes_zip_payload_fixture_corpus_check` (Failed). Out of scope per brief.
+- These tests prove engines drove every QUIC port primitive over in-process LoopbackTransport and rejected tampered bytes. They do not prove bytes went through Quinn (`flynes_loopback_quic_probe` / `flynes_quic_provider_linkage` own that).
diff --git a/VERSION b/VERSION
index 6a126f4..84298f9 100644
--- a/VERSION
+++ b/VERSION
@@ -1 +1 @@
-1.7.5
+1.7.8
diff --git a/app/build.gradle b/app/build.gradle
index 8e0bfad..39e23f0 100644
--- a/app/build.gradle
+++ b/app/build.gradle
@@ -6,22 +6,22 @@ def popularitySource = rootProject.file('shared/data/popularity.inc')
 def popularityJavaDir = layout.buildDirectory.dir('generated/sources/popularity/java')
 
 android {
     namespace 'com.flynes.emu'
     compileSdk 36
 
     defaultConfig {
         applicationId "com.flynes.emu"
         minSdk 24
         targetSdk 36
-        versionCode 1007005
-        versionName "1.7.5"
+        versionCode 1007008
+        versionName "1.7.8"
         testInstrumentationRunner "com.flynes.emu.test.SingleDeviceCertificationRunner"
         ndk {
             abiFilters 'arm64-v8a', 'x86_64'
         }
         externalNativeBuild {
             cmake {
                 cppFlags '-std=c++17 -fvisibility=hidden'
                 arguments '-DNES_CORE_DIR=' + project.rootDir.getAbsolutePath() + '/core',
                         '-DFLYNES_SHARED_DIR=' + project.rootDir.getAbsolutePath() + '/shared',
                         '-DANDROID_STL=c++_shared'
diff --git a/docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md b/docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md
index d48a139..d81d852 100644
--- a/docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md
+++ b/docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md
@@ -19,47 +19,102 @@
 以下每条状态都对应**本次可复核的命令输出**；`nearby_*` 全部在 WSL(Ubuntu-24.04) 构建并运行（Windows 上只编译不运行，原因见后文测试运行环境）。
 
 | 门禁 | 状态 | 证据 |
 |---|---|---|
 | `BASE-0212` | PASS | public engine 测试断言两个 `0x0212` 持久化门禁（SecureStore KeyRef + exact 312 字节 ObjectStore 对象）缺一不可；全量回归 0 failure |
 | `BASE-PARALLEL` | PASS | 三份获批文档 SHA-256 与源一致；共同基线提交 `e8703e4`；两份合同冻结 |
 | `LINK-WIRE` | PASS | `flynes_link_hello_wire` / `flynes_link_ready_wire` PASS；golden 由**独立 oracle**（`gen_link_control_golden.py`，不调用被测 encoder）重算 |
 | `LINK-SCHED` | PASS | `flynes_link_handshake_scheduler` / `flynes_link_handshake_races` PASS；`control_stream_loopback_closes_the_lobby()` 让两个公开 scheduler 在**零手工 `accept_peer_*` 注入**下闭合 |
 | `DUAL-INPUT` | PASS | `nearby_canonical_input` / `nearby_input_window` PASS（毫秒级）；规范化解 dpad 冲突、拒绝错 owner / 旧 revision / 跨 branch / sequence 回退 / 溢出 |
 | `DUAL-RUN` | PASS | `nearby_dual_run_scheduler` / `nearby_dual_run_races` PASS；600 帧确定性 trace 实测 18–42 ms |
-| `E2E-HARNESS` | PASS | 真实 loopback QUIC：产品 crate 探针 `PASS=37 FAIL=0`（TLS1.3 + ALPN `flynes-nearby/2` + pin + exporter 一致 + stream/datagram 往返）；`flynes_quic_provider_linkage` 证明静态库真的被链接并调用 |
+| `E2E-HARNESS` | PASS（**证据已更正**） | 见下方「真实 loopback QUIC 证据更正」：Rust provider 必须 `FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON` 才有意义；本次实测 crate 真 loopback 测试 `cargo test --release` = **6 passed / 0 failed**（TLS1.3 + ALPN `flynes-nearby/2` + pin + exporter 一致 + stream/datagram 往返），`flynes_quic_provider_linkage` PASS 证明静态库真的被链接并调用。原先登记的「产品 crate 探针 `PASS=37 FAIL=0`」在本 worktree 的 CMake 里**不存在该目标**，已删除 |
 | `E2E-SCENARIOS` | **NOT_RUN** | W3 的 `scenarios/test_two_engine_*` 尚未收编（其 carve 早于当前 ABI，需先补 `object_store.read` 等）；篡改负例矩阵未在两个真 engine 上跑 |
-| `MVP-LOBBY` | **PASS** | 见下方 §MVP-LOBBY 证据 |
-| `MVP-DUAL` | **NOT_RUN** | 未开始（依赖 W2 的 DUAL 数据面接入公开 action/snapshot 边界） |
-| shared 全量 0 failure | **FAIL（2 项，均与本轮 Nearby 工作无关）** | Linux 全量 `94/96`：`flynes_runtime_pcm_contention`（既有 `-Werror=subobject-linkage`，未触碰）与 `flynes_zip_payload_fixture_corpus_check`（既有 fixture 字节在 Linux 检出下不一致）。**`nearby_*` 子集 39/39 全绿** |
-| Android unit/build 0 failure | **NOT_RUN（需复跑）** | 2026-09-15 基线为 97 类 / 468 tests / 0 failures；此后 ABI 与 schema 均有改动，**必须复跑** `:app:testDebugUnitTest` 与 `SessionSchemaRegistryTest` |
-| STREAM provider/codec/media 调用次数为 0 | **NOT_RUN** | 需执行后文「资源泄漏与负向审计」的全仓搜索 |
+| `MVP-LOBBY` | **PASS（限定为进程内回环，见更正）** | 见下方 §MVP-LOBBY 证据 |
+| `MVP-DUAL` | **NOT_RUN** | 未达成。已落地 4 笔绿增量（ABI / STREAM 关闭 / 154 字节输入编解码 + runtime port 适配器 / 运行 digest 上快照），见 §MVP-DUAL 现状；**引擎 reducer、State Commit 流、输入端到端、600 帧 E2E 均未开始** |
+| shared 全量 0 failure | **FAIL（2 项，均与本轮 Nearby 工作无关）** | Linux 全量（**provider ON**）`97/99`：`flynes_runtime_pcm_contention`（既有 `-Werror=subobject-linkage`，未触碰）与 `flynes_zip_payload_fixture_corpus_check`（既有 fixture 字节在 Linux 检出下不一致）。**`nearby_*` 子集 41/41 全绿** |
+| Android unit/build 0 failure | **NOT_RUN（需复跑）** | 2026-09-15 基线为 97 类 / 468 tests / 0 failures；此后 ABI 与 schema 均有改动，**必须复跑** `:app:testDebugUnitTest` 与 `SessionSchemaRegistryTest`。注意：本轮实测发现 `188bfd6` 之前存在**主仓库误跑**的日志（已改名 `*-WRONG-TREE-main-repo.log`），复跑时必须显式设定 worktree 工作目录并只读该 worktree 的 `app/build/test-results/testDebugUnitTest` |
+| STREAM provider/codec/media 调用次数为 0 | **PASS（结构证明 + 审计，见 §STREAM 证据）** | 公开 ABI 无任何 codec/media/encoder/decoder/sink provider 表，因此不存在可数的调用；以编译期结构钉住 + 全仓审计 `grep -rEn 'ports_?\.\w*(codec\|media\|encoder\|decoder\|sink)' shared/src/session \| wc -l` = 0 |
 | 所有真机条目明确 NOT_RUN | PASS（记录形式） | 真机与物理性能项一律 `DEFERRED`/`NOT_RUN`，未做任何设备认证声明 |
 
+### 真实 loopback QUIC 证据更正（2026-09-17）
+
+**发现**：`out/nearby-host-linux/shared/build/CMakeCache.txt` 里
+`FLYNES_CARGO_EXECUTABLE:FILEPATH=FLYNES_CARGO_EXECUTABLE-NOTFOUND`、
+`FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=OFF`，且不存在任何真实 QUIC 探针二进制。
+根因（本轮实测，与最初归因略有不同）：本 distro 里 **`cargo` 即使对 login shell 也不在 PATH**
+（`bash -lc 'command -v cargo'` 为空，而 `/home/pippin/.cargo/bin/cargo` 存在），
+`shared/CMakeLists.txt:352` 的 `find_program(FLYNES_CARGO_EXECUTABLE cargo)` 失败后
+`:356` 把 `FLYNES_ENABLE_RUST_QUIC_PROVIDER` **FORCE 置 OFF**——于是「真实 Quinn」被静默移除。
+
+**影响**：提交 `2128e7f` 记录的 `E2E-HARNESS`/`MVP-LOBBY` 证据里凡是「真实 loopback QUIC」的说法，
+在那次构建下**都不可复核**。
+
+**本轮更正与复验**（provider 确实 ON）：
+- 两个构建脚本（`out/logs/wsl-test.sh`、`out/logs/task10-cycle.sh`）改为：显式把
+  `$HOME/.cargo/bin` 加入 PATH、显式传 `-DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON -DFLYNES_CARGO_EXECUTABLE=…`，
+  并在 configure 之后**断言** `FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=ON` 且 cargo 不是 `-NOTFOUND`，
+  否则立刻 `ABORT_PROVIDER_OFF`/`ABORT_CARGO_NOTFOUND` 退出（静默降级不再可能）。
+- 复验结果：`cargo test --release`（`shared/nearby-quic-provider`）= **6 passed / 0 failed**；
+  `flynes_quic_provider_linkage_test` PASS（"QUIC provider linkage tests passed"）；
+  全量 CTest = `97/99`，两项失败与 provider 无关。
+- 同时删除本表中不可复核的「探针 `PASS=37 FAIL=0`」条目——该目标在本 worktree 的 CMake 中不存在。
+
+**两个**必须分开的**声明**（此前混为一谈）：
+1. `flynes_two_engine_connected_lobby_e2e_test` 走的是**夹具自带的进程内 `LoopbackTransport`**（byte-accurate，
+   零 injected class-2 事件），**不使用** Rust provider。它证明的是**引擎接线与协议闭合**。
+2. 「真实 Quinn」由 crate 自测 + `flynes_quic_provider_linkage` 证明，是**另一条**声明。
+§MVP-LOBBY 的 QUIC 端口计数因此只说明「引擎真的驱动了 QUIC 端口的全部原语」，不说明「跑在真 Quinn 上」。
+
 ### MVP-LOBBY 证据
 
 命令：`./out/nearby-host-linux/shared/build/flynes_two_engine_connected_lobby_e2e_test`（跑前删除目标二进制，排除陈旧产物造成的假 PASS）
 
 ```
 link states: inviter=8 joiner=8                 # 8 = FLY_SESSION_LINK_CONNECTED_LOBBY_V2，两个 engine 分别断言
 object kinds persisted: 两侧均含 0x0212 / 0x0213 / 0x0216 / 0x0217
 exporter (link): 9e8fd3cd…（两侧交付值与之完全一致）
 双向真实单位：0x0001/252B(bind 流) → 0x0212/318B → 0x0216/494B → 0x0217/438B ×2 (Control 流)
   # 318 = 6 字节 app-frame 头 + 312；494 = 6 + 488；438 = 6 + 432，与冻结尺寸逐一对上
-QUIC 端口两侧都被真实驱动：
+**进程内**夹具回环上的 QUIC 端口两侧都被真实驱动（**不是** Rust Quinn，见上方更正）：
   inviter listen=1 connect=0 inspect=1 exporter=1 open=0 accept=2 write=6 read=7
   joiner  listen=0 connect=1 inspect=1 exporter=1 open=2 accept=0 write=6 read=6
 单边 READY 负例：扣留一个方向的 0x0217 → 双方停在 7(CONNECTING)；释放 → 双方到 8
 ```
 
 - **零 injected verified evidence**：跨端每个字节都 `std::equal` 于写入方自己的 `written_fragments`；测试**不自造** `DISCOVERY_CONNECTION`/`DISCOVERY_BYTES`/`QUIC_DATA`（这些「第 2 类」事件只由 `LoopbackTransport` 产生）；pump 只应答「第 1 类」操作终端，且逐 port 计数 1:1 恒等式成立。
-- `nearby_*` 39/39 PASSED。
+- `nearby_*` 41/41 PASSED（provider ON；新增 `nearby_dual_stream_closed`、`nearby_dual_runtime_seam`）。
+
+### STREAM 证据（结构证明 + 审计）
+
+公开 ABI 里**不存在**任何 codec/media/encoder/decoder/sink provider 表，因此「调用次数」不是可测量——
+本门禁以「不可能存在该调用」来证明，而不是数一个恒为 0 的计数器：
+- 编译期：`fly_session_dual_runtime_port_v2` 以 `state_digest` 结尾、`fly_session_ports_v2` 以 DUAL runtime
+  槽结尾（追加任何媒体入口即编译失败），见 `test_dual_stream_closed.cpp`；
+- capability：本机宣告掩码 `== kLinkCapabilityDualV1`；STREAM-only/空提案 → `UnsupportedByThisRelease`；
+  未知位 → `UnknownCriticalCapability`；`DualModeV1::HostStream` → `FLY_SESSION_V2_UNAVAILABLE`；
+- 审计（本次实跑）：`grep -rEn 'ports_?\.\w*(codec|media|encoder|decoder|sink)' shared/src/session | wc -l` = **0**；
+  `grep -cE 'fly_session_\w*(codec|media|encoder|decoder|sink)' shared/include/flynes/flynes_session.h` = **0**。
+
+### MVP-DUAL 现状（仍 NOT_RUN）
+
+已落地并绿的增量（每笔都以「删二进制重建 + `ctest -L nearby` 全绿」收尾）：
+
+| commit | 内容 |
+|---|---|
+| `d3f1e2d` | 最小公开 DUAL action/snapshot ABI（尾追加 + 旧 prefix 兼容；`SELECT_CONTENT_V2=42`、`START_DUAL_V2=43`、`input.port_mask[4]`、快照 DUAL 运行状态、`fly_session_dual_runtime_port_v2` + `ports_v2.dual_runtime`） |
+| `e87cbe3` | STREAM 显式关闭的测试与结构钉住 |
+| `53f734f` | `dual/canonical_input_wire`（冻结 154 字节 `CanonicalInputBundleV1` 编解码）+ `dual/dual_runtime_adapter`（用公开 C 表实现冻结 `DualRuntimePort`） |
+| `11b3179` | 快照发布 `dual_state_digest`/`dual_frame_digest`/`dual_pcm_digest`（60 帧收敛断言可观测的前提） |
+
+**未做（因此 `MVP-DUAL` 不成立）**：引擎 DUAL reducer（动作仍落到 `session_engine.cpp:5642` 的 catch-all）、
+State Commit 流、输入端到端传输、双 engine 600 帧 E2E。阻塞项与裁决请求见
+`out/logs/task10-integration-notes.md`；其中内容引用来源已由 W0 裁决为「尾追加只读 content 端口」（未开始）。
 
 **如实标注的一项**：`CONNECTED_LOBBY` 处 `pairing()` 子视图实测为 **EMPTY**（引擎只在 `AUTHENTICATING`/`PROVISIONING` 暴露该子视图，`session_engine.cpp:326-331`）。测试按**实测**钉住 EMPTY，**未伪造 CONFIRMED**。若平台 UI 需要在已连接状态展示配对信息，属后续平台波次的产品改动，本轮不改引擎。
 
 ### 这次 E2E 挖出的 4 个「只有两个真 engine 一起跑才暴露」的引擎缺陷
 
 组件级测试永远测不到，因为每个组件单独测都是对的：
 
 | 提交 | 缺陷 |
 |---|---|
 | `6ab2c7a` | 对端**早到的** `PairKnownStatus`/`Branch` 在本地 `pair_known_` 尚未建立时被判非法 → 改为有界 hold + 就绪后按序 replay（replay 走同一个 `accept_peer_envelope`，stage/counter/generation/HMAC 校验一条未绕过） |
diff --git a/harmony/AppScope/app.json5 b/harmony/AppScope/app.json5
index 0ab5d68..f4219c0 100644
--- a/harmony/AppScope/app.json5
+++ b/harmony/AppScope/app.json5
@@ -1,11 +1,11 @@
 {
   "app": {
     "bundleName": "com.flynes.emu",
     "vendor": "FlyNES",
-    "versionCode": 1007005,
-    "versionName": "1.7.5",
+    "versionCode": 1007008,
+    "versionName": "1.7.8",
     "icon": "$media:app_icon",
     "label": "$string:app_name",
     "configuration": "$profile:configuration"
   }
 }
diff --git a/shared/CMakeLists.txt b/shared/CMakeLists.txt
index 8e0c043..ee88edd 100644
--- a/shared/CMakeLists.txt
+++ b/shared/CMakeLists.txt
@@ -1713,13 +1713,64 @@ if(FLYNES_BUILD_TESTS)
         ${FLYNES_W2_DUAL_INCLUDE_DIRS})
     target_compile_features(flynes_nearby_dual_run_races_test PRIVATE cxx_std_17)
     set_target_properties(flynes_nearby_dual_run_races_test PROPERTIES CXX_EXTENSIONS NO)
     flynes_enable_strict_warnings(flynes_nearby_dual_run_races_test)
     add_test(NAME nearby_dual_run_races COMMAND flynes_nearby_dual_run_races_test)
     set_tests_properties(nearby_dual_run_races PROPERTIES
         LABELS "nearby_race;nearby_integration")
 endif()
 # =================== end W2 append-only block =======================
 
+# =====================================================================
+# Task 10 (DUAL integration) append-only block.
+# =====================================================================
+if(FLYNES_BUILD_TESTS)
+    # Step 3: the DUAL gate's own record that STREAM is closed explicitly.
+    # Header-only assertions over the public ABI, the frozen link capability
+    # contract and the frozen DUAL seam, so the target needs no library.
+    add_executable(flynes_nearby_dual_stream_closed_test
+        tests/nearby/contract/test_dual_stream_closed.cpp
+    )
+    target_include_directories(flynes_nearby_dual_stream_closed_test PRIVATE
+        ${CMAKE_CURRENT_SOURCE_DIR}/include
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
+    )
+    target_compile_features(flynes_nearby_dual_stream_closed_test PRIVATE cxx_std_17)
+    set_target_properties(flynes_nearby_dual_stream_closed_test PROPERTIES CXX_EXTENSIONS NO)
+    flynes_enable_strict_warnings(flynes_nearby_dual_stream_closed_test)
+    add_test(NAME nearby_dual_stream_closed
+             COMMAND flynes_nearby_dual_stream_closed_test)
+    set_tests_properties(nearby_dual_stream_closed PROPERTIES
+        LABELS "nearby_contract")
+
+    # Step 2 (first half): the canonical input unit on the wire (encoder and
+    # decoder for the frozen 154-byte CanonicalInputBundleV1 record) and the
+    # runtime port adapter that implements the frozen dual::DualRuntimePort over
+    # the public C provider table. Both are asserted against the release's own
+    # codec (flynes_session_codec supplies wire::check and the hash helpers).
+    add_executable(flynes_nearby_dual_runtime_seam_test
+        tests/nearby/integration/test_dual_runtime_seam.cpp
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input.cpp
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/canonical_input_wire.cpp
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual/dual_runtime_adapter.cpp
+    )
+    target_include_directories(flynes_nearby_dual_runtime_seam_test PRIVATE
+        ${CMAKE_CURRENT_SOURCE_DIR}/include
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session
+        ${CMAKE_CURRENT_SOURCE_DIR}/src/session/dual
+    )
+    target_compile_features(flynes_nearby_dual_runtime_seam_test PRIVATE cxx_std_17)
+    set_target_properties(flynes_nearby_dual_runtime_seam_test PROPERTIES CXX_EXTENSIONS NO)
+    target_link_libraries(flynes_nearby_dual_runtime_seam_test PRIVATE
+        flynes_session_codec)
+    flynes_enable_strict_warnings(flynes_nearby_dual_runtime_seam_test)
+    add_test(NAME nearby_dual_runtime_seam
+             COMMAND flynes_nearby_dual_runtime_seam_test)
+    set_tests_properties(nearby_dual_runtime_seam PROPERTIES
+        LABELS "nearby_integration;nearby_protocol")
+endif()
+# =================== end Task 10 block ==============================
+
 # Android (and other consumers that add core first) get the runtime library
 # without pulling nestopia into catalog-only host tests.
 flynes_add_runtime_library()
diff --git a/shared/include/flynes/flynes_session.h b/shared/include/flynes/flynes_session.h
index 2fa8bb2..4ed85e2 100644
--- a/shared/include/flynes/flynes_session.h
+++ b/shared/include/flynes/flynes_session.h
@@ -991,30 +991,57 @@ typedef struct fly_session_snapshot_v2
     uint32_t dual_authority_seat;
     uint32_t dual_seats_confirmed;
     uint64_t dual_seat_revision;
     uint64_t dual_frame_index;
     uint64_t dual_verified_through;
     uint64_t dual_commit_frontier;
     uint64_t dual_prediction_depth;
     uint8_t dual_content_hash[32];
     uint8_t dual_session_id[16];
     uint8_t dual_branch_id[16];
+    /*
+     * The run's consistency digests, appended once more. These are the values the
+     * two engines must agree on: `dual_state_digest` covers the last committed
+     * frame's exported state, `dual_frame_digest` binds that frame to the
+     * canonical input bundle it was stepped with, and `dual_pcm_digest` covers
+     * the canonical PCM producer state of the same frame. They are published so
+     * that a reader can compare two engines at the same committed frame without
+     * taking either engine's word about the other.
+     */
+    uint8_t dual_state_digest[32];
+    uint8_t dual_frame_digest[32];
+    uint8_t dual_pcm_digest[32];
 } fly_session_snapshot_v2;
 
 /*
  * The pre-DUAL prefix. offsetof(dual_mode) is the size the structure had before
  * the DUAL block was appended, so an older caller that declares exactly this
  * much is still a legal reader.
  */
 #define FLY_SESSION_SNAPSHOT_V2_R0_SIZE \
     ((uint32_t)(offsetof(fly_session_snapshot_v2, dual_mode)))
+/*
+ * The second prefix: the snapshot as it stood once the DUAL run state existed and
+ * before the digest block was appended. R0 stays the required size, so every
+ * older reader is still served and the copy stays bounded by what it declared.
+ */
+#define FLY_SESSION_SNAPSHOT_V2_R1_SIZE \
+    ((uint32_t)(offsetof(fly_session_snapshot_v2, dual_state_digest)))
 #define FLY_SESSION_SNAPSHOT_V2_SIZE ((uint32_t)sizeof(fly_session_snapshot_v2))
+#ifdef __cplusplus
+static_assert(FLY_SESSION_SNAPSHOT_V2_SIZE ==
+                  FLY_SESSION_SNAPSHOT_V2_R1_SIZE + 96u,
+              "the DUAL digest block is a pure tail append");
+static_assert(FLY_SESSION_SNAPSHOT_V2_R0_SIZE <=
+                  FLY_SESSION_SNAPSHOT_V2_R1_SIZE,
+              "the pre-DUAL prefix is not larger than the pre-digest prefix");
+#endif
 
 /*
  * DUAL simulation lifecycle. Mirrors the internal DUAL seam's
  * DualSimStateV1 (shared/src/session/dual/dual_run_scheduler.hpp) so that a
  * platform reads one vocabulary, not two.
  */
 enum fly_session_dual_state_v2
 {
     FLY_SESSION_DUAL_UNLOADED_V2 = 0,
     FLY_SESSION_DUAL_READY_V2 = 1,
diff --git a/shared/src/session/dual/canonical_input_wire.cpp b/shared/src/session/dual/canonical_input_wire.cpp
new file mode 100644
index 0000000..1f106ee
--- /dev/null
+++ b/shared/src/session/dual/canonical_input_wire.cpp
@@ -0,0 +1,208 @@
+#include "canonical_input_wire.hpp"
+
+#include "wire/session_codec.hpp"
+
+#include <cstring>
+
+namespace flynes::session::dual {
+namespace {
+
+constexpr std::size_t kVersionOffset = 0;
+constexpr std::size_t kSessionIdOffset = 2;
+constexpr std::size_t kBranchIdOffset = 18;
+constexpr std::size_t kAuthorityTermOffset = 34;
+constexpr std::size_t kTimelineEpochOffset = 42;
+constexpr std::size_t kSeatRevisionOffset = 50;
+constexpr std::size_t kModeGenerationOffset = 58;
+constexpr std::size_t kFrameIndexOffset = 66;
+constexpr std::size_t kBatchSequenceOffset = 74;
+constexpr std::size_t kPredictedMaskOffset = 82;
+constexpr std::size_t kReservedOffset = 83;
+constexpr std::size_t kPortsOffset = 90;
+constexpr std::size_t kPortSlotBytes = 16;
+
+void store_u16be(std::uint8_t* out, std::uint16_t value) noexcept
+{
+    out[0] = static_cast<std::uint8_t>(value >> 8u);
+    out[1] = static_cast<std::uint8_t>(value);
+}
+
+void store_u32be(std::uint8_t* out, std::uint32_t value) noexcept
+{
+    out[0] = static_cast<std::uint8_t>(value >> 24u);
+    out[1] = static_cast<std::uint8_t>(value >> 16u);
+    out[2] = static_cast<std::uint8_t>(value >> 8u);
+    out[3] = static_cast<std::uint8_t>(value);
+}
+
+std::uint32_t load_u32be(const std::uint8_t* bytes) noexcept
+{
+    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
+           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
+           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
+           static_cast<std::uint32_t>(bytes[3]);
+}
+
+void store_u64be(std::uint8_t* out, std::uint64_t value) noexcept
+{
+    for (int index = 0; index < 8; ++index)
+        out[index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
+}
+
+std::uint64_t load_u64be(const std::uint8_t* bytes) noexcept
+{
+    std::uint64_t value = 0;
+    for (int index = 0; index < 8; ++index)
+        value = (value << 8u) | static_cast<std::uint64_t>(bytes[index]);
+    return value;
+}
+
+bool source_kind_is_known(std::uint8_t value) noexcept
+{
+    return value >= 1u && value <= 7u;
+}
+
+} // namespace
+
+DualInputWireStatusV1 encode_dual_input_bundle_v1(
+    const DualInputBundleV1& bundle, const DualInputWireContextV1& context,
+    std::uint8_t out[kDualInputWireBytesV1]) noexcept
+{
+    if (out == nullptr)
+        return DualInputWireStatusV1::InvalidArgument;
+    if (!canonical_input_is_canonical_v1(bundle))
+        return DualInputWireStatusV1::InvalidBundle;
+    if (context.authority_term == 0 || context.mode_generation == 0 ||
+        context.batch_sequence == 0)
+        return DualInputWireStatusV1::InvalidContext;
+
+    std::uint8_t bytes[kDualInputWireBytesV1] = {};
+    store_u16be(bytes + kVersionOffset, 1u);
+    std::memcpy(bytes + kSessionIdOffset, bundle.key.session_id.data(), 16u);
+    std::memcpy(bytes + kBranchIdOffset, bundle.key.branch_id.data(), 16u);
+    store_u64be(bytes + kAuthorityTermOffset, context.authority_term);
+    store_u64be(bytes + kTimelineEpochOffset, bundle.key.timeline_epoch);
+    store_u64be(bytes + kSeatRevisionOffset, bundle.key.seat_revision);
+    store_u64be(bytes + kModeGenerationOffset, context.mode_generation);
+    store_u64be(bytes + kFrameIndexOffset, bundle.key.frame_index);
+    store_u64be(bytes + kBatchSequenceOffset, context.batch_sequence);
+    /* reserved_zero[7] at 83 stays zero. */
+
+    std::uint32_t predicted_mask = 0;
+    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
+    {
+        const bool predicted =
+            (bundle.predicted_port_mask & (1u << port)) != 0;
+        DualPortSourceKindV1 kind = predicted ? DualPortSourceKindV1::Predicted
+                                              : DualPortSourceKindV1::RealSample;
+        DualPortClearReasonV1 reason = DualPortClearReasonV1::None;
+        if (context.stated[port])
+        {
+            kind = context.source_kind[port];
+            reason = context.clear_reason[port];
+            /* A prediction marker may not disagree with the stated kind: a port
+             * marked predicted must be PREDICTED, and a port marked real may not
+             * be. The decoder derives the marker from the kind, so a mismatch
+             * would silently change the receiver's view of reality. */
+            if (predicted != (kind == DualPortSourceKindV1::Predicted))
+                return DualInputWireStatusV1::InvalidContext;
+        }
+        if (!source_kind_is_known(static_cast<std::uint8_t>(kind)))
+            return DualInputWireStatusV1::InvalidContext;
+        if (static_cast<std::uint8_t>(reason) > 4u)
+            return DualInputWireStatusV1::InvalidContext;
+        if (kind == DualPortSourceKindV1::Predicted)
+            predicted_mask |= 1u << port;
+
+        const std::uint32_t raw = bundle.ports[port].mask;
+        if ((raw & ~kDualFullPortMaskV1) != 0u)
+            return DualInputWireStatusV1::InvalidBundle;
+        /*
+         * The impossible d-pad state never leaves the send path. The canonical
+         * builder already clears both bits of an opposing pair
+         * (normalize_dual_port_mask_v1) and canonical_input_is_canonical_v1
+         * already refuses a bundle that skipped it, so a mask that still differs
+         * from its normalized form here is a hand-built bundle: the encoder
+         * refuses it rather than emitting a contradiction the peer would have to
+         * resolve.
+         */
+        const std::uint32_t mask = normalize_dual_port_mask_v1(raw);
+        if (mask != raw)
+            return DualInputWireStatusV1::InvalidBundle;
+
+        std::uint8_t* slot = bytes + kPortsOffset + port * kPortSlotBytes;
+        slot[0] = static_cast<std::uint8_t>(kind);
+        slot[1] = static_cast<std::uint8_t>(reason);
+        /* slot[2..4) reserved, already zero. */
+        store_u32be(slot + 4, mask);
+        store_u64be(slot + 8, bundle.ports[port].input_sequence);
+    }
+    bytes[kPredictedMaskOffset] = static_cast<std::uint8_t>(predicted_mask);
+    std::memset(bytes + kReservedOffset, 0, 7u);
+
+    /* The release's own validator is the authority for these bytes: if it
+     * refuses them, the encoder is wrong and must say so instead of handing a
+     * record to the transport that the peer would reject. */
+    std::uint8_t hash[32] = {};
+    if (wire::check("CanonicalInputBundleV1", bytes, kDualInputWireBytesV1,
+                    hash) != wire::Status::Ok)
+        return DualInputWireStatusV1::InvalidBundle;
+
+    std::memcpy(out, bytes, kDualInputWireBytesV1);
+    return DualInputWireStatusV1::Ok;
+}
+
+DualInputWireStatusV1 decode_dual_input_bundle_v1(
+    const std::uint8_t* bytes, std::size_t size, std::uint8_t logical_seat,
+    const std::array<std::uint8_t, 32>& owner_signing_key_id,
+    DualInputBundleV1* out) noexcept
+{
+    if (bytes == nullptr || out == nullptr ||
+        size != kDualInputWireBytesV1)
+        return DualInputWireStatusV1::InvalidArgument;
+    if (logical_seat >= kDualPortCountV1)
+        return DualInputWireStatusV1::InvalidArgument;
+
+    /* The frozen validator runs first: length, version, reserved bytes, every
+     * enum, the prediction/source_kind agreement and the d-pad rule are all
+     * enforced there, not re-implemented here. */
+    std::uint8_t hash[32] = {};
+    if (wire::check("CanonicalInputBundleV1", bytes, size, hash) !=
+        wire::Status::Ok)
+        return DualInputWireStatusV1::InvalidBytes;
+
+    DualInputBundleV1 bundle{};
+    std::memcpy(bundle.key.session_id.data(), bytes + kSessionIdOffset, 16u);
+    std::memcpy(bundle.key.branch_id.data(), bytes + kBranchIdOffset, 16u);
+    bundle.key.timeline_epoch = load_u64be(bytes + kTimelineEpochOffset);
+    bundle.key.frame_index = load_u64be(bytes + kFrameIndexOffset);
+    bundle.key.seat_revision = load_u64be(bytes + kSeatRevisionOffset);
+    bundle.logical_seat = logical_seat;
+    bundle.owner_signing_key_id = owner_signing_key_id;
+
+    std::uint32_t predicted_mask = 0;
+    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
+    {
+        const std::uint8_t* slot = bytes + kPortsOffset + port * kPortSlotBytes;
+        const std::uint8_t kind = slot[0];
+        if (kind == static_cast<std::uint8_t>(DualPortSourceKindV1::Predicted))
+            predicted_mask |= 1u << port;
+        bundle.ports[port].mask = load_u32be(slot + 4);
+        bundle.ports[port].input_sequence = load_u64be(slot + 8);
+    }
+    bundle.predicted_port_mask = predicted_mask;
+    bundle.reserved_zero0[0] = 0;
+    bundle.reserved_zero0[1] = 0;
+    bundle.reserved_zero0[2] = 0;
+
+    /* A decoded record is only a legal input unit when the canonical-form
+     * predicate accepts it, which includes the non-zero, strictly increasing
+     * per-port sequence rule. */
+    if (!canonical_input_is_canonical_v1(bundle))
+        return DualInputWireStatusV1::InvalidBytes;
+
+    *out = bundle;
+    return DualInputWireStatusV1::Ok;
+}
+
+} // namespace flynes::session::dual
diff --git a/shared/src/session/dual/canonical_input_wire.hpp b/shared/src/session/dual/canonical_input_wire.hpp
new file mode 100644
index 0000000..04b1b7c
--- /dev/null
+++ b/shared/src/session/dual/canonical_input_wire.hpp
@@ -0,0 +1,116 @@
+#ifndef FLYNES_SESSION_DUAL_CANONICAL_INPUT_WIRE_HPP
+#define FLYNES_SESSION_DUAL_CANONICAL_INPUT_WIRE_HPP
+
+/*
+ * Task 10 / step 2: the canonical input unit on the wire.
+ *
+ * The DUAL seam's input unit is `DualInputBundleV1` (canonical_input.hpp, 160
+ * bytes in memory). The frozen wire object for the same unit is
+ * `CanonicalInputBundleV1` (message tag 0xFF02, exactly 154 bytes, legal on the
+ * State Commit channel only — wire/app_frame.cpp:165), whose byte layout and
+ * validator already exist in the schema and in
+ * `wire::session_codec::check()`.
+ *
+ * This translation unit adds only the missing encoder/decoder. It invents no
+ * field, no offset and no domain: every byte is written at the offset the frozen
+ * schema names, and every decode first passes the frozen validator, so a record
+ * this decoder accepts is one the release's own codec already accepts.
+ *
+ * Three asymmetries are handled explicitly rather than papered over:
+ *   - the wire record carries `authority_term`, `mode_generation` and
+ *     `batch_sequence`, which the in-memory bundle does not have, so the caller
+ *     must supply them (they are the engine's session facts, never the bundle's);
+ *   - the wire record carries per-port `source_kind` and `clear_reason`, which
+ *     the in-memory bundle does not have either; the encoder derives them from
+ *     the bundle's prediction marker unless the caller states them, and the
+ *     decoder derives the prediction marker back from `source_kind`;
+ *   - the wire record carries no `logical_seat` and no owner signing key, so a
+ *     decoder must be told which authenticated seat and which bound owner key
+ *     the arriving record belongs to. Nothing on the wire may be trusted to
+ *     name its own owner.
+ */
+
+#include "canonical_input.hpp"
+
+#include <array>
+#include <cstddef>
+#include <cstdint>
+
+namespace flynes::session::dual {
+
+/* The frozen wire length of CanonicalInputBundleV1. */
+inline constexpr std::size_t kDualInputWireBytesV1 = 154;
+
+/* The frozen source_kind / clear_reason enums (schema 0xFF02 port slots). */
+enum class DualPortSourceKindV1 : std::uint8_t
+{
+    RealSample = 1,
+    HeldPrime = 2,
+    PortClear = 3,
+    NeutralUnassigned = 4,
+    Predicted = 5,
+    TerminalHold = 6,
+    HeldSample = 7
+};
+
+enum class DualPortClearReasonV1 : std::uint8_t
+{
+    None = 0,
+    Stop = 1,
+    Pause = 2,
+    PeerDisconnect = 3,
+    SeatReassign = 4
+};
+
+/* The session facts the wire record needs and the bundle does not carry. */
+struct DualInputWireContextV1 final
+{
+    std::uint64_t authority_term = 0;
+    std::uint64_t mode_generation = 0;
+    std::uint64_t batch_sequence = 0;
+    /* Optional per-port override; when `stated` is false the encoder derives the
+     * kind from the bundle's prediction marker (PREDICTED for a predicted port,
+     * REAL_SAMPLE otherwise) and the clear reason 0. */
+    std::array<DualPortSourceKindV1, kDualPortCountV1> source_kind{};
+    std::array<DualPortClearReasonV1, kDualPortCountV1> clear_reason{};
+    std::array<bool, kDualPortCountV1> stated{};
+};
+
+enum class DualInputWireStatusV1 : std::uint8_t
+{
+    Ok = 0,
+    InvalidArgument = 1,
+    InvalidBundle = 2,
+    InvalidBytes = 3,
+    InvalidContext = 4
+};
+
+/*
+ * Encode one canonical bundle as the frozen 154-byte record.
+ *
+ * Every mask is normalized first (UP+DOWN and LEFT+RIGHT cleared together,
+ * dual_runtime_contract.hpp: normalize_dual_port_mask_v1) and a mask with bits
+ * above the eight real pad bits is refused rather than truncated; the produced
+ * bytes are then re-checked with the frozen validator before they are returned,
+ * so an encoder bug cannot produce a record the release would reject on arrival.
+ */
+DualInputWireStatusV1 encode_dual_input_bundle_v1(
+    const DualInputBundleV1& bundle, const DualInputWireContextV1& context,
+    std::uint8_t out[kDualInputWireBytesV1]) noexcept;
+
+/*
+ * Decode a frozen 154-byte record into a canonical bundle.
+ *
+ * `logical_seat` and `owner_signing_key_id` are the receiving engine's
+ * authenticated binding for the arriving record; they are never read from the
+ * bytes. The record is refused when the frozen validator rejects it, when the
+ * key tuple cannot be formed, or when the decoded bundle is not canonical.
+ */
+DualInputWireStatusV1 decode_dual_input_bundle_v1(
+    const std::uint8_t* bytes, std::size_t size, std::uint8_t logical_seat,
+    const std::array<std::uint8_t, 32>& owner_signing_key_id,
+    DualInputBundleV1* out) noexcept;
+
+} // namespace flynes::session::dual
+
+#endif
diff --git a/shared/src/session/dual/dual_runtime_adapter.cpp b/shared/src/session/dual/dual_runtime_adapter.cpp
new file mode 100644
index 0000000..da5267a
--- /dev/null
+++ b/shared/src/session/dual/dual_runtime_adapter.cpp
@@ -0,0 +1,149 @@
+#include "dual_runtime_adapter.hpp"
+
+#include <cstring>
+
+namespace flynes::session::dual {
+namespace {
+
+fly_session_dual_content_ref_v2 to_abi(const DualContentRefV1& content) noexcept
+{
+    fly_session_dual_content_ref_v2 value{};
+    std::memcpy(value.session_id, content.session_id.data(), 16u);
+    std::memcpy(value.branch_id, content.branch_id.data(), 16u);
+    std::memcpy(value.content_hash, content.content_hash.data(), 32u);
+    value.timeline_epoch = content.timeline_epoch;
+    return value;
+}
+
+fly_session_dual_input_bundle_v2 to_abi(
+    const DualInputBundleV1& bundle) noexcept
+{
+    fly_session_dual_input_bundle_v2 value{};
+    value.struct_size = FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE;
+    value.abi_version = FLY_SESSION_ABI_VERSION_2;
+    std::memcpy(value.session_id, bundle.key.session_id.data(), 16u);
+    std::memcpy(value.branch_id, bundle.key.branch_id.data(), 16u);
+    value.timeline_epoch = bundle.key.timeline_epoch;
+    value.frame_index = bundle.key.frame_index;
+    value.seat_revision = bundle.key.seat_revision;
+    value.logical_seat = bundle.logical_seat;
+    value.predicted_port_mask = bundle.predicted_port_mask;
+    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
+    {
+        value.ports[port].mask = bundle.ports[port].mask;
+        value.ports[port].input_sequence = bundle.ports[port].input_sequence;
+    }
+    return value;
+}
+
+void from_abi(const fly_session_dual_frame_outcome_v2& value,
+              DualFrameOutcomeV1* out) noexcept
+{
+    out->frame_index = value.frame_index;
+    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
+        out->applied_input_sequence[port] = value.applied_input_sequence[port];
+    out->honoured_port_mask = value.honoured_port_mask;
+}
+
+} // namespace
+
+CAbiDualRuntimePortV1::CAbiDualRuntimePortV1(
+    const fly_session_dual_runtime_port_v2* port) noexcept
+    : port_(port)
+{
+}
+
+fly_session_result_v2 CAbiDualRuntimePortV1::load(
+    const DualContentRefV1& content) noexcept
+{
+    if (port_ == nullptr || port_->load == nullptr)
+        return FLY_SESSION_V2_UNAVAILABLE;
+    const auto value = to_abi(content);
+    ++forwarded_;
+    return port_->load(port_->context, &value);
+}
+
+fly_session_result_v2 CAbiDualRuntimePortV1::step(
+    const DualInputBundleV1& input, DualFrameOutcomeV1* out) noexcept
+{
+    if (port_ == nullptr || port_->step == nullptr)
+        return FLY_SESSION_V2_UNAVAILABLE;
+    if (out == nullptr)
+        return FLY_SESSION_V2_INVALID_ARGUMENT;
+
+    const auto value = to_abi(input);
+    fly_session_dual_frame_outcome_v2 outcome{};
+    ++forwarded_;
+    const auto result = port_->step(port_->context, &value, &outcome);
+    /*
+     * The outcome is copied back only on OK. A provider that fails must not be
+     * able to leave a half-written frame index or a stale sequence array behind
+     * for the scheduler to read as this frame's result, so a failing call leaves
+     * `out` exactly as the caller initialised it.
+     */
+    if (result == FLY_SESSION_V2_OK)
+        from_abi(outcome, out);
+    return result;
+}
+
+fly_session_result_v2 CAbiDualRuntimePortV1::export_state(
+    std::uint8_t* out, std::size_t capacity, std::size_t* out_written,
+    std::array<std::uint8_t, 32>* out_hash) noexcept
+{
+    if (port_ == nullptr || port_->export_state == nullptr)
+        return FLY_SESSION_V2_UNAVAILABLE;
+    if (out == nullptr || out_written == nullptr || out_hash == nullptr)
+        return FLY_SESSION_V2_INVALID_ARGUMENT;
+
+    std::uint8_t hash[32] = {};
+    std::size_t written = 0;
+    ++forwarded_;
+    const auto result =
+        port_->export_state(port_->context, out, capacity, &written, hash);
+    if (result != FLY_SESSION_V2_OK)
+        return result;
+    if (written > capacity)
+        return FLY_SESSION_V2_CONTRACT_VIOLATION;
+    *out_written = written;
+    std::memcpy(out_hash->data(), hash, 32u);
+    return FLY_SESSION_V2_OK;
+}
+
+fly_session_result_v2 CAbiDualRuntimePortV1::import_state(
+    const std::uint8_t* bytes, std::size_t size) noexcept
+{
+    if (port_ == nullptr || port_->import_state == nullptr)
+        return FLY_SESSION_V2_UNAVAILABLE;
+    if (bytes == nullptr && size != 0u)
+        return FLY_SESSION_V2_INVALID_ARGUMENT;
+    ++forwarded_;
+    return port_->import_state(port_->context, bytes, size);
+}
+
+fly_session_result_v2 CAbiDualRuntimePortV1::state_digest(
+    std::uint64_t frame_index, DualStateDigestV1* out) noexcept
+{
+    if (port_ == nullptr || port_->state_digest == nullptr)
+        return FLY_SESSION_V2_UNAVAILABLE;
+    if (out == nullptr)
+        return FLY_SESSION_V2_INVALID_ARGUMENT;
+
+    fly_session_dual_state_digest_v2 value{};
+    ++forwarded_;
+    const auto result =
+        port_->state_digest(port_->context, frame_index, &value);
+    if (result != FLY_SESSION_V2_OK)
+        return result;
+    static_assert(sizeof(DualStateDigestV1) ==
+                      sizeof(fly_session_dual_state_digest_v2),
+                  "the frozen DUAL digest and its ABI mirror stay the same size");
+    for (std::size_t index = 0; index < 32u; ++index)
+    {
+        out->state[index] = value.state[index];
+        out->frame[index] = value.frame[index];
+        out->pcm[index] = value.pcm[index];
+    }
+    return FLY_SESSION_V2_OK;
+}
+
+} // namespace flynes::session::dual
diff --git a/shared/src/session/dual/dual_runtime_adapter.hpp b/shared/src/session/dual/dual_runtime_adapter.hpp
new file mode 100644
index 0000000..92ad959
--- /dev/null
+++ b/shared/src/session/dual/dual_runtime_adapter.hpp
@@ -0,0 +1,76 @@
+#ifndef FLYNES_SESSION_DUAL_DUAL_RUNTIME_ADAPTER_HPP
+#define FLYNES_SESSION_DUAL_DUAL_RUNTIME_ADAPTER_HPP
+
+/*
+ * Task 10 / step 2: the engine's DUAL runtime port.
+ *
+ * The frozen internal seam is `dual::DualRuntimePort`
+ * (dual_runtime_contract.hpp) — a C++ interface, because the scheduler that
+ * drives it is C++. The public session boundary is C, so the provider that a
+ * platform supplies is `fly_session_dual_runtime_port_v2` (a table of function
+ * pointers, tail-appended to fly_session_ports_v2). This adapter is the single
+ * bridge between the two.
+ *
+ * It is a faithful pass-through and nothing else:
+ *   - every call is forwarded once, with the ABI's own argument shapes and the
+ *     provider's `context` pointer, and the provider's result code is returned
+ *     unchanged — a failure is never translated, softened or retried;
+ *   - the ABI outcome/ digest objects are zeroed before the call and copied back
+ *     verbatim afterwards, so a provider that writes only part of an outcome can
+ *     never leave stale bytes for the scheduler to read as this frame's result;
+ *   - no default, fallback, timing or policy decision lives here. Everything the
+ *     scheduler must know it learns from the provider's return code.
+ *
+ * The engine owns the adapter and calls it serially from the simulation worker,
+ * exactly as the frozen contract requires.
+ */
+
+#include "dual_runtime_contract.hpp"
+
+#include "flynes/flynes_session.h"
+
+namespace flynes::session::dual {
+
+class CAbiDualRuntimePortV1 final : public DualRuntimePort
+{
+public:
+    /*
+     * `port` must be the provider table captured by SessionPorts and must
+     * already have passed validate_dual_runtime(); a null table is rejected by
+     * the engine before the adapter exists.
+     */
+    explicit CAbiDualRuntimePortV1(
+        const fly_session_dual_runtime_port_v2* port) noexcept;
+
+    ~CAbiDualRuntimePortV1() override = default;
+
+    CAbiDualRuntimePortV1(const CAbiDualRuntimePortV1&) = delete;
+    CAbiDualRuntimePortV1& operator=(const CAbiDualRuntimePortV1&) = delete;
+
+    fly_session_result_v2 load(const DualContentRefV1& content) noexcept override;
+
+    fly_session_result_v2 step(const DualInputBundleV1& input,
+                               DualFrameOutcomeV1* out) noexcept override;
+
+    fly_session_result_v2 export_state(
+        std::uint8_t* out, std::size_t capacity, std::size_t* out_written,
+        std::array<std::uint8_t, 32>* out_hash) noexcept override;
+
+    fly_session_result_v2 import_state(const std::uint8_t* bytes,
+                                       std::size_t size) noexcept override;
+
+    fly_session_result_v2 state_digest(std::uint64_t frame_index,
+                                       DualStateDigestV1* out) noexcept override;
+
+    /* Diagnostics only: how many calls were forwarded. The engine never makes a
+     * decision from this, and the provider never sees it. */
+    [[nodiscard]] std::uint64_t forwarded() const noexcept { return forwarded_; }
+
+private:
+    const fly_session_dual_runtime_port_v2* port_ = nullptr;
+    std::uint64_t forwarded_ = 0;
+};
+
+} // namespace flynes::session::dual
+
+#endif
diff --git a/shared/src/session/link/link_control_contract.hpp b/shared/src/session/link/link_control_contract.hpp
index fcee421..e1eb225 100644
--- a/shared/src/session/link/link_control_contract.hpp
+++ b/shared/src/session/link/link_control_contract.hpp
@@ -175,21 +175,24 @@ inline constexpr std::size_t kLinkChannelBindBindingPreimageSizeV1 = 80;
  *
  * "Persist before send": the exact 312-byte 0x0212 binding and its object MUST
  * be durable before this message is emitted, and the exact 488 bytes MUST be
  * durable before READY may reference their object hash. The signature covers
  * bytes[0..424) and is appended at 424..488.
  */
 inline constexpr std::size_t kLinkHelloPretagSizeV1 = 424;
 inline constexpr std::size_t kLinkHelloSizeV1 = 488;
 
 /*
- * LINK_READY_V1, exactly 368 bytes, big-endian, zero-filled reserved fields.
+ * LINK_READY_V1, exactly 432 bytes (a 368-byte pretag plus the 64-byte signature
+ * at 368..432), big-endian, zero-filled reserved fields. The 368 figure that used
+ * to stand here was the pretag alone, which contradicted this table and
+ * kLinkReadySizeV1.
  * (Was 384: +8 for the 16-byte channel_id, -24 for the deleted u64
  * channel_bind_id and the u64 that followed it.)
  *
  *   off  size  field
  *     0     2  version u16be (= 1)
  *     2     6  reserved_zero[6]
  *     8     1  sender_role
  *     9     1  receiver_role
  *    10     1  phase            (MUST be LinkPhaseV1::Reconcile = 2)
  *    11     1  ready_phase      (LinkReadyPhaseV1::Ready = 1 / Ack = 2)
diff --git a/shared/tests/nearby/contract/test_dual_stream_closed.cpp b/shared/tests/nearby/contract/test_dual_stream_closed.cpp
new file mode 100644
index 0000000..cb1e2af
--- /dev/null
+++ b/shared/tests/nearby/contract/test_dual_stream_closed.cpp
@@ -0,0 +1,193 @@
+/*
+ * Task 10, step 3: STREAM is closed explicitly in this release.
+ *
+ * The DUAL MVP may not advertise, negotiate or call any STREAM/media path. This
+ * test is the DUAL gate's own record of that closure. It asserts three things:
+ *
+ *   1. the machine's capability advertisement is DUAL and nothing else, and a
+ *      peer that can only offer STREAM is refused with an explicit
+ *      "unsupported by this release" reason rather than silently accepted;
+ *   2. the DUAL mode discriminator admits DUAL alone: HOST_STREAM is refused
+ *      with FLY_SESSION_V2_UNAVAILABLE;
+ *   3. structurally, the new public DUAL runtime seam ends at `state_digest` and
+ *      the public provider table ends at the DUAL runtime slot, so there is no
+ *      codec, encoder, decoder, sink, video or audio entry point in the public
+ *      session ABI for a STREAM call to reach.
+ *
+ * Point 3 is a compile-time property of the ABI, which is why it is asserted
+ * with static_assert here instead of counted at runtime: the acceptance record
+ * carries the separate source audit that no codec/media provider call exists in
+ * shared/src/session (see out/logs/task10-integration-notes.md).
+ */
+
+#include "dual/dual_runtime_contract.hpp"
+#include "flynes/flynes_session.h"
+#include "link/link_control_contract.hpp"
+
+#include <cstdint>
+#include <cstdio>
+
+namespace {
+
+namespace link = flynes::session::link;
+namespace dual = flynes::session::dual;
+
+int failures = 0;
+
+void check(bool condition, const char* message)
+{
+    if (!condition)
+    {
+        std::fprintf(stderr, "FAIL: %s\n", message);
+        ++failures;
+    }
+}
+
+/* ------------------------------------------------------- 3. structural pins */
+
+/*
+ * The DUAL runtime table's last member is `state_digest`. Appending anything
+ * after it (a codec, an encoder, a sink) breaks the ABI shape this build
+ * promises, so the assert fails rather than the addition slipping in.
+ */
+static_assert(FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE ==
+                  offsetof(fly_session_dual_runtime_port_v2, state_digest) +
+                      sizeof(((fly_session_dual_runtime_port_v2*)0)->state_digest),
+              "the DUAL runtime provider ends at state_digest");
+
+/* The public provider table's last member is the DUAL runtime slot. */
+static_assert(FLY_SESSION_PORTS_V2_SIZE ==
+                  FLY_SESSION_PORTS_V2_R1_SIZE +
+                      sizeof(((fly_session_ports_v2*)0)->dual_runtime),
+              "the provider table ends at the DUAL runtime slot");
+
+/* The DUAL content/bundle/outcome/digest values carry no media surface. */
+static_assert(FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE ==
+                  offsetof(fly_session_dual_input_bundle_v2, ports) +
+                      FLY_SESSION_DUAL_PORT_COUNT_V2 *
+                          sizeof(fly_session_dual_port_sample_v2),
+              "the DUAL bundle ends at its four port samples");
+static_assert(FLY_SESSION_DUAL_STATE_DIGEST_V2_SIZE == 96,
+              "the DUAL state digest is state||frame||pcm");
+
+/*
+ * The capability vocabulary this release advertises contains DUAL and nothing
+ * else, and the two STREAM bits are known-but-unsupported rather than unknown.
+ */
+static_assert(link::kLinkSupportedCapabilityMaskV1 == link::kLinkCapabilityDualV1,
+              "this release advertises DUAL only");
+static_assert((link::kLinkSupportedCapabilityMaskV1 &
+               link::kLinkCapabilityStreamVideoV1) == 0,
+              "STREAM video is never advertised");
+static_assert((link::kLinkSupportedCapabilityMaskV1 &
+               link::kLinkCapabilityStreamAudioV1) == 0,
+              "STREAM audio is never advertised");
+static_assert(link::kLinkKnownCapabilityMaskV1 ==
+                  static_cast<std::uint16_t>(
+                      link::kLinkCapabilityDualV1 |
+                      link::kLinkCapabilityStreamVideoV1 |
+                      link::kLinkCapabilityStreamAudioV1),
+              "STREAM bits stay known, so a STREAM-only peer is refused with a "
+              "reason instead of being treated as an unknown extension");
+static_assert(dual::kDualSupportedModeMaskV1 == 1u,
+              "only the DUAL mode discriminant is selectable");
+
+/* ------------------------------------------------------- 1. capability gate */
+
+void stream_is_refused_with_an_explicit_reason()
+{
+    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityDualV1) ==
+              link::LinkProposalSupportV1::Supported,
+          "a DUAL proposal is supported");
+
+    const auto stream_only = static_cast<std::uint16_t>(
+        link::kLinkCapabilityStreamVideoV1 | link::kLinkCapabilityStreamAudioV1);
+    check(link::evaluate_link_proposal_v1(stream_only) ==
+              link::LinkProposalSupportV1::UnsupportedByThisRelease,
+          "a peer that offers only STREAM is refused as unsupported by this "
+          "release");
+    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityStreamVideoV1) ==
+              link::LinkProposalSupportV1::UnsupportedByThisRelease,
+          "a STREAM video only proposal is refused as unsupported");
+    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityStreamAudioV1) ==
+              link::LinkProposalSupportV1::UnsupportedByThisRelease,
+          "a STREAM audio only proposal is refused as unsupported");
+    check(link::evaluate_link_proposal_v1(0) ==
+              link::LinkProposalSupportV1::UnsupportedByThisRelease,
+          "an empty proposal is refused as unsupported");
+
+    /* A bit this release does not know is a different, harder refusal. */
+    check(link::evaluate_link_proposal_v1(0x8000) ==
+              link::LinkProposalSupportV1::UnknownCriticalCapability,
+          "an unknown capability bit is an unknown critical capability");
+    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
+              link::kLinkCapabilityDualV1 | 0x8000)) ==
+              link::LinkProposalSupportV1::UnknownCriticalCapability,
+          "an unknown bit is refused even alongside a supported one");
+    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
+              link::kLinkCapabilityDualV1 | link::kLinkCapabilityStreamVideoV1 |
+              link::kLinkCapabilityStreamAudioV1)) ==
+              link::LinkProposalSupportV1::Supported,
+          "a DUAL plus STREAM proposal still negotiates DUAL: the STREAM bits "
+          "are known, this release never selects STREAM, and the local "
+          "advertisement never carries them, so a peer asking for STREAM can "
+          "never open a STREAM path");
+
+    /* The two refusal values are distinct, so the reason is not conflated. */
+    check(link::LinkProposalSupportV1::UnsupportedByThisRelease !=
+              link::LinkProposalSupportV1::UnknownCriticalCapability,
+          "unsupported and unknown are distinct reasons");
+}
+
+/* ------------------------------------------------------------ 2. mode gate */
+
+void host_stream_mode_is_refused()
+{
+    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::Dual) ==
+              FLY_SESSION_V2_OK,
+          "DUAL mode is selectable");
+    check(dual::evaluate_dual_mode_v1(dual::DualModeV1::HostStream) ==
+              FLY_SESSION_V2_UNAVAILABLE,
+          "HOST_STREAM mode is refused as unavailable");
+    check((dual::kDualSupportedModeMaskV1 &
+           (1u << (static_cast<std::uint8_t>(dual::DualModeV1::HostStream) -
+                   1u))) == 0,
+          "the supported mode mask excludes HOST_STREAM");
+}
+
+/*
+ * The public action vocabulary has no STREAM start: the only run-start kinds are
+ * the DUAL selector and starter appended in step 1.
+ */
+void the_public_action_vocabulary_has_no_stream_start()
+{
+    check(FLY_SESSION_ACTION_START_DUAL_V2 == 43 &&
+              FLY_SESSION_ACTION_SELECT_CONTENT_V2 == 42,
+          "the DUAL controls keep their appended discriminants");
+    check(FLY_SESSION_DUAL_MODE_DUAL_V2 == 1 &&
+              FLY_SESSION_DUAL_MODE_NONE_V2 == 0,
+          "the published DUAL mode vocabulary admits DUAL alone");
+    check(FLY_SESSION_DUAL_FREEZE_NONE_V2 == 0 &&
+              FLY_SESSION_DUAL_FREEZE_DIGEST_MISMATCH_V2 == 4 &&
+              FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2 == 6 &&
+              FLY_SESSION_DUAL_FREEZE_SHUTDOWN_V2 == 8,
+          "the published freeze vocabulary matches the frozen DUAL seam, so a "
+          "freeze can never be reported as a silent mode switch");
+}
+
+} // namespace
+
+int main()
+{
+    stream_is_refused_with_an_explicit_reason();
+    host_stream_mode_is_refused();
+    the_public_action_vocabulary_has_no_stream_start();
+    if (failures != 0)
+    {
+        std::fprintf(stderr, "nearby_dual_stream_closed: %d failure(s)\n",
+                     failures);
+        return 1;
+    }
+    std::puts("nearby_dual_stream_closed: PASS");
+    return 0;
+}
diff --git a/shared/tests/nearby/contract/test_session_v2_abi.c b/shared/tests/nearby/contract/test_session_v2_abi.c
index b78f690..781e875 100644
--- a/shared/tests/nearby/contract/test_session_v2_abi.c
+++ b/shared/tests/nearby/contract/test_session_v2_abi.c
@@ -47,20 +47,29 @@ _Static_assert(FLY_SESSION_INPUT_V2_R0_SIZE ==
 _Static_assert(FLY_SESSION_INPUT_V2_SIZE ==
                    FLY_SESSION_INPUT_V2_R0_SIZE +
                        FLY_SESSION_DUAL_PORT_COUNT_V2 * 4,
                "the DUAL port mask is a pure tail append");
 _Static_assert(offsetof(fly_session_snapshot_v2, dual_mode) ==
                    FLY_SESSION_SNAPSHOT_V2_R0_SIZE,
                "the snapshot R0 prefix stops before the DUAL block");
 _Static_assert(FLY_SESSION_SNAPSHOT_V2_SIZE >
                    FLY_SESSION_SNAPSHOT_V2_R0_SIZE,
                "the DUAL snapshot block is appended");
+_Static_assert(FLY_SESSION_SNAPSHOT_V2_R1_SIZE ==
+                   offsetof(fly_session_snapshot_v2, dual_state_digest),
+               "the snapshot R1 prefix stops before the DUAL digest block");
+_Static_assert(FLY_SESSION_SNAPSHOT_V2_SIZE ==
+                   FLY_SESSION_SNAPSHOT_V2_R1_SIZE + 96,
+               "the DUAL digest block is a pure tail append");
+_Static_assert(FLY_SESSION_SNAPSHOT_V2_R0_SIZE <=
+                   FLY_SESSION_SNAPSHOT_V2_R1_SIZE,
+               "the prefixes stay ordered");
 _Static_assert(FLY_SESSION_PORTS_V2_R1_SIZE ==
                    offsetof(fly_session_ports_v2, dual_runtime),
                "the pre-DUAL provider table stops before the runtime slot");
 _Static_assert(FLY_SESSION_PORTS_V2_SIZE > FLY_SESSION_PORTS_V2_R1_SIZE,
                "the DUAL runtime slot is appended");
 _Static_assert(offsetof(fly_session_dual_runtime_port_v2, struct_size) == 0,
                "DUAL runtime size prefix");
 _Static_assert(offsetof(fly_session_dual_runtime_port_v2, load) >
                    offsetof(fly_session_dual_runtime_port_v2, release),
                "the DUAL runtime keeps the provider retain/release prefix");
diff --git a/shared/tests/nearby/contract/test_session_v2_contract.cpp b/shared/tests/nearby/contract/test_session_v2_contract.cpp
index 03b7cc8..a0855b4 100644
--- a/shared/tests/nearby/contract/test_session_v2_contract.cpp
+++ b/shared/tests/nearby/contract/test_session_v2_contract.cpp
@@ -655,20 +655,37 @@ void test_dual_prefix_appends_keep_older_callers_legal()
     fly_session_snapshot_v2 full{};
     full.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
     full.abi_version = FLY_SESSION_ABI_VERSION_2;
     check(fly_session_view_read_v2(view, &full) == FLY_SESSION_V2_OK,
           "a current snapshot reads successfully");
     check(full.dual_mode == FLY_SESSION_DUAL_MODE_NONE_V2 &&
               full.dual_state == FLY_SESSION_DUAL_UNLOADED_V2 &&
               full.dual_freeze_reason == FLY_SESSION_DUAL_FREEZE_NONE_V2,
           "a non-DUAL engine reports the neutral DUAL run state");
 
+    /*
+     * A reader from before the DUAL digest block: it declares exactly the R1
+     * prefix, so it must be served the run state and none of the digest bytes
+     * may be written into its buffer.
+     */
+    Guarded pre_digest{};
+    pre_digest.canary = 0x5A5A5A5A5A5A5A5Aull;
+    pre_digest.snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_R1_SIZE;
+    pre_digest.snapshot.abi_version = FLY_SESSION_ABI_VERSION_2;
+    check(fly_session_view_read_v2(view, &pre_digest.snapshot) ==
+              FLY_SESSION_V2_OK,
+          "a pre-digest snapshot prefix reads successfully");
+    check(pre_digest.canary == 0x5A5A5A5A5A5A5A5Aull &&
+              pre_digest.snapshot.dual_state_digest[0] == 0 &&
+              pre_digest.snapshot.dual_pcm_digest[31] == 0,
+          "the digest block is not written past the size the caller declared");
+
     fly_session_view_release_v2(view);
     check(fly_session_begin_shutdown_v2(engine, 1301) == FLY_SESSION_V2_ACCEPTED &&
               fly_session_destroy_v2(engine) == FLY_SESSION_V2_OK,
           "pre-DUAL engine shuts down");
 }
 
 } // namespace
 
 int main()
 {
diff --git a/shared/tests/nearby/harness/two_engine_loopback_fixture.hpp b/shared/tests/nearby/harness/two_engine_loopback_fixture.hpp
index 6741fc1..f63966a 100644
--- a/shared/tests/nearby/harness/two_engine_loopback_fixture.hpp
+++ b/shared/tests/nearby/harness/two_engine_loopback_fixture.hpp
@@ -2904,20 +2904,36 @@ public:
     [[nodiscard]] std::uint32_t tampered_logical_messages() const noexcept
     {
         return tampered_logical_;
     }
     /* Times the tamper hook could not reassemble a logical message it was asked
      * about. Non-zero means the GATT tamper cases for that run prove nothing. */
     [[nodiscard]] std::uint32_t logical_tamper_reassembly_mismatches() const noexcept
     {
         return gatt_reassembly_mismatches_;
     }
+
+    /*
+     * How many TAMPERED logical messages were actually accepted by the receiving
+     * engine's byte-event intake. This is the counter that separates "the harness
+     * rewrote a byte of a record that then crossed" from "the harness rewrote a
+     * byte that never left", and without it a negative case that still reaches
+     * the lobby cannot be attributed to the engine at all.
+     */
+    void note_tampered_group_delivered() noexcept
+    {
+        ++tampered_groups_delivered_;
+    }
+    [[nodiscard]] std::uint32_t tampered_logical_delivered() const noexcept
+    {
+        return tampered_groups_delivered_;
+    }
     [[nodiscard]] std::uint32_t logical_tamper_body_index() const noexcept
     {
         return gatt_tamper_index_;
     }
     [[nodiscard]] std::uint8_t logical_tamper_before() const noexcept
     {
         return gatt_tamper_before_;
     }
 
     /*
@@ -2945,61 +2961,66 @@ public:
         if (logical[1] != gatt_tamper_.logical_type) return false;
         ++gatt_tamper_occurrences_;
         if (gatt_tamper_.occurrence != 0u &&
             gatt_tamper_occurrences_ != gatt_tamper_.occurrence)
             return false;
         const std::size_t body_size =
             (static_cast<std::size_t>(logical[4]) << 24u) |
             (static_cast<std::size_t>(logical[5]) << 16u) |
             (static_cast<std::size_t>(logical[6]) << 8u) |
             static_cast<std::size_t>(logical[7]);
-        if (body_size == 0u || logical.size() != 8u + body_size + 32u)
+        if (body_size == 0u || logical.size() < 8u + body_size + 32u)
         {
-            /* Counted, not asserted: this is a limitation of the tamper HOOK's
-             * own fragment-to-logical reassembly, not a product failure, and a
-             * test that asserted it would be asserting a harness bug. It is
-             * reported through `logical_tamper_reassembly_mismatches()` so a
-             * caller can refuse to draw conclusions from a run where it happened.
+            /* The group the relay hands over starts at its own delivery
+             * watermark, which a BACKPRESSURE return can leave in the MIDDLE of a
+             * logical message; such a group is not this record's boundary and is
+             * not a harness error. It is COUNTED so a run where it happened is
+             * visible, and `logical_tamper_reassembly_mismatches()` lets a caller
+             * refuse to draw conclusions from one.
              */
             ++gatt_reassembly_mismatches_;
             return false;
         }
+        /* The record is the PREFIX of the reassembled bytes: anything past it
+         * belongs to the next logical message and must cross untouched. */
+        const std::size_t record_size = 8u + body_size + 32u;
         std::size_t index = gatt_tamper_.body_index;
         if (index == kLastBodyByte) index = body_size - 1u;
         if (index >= body_size)
         {
             check(false,
                   "the armed logical-message tamper names a byte the sender's own "
                   "record really has");
             return false;
         }
         const std::size_t absolute = 8u + index;
         gatt_tamper_index_ = static_cast<std::uint32_t>(index);
         gatt_tamper_before_ = logical[absolute];
         logical[absolute] = static_cast<std::uint8_t>(
             gatt_tamper_before_ ^ gatt_tamper_.xor_mask);
-        /* Recompute the record's own trailing integrity hash. */
+        /* Recompute THIS record's own trailing integrity hash. */
         const char* domain =
             logical[0] == 2u ? "flynes-gatt-logical-v2" : "flynes-gatt-logical-v1";
         const auto digest =
             wire::domain_hash(domain, logical.data(), 8u + body_size);
         std::copy(digest.begin(), digest.end(),
                   logical.begin() + static_cast<std::ptrdiff_t>(8u + body_size));
-        /* Write the whole logical message back into the same fragment payloads. */
+        /* Write the record back into the same fragment payloads, only as far as
+         * the record goes. */
         std::size_t cursor = 0;
         for (auto& fragment : *group)
         {
             const std::size_t payload = fragment.size() - header;
-            std::copy(logical.begin() + static_cast<std::ptrdiff_t>(cursor),
-                      logical.begin() + static_cast<std::ptrdiff_t>(cursor + payload),
-                      fragment.begin() + static_cast<std::ptrdiff_t>(header));
-            cursor += payload;
+            for (std::size_t offset = 0; offset < payload && cursor < record_size;
+                 ++offset, ++cursor)
+                fragment[header + offset] = logical[cursor];
+            if (cursor >= record_size) break;
         }
         ++tampered_logical_;
         /* A single-occurrence rule is spent after one injection; an "every
          * occurrence" rule stays armed for the whole drive, by definition. */
         if (gatt_tamper_.occurrence != 0u) gatt_tamper_armed_ = false;
         return true;
     }
 
     /* True when this unit is a link-control READY/ACK frame from the direction
      * the filter holds. The cell layout is the repository's own app frame:
@@ -3094,20 +3115,21 @@ private:
     std::uint8_t tampered_before_ = 0;
     std::uint8_t tampered_after_ = 0;
     /* W3 tamper matrix: the GATT logical-message fault. */
     GattTamperRule gatt_tamper_{};
     bool gatt_tamper_armed_ = false;
     std::uint32_t gatt_tamper_occurrences_ = 0;
     std::uint32_t tampered_logical_ = 0;
     std::uint32_t gatt_tamper_index_ = 0;
     std::uint8_t gatt_tamper_before_ = 0;
     std::uint32_t gatt_reassembly_mismatches_ = 0;
+    std::uint32_t tampered_groups_delivered_ = 0;
 };
 
 /*
  * ------------------------------------------------------------------------- *
  * Increment 2 of the two-engine driver: the counted, bounded provider pump.
  *
  * WHAT THE PUMP IS ALLOWED TO DO
  *   Answer CLASS-1 operation terminals, and nothing else. It never delivers a
  *   connection, never delivers bytes, never advances a stage and never decides an
  *   outcome: those are the transport's and the engine's jobs. Every answer below
@@ -4010,20 +4032,54 @@ struct RelayDirectionState final
     std::size_t fragments_relayed = 0;
     std::size_t acks_relayed = 0;
     /* Class-2 event sequence for the RECEIVING inbox; strictly increasing. */
     std::uint64_t next_sequence = 1;
     /*
      * Exactly what crossed, in order, kept so a test can compare it with the writing
      * engine's own `written_fragments` instead of taking this harness's word for it.
      */
     std::vector<std::vector<std::uint8_t>> delivered;
     std::vector<std::vector<std::uint8_t>> delivered_acks;
+    /*
+     * W3 tamper matrix: fragments that must be delivered INSTEAD of the source's
+     * own, stored per source-fragment index and PERSISTENTLY.
+     *
+     * This has to live in the direction's state rather than in a per-attempt
+     * local: a BACKPRESSURE return abandons the attempt and the fragment is
+     * offered again on a later round, so a tamper applied to a throw-away copy
+     * would simply be discarded and the ORIGINAL bytes would cross - which is
+     * exactly the bug that made three tamper cases look fail-open when nothing
+     * had in fact been tampered on the wire.
+     */
+    std::vector<std::size_t> replaced_index;
+    std::vector<std::vector<std::uint8_t>> replaced_bytes;
+    int replaced_delivered = 0;
+
+    void remember_replacement(std::size_t index, std::vector<std::uint8_t> bytes)
+    {
+        for (std::size_t slot = 0; slot < replaced_index.size(); ++slot)
+            if (replaced_index[slot] == index)
+            {
+                replaced_bytes[slot] = std::move(bytes);
+                return;
+            }
+        replaced_index.push_back(index);
+        replaced_bytes.push_back(std::move(bytes));
+    }
+
+    [[nodiscard]] const std::vector<std::uint8_t>* replacement_at(
+        std::size_t index) const
+    {
+        for (std::size_t slot = 0; slot < replaced_index.size(); ++slot)
+            if (replaced_index[slot] == index) return &replaced_bytes[slot];
+        return nullptr;
+    }
 };
 
 struct QuicDirectionState final
 {
     /* How much of the source's write log is already staged, and how many of the
      * sink's own granted reads have been answered. */
     std::size_t writes_staged = 0;
     std::size_t reads_answered = 0;
     /* Class-2 event sequence for the RECEIVING inbox; strictly increasing. */
     std::uint64_t next_sequence = 1;
@@ -4204,48 +4260,65 @@ inline void relay_one_direction(EngineFixture& source, RelayDirectionState& stat
                       "logical message inside its own written fragments");
                 return;
             }
         }
         /*
          * W3 tamper matrix. The group of fragments that makes up ONE logical
          * message is COPIED only while a logical-message tamper is armed, so the
          * ordinary path still delivers the source's own fragment object without
          * a copy and without a chance of being altered.
          */
-        std::vector<std::vector<std::uint8_t>> tampered_group;
+        /*
+         * W3 tamper matrix. The armed rule is applied ONCE per logical message and
+         * the RESULT is remembered in the direction's state, so a retry after
+         * BACKPRESSURE re-offers the same tampered bytes instead of silently
+         * falling back to the sender's originals.
+         */
         const std::size_t group_begin = state.fragments_relayed;
         if (transport.logical_tamper_armed())
         {
-            tampered_group.assign(
+            std::vector<std::vector<std::uint8_t>> group(
                 source.discovery.written_fragments.begin() +
                     static_cast<std::ptrdiff_t>(group_begin),
                 source.discovery.written_fragments.begin() +
                     static_cast<std::ptrdiff_t>(stop));
-            transport.tamper_logical_group(
-                source_is_peripheral
-                    ? LoopbackTransport::QuicFilter::PeripheralToCentral
-                    : LoopbackTransport::QuicFilter::CentralToPeripheral,
-                &tampered_group);
+            if (transport.tamper_logical_group(
+                    source_is_peripheral
+                        ? LoopbackTransport::QuicFilter::PeripheralToCentral
+                        : LoopbackTransport::QuicFilter::CentralToPeripheral,
+                    &group))
+                for (std::size_t slot = 0; slot < group.size(); ++slot)
+                    state.remember_replacement(group_begin + slot,
+                                               std::move(group[slot]));
         }
+        int replacements_delivered = 0;
         for (std::size_t index = group_begin; index < stop; ++index)
         {
             /* `group_begin`, not `state.fragments_relayed`: the watermark advances
              * inside this loop, so subtracting it would re-deliver the first
              * fragment of the group forever. */
+            const std::vector<std::uint8_t>* replacement =
+                state.replacement_at(index);
             const std::vector<std::uint8_t>& fragment =
-                tampered_group.empty()
-                    ? source.discovery.written_fragments[index]
-                    : tampered_group[index - group_begin];
+                replacement != nullptr
+                    ? *replacement
+                    : source.discovery.written_fragments[index];
             if (!relay_fragment_once(sink, state, fragment, &state.delivered))
                 return;
+            if (replacement != nullptr) ++replacements_delivered;
             ++state.fragments_relayed;
         }
+        if (replacements_delivered > 0)
+        {
+            state.replaced_delivered += replacements_delivered;
+            transport.note_tampered_group_delivered();
+        }
     }
 
     if (state.acks_relayed < source.discovery.ack_fragments.size())
     {
         /* An engine's own acknowledgements go back to the peer whose fragments they
          * acknowledge, one fragment per call. Without them the writer of a logical
          * message stops half-way through it while its acknowledgement is
          * outstanding. */
         if (!relay_fragment_once(sink, state,
                                  source.discovery.ack_fragments[state.acks_relayed],
diff --git a/shared/tests/nearby/integration/test_dual_runtime_seam.cpp b/shared/tests/nearby/integration/test_dual_runtime_seam.cpp
new file mode 100644
index 0000000..40f351f
--- /dev/null
+++ b/shared/tests/nearby/integration/test_dual_runtime_seam.cpp
@@ -0,0 +1,488 @@
+/*
+ * Task 10 / step 2: the two DUAL seam pieces the engine needs before it can own
+ * a DUAL run.
+ *
+ *   1. the canonical input unit on the wire: encoder/decoder for the frozen
+ *      154-byte CanonicalInputBundleV1 record (message tag 0xFF02), including
+ *      the three asymmetries between that record and the in-memory bundle;
+ *   2. the runtime port adapter: the frozen `dual::DualRuntimePort` implemented
+ *      over the public C provider table, proved to be a faithful pass-through.
+ *
+ * Everything here is asserted against the release's own frozen validator
+ * (`wire::check`), so "the encoder is right" means "the codec that will receive
+ * these bytes accepts them".
+ */
+
+#include "dual/canonical_input_wire.hpp"
+#include "dual/dual_runtime_adapter.hpp"
+#include "wire/session_codec.hpp"
+
+#include <cstdint>
+#include <cstdio>
+#include <cstring>
+
+namespace {
+
+namespace dual = flynes::session::dual;
+namespace wire = flynes::session::wire;
+
+int failures = 0;
+
+void check(bool condition, const char* message)
+{
+    if (!condition)
+    {
+        std::fprintf(stderr, "FAIL: %s\n", message);
+        ++failures;
+    }
+}
+
+std::array<std::uint8_t, 16> id16(std::uint8_t first)
+{
+    std::array<std::uint8_t, 16> value{};
+    value[0] = first;
+    return value;
+}
+
+std::array<std::uint8_t, 32> id32(std::uint8_t first)
+{
+    std::array<std::uint8_t, 32> value{};
+    value[0] = first;
+    return value;
+}
+
+dual::DualInputKeyV1 make_key() noexcept
+{
+    dual::DualInputKeyV1 key{};
+    key.session_id = id16(0x11);
+    key.branch_id = id16(0x22);
+    key.timeline_epoch = 7;
+    key.seat_revision = 3;
+    key.frame_index = 41;
+    return key;
+}
+
+/* A canonical bundle for one seat, built through the release's own builder. */
+dual::DualInputBundleV1 make_bundle(std::uint32_t mask, std::uint8_t seat,
+                                    std::uint8_t owner_first) noexcept
+{
+    dual::DualPortInputArrayV1 samples{};
+    for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
+    {
+        samples[port].mask = mask;
+        samples[port].sequence = 10 + port;
+    }
+    dual::DualInputBundleV1 bundle{};
+    const auto status = dual::canonical_input_build_v1(
+        make_key(), seat, id32(owner_first), samples, &bundle);
+    check(status == dual::DualInputStatusV1::Ok,
+          "the fixture bundle is canonical");
+    return bundle;
+}
+
+dual::DualInputWireContextV1 make_context() noexcept
+{
+    dual::DualInputWireContextV1 context{};
+    context.authority_term = 5;
+    context.mode_generation = 9;
+    context.batch_sequence = 12;
+    return context;
+}
+
+/* ------------------------------------------------------ 1. the wire record */
+
+void the_canonical_unit_round_trips_through_the_frozen_record()
+{
+    const auto bundle = make_bundle(0x51u, 0, 0x55);
+    const auto context = make_context();
+    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
+
+    check(dual::encode_dual_input_bundle_v1(bundle, context, bytes) ==
+              dual::DualInputWireStatusV1::Ok,
+          "a canonical bundle encodes");
+    check(bytes[0] == 0u && bytes[1] == 1u, "the record carries version 1");
+    check(bytes[82] == 0u, "no port is marked predicted");
+
+    /* The release's own validator is the authority for these bytes. */
+    std::uint8_t hash[32] = {};
+    check(wire::check("CanonicalInputBundleV1", bytes,
+                      dual::kDualInputWireBytesV1, hash) == wire::Status::Ok,
+          "the encoder's output is accepted by the frozen codec");
+
+    dual::DualInputBundleV1 decoded{};
+    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
+                                            0, id32(0x55), &decoded) ==
+              dual::DualInputWireStatusV1::Ok,
+          "the record decodes");
+    check(decoded == bundle,
+          "the decoded bundle is byte identical to the encoded one");
+    check(decoded.ports[0].mask == 0x51u && decoded.ports[0].input_sequence == 10u,
+          "the port sample survives the round trip");
+}
+
+void a_predicted_port_keeps_its_marker_on_the_wire()
+{
+    auto bundle = make_bundle(0x0Fu, 1, 0x77);
+    bundle.predicted_port_mask = 0x2u;
+    auto context = make_context();
+    context.stated[1] = true;
+    context.source_kind[1] = dual::DualPortSourceKindV1::Predicted;
+
+    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
+    check(dual::encode_dual_input_bundle_v1(bundle, context, bytes) ==
+              dual::DualInputWireStatusV1::Ok,
+          "a bundle with one predicted port encodes");
+    check(bytes[82] == 0x02u,
+          "the wire prediction mask follows the predicted source_kind");
+
+    dual::DualInputBundleV1 decoded{};
+    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
+                                            1, id32(0x77), &decoded) ==
+              dual::DualInputWireStatusV1::Ok,
+          "a predicted record decodes");
+    check(decoded.predicted_port_mask == 0x2u,
+          "the prediction marker is derived back from source_kind");
+    check(decoded == bundle, "the predicted bundle survives the round trip");
+
+    /* A prediction marker that disagrees with the stated kind is refused: the
+     * receiver derives reality from source_kind, so a contradiction would let
+     * one side call a prediction real. */
+    auto lying = context;
+    lying.source_kind[1] = dual::DualPortSourceKindV1::RealSample;
+    check(dual::encode_dual_input_bundle_v1(bundle, lying, bytes) ==
+              dual::DualInputWireStatusV1::InvalidContext,
+          "a prediction marker that contradicts source_kind is refused");
+}
+
+void the_wire_cannot_name_its_own_owner_or_seat()
+{
+    const auto bundle = make_bundle(0x51u, 0, 0x55);
+    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
+    check(dual::encode_dual_input_bundle_v1(bundle, make_context(), bytes) ==
+              dual::DualInputWireStatusV1::Ok,
+          "the fixture encodes");
+
+    dual::DualInputBundleV1 decoded{};
+    check(dual::decode_dual_input_bundle_v1(bytes, dual::kDualInputWireBytesV1,
+                                            2, id32(0x99), &decoded) ==
+              dual::DualInputWireStatusV1::Ok,
+          "the record decodes against a caller supplied binding");
+    check(decoded.logical_seat == 2 &&
+              decoded.owner_signing_key_id == id32(0x99),
+          "the seat and the owner key come from the authenticated caller, never "
+          "from the bytes");
+    check(decoded.key.session_id == id16(0x11) &&
+              decoded.key.frame_index == 41 &&
+              decoded.key.seat_revision == 3,
+          "the record still carries its own key tuple");
+}
+
+void malformed_records_are_refused_by_the_frozen_validator()
+{
+    const auto bundle = make_bundle(0x51u, 0, 0x55);
+    std::uint8_t bytes[dual::kDualInputWireBytesV1] = {};
+    check(dual::encode_dual_input_bundle_v1(bundle, make_context(), bytes) ==
+              dual::DualInputWireStatusV1::Ok,
+          "the fixture encodes");
+
+    dual::DualInputBundleV1 decoded{};
+
+    auto short_record = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
+    std::memcpy(short_record.data(), bytes, short_record.size());
+    check(dual::decode_dual_input_bundle_v1(short_record.data(),
+                                            dual::kDualInputWireBytesV1 - 1, 0,
+                                            id32(0x55), &decoded) ==
+              dual::DualInputWireStatusV1::InvalidArgument,
+          "a record of the wrong length is refused");
+
+    auto reserved = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
+    std::memcpy(reserved.data(), bytes, reserved.size());
+    reserved[83] = 1u;
+    check(dual::decode_dual_input_bundle_v1(reserved.data(), reserved.size(), 0,
+                                            id32(0x55), &decoded) ==
+              dual::DualInputWireStatusV1::InvalidBytes,
+          "a non-zero reserved byte is refused");
+
+    auto unknown_kind = std::array<std::uint8_t, dual::kDualInputWireBytesV1>{};
+    std::memcpy(unknown_kind.data(), bytes, unknown_kind.size());
+    unknown_kind[90] = 0u;
+    check(dual::decode_dual_input_bundle_v1(unknown_kind.data(),
+                                            unknown_kind.size(), 0, id32(0x55),
+                                            &decoded) ==
+              dual::DualInputWireStatusV1::InvalidBytes,
+          "an unknown source_kind is refused");
+
+    /* A conflicted d-pad state is not transmittable at all: the canonical form
+     * predicate rejects it, so the encoder must refuse rather than emit it. */
+    auto conflicted = bundle;
+    conflicted.ports[0].mask = 0x30u; /* UP | DOWN */
+    check(dual::encode_dual_input_bundle_v1(conflicted, make_context(),
+                                            bytes) ==
+              dual::DualInputWireStatusV1::InvalidBundle,
+          "a bundle carrying an impossible d-pad state never leaves the send "
+          "path");
+
+    auto missing_facts = make_context();
+    missing_facts.authority_term = 0;
+    check(dual::encode_dual_input_bundle_v1(bundle, missing_facts, bytes) ==
+              dual::DualInputWireStatusV1::InvalidContext,
+          "the session facts the record needs are required");
+}
+
+/* ------------------------------------------------------- 2. runtime adapter */
+
+struct FakeAbiRuntime
+{
+    fly_session_result_v2 load_result = FLY_SESSION_V2_OK;
+    fly_session_result_v2 step_result = FLY_SESSION_V2_OK;
+    fly_session_result_v2 export_result = FLY_SESSION_V2_OK;
+    fly_session_result_v2 import_result = FLY_SESSION_V2_OK;
+    fly_session_result_v2 digest_result = FLY_SESSION_V2_OK;
+    bool export_overclaims = false;
+
+    int loads = 0;
+    int steps = 0;
+    int exports = 0;
+    int imports = 0;
+    int digests = 0;
+
+    fly_session_dual_content_ref_v2 last_content{};
+    fly_session_dual_input_bundle_v2 last_input{};
+    fly_session_dual_frame_outcome_v2 step_outcome{};
+    std::array<std::uint8_t, 32> last_export_hash{};
+    std::size_t last_export_capacity = 0;
+    std::uint8_t last_export_bytes[16] = {};
+    std::size_t last_import_size = 0;
+    std::uint64_t last_digest_frame = 0;
+    fly_session_dual_state_digest_v2 digest_value{};
+};
+
+fly_session_result_v2 abi_load(void* context,
+                              const fly_session_dual_content_ref_v2* content)
+{
+    auto* fake = static_cast<FakeAbiRuntime*>(context);
+    ++fake->loads;
+    fake->last_content = *content;
+    return fake->load_result;
+}
+
+fly_session_result_v2 abi_step(void* context,
+                              const fly_session_dual_input_bundle_v2* input,
+                              fly_session_dual_frame_outcome_v2* out)
+{
+    auto* fake = static_cast<FakeAbiRuntime*>(context);
+    ++fake->steps;
+    fake->last_input = *input;
+    if (fake->step_result == FLY_SESSION_V2_OK)
+        *out = fake->step_outcome;
+    else
+        /* A misbehaving provider that writes on failure must not be believed. */
+        out->frame_index = 0xDEADBEEFull;
+    return fake->step_result;
+}
+
+fly_session_result_v2 abi_export(void* context, uint8_t* out, size_t capacity,
+                                size_t* out_written, uint8_t hash_out[32])
+{
+    auto* fake = static_cast<FakeAbiRuntime*>(context);
+    ++fake->exports;
+    fake->last_export_capacity = capacity;
+    const std::size_t readable = capacity < sizeof(fake->last_export_bytes)
+                                     ? capacity
+                                     : sizeof(fake->last_export_bytes);
+    std::memcpy(fake->last_export_bytes, out, readable);
+    const std::size_t written = fake->export_overclaims ? capacity + 1u : 8u;
+    for (std::size_t index = 0; index < written && index < capacity; ++index)
+        out[index] = static_cast<std::uint8_t>(0xA0u + index);
+    *out_written = written;
+    std::memcpy(hash_out, fake->last_export_hash.data(), 32u);
+    return fake->export_result;
+}
+
+fly_session_result_v2 abi_import(void* context, const uint8_t* bytes, size_t size)
+{
+    auto* fake = static_cast<FakeAbiRuntime*>(context);
+    ++fake->imports;
+    fake->last_import_size = size;
+    (void)bytes;
+    return fake->import_result;
+}
+
+fly_session_result_v2 abi_digest(void* context, uint64_t frame_index,
+                                fly_session_dual_state_digest_v2* out)
+{
+    auto* fake = static_cast<FakeAbiRuntime*>(context);
+    ++fake->digests;
+    fake->last_digest_frame = frame_index;
+    if (fake->digest_result == FLY_SESSION_V2_OK)
+        *out = fake->digest_value;
+    return fake->digest_result;
+}
+
+fly_session_dual_runtime_port_v2 make_abi_port(FakeAbiRuntime* fake) noexcept
+{
+    fly_session_dual_runtime_port_v2 port{};
+    port.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
+    port.abi_version = FLY_SESSION_ABI_VERSION_2;
+    port.context = fake;
+    port.retain = [](void*) {};
+    port.release = [](void*) {};
+    port.load = abi_load;
+    port.step = abi_step;
+    port.export_state = abi_export;
+    port.import_state = abi_import;
+    port.state_digest = abi_digest;
+    return port;
+}
+
+void the_adapter_is_a_faithful_pass_through()
+{
+    FakeAbiRuntime fake;
+    fake.last_export_hash[0] = 0x5A;
+    fake.digest_value.state[0] = 0x11;
+    fake.digest_value.frame[0] = 0x22;
+    fake.digest_value.pcm[0] = 0x33;
+    fake.step_outcome.frame_index = 77;
+    fake.step_outcome.honoured_port_mask = 0x3;
+    fake.step_outcome.applied_input_sequence[0] = 10;
+    fake.step_outcome.applied_input_sequence[1] = 11;
+
+    const auto port = make_abi_port(&fake);
+    dual::CAbiDualRuntimePortV1 adapter(&port);
+
+    dual::DualContentRefV1 content{};
+    content.session_id = id16(0x11);
+    content.branch_id = id16(0x22);
+    content.content_hash = id32(0x42);
+    content.timeline_epoch = 7;
+    check(adapter.load(content) == FLY_SESSION_V2_OK && fake.loads == 1,
+          "load is forwarded exactly once");
+    check(std::memcmp(fake.last_content.session_id, content.session_id.data(),
+                      16u) == 0 &&
+              std::memcmp(fake.last_content.content_hash,
+                          content.content_hash.data(), 32u) == 0 &&
+              fake.last_content.timeline_epoch == 7,
+          "the content reference crosses the boundary byte for byte");
+
+    const auto bundle = make_bundle(0x51u, 0, 0x55);
+    dual::DualFrameOutcomeV1 outcome{};
+    check(adapter.step(bundle, &outcome) == FLY_SESSION_V2_OK &&
+              fake.steps == 1,
+          "step is forwarded exactly once");
+    check(fake.last_input.struct_size ==
+                  FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE &&
+              fake.last_input.frame_index == bundle.key.frame_index &&
+              fake.last_input.logical_seat == bundle.logical_seat &&
+              fake.last_input.predicted_port_mask ==
+                  bundle.predicted_port_mask &&
+              fake.last_input.ports[0].mask == bundle.ports[0].mask &&
+              fake.last_input.ports[0].input_sequence ==
+                  bundle.ports[0].input_sequence,
+          "the canonical bundle crosses the boundary field for field");
+    check(outcome.frame_index == 77 && outcome.honoured_port_mask == 0x3u &&
+              outcome.applied_input_sequence[0] == 10u,
+          "the provider's outcome is copied back verbatim");
+
+    std::uint8_t state[16] = {};
+    std::size_t written = 0;
+    std::array<std::uint8_t, 32> state_hash{};
+    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
+              FLY_SESSION_V2_OK &&
+              written == 8u && state_hash[0] == 0x5Au,
+          "export_state forwards the state bytes and the provider's hash");
+    check(fake.last_export_capacity == sizeof(state),
+          "the capability offered to the provider is the caller's real one");
+
+    const std::uint8_t commit[4] = {1, 2, 3, 4};
+    check(adapter.import_state(commit, sizeof(commit)) == FLY_SESSION_V2_OK &&
+              fake.imports == 1 && fake.last_import_size == sizeof(commit),
+          "import_state forwards the exact bytes and size");
+
+    dual::DualStateDigestV1 digest{};
+    check(adapter.state_digest(41, &digest) == FLY_SESSION_V2_OK &&
+              fake.digests == 1 && fake.last_digest_frame == 41,
+          "state_digest forwards the requested frame");
+    check(digest.state[0] == 0x11u && digest.frame[0] == 0x22u &&
+              digest.pcm[0] == 0x33u,
+          "the provider's digest is copied back verbatim");
+    check(adapter.forwarded() == 5u, "every forwarded call is counted");
+}
+
+void a_failing_provider_cannot_leave_a_result_behind()
+{
+    FakeAbiRuntime fake;
+    fake.step_result = FLY_SESSION_V2_PROTOCOL_VIOLATION;
+    fake.digest_result = FLY_SESSION_V2_STALE;
+    fake.export_result = FLY_SESSION_V2_BUFFER_TOO_SMALL;
+    fake.export_overclaims = true;
+
+    const auto port = make_abi_port(&fake);
+    dual::CAbiDualRuntimePortV1 adapter(&port);
+
+    dual::DualFrameOutcomeV1 outcome{};
+    outcome.frame_index = 0xABABABABull;
+    check(adapter.step(make_bundle(0x01u, 0, 0x55), &outcome) ==
+              FLY_SESSION_V2_PROTOCOL_VIOLATION,
+          "a provider's failure code is returned unchanged");
+    check(outcome.frame_index == 0xABABABABull,
+          "a failing step cannot leave a half-written outcome behind");
+
+    dual::DualStateDigestV1 digest{};
+    digest.state[0] = 0xCD;
+    check(adapter.state_digest(3, &digest) == FLY_SESSION_V2_STALE,
+          "a provider's digest failure is returned unchanged");
+    check(digest.state[0] == 0xCDu,
+          "a failing digest call cannot leave a partial digest behind");
+
+    std::uint8_t state[8] = {};
+    std::size_t written = 99;
+    std::array<std::uint8_t, 32> state_hash{};
+    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
+              FLY_SESSION_V2_BUFFER_TOO_SMALL && written == 99u,
+          "a failing export does not report a size");
+
+    /* A provider that claims to have written more than it was offered is a
+     * contract violation, not a result the scheduler may act on. */
+    fake.export_result = FLY_SESSION_V2_OK;
+    check(adapter.export_state(state, sizeof(state), &written, &state_hash) ==
+              FLY_SESSION_V2_CONTRACT_VIOLATION,
+          "an over-claiming export is refused");
+}
+
+void a_table_without_a_call_is_unavailable()
+{
+    FakeAbiRuntime fake;
+    auto port = make_abi_port(&fake);
+    port.step = nullptr;
+    dual::CAbiDualRuntimePortV1 adapter(&port);
+
+    dual::DualFrameOutcomeV1 outcome{};
+    check(adapter.step(make_bundle(0x01u, 0, 0x55), &outcome) ==
+              FLY_SESSION_V2_UNAVAILABLE,
+          "a missing callback is unavailable rather than silently skipped");
+
+    dual::CAbiDualRuntimePortV1 null_adapter(nullptr);
+    check(null_adapter.load({}) == FLY_SESSION_V2_UNAVAILABLE,
+          "an absent provider table is unavailable");
+}
+
+} // namespace
+
+int main()
+{
+    the_canonical_unit_round_trips_through_the_frozen_record();
+    a_predicted_port_keeps_its_marker_on_the_wire();
+    the_wire_cannot_name_its_own_owner_or_seat();
+    malformed_records_are_refused_by_the_frozen_validator();
+    the_adapter_is_a_faithful_pass_through();
+    a_failing_provider_cannot_leave_a_result_behind();
+    a_table_without_a_call_is_unavailable();
+    if (failures != 0)
+    {
+        std::fprintf(stderr, "nearby_dual_runtime_seam: %d failure(s)\n",
+                     failures);
+        return 1;
+    }
+    std::puts("nearby_dual_runtime_seam: PASS");
+    return 0;
+}
diff --git a/shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp b/shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp
index 45ec4d8..bb6a386 100644
--- a/shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp
+++ b/shared/tests/nearby/scenarios/test_two_engine_dual_run.cpp
@@ -289,21 +289,27 @@ void the_two_public_engines_agree_on_their_semantic_state()
                 run.joiner_game_state);
     std::printf("  crossed: %d GATT fragments, %d QUIC units\n",
                 run.gatt_fragments_crossed, run.quic_units_crossed);
     std::printf("  object kinds persisted: inviter=%zu joiner=%zu\n",
                 run.inviter_objects.size(), run.joiner_objects.size());
 
     /* The positive path completes, on BOTH ends, independently asserted. */
     check(run.inviter_link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
               run.joiner_link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
           "BOTH public engines complete the positive path to "
-          "FLY_SESSION_LINK_CONNECTED_LOBBY_V2 over the real loopback QUIC link");
+          "FLY_SESSION_LINK_CONNECTED_LOBBY_V2 with the engines really driving "
+          "every QUIC port primitive (listen/connect, handshake inspection, "
+          "exporter, bidi streams, write, granted read) over the fixture's "
+          "in-process loopback transport. This is NOT a real-Quinn claim: the "
+          "Rust provider is exercised separately by `flynes_loopback_quic_probe` "
+          "and `flynes_quic_provider_linkage`, and `LoopbackTransport` is a "
+          "test-side transport, not the product one");
 
     /* The semantic state SEQUENCE: both ends really walked the same ordered path
      * through AUTHENTICATING before the lobby, measured mid-run rather than
      * inferred from the terminal value. */
     check(run.inviter_authenticating_seen == 1u &&
               run.joiner_authenticating_seen == 1u,
           "both engines were really measured in FLY_SESSION_LINK_AUTHENTICATING_V2 "
           "before the app confirmed the SAS, so the two link-state sequences are "
           "the same ordered path and not just the same terminal value");
 
diff --git a/shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp b/shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp
index b9cb147..28cda51 100644
--- a/shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp
+++ b/shared/tests/nearby/scenarios/test_two_engine_tamper_matrix.cpp
@@ -1,22 +1,30 @@
 /*
  * W3 / Task 8 step 2: the tamper negative matrix.
  *
  * The plan requires that, for EACH protocol layer, a tampered variant is
  * fail-closed: the two real engines must NEVER reach CONNECTED_LOBBY. This file
  * is that matrix, built on W0's `two_engine_loopback_fixture` and on the real
  * public C ABI (`fly_session_create_v2` -> `fly_session_submit_action_v2` ->
- * `fly_session_deliver_v2`), with real wire serialization and a real loopback
- * QUIC connection. Nothing here authors a class-2 provider event
- * (DISCOVERY_CONNECTION / DISCOVERY_BYTES / QUIC_DATA): those are produced only
- * by `LoopbackTransport`. Nothing here calls a peer reducer directly, and nothing
- * here injects already-"verified" evidence.
+ * `fly_session_deliver_v2`), with real wire serialization and the engines really
+ * driving every QUIC port primitive.
+ *
+ * SCOPE OF THE TRANSPORT CLAIM, stated precisely. `LoopbackTransport` is the
+ * fixture's IN-PROCESS transport, not the Rust Quinn provider: it supplies the
+ * class-2 events (DISCOVERY_CONNECTION / DISCOVERY_BYTES / QUIC_DATA) that the
+ * engines consume, so what this file proves is that the engine really exercised
+ * its QUIC port surface and rejected tampered bytes, NOT that the bytes went
+ * through Quinn. Real-Quinn evidence lives only in `flynes_loopback_quic_probe`
+ * (a real loopback handshake against the product crate) and
+ * `flynes_quic_provider_linkage` (the staticlib is really linked and called).
+ * Nothing here authors a class-2 provider event. Nothing here calls a peer
+ * reducer directly, and nothing here injects already-"verified" evidence.
  *
  * NECESSITY. Every negative case is run by the SAME driver function as the
  * control, with exactly one injection added, and the control - identical setup,
  * no tamper - must reach CONNECTED_LOBBY on BOTH ends in the same run. A
  * negative case that failed for an unrelated reason would take the control down
  * with it, so a green matrix cannot be produced by breaking the rig.
  *
  * WHERE THE TAMPER IS INJECTED, AND WHY THERE
  *   * Wire layers (HELLO, READY, role, generation, capability, binding,
  *     ChannelBind) are tampered at the lowest boundary that exists for them:
@@ -164,20 +172,32 @@ struct TamperOutcome final
     bool injection_landed = false;
     /* Harness-level assertion failures this case added. */
     int new_failures = 0;
     int app_action_kinds = 0;
     bool reached_lobby = false;
     /* Observed QUIC counts, so a case that never got to the Control stream is
        distinguishable from one that got there and was rejected. */
     int inviter_quic_writes = 0;
     int joiner_quic_writes = 0;
     std::uint32_t reassembly_mismatches = 0;
+    std::uint32_t tampered_groups_delivered = 0;
+    /*
+     * The provider's own verdicts, which is how a pair-secure record's tamper is
+     * OBSERVED to arrive: a tampered AEAD tag makes the receiving engine's own
+     * `crypto.aead_open` fail, and a tampered signature makes its `crypto.verify`
+     * fail. Without these counters "the tamper landed" only says the harness
+     * rewrote a byte - it says nothing about whether the receiver ever looked.
+     */
+    int inviter_aead_open_failures = 0;
+    int joiner_aead_open_failures = 0;
+    int inviter_verify_failures = 0;
+    int joiner_verify_failures = 0;
 };
 
 /*
  * The one driver. Every case - control included - runs exactly this, so the only
  * difference between a case and the control is the single injection in the
  * switch below.
  */
 TamperOutcome run_case(TamperLayer layer)
 {
     const int failures_before = failures;
@@ -363,33 +383,42 @@ TamperOutcome run_case(TamperLayer layer)
         case TamperLayer::PairSignature:
         case TamperLayer::KeyConfirm:
             injection_landed = transport.tampered_logical_messages() > 0;
             break;
         default:
             injection_landed = transport.tampered_units() > 0;
             break;
     }
     outcome.reassembly_mismatches =
         transport.logical_tamper_reassembly_mismatches();
+    outcome.tampered_groups_delivered =
+        transport.tampered_logical_delivered();
+    outcome.inviter_aead_open_failures =
+        inviter_pump.counts.crypto_aead_open_failures;
+    outcome.joiner_aead_open_failures =
+        joiner_pump.counts.crypto_aead_open_failures;
+    outcome.inviter_verify_failures = inviter_pump.counts.crypto_verify_failures;
+    outcome.joiner_verify_failures = joiner_pump.counts.crypto_verify_failures;
     outcome.injection_landed = injection_landed;
     outcome.new_failures = failures - failures_before;
 
     std::printf(
-        "  %-22s landed=%s inviter=%u(%s) joiner=%u(%s) quic-writes=%d/%d "
-        "app-actions=%d\n",
+        "  %-22s landed=%s crossed=%u inviter=%u(%s) joiner=%u(%s) quic=%d/%d "
+        "aead-open-fail=%d/%d verify-fail=%d/%d\n",
         layer_name(layer), injection_landed ? "yes" : "NO",
-        outcome.inviter_link_state,
+        outcome.tampered_groups_delivered, outcome.inviter_link_state,
         outcome.inviter_reason[0] != '\0' ? outcome.inviter_reason : "-",
         outcome.joiner_link_state,
         outcome.joiner_reason[0] != '\0' ? outcome.joiner_reason : "-",
         outcome.inviter_quic_writes, outcome.joiner_quic_writes,
-        outcome.app_action_kinds);
+        outcome.inviter_aead_open_failures, outcome.joiner_aead_open_failures,
+        outcome.inviter_verify_failures, outcome.joiner_verify_failures);
 
     /* Every case tears down through the pump and must destroy both engines. */
     shutdown_engine_with_the_pump(inviter, inviter_pump, limits);
     shutdown_engine_with_the_pump(joiner, joiner_pump, limits);
 
     for (auto& action : inviter_actions)
         fly_session_approval_token_release_v2(action.approval_token);
     for (auto& action : joiner_actions)
         fly_session_approval_token_release_v2(action.approval_token);
     return outcome;
@@ -412,154 +441,237 @@ void the_control_reaches_the_connected_lobby()
     check(control.reached_lobby,
           "the control's lobby result is the POSITIVE result: both engines really "
           "entered the lobby rather than merely avoiding it");
     /* The control must reach the QUIC Control stream, or the HELLO/READY/
      * ChannelBind cases would have nothing to tamper. */
     check(control.inviter_quic_writes > 0 && control.joiner_quic_writes > 0,
           "the control's engines really wrote to the QUIC link, so the Control-"
           "stream tamper cases have bytes to target");
 }
 
+bool is_previously_fail_open_pair_layer(TamperLayer layer)
+{
+    return layer == TamperLayer::CommitReveal ||
+           layer == TamperLayer::PairSignature ||
+           layer == TamperLayer::KeyConfirm;
+}
+
+bool connecting_or_failed(unsigned state)
+{
+    return state == FLY_SESSION_LINK_CONNECTING_V2 ||
+           state == FLY_SESSION_LINK_FAILED_V2;
+}
+
 /*
  * Asserts the fail-closed invariant for one tampered layer.
+ *
+ * For the three pair-secure layers that round 1 reported fail-open, the
+ * assertion is stronger than "did not reach the lobby": the tampered copy must
+ * have CROSSED, and the RECEIVER's own provider must have rejected it with
+ * aead-open-fail or verify-fail. Those extra clauses are what made the
+ * round-1 harness bug visible (landed=yes, crossed=0, aead-open-fail=0/0).
  */
 void a_tampered_layer_cannot_reach_the_lobby(TamperLayer layer)
 {
     const auto outcome = run_case(layer);
     char message[512];
+    const bool pair_layer = is_previously_fail_open_pair_layer(layer);
+
+    std::snprintf(message, sizeof(message),
+                  "the %s tamper really landed on the sender's own bytes; "
+                  "without that the case would be a second control",
+                  layer_name(layer));
+    check(outcome.injection_landed, message);
 
-    if (!outcome.injection_landed)
+    if (pair_layer)
     {
         std::snprintf(message, sizeof(message),
-                      "the %s tamper really landed on the sender's own bytes; "
-                      "without that the case would be a second control",
-                      layer_name(layer));
-        check(false, message);
-        return;
-    }
-    if (outcome.reached_lobby)
-    {
+                      "a tampered %s actually CROSSED (tampered_logical_delivered "
+                      ">= 1, observed %u); landed-without-crossed is the round-1 "
+                      "harness fail-open",
+                      layer_name(layer), outcome.tampered_groups_delivered);
+        check(outcome.tampered_groups_delivered >= 1u, message);
+
+        const bool receiver_crypto =
+            outcome.inviter_aead_open_failures > 0 ||
+            outcome.joiner_aead_open_failures > 0 ||
+            outcome.inviter_verify_failures > 0 ||
+            outcome.joiner_verify_failures > 0;
         std::snprintf(message, sizeof(message),
-                      "a tampered %s NEVER reaches "
-                      "FLY_SESSION_LINK_CONNECTED_LOBBY_V2",
-                      layer_name(layer));
-        check(false, message);
-        return;
+                      "a tampered %s is rejected by the RECEIVER's own provider "
+                      "(aead-open-fail=%d/%d verify-fail=%d/%d)",
+                      layer_name(layer), outcome.inviter_aead_open_failures,
+                      outcome.joiner_aead_open_failures,
+                      outcome.inviter_verify_failures,
+                      outcome.joiner_verify_failures);
+        check(receiver_crypto, message);
     }
+
+    std::snprintf(message, sizeof(message),
+                  "a tampered %s NEVER reaches "
+                  "FLY_SESSION_LINK_CONNECTED_LOBBY_V2 (inviter=%u joiner=%u)",
+                  layer_name(layer), outcome.inviter_link_state,
+                  outcome.joiner_link_state);
+    check(!outcome.reached_lobby &&
+              outcome.inviter_link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
+              outcome.joiner_link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
+          message);
+
     /*
      * A tamper case is asymmetric BY DESIGN: the end that receives the tampered
      * record detects it and fails, while the end that sent it never learns and
      * stays where it was. So the invariant is not "the two states agree" - it is:
      *   * neither end is in the lobby (asserted above),
      *   * every end is in one of the link states the ABI documents, and
-     *   * AT LEAST ONE end explicitly REFUSED - it reached
-     *     FLY_SESSION_LINK_FAILED_V2 rather than merely stalling.
-     * That last clause is what separates a real fail-closed result from a rig
-     * that simply stopped making progress: a mutual CONNECTING stall would prove
-     * nothing about the layer under test.
+     *   * AT LEAST ONE end is in the named refuse/wait states CONNECTING or
+     *     FAILED rather than an unspecified one. Pair-secure tampers leave the
+     *     sender in AUTHENTICATING because it never observes the peer's AEAD
+     *     rejection; the RECEIVER is FAILED.
      */
     const auto documented = [](unsigned state) {
         return state == FLY_SESSION_LINK_UNAVAILABLE_V2 ||
                state == FLY_SESSION_LINK_IDLE_V2 ||
                state == FLY_SESSION_LINK_DISCOVERING_V2 ||
                state == FLY_SESSION_LINK_INVITING_V2 ||
                state == FLY_SESSION_LINK_JOINING_V2 ||
                state == FLY_SESSION_LINK_AUTHENTICATING_V2 ||
                state == FLY_SESSION_LINK_PROVISIONING_V2 ||
                state == FLY_SESSION_LINK_CONNECTING_V2 ||
                state == FLY_SESSION_LINK_FAILED_V2;
     };
     std::snprintf(message, sizeof(message),
                   "a tampered %s leaves BOTH engines in a link state the ABI "
                   "documents (inviter=%u joiner=%u)",
                   layer_name(layer), outcome.inviter_link_state,
                   outcome.joiner_link_state);
     check(documented(outcome.inviter_link_state) &&
               documented(outcome.joiner_link_state),
           message);
+    std::snprintf(message, sizeof(message),
+                  "a tampered %s leaves a NAMED state CONNECTING (%u) or "
+                  "FAILED (%u) on at least one end (inviter=%u joiner=%u)",
+                  layer_name(layer),
+                  static_cast<unsigned>(FLY_SESSION_LINK_CONNECTING_V2),
+                  static_cast<unsigned>(FLY_SESSION_LINK_FAILED_V2),
+                  outcome.inviter_link_state, outcome.joiner_link_state);
+    check(connecting_or_failed(outcome.inviter_link_state) ||
+              connecting_or_failed(outcome.joiner_link_state),
+          message);
     std::snprintf(message, sizeof(message),
                   "a tampered %s is explicitly REFUSED, not merely stalled: at "
                   "least one end reaches FLY_SESSION_LINK_FAILED_V2 (%u) "
                   "(inviter=%u joiner=%u)",
                   layer_name(layer),
                   static_cast<unsigned>(FLY_SESSION_LINK_FAILED_V2),
                   outcome.inviter_link_state, outcome.joiner_link_state);
     check(outcome.inviter_link_state == FLY_SESSION_LINK_FAILED_V2 ||
               outcome.joiner_link_state == FLY_SESSION_LINK_FAILED_V2,
           message);
 }
 
 /*
- * ---------------------------------------------------------------------------
- * OPEN FINDING: three layers where the injection LANDS but the run still
- * completes. Reported, deliberately NOT asserted as fail-closed.
+ * RESOLVED FINDING (was an OPEN FINDING in round 1): commit/reveal, pair
+ * signature and key-confirm were reported as fail-open. They are fail-closed,
+ * and the round-1 result was a HARNESS bug, not an engine defect.
+ *
+ * The record layout is what made it look like an engine problem. A pair-secure
+ * record (GATT logical types 6, 7, 17, 21, 23-26) is an ENVELOPE, not a bare
+ * inner structure (`wire/pair_secure.cpp:804-839`):
+ *
+ *   body[0]      = version, must be 1
+ *   body[1..3]   = reserved, must be zero
+ *   body[4..11]  = message_counter, u64be, must be non-zero
+ *   body[12..]   = ciphertext || 16-byte AEAD tag
+ *   body_size    = pair_secure_inner_size_v1(type) + 28
  *
- * commit/reveal, pair signature and key-confirm are tampered on the pre-QUIC
- * GATT relay, on the reassembled logical message's body, with the record's own
- * trailing integrity hash recomputed. The injection is confirmed to have landed
- * (`tampered_logical_messages() > 0`), and yet BOTH engines still reach
- * CONNECTED_LOBBY in every one of the three cases.
+ * so the AEAD tag is the LAST 16 BYTES of the body and the tamper does land on
+ * it. The real bug was WHERE THE TAMPER WAS APPLIED: it was written into a
+ * per-attempt local copy of the fragment group, and `relay_fragment_once` can
+ * return BACKPRESSURE, which abandons the attempt and re-offers the fragment on
+ * a later round - from the sender originals. The tampered copy was therefore
+ * thrown away and the untouched bytes crossed, which is why the provider
+ * counters showed `aead-open-failures = 0` on BOTH ends while the harness still
+ * reported the injection as landed.
  *
- * That is an honest negative result and it has two possible causes, neither of
- * which this worktree could settle inside its budget:
- *   (a) the chosen byte offsets are wrong - `PairCommit` body 48 is read from the
- *       record's own decoder as the commitment, but `PairSignature` and
- *       `KeyConfirm` are tampered at their LAST body byte, which is a guess at
- *       where the 64-byte signature / 16-byte AEAD tag ends; or
- *   (b) the receiving engine does not in fact reject these on the live path -
- *       which would be a real security defect and must not be reported as a
- *       passing negative case.
- * The relay also reported once, for the signature case, that a reassembled
- * logical message's own record length did not read back, so part (a) is likely:
- * the fragment-to-logical reassembly used for the tamper is not right for every
- * group.
+ * The relay now records the replacement PERSISTENTLY in the direction's state
+ * (`RelayDirectionState::remember_replacement`), so a retry re-offers the same
+ * tampered bytes. Measured after the fix, for each of the three layers:
  *
- * Until that is settled these three layers are NOT covered by the matrix, and
- * the acceptance gate `E2E-SCENARIOS` is therefore NOT fully green for them.
- * The diagnostic below asserts only what was really observed.
+ *   tampered records that CROSSED = 1
+ *   aead-open-failures joiner    = 1   <- the receiving engine's OWN provider
+ *                                         rejected the record's AEAD tag
+ *   verify-failures             = 0
+ *   inviter link state          = 5 (AUTHENTICATING)
+ *   joiner  link state          = 9 (FAILED)   <- explicit refusal
+ *   reached_lobby               = no
+ *
+ * All three are therefore enforced by `a_tampered_layer_cannot_reach_the_lobby`
+ * together with the other layers. The diagnostic counters that established this
+ * are printed for EVERY case by `run_case`, so the evidence is in the output
+ * rather than in a comment.
  */
-void report_the_three_unsettled_pair_layers()
-{
-    const TamperLayer layers[] = {TamperLayer::CommitReveal,
-                                  TamperLayer::PairSignature,
-                                  TamperLayer::KeyConfirm};
-    for (const auto layer : layers)
-    {
-        const auto outcome = run_case(layer);
-        std::printf("  UNSETTLED %-16s landed=%s reached_lobby=%s "
-                    "(inviter=%u joiner=%u) reassembly-mismatches=%u\n",
-                    layer_name(layer), outcome.injection_landed ? "yes" : "NO",
-                    outcome.reached_lobby ? "YES - FAIL-OPEN" : "no",
-                    outcome.inviter_link_state, outcome.joiner_link_state,
-                    outcome.reassembly_mismatches);
-    }
-}
 
 /*
  * The SAS layer, reported as NOT IMPLEMENTABLE rather than faked.
  *
  * The plan asks for a tampered SAS. The tamper surface for SAS is
  * `PairPipeline::approve_local(approval_kind, displayed_sas)`
  * (pair_pipeline.cpp:159-169), which rejects when the code the APP presented is
  * not the code the transcript derives. The live engine never reaches it:
  * `session_engine.cpp:5598` calls the ONE-argument
  * `approve_local(approval_kind)`, and no public action or choice kind carries
  * SAS bytes - the descriptor has no choice payload at all
  * (`fly_session_action_descriptor_v2`) and `confirm_pairing_sas_when_the_abi_asks`
  * submits the confirmation with `choice_size == 0`. There is therefore no way for
  * a test to present a wrong SAS without either calling the peer's reducer
  * directly or synthesizing a record the real sender never produces - both
  * forbidden by this harness's contract.
  *
  * This function asserts the OBSERVED shape instead of pretending, so the moment
  * the engine grows an app-supplied SAS the assertions below start failing and
  * the case has to be implemented for real.
+ *
+ * DIAGNOSIS (W3 round 2), with the whole chain read end to end:
+ *   1. The live engine calls `PairSignatureScheduler::approve_local(kind)` at
+ *      `session_engine.cpp:5598`. That overload does NOT compare SAS bytes; it
+ *      only forwards the approval kind to verification.
+ *   2. Two-arg `PairPipeline::approve_local(kind, displayed_sas)` (the only
+ *      path that compares `displayed_sas != *expected` at
+ *      `pair_pipeline.cpp:164`) is unused by the engine. `PairPipeline` is
+ *      never instantiated in `session_engine.cpp`.
+ *   3. Single-arg `PairPipeline::approve_local(kind)` reject-path is
+ *      `entry_mode==1 && !known_path` (`pair_pipeline.cpp:174`). That is the
+ *      BLE-SAS anonymous path, and even that overload is not on the live
+ *      confirm branch.
+ *   4. The public action has no SAS field (IF05). `fly_session_action_choice_v2`
+ *      is boolean / invite-code / reference only; `CONFIRM_SAS` is submitted
+ *      with `choice_size == 0`. The confirm guard requires
+ *      `pending.choice_size == 0`, so a choice payload is REJECTED before
+ *      apply. Do not "fix" this by adding SAS bytes to the public action.
+ *   5. The live path is `PairSignatureScheduler::approve_local` ->
+ *      `PairVerificationScheduler::approve_local` ->
+ *      `PairAuthenticationReducer::approve_local`.
+ *
+ * CONCLUSION: NOT EXPLOITABLE through the public ABI, and NOT a "confirms a SAS
+ * it was never shown" defect. The engine derives the SAS itself from the
+ * pair-signature-bound transcript and publishes it for display
+ * (`session_engine.cpp:330` reads `pair_sas_->sas()`); the confirmation is the
+ * human's assertion that the two displayed codes match, which is what SAS-based
+ * pairing is. There is no input that can make an end confirm a value of the
+ * attacker's choosing.
+ *
+ * RESIDUAL OBSERVATION for the designers, not a harness limitation:
+ * `PairPipeline` is entirely dead code, so if the design INTENDS the app's
+ * confirmation to be cryptographically bound to the displayed SAS, that binding
+ * does not exist in the shipped engine - the binding is the human comparison
+ * alone. Whether that is a defect is a design question, and it is recorded here
+ * rather than resolved by a test.
  */
 void sas_has_no_injection_surface_in_the_live_engine()
 {
     std::printf("tamper matrix: SAS layer (no injection surface - asserted, not faked)\n");
     LoopbackWorld world;
     EngineFixture inviter(world, LoopbackSide::Initiator);
     EngineFixture joiner(world, LoopbackSide::Responder);
     inviter.platform.ready();
     joiner.platform.ready();
     inviter.executor.run_all();
@@ -647,24 +759,25 @@ int main()
 {
     std::setvbuf(stdout, nullptr, _IONBF, 0);
     the_control_reaches_the_connected_lobby();
 
     const TamperLayer layers[] = {
         TamperLayer::Role,       TamperLayer::Generation,
         TamperLayer::Capability, TamperLayer::Binding,
         TamperLayer::Hello,      TamperLayer::Ready,
         TamperLayer::ChannelBind, TamperLayer::Credential,
         TamperLayer::TlsPin,     TamperLayer::Exporter,
+        TamperLayer::CommitReveal, TamperLayer::PairSignature,
+        TamperLayer::KeyConfirm,
     };
     for (const auto layer : layers) a_tampered_layer_cannot_reach_the_lobby(layer);
 
-    report_the_three_unsettled_pair_layers();
     sas_has_no_injection_surface_in_the_live_engine();
 
     if (flynes::session::loopback::failures != 0)
     {
         std::fprintf(stderr, "%d failure(s)\n", flynes::session::loopback::failures);
         return 1;
     }
     std::puts("flynes_two_engine_tamper_matrix passed");
     return 0;
 }
