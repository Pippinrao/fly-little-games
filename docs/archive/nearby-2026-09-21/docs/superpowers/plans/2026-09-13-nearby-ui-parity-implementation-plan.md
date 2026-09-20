> 历史归档（2026-09-21）：本文的执行顺序、首版范围和进度结论已被[当前最小可玩方案](../../../../../nearby/README.md)取代。保留供追溯，不作为新增任务或发布承诺。

# Nearby UI Parity Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task in the existing authorized worktree. Steps use checkbox (`- [ ]`) syntax for tracking. Do not start additional agents or worktrees unless separately authorized.

**Goal:** 在 Android、HarmonyOS NEXT、iOS 实现同一套已批准联机 UX，保留原大厅，提供输入配对码和扫码加入、真实验证、独立支持双人筛选及同一套错误和开局确认。

**Architecture:** 共享 session 拥有认证和连接事实，共享产品合同拥有三端相同的可见状态/动作/原因与游戏能力语义，平台只适配系统能力和渲染。连接生命周期与单局游戏配置分开；不存在已选 ROM 时仍可创建并完成认证连接。界面先通过同一组 fixture 验证，再接真实能力，最终用跨端实体机验收。

**Tech Stack:** C/C++17 shared product/session + 现有 C ABI；Android Java/XML/Material；Harmony ArkTS/ArkUI/NAPI；iOS SwiftUI/ObjC++；CTest、JUnit/Espresso、Hypium、XCTest。

---

设计基线：[三端设计](../specs/2026-09-13-nearby-ui-parity-design.md)。所有 U01–U11、C01–C18 必须覆盖。
工作分支 `codex/main-test-repair`，工作区 `E:/workspace/codes/games/fly-little-games/.worktrees/main-test-repair`，代码基线 `3086a3f`。本文是待执行计划，不是已完成清单。本文的新增文件、测试、类型和命令目标均在相应任务中创建后才可运行。

## 0. 执行规则与可复用门禁

- [ ] 运行 `git status --short`、`git log -1 --oneline`、`git worktree list`，重新确认没有接管其他人的改动。
- [ ] 运行 `./tools/versioning/Install-GitHooks.ps1`。保持 VERSION_MAJOR=1；每个提交由 hook 增 PATCH，同步平台元数据。不能手改 versionName/versionCode。
- [ ] 创建忽略的证据目录 `out/evidence/nearby-ui-parity/`；每轮记录 revision、case ID、platform、OS、设备/模拟器、语言、可用尺寸、文字倍率、主题、actual/expected 和测试日志。
- [ ] 对每个行为先增加失败断言并运行，保存失败原因；最小实现后运行相关 suite。fixture/注入测试通过不代表真实网络已接入。
- [ ] 每个任务只提交本任务的明确文件清单及 hook 生成的版本文件。无自动 merge/push，不清理其他工作树或设备上的既有产品证据。

复用当前仓库的完整 Android 门禁（相关变更落地后运行）：

```powershell
./gradlew.bat :app:testDebugUnitTest --console=plain
./tools/quality/run_harmony_completion_gate.ps1 -Stage AndroidEmulator
```

后者会运行 Android 单测、构建和 `:app:connectedDebugAndroidTest`，需要已启动且可用的模拟器。必须确认报告有实际执行用例且 0 失败，不能仅看进程退出码或跳过结果。

Host 门禁复用脚本；先按 `harmony/README.md` 配置已验证的 zlib prefix：

```powershell
./tools/quality/run_harmony_completion_gate.ps1 -Stage Host
```

shared 独立新测试也必须构建并执行，不能假定 Harmony host target 自动覆盖新增 shared tests：

```powershell
cmake -S shared -B out/nearby-shared -G "Visual Studio 17 2022" -A x64 -DFLYNES_BUILD_TESTS=ON "-DZLIB_ROOT=$env:FLYNES_ZLIB_ROOT"
cmake --build out/nearby-shared --config Release
ctest --test-dir out/nearby-shared -C Release --output-on-failure
```

Harmony 和 iOS 的实体设备命令放在 P8，不能用 Windows 静态检查替代 macOS/Xcode 或 signed-device 安装。

## P1. 固定三端 UI 合同与失败 fixture

**新增：**

- `shared/schema/nearby_ui_v1.json`：应用 UI 合同，不是 wire 协议；固定 screen ID、reason ID、string key、可用动作、布局常量与能力三值。
- `shared/tests/fixtures/nearby_ui_v1/cases.json`：设计 C01–C18 的语义输入输出，去除真实设备身份和包路径。
- `shared/include/flynes/product/nearby_ui_state.hpp`、`shared/src/product/nearby_ui_state.cpp`：纯产品投影，不执行平台 API。
- `shared/tests/test_product_nearby_ui.cpp`：状态/动作/原因与权限、配置失效测试。
- `tools/quality/check_nearby_ui_contract.py`：验证合同、fixture case ID、三端 string key 覆盖、中文文本相等；不生成成功的网络结果。

