# Nearby 可玩首版纠偏执行计划

> For agentic workers: 使用 executing-plans 逐任务执行。并行由用户自行启动，本文件不授权自动启动子代理。每项先写失败断言、实现最小修复，再运行相关回归。

**Goal:** 恢复原稿主线，并让两个原生应用实例用真实游戏核心完成一局受限 DUAL；复杂功能后置。

**Architecture:** 保留现有 V2 engine、协议和输入调度，补齐双确认、真实 runtime/network、原生 owner。平台仅适配和展示，不另造协议/连接状态。

**Tech Stack:** C++17 / NestopiaUE / Quinn-rustls / JNI-Java / ArkUI-NAPI / SwiftUI-ObjC++。

**最新补充（字体与交互复核）：** 先看[三端字体审计](../../audits/2026-09-17-nearby-typography-and-function-audit.md)与[字体纠偏/功能续作计划](2026-09-17-nearby-typography-correction-and-completion-plan.md)。鸿蒙、Android、iOS均有文字角色/缩放偏差；补充TYPO/REG卡与本计划UX/ENG卡一起执行，不将旧截图或ENG-01基线提交视为当前UX/可玩验收通过。

依据：[审计](../../audits/2026-09-17-nearby-ux-course-correction-audit.md)、[纠偏方案](../specs/2026-09-17-nearby-playable-mvp-correction-design.md)。原 U01–U11/C01–C18 仍是完整目标；下文仅改变交付顺序和首版能力集，不宣告完整设计通过。

**执行入口已细化：** [UX 逐页任务卡 UX-00–18](2026-09-17-nearby-ux-restoration-task-cards.md)和[可玩链路任务卡 ENG-01–12](2026-09-17-nearby-playable-engine-task-cards.md)。本文件保留总览；实际委派使用具体卡号及平台后缀，不再将整个 T5/T7 一次扔给执行者。细化卡明确收紧了固定主机/座位和好友保存的临时限制，按 M0/M1/M2 分别验收。

## 0. 开工规则与依赖

产品集成基点是 `.worktrees/nearby-ui-acceptance-fixes`（W0），不是 main。本文路径以各执行 worktree 根为基准；本批三份文档保存在 main 的 docs 方便用户统一查阅，产品改动不落 main。

```text
T0 固定现状/接收已有工作
  → T1 UI/快照合同
    → T2 配置/双确认 ─┐
    → T3 真实 runtime ├→ T5 Android 纵向闭环 → T6 首个可玩验收
    → T4 真实传输 ───┘                         → T7 Harmony/iOS 推广
    → 三端纯布局可并行（接口冻结后再接线）       → T8 三端收口
T9 后续复杂能力（不阻塞 T6）
```

用户启动独立 worktree 时只用 `tools/versioning/New-VersionedWorktree.ps1`。完成 T0 后记录共同基线完整 SHA；不从缺少 W0 dirty 实现的旧 HEAD 直接分支。W1/W2 已合入，不重新派原任务；W3 只接收未集成增量。

同一时刻只由集成人修改 `shared/include/flynes/flynes_session.h`、`shared/src/session/engine/*`、`shared/schema/*`、`shared/CMakeLists.txt`。各 worker 交付组件和测试；集成人接线。版本由仓库 hook 管理，不手改平台版本、不改 major。

## T0：固定基线和恢复可信进度（集成人，先做）

**文件：** W0 `docs/acceptance/2026-09-15-nearby-dual-nondevice-acceptance.md`；新建 `docs/acceptance/2026-09-17-nearby-playable-progress.md`；保留 W0/W3 dirty/untracked 全部内容。

- [ ] 记录 `git worktree list --porcelain`、各 `git status --short` 和 `git rev-parse HEAD`；证据保存 `out/evidence/nearby-playable/`。
- [ ] 单列 W0 未提交 DUAL/content/recovery 与三端 UI 改动的所有者和内容，逐项审阅，不整树覆盖或提交无关变动。
- [ ] 将旧验收表拆成 L1/L2/L3/L4；消除同文 PASS/NOT_RUN 冲突。历史记录保留日期，当前源树无对应运行时标“未复验”。
- [ ] 审阅 W3 `5e8e73f` 及未合入场景增量，接收测试修复；不重复合并已经存在的 W0 fixture。
- [ ] 按首版范围隔离 content/recovery 的未完成入口，确保编译不依赖缺失未跟踪文件；安全门禁和断线冻结保留。
- [ ] 相关 host 测试后生成可复现基线提交；提交前确认暂存仅该任务与 hook 要求的版本文件，不吸收他人产品改动。

