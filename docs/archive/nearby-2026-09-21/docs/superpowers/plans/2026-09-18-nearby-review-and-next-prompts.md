> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby 最新复审与继续执行交接

> 后续增量复审：CP0 声明尺寸/stride 回归与 CP1 目录解析测试已有进展，当前进入 CP2 两条失败断言。继续执行请以 [CP2 最新交接](2026-09-18-nearby-cp2-continuation-prompt.md) 为起点；本文保留历史发现及 CP3/CP4 范围，不要重复派发已通过的越界修复。

> **For agentic workers:** 使用 executing-plans 按检查点逐项执行。用户自行启动并行会话；本文件不授权自动启动子代理。已有 ENG/UX 卡继续有效，本文更正最新状态、增加 ABI 阻断项并给出可复制 prompt。

**Goal:** 先修复已复现的目录 ABI 兼容缺陷，随后交付非串流、双端本地 NES 运行的首平台可玩闭环。

**Architecture:** 保留现有 shared V2 engine、NestopiaUE、Quinn 和原生 UX；配置、确认、运行状态只由共享 session 决定。平台适配不另造状态机，不重写加密或传输栈。

**Tech Stack:** C++17 / C ABI、Rust Quinn、Android Java/JNI、ArkTS/Hypium、SwiftUI/ObjC++。

---

## 1. 审核范围与基线

日期：2026-09-18。本次复审集中于上一轮阻断项、目录 ABI、确认开局链路及字体测试，不是三端完整原生验收。