**修改：**`shared/CMakeLists.txt` 注册新增源与 CTest；三端现有资源文件只加入同一合同所需 key，不改变旧资源含义。

- [ ] 写入合同，状态字段明确为 `connectionStatus`、`screenId`、`stage`、`failureReason`、`verifiedPeer`、`actions`、`pendingConfigId`、`generation`。这些是新产品投影字段，不直接替换既有 ABI 的结构布局。
- [ ] 定义连接显示规则并加入 C01/C09/C14 失败用例；输入码正确、扫码成功、单端确认都必须保持未连接状态。

```text
projectEntry(snapshot):
  if snapshot.connectionEstablished AND snapshot.peerVerified
     AND snapshot.channelBound AND snapshot.compatibilityVerified:
      return ("nearby.entry.connected", "双人联机中")
  if snapshot.previousConnectionEstablished AND snapshot.reconnecting:
      return ("nearby.entry.interrupted", "联机中断")
  return ("nearby.open", "附近联机")
```

- [ ] 将“阶段尚未开始”和“阶段失败”分开；对 D5 的首失败优先顺序写 exact expected actions/reasons。
- [ ] 在现有会话状态尚不足时，投影为不可用并带原因；不靠 `elapsed time`、页面路径、测试 flag 推断成功。
- [ ] 增加 `nearby_product_ui` CTest target；执行 shared 独立测试，先记录失败，再实现最小投影直到通过。
- [ ] 执行 `python tools/quality/check_nearby_ui_contract.py`，预期全部 screen/strings/cases 覆盖。提交合同与纯策略，不声明三端功能完成。

## P2. 手动配对码的邀请查找与无游戏连接生命周期

这是原规格没有的产品能力，必须在三端真入口启用前落地。

**新增：**

- `docs/superpowers/specs/2026-09-13-nearby-invite-code-protocol-amendment.md`：在原协议基础上明确邀请查找消息、版本协商、字段布局、限额、超时和攻击面；不得将 UI 文档当 wire 格式。
- `shared/src/session/invite_code_route.hpp`、`shared/src/session/invite_code_route.cpp`：临时邀请查找状态与生命周期；复用既有 BLE/SAS 身份流程。
- `shared/tests/test_invite_code_route.cpp`：C05/C06/C07/C08/C16 的入口、取消、过期与身份释放门禁。

**修改：**

- `shared/schema/flynes_session_v1.schema`、`shared/src/session/wire/session_codec.hpp`、`shared/src/session/wire/session_codec.cpp` 与生成的对应 golden。
- `shared/include/flynes/flynes_session.h`、`shared/src/session/flynes_session.cpp`、`shared/tests/test_session_public_path.cpp`。

- [ ] 先完成协议补充文档：六位输入保留前导零，码只定位匿名的临时 invitation；不等于 SAS/密码/会话密钥，不在广播中发布昵称、长期公钥或稳定身份。
- [ ] 固定新增查找行为的 wire 消息类型、长度、角色、generation、60 秒期限、并发候选上界、速率限制、冲突处理和错误码；与原 registry 自动校验唯一性。上界和字节布局必须在该补充文档及 schema 内一致，不能各平台自定。
- [ ] 选择已有 BLE/SAS 作为找到邀请后的认证路径；如果当前平台组合没有共同 bearer，返回 NOT_SUPPORTED。不得为手输码增加云房间服务、明文凭据或跳过匿名接受。
- [ ] 在 `test_invite_code_route.cpp` 先写以下失败向量。通过各向量前不冻结 public ABI：

```text
InputCode("012345") retains all six bytes.
InputCode("12345" / "1234567" / "12a456") emits no lookup request.
LookupMatch + HostApprovalAbsent emits no identity reveal or credentials.
LookupMatch + HostAccept + LocalSasApproveOnly remains unconnected.
Regenerate(invite G1 -> G2); ReceiveMatch(G1) remains cancelled.
Cancel/Timeout; ReceiveHostAccept(old generation) remains cancelled.
Code collision among candidates never silently picks an identity.
No ROM selected; finish verified pairing -> connected game-less lobby.
```