**验收 BASE：** 同一 SHA 新 worktree 能构建；清单能区分已提交/dirty/后置；W3 纳入项逐条有结果；不能有“0 tests matched = PASS”。基线未固定前其他任务只做独立分析/布局，不做重复引擎接线。

## T1：冻结原稿到页面/快照映射（集成人 + UX）

**修改：** `shared/schema/nearby_ui_v1.json`、`shared/include/flynes/product/nearby_ui_state.hpp`、`shared/src/product/nearby_ui_state.cpp`、`shared/tests/fixtures/nearby_ui_v1/cases.json`、`shared/tests/test_product_nearby_ui.cpp`。

- [ ] 将 N00–N10/G00 的标题、首屏内容、主动作、返回目标、错误位置逐项对照原稿；N11/N12 记录首版不可用功能与文案。
- [ ] 定义平台唯一消费的真实 session snapshot → screen/action 投影，禁止 N00 常量函数代替真实页面断言。
- [ ] 增加 N00 不显示阶段表/重复扫码入口，N09 详情默认收起，缺 ROM 保留连接，STREAM 不可用的失败用例。
- [ ] 定义全部产品状态：未连接、等待接受、待 SAS、连接中、已连接、待双方确认、运行、暂停、冻结、结束；不伪造已保存好友或已验证身份。
- [ ] 记录 M0 默认角色样本、QR 后置等能力边界；M1 恢复原稿开局前主机/座位选择，禁止把 M0 限制变成永久只读改版。

**验收 UX-CONTRACT：** C01–C04/C09/C11/C13/C14 对应清晰状态和动作；原稿左右顺序、按钮文案、详情层级可被原生测试读取；所有省略功能有理由和后续任务，未被标完成。

## T2：真实配置、双方确认及基本局生命周期（集成人）

**修改：** `shared/src/session/engine/session_engine.cpp`、`shared/src/session/dual/dual_session_controller.{hpp,cpp}`、`shared/src/session/view/session_view.cpp`；按需由集成人修改公开头/wire/schema。
**新测试：** `shared/tests/nearby/integration/test_game_config_consent.cpp`、`test_game_lifecycle.cpp`。

- [ ] 先复现：仅一端选择并 START 不得使任何一端 GAME_RUNNING；仅 SELECT 不得设置双方确认。
- [ ] 用完整 pending config fingerprint 绑定 ROM/core/profile/options/seat/authority/mode/epoch/revision，两端交换并显示相同短码。
- [ ] 接入每端一次确认、另一端待确认、配置变更全部撤销、旧 token/迟到事件拒绝；不由测试 pump 自动替用户确认后省略测试。
- [ ] 两端 runtime ready 后协同进入同一帧；ROM 不同、profile 未验证、缺端口、单边确认都无法启动。
- [ ] 加入健康连接的 PAUSE/RESUME 与结束本局/RETURN_TO_LOBBY；结束清除游戏授权和输入队列，保留朋友连接；下一局使用新 epoch。
- [ ] 断链冻结，不发布不可用的重连恢复/迁移 action；有明确终止动作可以离开冻结态。

**验收 GAME-GATE：**

| 输入 | 必须断言 |
|---|---|
| A 确认，B 未确认 | 两端不步进、A 显示等待，B 可确认 |
| B 在确认前更换 ROM/配置 | A 旧确认立即失效；旧 token 不能启动 |
| 两端配置不同/未知 profile | 不进入 RUNNING，有具体原因 |
| 双端确认 + runtime ready | 同一配置、同一起点、只有各自 seat 的输入生效 |
| 暂停→等待输入→继续 | 暂停不步进、恢复同帧，无按钮残留按下 |
| 结束→大厅→换游戏 | 连接保留、过滤偏好不变、必须重新确认 |
| 旧局包/重复终端事件 | 不改变新局，不重复启动/释放 |