- 仓库根目录 R：`E:/workspace/codes/games/fly-little-games`，main HEAD `5389585f8e3fa7ddc3e3dcd125d9b0a299c6c79c`。
- 产品集成目录 W0：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`，HEAD `6ec6605de0f5275295fc08b5905fed3d329f37a4`，大量 tracked/untracked 改动尚未提交。
- W1/W2/W3 HEAD 仍分别是 `66c048e` / `f0761db` / `5e8e73f`。不能把其他 worktree 的组件证据计作 W0 产品通过。
- 本次未修改产品代码；新增本文，以及 W0 ignored `out/evidence/nearby-review-2026-09-18/game_choice_abi_probe.cpp` 独立诊断程序。
- 真机未执行，未安装包，未读取签名秘密；Android instrumentation 和 iOS XCTest 本轮未重跑。
- 用户已撤销 25% 停止线。旧任务卡中的 25% 条款在本交接中失效，不恢复早期阈值，不消费重置信用。

权威输入（路径以 R 为根，seams 和进度以 W0 为根）：

1. `AGENTS.md`、`docs/DEVELOPMENT.md`。
2. `docs/superpowers/specs/assets/nearby-ui-parity-review.html`。
3. `docs/superpowers/specs/2026-09-13-nearby-ui-parity-design.md`。
4. `docs/superpowers/plans/2026-09-17-nearby-playable-engine-task-cards.md`。
5. `docs/superpowers/plans/2026-09-17-nearby-ux-restoration-task-cards.md`。
6. `docs/superpowers/plans/2026-09-17-nearby-typography-correction-and-completion-plan.md`。
7. W0 `docs/superpowers/specs/2026-09-17-nearby-playable-seams.md`。
8. W0 `docs/acceptance/2026-09-17-nearby-playable-progress.md`。

## 2. 新鲜验证结果

| 检查 | 本轮结果 | 证明边界 |
|---|---|---|
| 重新构建后定向 CTest | 5/5，通过 | UI projection、ABI/contract 现有覆盖、config consent、lobby/select/no-false-start |
| 字体静态 unittest | 16/16，通过 | 源码合同和静态断言，不是原生字号实测 |
| UI / typography checker | 通过 | 137 条三端文案、10 个角色等静态合同 |
| Harmony ohosTest assembleHap | 成功，增量构建，未签名 | 测试包可构建，不是 Hypium 已执行；不是全新 clean build |
| DUAL 运行场景 | 明确 NOT_RUN | 600 帧、暂停断线、REC04 300ms/30s 均未恢复 |
| 新 ABI guard probe | FAIL，退出码 1 | 旧调用方边界外 96 字节被覆盖 |

定向 host 命令，在 W0 的 WSL 对应目录运行：

```sh
cmake --build out/nearby-playable/shared-linux --target flynes_game_config_consent_test flynes_two_engine_dual_mvp_test flynes_session_v2_abi_test flynes_session_v2_contract_test flynes_product_nearby_ui_test --parallel 2
ctest --test-dir out/nearby-playable/shared-linux -R 'flynes_game_config_consent|flynes_two_engine_dual_mvp|flynes_session_v2_abi|flynes_session_v2_contract|nearby_product_ui' --output-on-failure --no-tests=error
out/nearby-playable/shared-linux/flynes_two_engine_dual_mvp_test
```

静态命令，在 W0 运行，每条单独检查退出码：

```powershell
python -m unittest discover -s tools/quality/tests -p 'test_nearby*typography.py' -v
python tools/quality/check_nearby_ui_contract.py
python tools/quality/check_nearby_typography.py
```

## 3. 相比上一轮已纠正

- `confirm_local_pending` 和 `apply_peer_pending_confirm` 均拒绝 `pending_start_bound_ == false`；engine 不再给未绑定配置发布确认动作。新增零配置负例通过。允许浏览/选择未知条目不等于允许确认，这个区分可保留。
- `test_two_engine_dual_mvp.cpp` 已删除误导的 600 帧通过函数名，打印明确 NOT_RUN；结尾只宣称大厅/选择/禁止误开局通过。尚无恢复运行测试的证据。
- 鸿蒙测试已恢复 `headline13 > headline1`，并以 `px2vp` 校验 48vp。过时 `getTextSize` 静态要求已去掉，16/16 通过。
- 台账已标注旧 REC04 和旧字体断言为“历史失效”。不再把这些作为当前新缺陷重复派发。

## 4. 当前问题与优先级

### F1 / P1：目录结构尾追加破坏旧数组复制 ABI（新增、已复现）

位置：W0 `shared/include/flynes/flynes_session.h` 的 `fly_session_game_choice_v2`；`shared/src/session/view/session_view.cpp` 的 `copy_view_page`、`fly_session_view_copy_game_choices_v2`。

结构由 272 扩大为 368 字节，但函数仍执行新类型 `std::copy_n`，没有旧布局边界或显式 element stride 协商。仅新增 `R0_SIZE` 宏不产生兼容性。现有 contract 测试只覆盖空目录复制，不能捕获有元素时的越界。

独立 probe 在完整新结构内分配安全哨兵区，声明旧尺寸后调用真实复制函数，输出：

```text
old_size=272 new_size=368 result=0 written=1 tail_bytes_changed=96
FAIL: copy writes past old caller boundary
```

这证明越过旧调用方尺寸写入；并非宣称当前全量重编译 App 已发生崩溃。旧二进制调用者或按旧 stride 分配的数组会受影响。两个元素的 stride 兼容也必须测试，不能只修第一元素的 memcpy 长度。

验收要求：保留旧公开符号的布局/写入边界，扩展资料采用兼容读口（例如独立扩展读口或明确 version/stride 新接口，由集成人按已有 ABI 政策定稿）；不靠要求所有调用方同步重编译掩盖问题。必须有旧布局 canary、capacity=2、分页、新布局完整元数据、错误版本/尺寸无副作用测试。

### F2 / P1：已能安全阻止开局，但正常开局仍不可达

- `dual_session_controller.cpp::parse_choice_record` 仍只解析旧目录记录；core/profile/options 默认零。加上新门禁后，现有目录路径不能确认。这是安全的中间状态，不是完成状态。
- `apply_peer_pending_confirm` 仍只有测试调用，未见生产已验证消息接入。
- 指纹仍包含本地/authority seat stub；完整的共享座位配置、DUAL 模式及双方 runtime-ready 屏障尚未闭环。

验收要求：内容和配置来自真实目录/确定性运行配置；不是 UI 提交 hash，不是给零字段填随便一个非零值。两端对同一规范化提议计算同一标识；本地不同目录版本/显示名不影响标识；绑定配置变化撤销确认。对端确认必须来自验证后的连接消息，错误 session/config/revision、重复/乱序/迟到消息安全处理。双方明确确认且双方加载就绪后才能步进。

### F3 / P2：鸿蒙测试已有改进，仍缺执行与完整验收

`NearbyUxRestoration.test.ets` 的 scale=2 仍只操作创建/返回；没有覆盖 640×360、中英双语、长原因、末项可达。测文本 bounds 高度增加可以证明布局变化，不能单独证明准确的 21/12/14 角色字号。

`openPage` 重复 startAbility，`EntryAbility.ets` 只在 onCreate 处理测试 Want、没有 onNewWant。**这是待运行核实的生命周期风险，不写成已复现故障**：测试必须证明每次切换的页面和比例实际生效。不能通过固定 delay 或仅检查 parser 成功代替证据。

验收要求：使用当前 SDK 实际支持的测量；默认角色字号/权重与渲染 bounds 分别验证；记录 requested/applied scale、density、viewport；测试比例转换不得受上个用例残留影响。按原字体卡补小矩阵，缺签名/模拟器则报告精确阻塞，不复制证书，不做真机。

## 5. 下一执行检查点（复用已有卡，不增加复杂首版范围）

### CP0：ABI 修复，优先于继续向平台扩散字段

文件范围：W0 `shared/include/flynes/flynes_session.h`、`shared/src/session/view/session_view.{hpp,cpp}`、`shared/tests/nearby/contract/test_session_v2_{abi.c,contract.cpp}`；必要的调用方适配及 seams 文档。需要新目标时才修改 `shared/CMakeLists.txt`。

- [ ] 将 F1 复制越界复现变成正式失败测试，并补两个旧元素 stride 测试。
- [ ] 核对已有 ABI 兼容政策，冻结一种兼容扩展接口；不得改旧符号语义为直接写新数组。
- [ ] 最小实现后跑新旧调用方正反例、既有 ABI/contract 与 consent 回归。
- [ ] 输出红绿证据和接口变更说明，再进入 CP1；不能仅消除诊断程序的断言。

### CP1：ENG-05 完整目录配置

文件范围：W0 `shared/src/session/dual/dual_session_controller.{hpp,cpp}`、`shared/src/session/engine/session_engine.cpp`、必要目录 provider/codec、`shared/tests/nearby/integration/test_game_config_consent.cpp` 和 `shared/tests/nearby/harness/two_engine_loopback_fixture.hpp`。接口变更由同一个集成人负责。

- [ ] 先测生产记录解析后缺任一 core/profile/options 时不可确认，非零但不受支持的 profile 也不能冒充已验证。
- [ ] 复用目录和 runtime 现有身份来源，接齐三个字段；测试应覆盖生产解析路径，不只 `publish_imported_choice` 注入。
- [ ] M0 可使用明确的默认主机/P1/P2 组合，但须成为双方相同的规范化配置；不能把各端“本地座位”直接当共享身份。可切换座位仍归 ENG-07/M1，不删原稿控件。
- [ ] 同内容/配置、不同本地目录版本与显示名得到相同标识；内容/core/profile/options/绑定座位或模式变更使旧确认失效；A→B→A 旧确认继续 STALE。
- [ ] 未知配置/缺资源有实际 reason，不自动下载、不断朋友连接、不放开确认按钮。

### CP2：ENG-06 真实确认与开局屏障

文件范围：W0 shared engine/dual，复用既有 wire 控制消息和验证流程；确实缺协议时按 ENG-02 先补精确字段/golden，再实现，禁止臆造与既有协议冲突的新编号。

- [ ] 负例先行：本端确认不能写 peer；错误 session/config/revision、旧 token、重复确认、乱序 ready、加载失败均不提前步进。
- [ ] 本端确认经真实发送路径到对端，验证消息后才能写 peer；不得由通用 pump 自动批准或直接调用对端内部 setter 充当集成测试。
- [ ] 明确配置 revision 的共同提议来源；双方本地选择历史不同仍能对同一当前提议达成确认，不能假设各自计数器必然相等。
- [ ] 两端 runtime ready 并达成相同起始边界后开局；一端加载失败，双方保持不运行并可见原因；关闭后回调不启动旧局。
- [ ] 恢复 600 帧、暂停/断线冻结、REC04 真断言；L1 fake-runtime/loopback 只算逻辑层证据，不冒充 L2。

### CP3：ENG-08/09 真核心 + 真网络（同一条测试）

复用 W0 `shared/src/session/dual/dual_runtime_adapter.*`、`shared/nearby-quic-provider/` 和既有 scheduler；开工先检查同职责实现，不照旧卡新建第二套 runtime。

- [ ] 按 ENG-08 验证真实 NestopiaUE、真实 P1/P2 输入、唯一步进拥有者；使用已授权双人样本，产品源码不写死游戏名。
- [ ] 按 ENG-09 让两个 engine 控制与输入字节实际走 Quinn socket；crate 自测/链接成功不算双 engine 网络通过。
- [ ] 同一测试同时满足真实 core 和真实 provider，至少 600 帧按仿真帧对齐比较状态摘要；输入变化必须有实际效果。
- [ ] 注入断流、篡改、旧局输入时冻结/拒绝，不继续单人、不回退 STREAM；记录有无 discovery 替身，不能把 loopback socket 称作物理无线认证。

### CP4：首平台 Android 原生闭环，再扩三端

按既有 ENG-03.A、ENG-04.1–.5.A、ENG-10、ENG-11.A、ENG-12 卡推进；平台 owner 文件只由一个执行者串行修改。

- [ ] 原生 owner 持有真实 engine，页面读 snapshot/action；不使用另一套 connected/confirmed 布尔，不假启用 N09。
- [ ] 原始入口→输入码→房主接受→双方 SAS→连接→原大厅选游戏→双确认→既有游戏视图。
- [ ] 非真机环境可执行的两 App 连续交互至少 10 分钟，两路输入有效；暂停继续、结束回大厅、第二局重确认通过，旧局输入无效。
- [ ] Android host + unit + emulator instrumentation，并做单人大厅/导入/播放/存档关键回归。模拟器不具备的生产发现/无线能力单列 BLOCKED；不可通过测试替身认证整条生产链。
- [ ] 首平台 M0 达成即交付可运行结果和限制；不等 STREAM/复杂恢复，也不称三端全部完成。之后按同一契约推进 H/I，缺 Mac 如实标记。

## 6. 可复制主线执行 prompt

```text
继续实现 FlyNES Nearby 最小可玩 DUAL，本会话是实施，不是再写一轮泛化审计。