- [ ] 用生成 codec/golden 测试消息约束；补齐纯状态机实现；将格式审查、原握手边界审查和负测结果归档。发现冲突时修改补充文档与 schema，再运行相同向量。
- [ ] 再扩展 C ABI 的事件、命令与 snapshot，保留 size/version 和零初始化规则；三端 binding 必须同一 ABI 版本。
- [ ] 运行 shared 全部 CTest 和公开 session path 测试；仅内部 seam 测试通过不能结束此任务。提交协议补充、wire 与 shared 变化。

## P3. 原大厅的独立能力筛选与状态投影

**新增：**`shared/include/flynes/product/multiplayer_eligibility.hpp`、对应 `.cpp` 与 `shared/tests/test_multiplayer_eligibility.cpp`；如复用已存在的 profile registry，文件只做产品投影，不再创建第二份游戏清单。

**修改：**

- `shared/include/flynes/product/game_center_item.hpp`、`shared/include/flynes/product/game_center_state.hpp`、`shared/src/product/game_center_state.cpp`、`shared/tests/test_product_game_center.cpp`。
- `app/src/main/java/com/flynes/emu/gamecenter/GameCenterState.java`、`GameCenterItem.java` 与同目录对应 unit tests。
- `harmony/entry/src/main/ets/service/CatalogProductService.ets`、`ios/app/CatalogLibraryView.swift` 的既有查询/偏好适配。

- [ ] 为 4 categories × 2 multiplayerOnly × 3 connection states 的全部组合写断言，并覆盖 search 非空、前导零 canonical IDs、UNKNOWN、空分类、重复 canonical ID 与热度并列。
- [ ] 能力数据只能由共享 profile/catalog 投影给平台；定义 `SUPPORTED / UNSUPPORTED / UNKNOWN`，未读到或版本不匹配为 UNKNOWN。
- [ ] 在原筛选之后稳定过滤，不改变排序：

```text
before = currentCategorySearchAndSort(catalog, category, query)
after = multiplayerOnly ? [g for g in before if eligibility(g.id)==SUPPORTED] : before
assert relativeOrder(after) == relativeOrderOfSameIds(before)
onConnectionEvent: update connection projection only
```

- [ ] 保存独立 `multiplayerOnly` 偏好，旧安装缺 key 时 false；与 category 和 query 分离。沿用每分类选择及 reconcile，不清空未当前显示分类的记忆。
- [ ] 三端消费同一 fixture 验证输出 canonical IDs 与顺序，不只比数量。运行 shared 和 Android unit tests。提交策略及测试。

## P4. Android 页面接入

**修改：**

- `app/src/main/res/layout/activity_home.xml`、`app/src/main/java/com/flynes/emu/HomeActivity.java`。
- `activity_nearby_friends.xml`、`activity_nearby_pairing.xml`、`activity_nearby_lobby.xml`、`activity_nearby_friends_manage.xml`。
- `NearbyFriendsActivity.java`、`NearbyPairingActivity.java`、`NearbyLobbyActivity.java`、`NearbyFriendsManageActivity.java`、`NearbyStagePipeline.java`、`NearbyInGameStatus.java`。
- `app/src/main/java/com/flynes/emu/app/FlyNesApp.java`、`app/src/main/cpp/flynes_app_jni.cpp`、`app/src/main/AndroidManifest.xml` 与两套 strings。
- 现有 `Nearby*Test.java`、`HomeContinuousLibraryTest.java`；新增 `NearbyInviteCodeTest.java`、`NearbyUiParityTest.java`、`HomeMultiplayerFilterTest.java`（均在 `app/src/androidTest/java/com/flynes/emu/ui/`）。

- [ ] 注入同一合同 fixture，先断言 N00 三动作无需滚动可点击、U02 exact 文案、C02 分类/筛选独立、底栏无全宽溢出；运行相应用例证明旧布局失败。
- [ ] 保留大厅结构，只改原 `open_nearby` 状态显示并在 `library_status` 同一行增加独立筛选。不得把附近页内容替换大厅详情卡。
- [ ] 在 NearbyPairing 的现有流程容器中实现 N01–N07，字段错误紧邻输入，提交锁定当前 request generation。复用 Activity/ViewModel 层的生命周期取消机制，迟到回调先比 generation。
- [ ] 为新 view id 和 test semantic id 建立对应，统一尺寸用设计 §6。父容器 match available width；底栏按钮 wrap/content constrained width，正文 weight/scroll 与底栏分离。
- [ ] 实际绑定 snapshot 后才启用动作；权限随使用声明，扫码才申请相机。真 QR 编解码只接已冻结 wire 格式。
- [ ] N09 真实显示候选主机、seat、文件与配置短码；每端一次确认绑定 fingerprint；修改配置通知双方失效。
- [ ] 运行 Android unit tests、Host tests 和完整 emulator instrumentation。归档 C01–C18 Android 截图、节点树和日志；真实端到端留待 P8，提交原生 UI 接入。