## T3：真 NES runtime 接入（独立 runtime worker）

**读取/复用：** `shared/include/flynes/flynes_runtime.h`、`shared/src/runtime/flynes_runtime.cpp`、`shared/src/session/dual/dual_runtime_contract.hpp`、`dual_runtime_adapter.*`。
**新建：** `shared/src/session/dual/nes_dual_runtime.{hpp,cpp}`、`shared/tests/nearby/integration/test_dual_real_runtime.cpp`。

- [ ] 先写使用真 core 的双实例测试，缺 adapter 时失败；与 fake runtime 测试分目标保留。
- [ ] 实现 load/step/export/import/digest；复用现有核心和状态格式，固定相同运行选项；明确状态保存容量错误处理。
- [ ] 提供真实帧和 PCM 给现有本地 renderer/audio，保证 scheduler 唯一步进，原单人线程不能竞争同一 runtime。
- [ ] 用合法且 profile 可验证的双人样本分别输入 P1/P2，证明两路独立且共同影响真实游戏状态；改一端输入轨迹要使预期状态变化。
- [ ] 做 600 帧确定性烟测，再持续至少 10 分钟；状态/帧摘要按同一模拟帧比较，音频比较固定核心配置输出而不是设备播放时刻。

**验收 REAL-CORE：** 日志包含 runtime=NestopiaUE/版本、合法样本标识/哈希（不附私有 ROM）、两端帧号与输入轨迹；无 fake `DualRuntime`；保存/恢复后相同输入产生相同结果；10 分钟无失步、队列不越界。

## T4：真实传输与 engine 集成（独立 transport worker）

**复用：** `shared/nearby-quic-provider/`、`shared/src/session/ports/`、`shared/tests/nearby/harness/`。
**新建：** `shared/tests/nearby/integration/test_two_engine_real_quic.cpp`。

- [ ] 先写两个 engine 通过产品 Quinn provider 连接的失败测试；明示发现是否使用测试适配，不能整体标为真无线。
- [ ] 接入 listen/connect/pin/exporter/bind/control/state-commit/read-credit/close 的真实 provider 生命周期。
- [ ] 在 configure 后断言 Rust provider ON、cargo 路径有效、测试目标存在；不能 find_program 失败后静默跳过。
- [ ] 合并 T2/T3 后使用真实运行时传输输入；覆盖分片/延迟/断流/篡改、取消后回调、资源退出。
- [ ] 对 W3 pair 篡改矩阵重新跑，确认字节确实跨到接收端并由接收验证拒绝，不能在发送前丢弃当作通过。

**验收 REAL-NET：** 两个真实 engine + 真 Quinn + 真 core 同时运行；不得用进程内 LoopbackTransport 承担被测网络；加密绑定不回退，单边 READY/确认不能开局；断流冻结且可结束。crate 自测单独列，不能替代此 gate。

## T5：Android 原稿恢复和产品纵向闭环（Android worker）

**修改：** `app/src/main/java/com/flynes/emu/NearbyFriendsActivity.java`、`NearbyPairingActivity.java`、`NearbyLobbyActivity.java`、`HomeActivity.java`；`app/src/main/res/layout/activity_nearby_*.xml`；`app/src/main/cpp/flynes_app_jni.cpp`。
**新建：** `app/src/main/java/com/flynes/emu/NearbySessionOwner.java`、`app/src/main/cpp/nearby/session_owner.{hpp,cpp}`；在现有 Android native CMake 注册。
**测试：** `app/src/androidTest/java/com/flynes/emu/ui/NearbyUiParityTest.java`、`NearbyPairingTest.java`、`NearbyLobbyTest.java`；新建 `NearbyPlayableFlowTest.java`。