先完整阅读：
E:/workspace/codes/games/fly-little-games/docs/superpowers/plans/2026-09-18-nearby-review-and-next-prompts.md
及该文 §1 列出的仓库规则、原稿、获批 UX、ENG/UX 卡和 W0 seams。

产品工作目录：
E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes
不要在 main 写产品。先核对 HEAD、dirty/untracked 和最新源码；已有修复不要重做。保留他人改动、证据与其他 worktree，禁止 reset/stash/clean、自动提交全部或擅自合并。不要自动启动子代理；需要新 worktree 时只用仓库 New-VersionedWorktree.ps1，并先确认基线包含所需 dirty 实现。

目标：先首平台 Android 两端本地运行 NES、交换输入/状态的可玩版本，保持三端扩展接缝和原始 UX。不是串流。STREAM、ROM 传输、复杂恢复/迁移、高级好友功能后置；真机不测，host/模拟器可以继续。旧文档 25% 停止线已撤销。

按交接文 CP0→CP1→CP2→CP3→CP4 实施。每个检查点先失败测试→最小实现→回归→记录证据，有安全可执行下一项就继续，不以“文档已更新”结束实现。

第一优先 CP0：目录 ABI 尾追加把272字节变成368字节，copy_game_choices_v2仍按新结构复制。复现诊断位于 W0/out/evidence/nearby-review-2026-09-18/game_choice_abi_probe.cpp。将旧布局 canary 和 capacity=2 stride 变成正式负例，按现有 ABI 政策兼容修复；不能仅在新结构中增加R0宏，也不能要求旧调用方同步重编译当修复。