## P5. HarmonyOS NEXT 按同一合同接入

**修改：**

- `harmony/entry/src/main/ets/pages/GameCenter.ets`、`NearbyFriends.ets`、`NearbyPairing.ets`、`NearbyLobby.ets`、`NearbyFriendsManage.ets`、`RunGame.ets`。
- `harmony/entry/src/main/ets/service/NearbyService.ets`、`harmony/entry/src/main/cpp/napi_init.cpp`、`harmony/entry/src/main/cpp/types/libentry/Index.d.ts`。
- `harmony/entry/src/main/module.json5`、`resources/base/element/string.json`、`resources/zh_CN/element/string.json`（均位于 `entry/src/main/`）。
- `harmony/entry/src/ohosTest/ets/test/NearbyService.test.ets`、`List.test.ets`；新增同目录 `NearbyUiParity.test.ets`。

- [ ] 以 P1 fixture 注入 NearbyService，只在测试构建允许；C01–C18 用 screen/动作/原因/边界一致的断言，先运行 Hypium 留下旧布局失败证据。
- [ ] 将现有纵向联机页面改为与 Android 相同断点的分栏；所有 ArkUI Row/Column 子项明确 constraint/weight，禁止内容撑开父宽。移除 Nearby 页面独有的偏蓝硬编码，映射设计共用颜色。
- [ ] NAPI 只解码 shared snapshot 并转发类型化命令，不生成 nickname、成功状态或独立六位认证算法。
- [ ] 现有大厅保留分类/详情/横向网格，双人开关与数量同行；与 Android 使用相同偏好生命周期。
- [ ] 接入 N01–N12 与当前 native service，权限和错误使用同一 reason key；系统调用不同不等于应用内流程不同。
- [ ] 运行 Host CTest、HarmonyEmulator/Hypium；有兼容连接设备时必须执行 HarmonyDevice signed install + Hypium。保存真实 target 和安装限制，不能用 unsigned 构建替代。提交 Harmony 接入。

## P6. iOS 按同一合同接入

**修改：**

- `ios/app/CatalogLibraryView.swift`、`NearbyFriendsView.swift`、`NearbyPairingView.swift`、`NearbyLobbyView.swift`、`NearbyFriendsManageView.swift`。
- `ios/app/bridge/FlyNesAppBridge.h`、`FlyNesAppBridge.mm`、`ios/app/Info.plist.in`、`ios/app/CMakeLists.txt`。
- `ios/app/en.lproj/Localizable.strings`、`ios/app/zh-Hans.lproj/Localizable.strings`。
- `ios/tests/ProductUITests.mm`；新增 `ios/tests/NearbyUiParityTests.mm` 并注册到现有 product XCTest bundle。

- [ ] 基于 P1 fixture 写与另外两端相同的屏幕/可见性/点击结果断言；使用 `ios/app` 产品构建，不是 `ios` portability probe。
- [ ] NearbyFriends 的现有 `List` 改为批准的左右分栏容器；使用相同逻辑宽度断点、安全区和 content-priority。系统 Dynamic Type 仅改变排版，不改变页面信息与授权顺序。
- [ ] CatalogLibraryView 保留 NavigationStack 与原大厅框架；原 `.nearby` 无 ROM 导航继续有效。增加状态标签和独立开关，不把 `.run` 路由作为认证连接的前置条件。
- [ ] ObjC++ bridge 接入同一 shared snapshot、generation 与命令结果；静态合同检查只作辅助，不能标记运行验证通过。
- [ ] macOS/Xcode 构建产品并执行 XCTest UI 和 runtime suite；记录 SDK、simulator UDID、尺寸与语言，实体 iPhone 配对留到 P8。提交 iOS 接入。

## P7. 三端横屏一致性与异常回归

**新增：**`tools/quality/check_nearby_ui_parity.py`；只消费证据 JSON 与 PNG 尺寸/节点 bounds，不生成设备执行结果。

- [ ] 每端导出相同结构：

```json
{
  "caseId": "C09-connected",
  "platform": "android",
  "locale": "zh-CN",
  "contentSize": [736, 414],
  "fontScale": 1.0,
  "screenId": "G00",
  "entryText": "双人联机中",
  "category": "FAVORITES",
  "multiplayerOnly": true,
  "visibleIds": ["fixture-a", "fixture-d"],
  "enabledActions": ["selectGame", "manageConnection"],
  "overflowOutsideGameGrid": false
}
```