- [ ] UI 先以明确 fixture 检查 N00 原稿布局、N09 首屏/详情/固定底栏；在双方确认页测试必须等待真实动作结果。
- [ ] 创建进程/session 生命周期 owner，提供真实平台端口并通过 JNI 调用 V2；替换 `verify_nearby_v2_composition_contract()` 式的空装配。
- [ ] 邀请来自 shared；创建、重生成、取消、60 秒过期、房主接受/拒绝、SAS 双确认逐页接线。
- [ ] N08 回原大厅选游戏，从真实 catalog/profile 产生 choice；N09 消费 T2 的配置与确认；不直接跳 START_DUAL。
- [ ] 输入、画面、声音、暂停继续和结束接 T3；返回/旋转不重建朋友连接、不伪造已连接布尔值。
- [ ] 缺 ROM/未支持 profile/权限拒绝/断线显示可执行动作；QR 未交付时保留原入口与明确原因。

**验收 ANDROID-PLAY：** 两个 app 实例从原界面连接并玩至少 10 分钟，两人输入各有实际效果，可暂停继续、结束换局；截图/动作记录包含 N00/N01/N02/N04/N05/N06/N07/N08/G00/N09/游戏/失败。发现替身若存在必须单列，不能称无线验收。

## T6：首个可玩版本收口（集成人 + 验收者）

- [ ] T2–T5 gate 全部满足后，记录首平台可玩候选版本和已知限制；未达 L3 不用“可玩版完成”。
- [ ] 正向至少两局不同开局、正常暂停继续、返回大厅换游戏；负向单边确认、ROM 不同、失步/断流、拒绝权限、迟到事件。
- [ ] 新鲜构建 + 全量相关回归，证据全部来自本次相同 worktree；既有 unrelated FAIL 单列，不能写全量 0 failure。
- [ ] 恢复单人回归：原大厅、导入、播放、声音、暂停/存档、四分类/搜索不退化。
- [ ] 到此停止增加首版功能。若已有受限可玩候选，先交付该结果，不以完成复杂恢复为由持续延期。

**验收 FIRST-PLAYABLE：** REAL-CORE、REAL-NET、GAME-GATE、ANDROID-PLAY 全通过；剩余真机项 NOT_RUN；功能限制在界面和验收记录一致。

## T7：Harmony/iOS 同一体验推广（可各自并行）

**Harmony 文件：** `harmony/entry/src/main/ets/pages/NearbyFriends.ets`、`NearbyPairing.ets`、`NearbyLobby.ets`、`GameCenter.ets`；`service/NearbyService.ets`；`harmony/entry/src/main/cpp/napi_init.cpp`、`native_play_runtime.cpp`。
**iOS 文件：** `ios/app/NearbyFriendsView.swift`、`NearbyPairingView.swift`、`NearbyLobbyView.swift`、`CatalogLibraryView.swift`；`ios/app/bridge/FlyNesAppBridge.mm`、`FlyNesRuntimeBridge.mm`。

- [ ] 从 T6 的冻结 ABI/shared 行为接各平台 owner 与真实端口，不独立设计 wire/状态。
- [ ] iOS 移除 N00 阶段流水线/额外配对入口；三端 N09 收起技术详情、保留主操作；按原稿做布局。
- [ ] 分别完成与 T5 相同的配对/配置/运行/结束路径；系统权限行为可不同，应用内步骤和文案相同。
- [ ] Harmony host + HAP/ohosTest + Hypium；iOS 新鲜产品/test bundle + runtime/UI simulator suite。

**验收 PLATFORM-PLAY：** 各平台独立 L3 证据；支持的非真机跨端组合另列结果，未能运行的组合不能借同平台通过代签。物理 radio、硬件安全存储、跨端真机仍 NOT_RUN。

## T8：原稿一致性和三端回归收口

**工具：** `python tools/quality/check_nearby_ui_contract.py .`、`python tools/quality/check_nearby_ui_parity.py out/evidence/nearby-ux-restoration/native`；已按当前脚本源码确认位置参数，不使用脚本未实现的 `--help`。

- [ ] 原生采集宽度 320/375/550/580/640/736/900/1024；横屏 640×360/736×414/844×390；字体 1.0/1.3/2.0；输入页键盘与安全区。
- [ ] 用 N00–N10/G00 的真实共享状态 fixture 比较三端：相同文字、动作次序、禁用原因；≤2 逻辑单位几何差；主操作可见/可达。
- [ ] 四分类×搜索×双人过滤×连接切换验证 C01–C03/C13；无结果留原分类，不意外重置偏好。
- [ ] C05–C11/C14/C16 按首版已交付路径跑；C08(QR)、C12(传输)、C15(完整好友)标后置而不是 PASS。
- [ ] 汇总 L1/L2/L3 各自证据与当前源码指纹，单列全部 L4 NOT_RUN。