然后 CP1/2：接齐真实 catalog 的 core/profile/options 和规范化共同配置，维持未知配置不能确认。实现验证后的对端配置确认消息与双方 runtime-ready 开局屏障。禁止恢复 BOOLEAN 代填 peer、直接调用对端内部 setter、pump 自动批准。目录显示名/本地版本不参与共同身份；配置变化/旧 revision/错 session/迟到回调必须拒绝。M0 默认座位可用，但必须是明确共同配置，不能让双方本地 seat 各算各的。

再 CP3/4：复用现有 NestopiaUE adapter 与 Quinn，同一测试两个真实 engine、真实 core、真实 socket 跑600帧并核对同帧摘要；恢复断线冻结和超时断言。接 Android 真实 owner 和原界面，验证双方确认、P1/P2输入、暂停继续、结束/第二局和单人回归。模拟器缺硬件能力可记录局部阻塞，不用 fake provider 冒充生产完成。

UI必须服从原始 HTML 和获批 UX；不重新设计大厅、不全局改字号/主题、不删除原稿控件、不伪造连接/确认。新 wire 缺口先按 ENG-02 冻结精确合同和golden，不手写密码算法，不展开后置协议。

每个检查点交付：变更文件、源码版本及dirty范围、实际命令/退出码/数量、红绿证据、provider/runtime类型、PASS/FAIL/NOT_RUN/BLOCKED/DEFERRED分项。静态检查不算原生UI，L1不算真核心真网络，600帧测试未运行不能计PASS。必要上游选择或权限缺失时给出精确阻塞，不擅自扩范围。
```

## 7. 可选独立会话 prompt：鸿蒙字体证据补齐

用户可以另开会话执行；不与主线共享写入 engine/ABI，也不与另一个鸿蒙页面 worker 同时改同一文件。

```text
仅执行 FlyNES 鸿蒙 TYPO-01.H / TYPO-05.H 的剩余非真机验收，不接管后端或重新设计 UX。
先读 E:/workspace/codes/games/fly-little-games/docs/superpowers/plans/2026-09-18-nearby-review-and-next-prompts.md §1–4，以及09-17字体计划 §3/8。