- [ ] checker 对 case/locale/size/fontScale 分组，必须恰有 android/harmony/ios 三端；缺一端 FAIL 而不是跳过。比对 screen、字符串、动作、原因、visibleIds、布局容器顺序。
- [ ] 以 ≤2 逻辑单位检验同尺寸 bounds；字体抗锯齿和 OS 系统弹窗不作逐像素等值。任何主动作遮挡、页面横向溢出或流程不同均 FAIL。
- [ ] 覆盖 C17 的 8 种宽度及 C18 横屏高度、大字、键盘、安全区、中文/英文；只有游戏网格允许横向滚动。
- [ ] 对输入码校验、QR 解析/重放、单端确认、文件双方许可、配置更改、取消迟到结果进行三端同结果回归。
- [ ] 执行 `python tools/quality/check_nearby_ui_parity.py out/evidence/nearby-ui-parity`，预期 0 missing、0 semantic mismatch、0 layout violation。原生 UI 未齐全不得用 HTML 报告填数。

## P8. 真实设备闭环及交付

- [ ] Android↔Harmony、Android↔iOS、Harmony↔iOS 和三种同平台组合，每对交换邀请者；输入配对码和扫码各走完整 N00→验证→原大厅→选游戏→双方配置确认→双机游戏。
- [ ] 两端分别验证 P1/P2、声音独立、文件缺失与拒绝接收、主机变化失效、退出换游戏保留连接、取消/重连、旧 QR 重放及支持模式门禁。没有匹配证据不启用硬件限定模式。
- [ ] Harmony 设备命令使用现场读取的 serial 与已有签名配置；不输出 profile/密码：

```powershell
$harmonyToolRoot = $env:DEVECO_STUDIO_HOME
if (-not $harmonyToolRoot) { throw 'Set DEVECO_STUDIO_HOME to the installed DevEco directory.' }
./tools/quality/run_harmony_completion_gate.ps1 -Stage HarmonyDevice `
  -NodeExe "$harmonyToolRoot/tools/node/node.exe" `
  -HvigorScript "$harmonyToolRoot/tools/hvigor/bin/hvigorw.js" `
  -DevEcoSdkHome "$harmonyToolRoot/sdk" `
  -HdcExe "$harmonyToolRoot/sdk/default/openharmony/toolchains/hdc.exe" `
  -HarmonyTarget $env:FLYNES_HARMONY_TARGET
```

- [ ] iOS 在实际 macOS checkout 运行现有脚本，使用现场选择的 simulator UUID；UI 测试 bundle 必须包含 P6 新用例：

```bash
bash ios/scripts/build_simulator.sh
xcodebuild -project build/ios-simulator/flynes_ios_product.xcodeproj -configuration Debug -target FlyNESUITests -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO build
xcodebuild -project build/ios-simulator/flynes_ios_product.xcodeproj -configuration Debug -target FlyNESRuntimeTests -sdk iphonesimulator CODE_SIGNING_ALLOWED=NO build
python3 ios/scripts/run_simulator_tests.py "$FLYNES_IOS_SIMULATOR_UDID" FlyNESUITests
python3 ios/scripts/run_simulator_tests.py "$FLYNES_IOS_SIMULATOR_UDID" FlyNESRuntimeTests
```

- [ ] 将三端同用例对比和真机矩阵结果写入 `docs/acceptance/2026-09-13-nearby-ui-parity.md`，原始日志/截图保留 ignored 目录。缺设备或某组合不支持时明确写未验证/不支持，不声明三端完成。
- [ ] 普通交付说明写明 revision、测试数量、设备组合、安装限制。若用户另行要求 release，才按仓库政策从干净 main 和版本匹配 tag 发布，并附包 hash；测试签名不叫生产证书。

## 完成标准与自检映射

| 设计要求 | 实现任务 | 必须通过的用例 |
|---|---|---|
| U01–U04 原大厅/状态/筛选 | P1/P3/P4/P5/P6 | C01–C03、C09、C17–C18 |
| U05–U09 先连接、双入口、匿名许可与验证 | P1/P2/P4/P5/P6/P8 | C04–C10、C16 |
| U10 换游戏/配置/文件/恢复 | P1/P2/P4/P5/P6/P8 | C11–C16 |
| U11 三端完全一致 | P1/P4/P5/P6/P7 | C01–C18 + 三端实测矩阵 |

实现不得以某端 UI 已更新、某个内部网络测试通过或文案数量一致作为终点。完成要求：相同应用 UX + 真实验证入口 + 原大厅保持 + 三端用例同结果 + 已声明支持组合的实体机闭环。