**验收 THREE-PLATFORM-CANDIDATE：** T7 两端和首平台均通过；已交付页面矩阵无结构/动作不一致；不能仅凭字符串检查或 HTML 边界检查通过。

## T9：复杂功能后续任务池

| 顺序 | 独立交付 | 验收边界 |
|---|---|---|
| P1 | QR 相机、签名邀请、单次消费全链 | C08 篡改/过期/重放拒绝，房主仍接受，不多加 SAS |
| M1 必须完成 | 已认证好友基本保存、开局前主机选择/换座 | 持久化成功才显示已保存；C11 变更撤销双确认，角色不混同 network owner |
| P1 | 好友改名/删除/拉黑/身份重置 | C15 权限独立，附近广播不能冒认好友 |
| P2 | 内容双许可、可靠传输、校验、导入 | C12 各端许可独立，拒绝/取消不传，不绕过导入 |
| P2 | 重连恢复、digest/activation fence、durable ledger | REC 各族分别有崩溃/重放/水位一致性证据 |
| P3 | authority 迁移/接管、存档与分支恢复 | 不静默迁移、不自动合并，端到端故障验收 |
| 用户重新排期 | STREAM | 保留扩展接口，当前能力始终关闭 |
| 用户恢复真机安排 | 六种设备组合与角色轮换 | 物理网络/权限/延迟/温度等单独认证 |

## 运行命令和证据要求

命令在执行者 worktree 运行；先 `git rev-parse --show-toplevel` 输出绝对路径。Android 同一 worktree 不并发跑多个 Gradle。平台工具chain细节按 `docs/DEVELOPMENT.md`。

共享层建议在 WSL 的执行 worktree 中运行以下命令（这是后续执行命令，本次文档审计未运行）：

```bash
set -euo pipefail
git rev-parse --show-toplevel
test -x /home/pippin/.cargo/bin/cargo
cmake -S shared -B out/nearby-playable/shared-linux \
  -DFLYNES_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug \
  -DFLYNES_ENABLE_RUST_QUIC_PROVIDER=ON \
  -DFLYNES_CARGO_EXECUTABLE=/home/pippin/.cargo/bin/cargo
rg -q '^FLYNES_ENABLE_RUST_QUIC_PROVIDER:BOOL=ON$' out/nearby-playable/shared-linux/CMakeCache.txt
cmake --build out/nearby-playable/shared-linux --parallel 2
ctest --test-dir out/nearby-playable/shared-linux --output-on-failure --no-tests=error
```

平台回归命令：

```powershell

# Android：显式指定可用模拟器 serial，再运行 instrumentation
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
.\gradlew.bat :app:connectedDebugAndroidTest

# Harmony host
cmake -S harmony/tests -B out/nearby-playable/harmony-host -G "Visual Studio 17 2022" -A x64 -DFLYNES_BUILD_TESTS=ON
cmake --build out/nearby-playable/harmony-host --config Debug --parallel 2
ctest --test-dir out/nearby-playable/harmony-host -C Debug --output-on-failure
```

不要复制另一个平台的 CMake 缓存。Linux/Windows 各自记录，不互认；Linux 若仍遇现有 PCM/ZIP fixture 失败，保留失败与原因，不能删测试让全量变绿。iOS 在 Mac 先 build 新 bundle，再运行 `ios/scripts/run_simulator_tests.py`，不能只 `test-without-building` 复用旧产物。

每个 gate 的交付记录包含：完整 SHA + dirty 指纹、绝对路径、命令、退出码、用例数、provider 开关、runtime 真/假、运行设备/模拟器、截图/日志路径、已知失败。未执行写 NOT_RUN，缺环境写 BLOCKED；物理证据延期不阻塞文档和非真机实现。

套餐核心窗口余量低于 25% 立即停止当前及本线程待执行任务，并记录断点；不自动兑换重置信用。用户自行启动的其他会话需在其任务提示中携带同一限制。