基于最新 W0 实现，先核对未提交内容；若使用独立 worktree，必须拿到包含最新实现的已确认基线，不能假定只取 HEAD 就包含字体改动。创建 worktree 只用仓库脚本；不要覆盖他人文件，不自动启动代理。

目前16/16静态测试和测试HAP构建通过；Hypium仍未执行。不要重复修getTextSize或恢复旧API。检查重复 startAbility 后测试 Want是否真正应用（onCreate/onNewWant/页面切换），先用运行证据确定，不凭猜测改生命周期。

用本地SDK支持的API测默认21/12/14角色、权重和实际渲染边界；bounds高度增加不能代替准确字号。记录requested/applied比例、density、实际可用区；1→1.3→2变化应确实生效、无重复放大，release不接受测试覆盖。48vp按正确窗口density换算。

补640×360中英双语、2倍文字、长原因、末项主操作和返回可读可滚动可触发，保留原稿结构，不缩字、不关系统放大、不改全局主题。新增产品修复遵守TDD。

只跑host/HAP/兼容模拟器Hypium，不做真机。缺本地测试签名时不要复制证书或制造PASS；输出具体阻塞和可执行命令。只改本平台字体helper、相关页面/EntryAbility、对应Hypium及字体静态测试；不写shared ABI/engine/原稿。交付源版本、命令、测试结果、原生测量/截图及NOT_RUN清单。25%停止线已撤销。
```

## 8. 本次关键源码指纹

以下均为 W0 SHA-256，复审末读取；不是全工作区指纹。新会话必须重新核对，不能把本报告当永久状态。

| 文件 | SHA-256 |
|---|---|
| `shared/include/flynes/flynes_session.h` | `AF57D70F52DD83FA88BE1B9ED72AD1F8EE022B59E0206C03DBD0A4C73B471458` |
| `shared/src/session/view/session_view.cpp` | `9D5DF6A528D29CC891698796BD4665100BCFF5F08B2C62991DF40F748DB9DB78` |
| `shared/src/session/dual/dual_session_controller.cpp` | `AAD633E7672C38A6D723DF26FBA9002D9E8E7B8D6D48852F45B7981F375644B2` |
| `shared/tests/nearby/integration/test_two_engine_dual_mvp.cpp` | `ED0253135373A937664F70586B77236B35C25F36DD5C7705ACDA85885808725D` |
| `harmony/entry/src/ohosTest/ets/test/NearbyUxRestoration.test.ets` | `FCDEF2EB15BBBEA0AEBBA21CEC0BCE9FCF1E0F8FA4B655458F163F7677BD87F2` |
