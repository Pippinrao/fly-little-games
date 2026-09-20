# main 现状、测试失败处置与多人联机实机测试方案

日期：2026-09-13（Asia/Shanghai）  
基线：`main` / `661e84bc97023676882d5f8ffbbd97634d25e8b3` / `VERSION=1.1.12` / `VERSION_MAJOR=1`

本次按“输出文档即可”执行：检查源码、已有报告和轻量主机检查，只新增本文档，不实施修复、不安装应用、不更改设备设置、不提交或推送。下文修复动作均为后续执行方案。

## 1. 结论与证据边界

1. `main` 已合入 Android、HarmonyOS、iOS 的附近联机页面骨架。入口、配对页、好友管理、游戏内状态说明已存在，真实联机能力仍被明确阻塞，不能按“可双机游玩”验收。
2. 当前检出环境实际复现两项失败：标题生成表的换行符校验失败、Harmony 产品契约测试匹配过时的调用形式。第一项不是标题数据过期，第二项不能据此判断产品丢失启动参数。
3. 历史 Android UiAutomation 失败、iOS 导入夹具失败、Harmony CTest 可执行文件缺失需分别处置，不能合并称为当前产品回归。本轮没有重跑三端完整构建或设备套件。
4. 实机验收分为“当前页面与安全阻塞态”“手机传输实验”“产品双机链路”“九方向认证”。前一层通过不能替代后一层；当前正式双机功能验收为 **BLOCKED_IMPLEMENTATION**。

证据标签：**本次复现**＝在上述当前检出上实际执行；**源码确认**＝静态检查；**历史记录**＝读取旧报告，未证明对应当前 HEAD；**计划**＝尚未执行。

本地 `origin/main` 引用也指向上述提交。本次没有 fetch 或查询远端 CI，因此不据此断言服务器端最新状态或远端检查全绿。

## 2. 主干状态

### 2.1 最近相关提交

| 提交 | 内容 | 对本次判断的影响 |
|---|---|---|
| `661e84b` | 恢复合并丢失的 Android popularity 生成任务 | `app/build.gradle` 已有 `generatePopularity`、生成源码目录和 `preBuild` 依赖；本轮未重新构建 APK |
| `8c01a21` | 合入 nearby multiplayer spine | 三端 UI 骨架已进入 main |
| `587caea` | 修复 iOS nearby banner 挂载前激活约束导致的启动崩溃 | 已合入的修复；历史失败不能继续直接列为未修复 |
| `34ace6b` | 离线双语索引和目录修复 | 引入需跨平台一致检出的标题生成表检查 |

工作区开始与检查结束时均已有 `harmony/build-profile.json5` 本地修改，以及 `.superpowers/brainstorm/`、`docs/audits/`、`docs/legal-nes-homebrew-preship-research.md`、Harmony 编辑器配置和 `sideloadlydaemon.log` 等未跟踪内容。全部保留；未读取签名配置内容。当前 tracked tree 不干净，尚不满足发布前置条件。

### 2.2 联机能力核查

| 层级 | 当前事实 | 尚缺内容 / 验收限制 |
|---|---|---|
| Android 页面 | `NearbyFriendsActivity`、`NearbyPairingActivity`、`NearbyLobbyActivity`、`NearbyFriendsManageActivity`、`NearbyInGameStatus` 存在 | 显示缺失服务的原因，不代表发现或配对成功 |
| Harmony 页面 | `pages/Nearby*.ets` 与 `service/NearbyService.ets` 存在 | service 是页面决策逻辑，不是无线服务 |
| iOS 页面 | `Nearby*View.swift`、游戏内 banner 和导航接入已存在 | 页面可用性与真实网络会话必须分开验收 |
| shared session | 已有初始方案 reducer、poll/complete/snapshot 接缝和应用帧类型识别 | `submit_event` 的 V1 无 payload，仍返回 `INVALID_STATE`；`receive_stream/datagram` 仅记录诊断后拒绝，尚无认证解码到可信状态的路径 |
| 平台绑定 | 在 Android JNI、Harmony NAPI、iOS `FlyNesAppBridge.h/.mm` 未找到 `fly_session` / `flynes_session` 引用 | 页面尚未通过这些绑定驱动真实 session |
| 传输 | 独立 QUIC probe 的历史 Android↔Windows 正测、错误 pin 负测有文档 | 不等于手机↔手机、BLE/QR 身份、无路由器建网或产品 QUIC |
| 游戏同步 | runtime 四端口、checkpoint/rollback 基础存在 | 没有据此建立端到端输入、HOST_STREAM/DUAL、恢复和降级验收证据 |
| 设备认证 | 未找到当前版本九方向完整认证证据 | 本轮未盘点在线设备、未安装候选包；设备可用性和签名适用范围待执行前核实 |

源码依据：[shared session](../../../shared/src/session/flynes_session.cpp)、[Harmony 页面服务](../../../harmony/entry/src/main/ets/service/NearbyService.ets)。

文档时序注意：[9 月 11 日交接](../../handoffs/2026-09-11-nearby-multiplayer-handoff.md)中的“没有产品入口”和[旧 M2–M5 状态](../specs/m2-m5-status.md)中的“session 全部为桩”已不完全准确。[产品主线计划](2026-09-11-nearby-product-spine-plan.md)末尾包含 9 月 13 日更新，应结合当前源码阅读，不能只引用其早期基线。

## 3. 测试现状与失败分类

### 3.1 本次实际执行

在仓库根目录执行以下命令，未运行生成命令或产品构建：

| 命令 | 结果 | 解释 |
|---|---|---|
| `python tools/game_titles/game_titles.py check` | exit 1：`generated table is stale; run generate` | 已确认是 Windows 检出换行符差异，见 F1 |
| `python harmony/tests/test_harmony_product_contract.py` | exit 1，第 68 行断言失败 | 旧测试要求两参数调用，当前实际调用有三个参数，见 F2 |
| `python -m unittest discover -s ios/tests -p 'test_*.py'` | exit 0，14 tests，OK | 仅 unittest discovery 发现的主机用例；不涵盖所有独立脚本、XCTest、模拟器或实机。临时夹具写入 ignored `build/` |

### 3.2 已有报告，不能替代当前全量回归

| 来源 | 读取结果 | 限制 |
|---|---|---|
| `app/build/test-results/testDebugUnitTest/TEST-*.xml` | 448 tests、0 failures、0 errors、2 skipped | XML 时间为 2026-09-13 04:50:27–04:50:33 UTC，早于 HEAD 的 12:52:22 +08 提交；不归属为当前 HEAD 全绿 |
| `app/build/outputs/androidTest-results/connected/debug/` | 16 tests、0 failures、0 errors | 既有单设备结果；不代表 Android 全量或双机结果 |
| `.artifacts/merged-host/Testing/Temporary/LastTest.log` | 12:51 的 `flynes_game_title_data_check` 失败 | 日志本次只运行了 1 项（显示编号 1/49），不能称为 49 项全量执行结果 |
| `out/android-loading-20260913/host-tests.log` | Harmony 12 项中 10 项通过、2 项 Not Run | 缺 `native_play_support`、`motion_frame_scheduler` Release 可执行文件；未运行不是断言失败 |
| 产品主线计划的 A1 更新 | 历史 shared 46/46、Harmony host 12/12、Android nearby UI 19/19、Hypium 31/31 | 合并前/其他构建目录的历史记录；不能直接替代当前构建 |
| 同一 A1 更新中的 Android 全套 | 历史 113 run、111 pass、2 fail，且排除 3 个 GL 类 | 两项在 UiAutomation 初始化失败；这是过滤后套件，不是无条件全绿 |
| 同一更新中的 iOS XCTest | 历史 runtime 49/49，UI 10/13 | 两项导入夹具失败有具体解释；第三个失败不能仅由汇总唯一确认，后续需逐项读 xcresult |

### 3.3 F1：标题生成表校验——已确认换行符根因

相关文件：`.gitattributes`、`shared/data/game_titles.inc`、`tools/game_titles/game_titles.py`、`tools/game_titles/test_game_titles.py`。

本次字节比较结果：

```text
generator expected bytes = 511917
working-tree bytes       = 514141
working-tree CRLF count  = 2224
working bytes CRLF→LF == generator bytes : True
git main blob == generator bytes        : True
core.autocrlf                             : true
game_titles.inc text/eol attributes       : unspecified / unspecified
```

生成器使用 `newline='\n'` 写出，并用 `read_bytes()` 严格比较。Git blob 正确，但检出时添加了 2224 个 CR，因此在本机失败。已有 `.gitattributes` 对 schema 和部分夹具采用 `text eol=lf`，此文件遗漏了同类约束。

**建议修复：**为该文件增加下面的精确规则，保持生成器的逐字节校验，不改变标题内容或排名数据。

```gitattributes
shared/data/game_titles.inc text eol=lf
```

后续 TDD / 验证顺序：

- [ ] 保留上述 RED；在隔离检出中用 `core.autocrlf=true` 复现，记录 `git ls-files --eol`。
- [ ] 在 `tools/game_titles/test_game_titles.py` 增加 Git 检出集成回归：临时仓库包含真实生成表与属性，提交后 clone，断言新检出表为 LF 且 `check` 成功。仅比较生成器与自身输出不足以覆盖此缺陷。
- [ ] 添加精确 LF 属性，在本任务隔离副本中用官方 `generate` 刷新检出文件；检查内容与基线 blob 一致。
- [ ] 运行下列主机检查，再运行新目录 shared CTest 和 Android unit；确认新检出同样成功，而非只有原工作区暂时通过。

```powershell
python -m unittest discover -s tools/game_titles -p 'test_*.py'
python tools/game_titles/game_titles.py check
git ls-files --eol shared/data/game_titles.inc
```

### 3.4 F2：Harmony 启动契约——测试匹配形式已过时

源码依据：[契约测试第 68 行](../../../harmony/tests/test_harmony_product_contract.py)、[RunGame](../../../harmony/entry/src/main/ets/pages/RunGame.ets)。实际调用为：

```typescript
await this.play.open(context, this.locator, this.autosaveEnabled);
```

测试却要求压缩空白后包含 `play.open(context,this.locator)`，括号紧跟 locator，导致合法的第三个参数无法匹配。当前调用确实传递了选中游戏 locator；本次证据不支持改回产品两参数调用。

**建议修复：**测试按完整三参数调用匹配并容忍空白差异，保留 locator 和 autosave 转发约束。可使用以下匹配作为最小修复候选：

```python
assert re.search(
    r'this\.play\.open\s*\(\s*context\s*,\s*this\.locator\s*,\s*this\.autosaveEnabled\s*\)',
    start_body,
), 'RunGame must forward the selected locator and autosave preference'
```

这需要 `import re`；仅为计划，尚未修改文件。后续步骤：

- [ ] 在测试辅助校验中覆盖真实三参数调用、换行/空白、错误 locator、遗漏 autosave。后三者中错误参数必须失败，避免通过删除断言获得绿色结果。
- [ ] 修订失效断言并运行整个 `python harmony/tests/test_harmony_product_contract.py`；脚本在首个失败即终止，当前不能保证后续所有断言均正确。
- [ ] 若进一步确认有产品转发问题，先加入 Hypium 行为断言再改产品；否则本项只修测试。
- [ ] 运行 Harmony host Debug CTest、Hypium；有兼容真机时完成签名安装及“两个不同游戏 + autosave 开/关”启动检查。保留签名范围与结果，不记录密钥或密码。

### 3.5 F3–F5：历史失败的下一步处置

| ID / 优先级 | 已知现象 | 后续执行与通过条件 |
|---|---|---|
| F3 / P1 | `SettingsMasterDetailTest.twoHundredPercentFontKeepsMasterTargetsVisible`、`FirstRunNavigationTest.largeFontKeepsStatusCardAndCtaFullyVisible` 历史在 `getUiAutomation()` 初始化失败 | 先各自单跑，再合跑、再放回完整套件，保留 runner/logcat，区分连接生命周期与布局断言。当前两个 helper 的读管道方式不同，其中一个仅固定 sleep 400 ms；这是检查线索，不是已证实根因。确认根因后最小修复，不增加无限重试或删掉大字体检查。恢复原 font_scale 和动画值，不固定覆盖用户原设置 |
| F4 / P1 | iOS 两个 `ProductImportUITests` 找不到 `FlyNES-Import-E2E-v1` | 在专用模拟器按 `python3 ios/scripts/stage_import_fixtures.py --udid <UDID>` 准备；先在 Files 验证目录可见，再跑导入 UI。分别归档夹具、构建与 xcresult。完整解析历史第三个失败，未确认前不归为夹具问题 |
| F5 / P1 | Harmony host 两个用例 Not Run，缺 Release exe | 新建独立 build 目录，完整 configure/build 成功后再跑 CTest；先 Debug，防止 bare `assert` 被 NDEBUG 消除。若继续要求 Release 行为门禁，改成始终生效的断言并验证；不得将 Release “运行成功但无断言”视为有效测试 |

GL 限制单列：历史 `MotionComputeParityTest`、`MotionShadowPresenterTest`、`GlCapabilityProbeInstrumentedTest` 因 `EGL_CONTEXT_UNAVAILABLE` 被排除。后续在支持所需 EGL/GL 的模拟器/设备上执行，缺能力时记录 BLOCKED_ENV，不记 PASS，也不能据此放行硬件模式。

## 4. 主干修复执行与回归门禁

建议顺序：F1/F2（确定性复现）→ F5（建立可信 host 基线）→ F3/F4（平台完整套件）→ 联机当前阶段 UI 回归。实现新联机能力不属于这些测试修复的隐式范围。

后续实现前用仓库工具建立隔离 worktree，安装 hook。下面命令仅供执行者使用，本文档任务未运行：

```powershell
.\tools\versioning\Install-GitHooks.ps1
.\tools\versioning\New-VersionedWorktree.ps1 `
  -Path '.worktrees/main-test-repair' -Branch 'codex/main-test-repair' -StartPoint main
```

目录或分支已有内容时选新名称，不清理占用目录。allocator 分配 MINOR，commit hook 增加 PATCH；major 必须保持 1，不手改平台版本字段。

在隔离 worktree 根目录执行的 host 模板（本机已存在对应 VS 工具与 zlib 前缀；其他机器按仓库 bootstrap 文档准备）：

```powershell
$cmakeExe = 'C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe'
$ctestExe = Join-Path (Split-Path $cmakeExe) 'ctest.exe'
$zlibPrefix = 'E:\workspace\codes\games\fly-little-games\.artifacts\host-deps\zlib-1.3.1-install'
& $cmakeExe -S shared -B out/main-repair-shared -G 'Visual Studio 17 2022' -A x64 `
  -DFLYNES_BUILD_TESTS=ON "-DZLIB_ROOT=$zlibPrefix"
# 每步确认 exit 0 后才运行下一步。
& $cmakeExe --build out/main-repair-shared --config Release
& $ctestExe --test-dir out/main-repair-shared -C Release --output-on-failure
& $cmakeExe -S harmony/tests -B out/main-repair-harmony -G 'Visual Studio 17 2022' -A x64 `
  "-DZLIB_ROOT=$zlibPrefix"
& $cmakeExe --build out/main-repair-harmony --config Debug
& $ctestExe --test-dir out/main-repair-harmony -C Debug --output-on-failure
.\gradlew.bat :app:testDebugUnitTest
```

MSBuild 如出现大小写重复代理环境键的 MSB6001，应按[既有计划的环境说明](2026-09-11-nearby-product-spine-plan.md)在构建子进程处理；不输出代理值，不改产品代码掩盖环境问题。

平台门禁使用 [Harmony completion gate](../../../tools/quality/run_harmony_completion_gate.ps1) 的 `HarmonyEmulator` / `AndroidEmulator` 与明确目标的 Device 阶段，参数见 [Harmony README](../../../harmony/README.md)。先核对脚本的安装与目标选择，记录每一阶段；不得将模拟器结论迁移到实体机。

验收清单：

- [ ] 每个修复保留独立 RED→GREEN、提交 SHA、命令、退出码、JUnit/CTest 报告；所有输出进入 ignored `out/evidence/`。
- [ ] Android shared/native 改动跑 host + unit；UI 改动跑模拟器 instrumentation；全套结果明确列出 skipped/excluded，不只报告通过数。
- [ ] Harmony 改动跑 host CTest + Hypium，有兼容真机时签名安装；iOS 相关修复跑真实 Apple SDK 构建与相应 XCTest。
- [ ] 新增 shared 生成表和 session 门禁的 CI 覆盖建议单独审查：当前 `.github/workflows/stage0.yml` 只构建 core smoke / ABI，不能代表 shared 全套。不要把“CI 绿”作为覆盖范围外功能的证据。
- [ ] 发布仍需 main 干净 tracked tree、版本匹配 tag、包哈希和测试结果；本文档不执行发布。

## 5. 多人联机实机测试设计

权威规格：[跨平台 nearby multiplayer design §25–28](../specs/2026-09-04-cross-platform-nearby-multiplayer-design.md)。下列阶段、用例组织及排程为执行建议；协议与数值门槛沿用规格，不重新定义。

### 5.1 分阶段进入条件

| 阶段 | 入场条件 | 本轮可接受的结论 |
|---|---|---|
| D0：当前产品页面 | 相应平台可安装候选包 | 入口、阻塞原因、国际化、单机不回归；真实连接必须仍诚实显示未实现 |
| D1：手机传输实验 | 两台手机、独立 probe、适用的本地测试签名 | STREAM/DATAGRAM、错误 pin 拒绝等传输证据；不是产品双机或身份认证完成 |
| D2：产品纵向双机 | 真实 BLE/QR、安全存储、承载、QUIC ChannelBind、session 绑定、输入/媒体链路具备 | 在一个具体方向中从入口到真实双人游戏跑通；当前 BLOCKED_IMPLEMENTATION |
| D3：支持矩阵认证 | D2 通过、指标采样与故障注入可用 | 九方向及所宣称设备组合、模式、bearer 的认证；当前未执行 |

### 5.2 设备与实验准备

全面覆盖同平台组合需至少 Android×2、HarmonyOS NEXT×2、iPhone×2，可分批借用。Android↔Harmony 先行只能覆盖该跨平台对，不能完成同平台或 iPhone 认证。

每台设备登记匿名设备编号、型号、SoC、系统/补丁、刷新率设置、应用版本与包 SHA-256、codec/backend 版本、capability 结果。签名证书只记录类型和适用范围，不保存签名 profile 或设备凭据。USB/工具 serial 仅留本地受控映射。

设备盘点用 `adb devices -l`、DevEco SDK 下 `hdc list targets`，iOS 在 Mac/Xcode 确认物理设备；当前在线情况不能引用 9 月 9 日或 11 日记录。安装前核对包身份、现有应用数据、兼容系统及签名目标，不卸载清数据来消除问题。

准备：合法可分发的双人玩法 ROM、带唯一 input edge 可视响应的诊断 ROM、相同完整 ROM SHA-256 的两端副本、缺 ROM/不匹配/损坏内容负测。日志只记录内容摘要和 fixture ID，不归档私人 ROM。

近场基准使用无路由器、无互联网环境，两机距离 1–3 米、电量至少 30%、初始温控正常。已有路由器 LAN 只用于排错，不纳入无路由器支持认证。不启用未经设备证据认证的 Extreme 等模式。

### 5.3 方向与角色矩阵

箭头表示 **authority → peer**，不是网络创建者、邀请者或 QUIC listener。

| 配置族 | 方向 | authority 座位 | 当前状态 |
|---|---|---|---|
| AA | Android → Android | P1、P2 | 未执行 |
| HH | Harmony → Harmony | P1、P2 | 未执行 |
| II | iPhone → iPhone | P1、P2 | 未执行 |
| AI | Android → iPhone | P1、P2 | 未执行 |
| IA | iPhone → Android | P1、P2 | 未执行 |
| AH | Android → Harmony | P1、P2 | 未执行 |
| HA | Harmony → Android | P1、P2 | 未执行 |
| IH | iPhone → Harmony | P1、P2 | 未执行 |
| HI | Harmony → iPhone | P1、P2 | 未执行 |

至少 18 个角色配置。每个平台对均覆盖 BLE+SAS、QR、宣称支持的保证 bearer、同 ROM DUAL、缺 ROM/DUAL 不合格的 STREAM、文件接受/拒绝、两端声音及独立静音。不同机型的同平台组合额外交换实体 authority，不用 P1/P2 交换替代。

每个平台对至少一轮由 A 邀请建大厅，再由双方改选 B 为 authority。记录 inviter、network owner、QUIC listener、authority、seat 五种角色；平台固定 network owner 不得暗中改写 authority。只有双方确实都能建网的 bearer 才要求交换 owner；第二条 bearer 只对已有认证支持的组合测试，不假设 iPhone 可程序创建标准热点。

### 5.4 可直接执行的用例清单

| ID / 阶段 | 操作 | 通过标准与留证 |
|---|---|---|
| UI-01 / D0 | 从游戏中心进入附近页、好友页、配对页、设置好友管理；中英各一轮，返回再进入 | 空列表、不可用按钮与具体阶段原因一致；无假好友/假连接；截屏或录屏 |
| UI-02 / D0 | 启动单机游戏、打开暂停状态、返回设置及游戏中心；大字体/横竖屏 | 游戏不因 nearby banner 崩溃；单机输入/音频不被联机占用；不凭空显示已入局 |
| PAIR-01 / D2 | 首次 BLE 发现，选匿名设备，邀请者接受请求，两端核对六位码 | 按 exact context/generation 授权后才释放身份/凭据；成功进入同一大厅后保存好友 |
| PAIR-02 / D2 | QR 正常扫码，再试过期、重复消费、错误签名、替换 context | 正常扫码仍需房主确认入局；负测在建网前拒绝，日志说明阶段 |
| PAIR-03 / D2 | 六位码不符、拒绝、页面退出、超过 60 秒；重新邀请 | 当前尝试失效，不发送/使用旧凭据；新尝试不继承旧批准 |
| NET-01 / D2 | 无路由器建网，核对选择方案与双方确切计划摘要 | 只使用已认证 bearer；自动流程的系统确认最多一次；认证失败不退回明文 |
| NET-02 / D1/D2 | 有效连接与错误有效 SPKI pin 对照；D2 再测 ChannelBind/exporter 不匹配、错误通道 | 错误 pin 在 TLS 拒绝；D2 绑定错误不能进入 session；D1 与 D2 结果分开归档 |
| LOBBY-01 / D2 | 双方查看游戏、ROM 状态、能力、主机、座位；改选主机、交换 P1/P2，一方不确认 | 角色与能力显示真实，双方确认前不开始；不存在静默选主机或隐式 port 0 |
| PLAY-01 / D2 | 相同 ROM、门禁合格进入 DUAL；两端同时移动、长按、快速点按、START/SELECT | 两人各控制对应角色；canonical 时间线一致，预测差异按协议纠正；记录四端口 applied input |
| PLAY-02 / D2 | 客机无 ROM 或 DUAL 不合格时尝试开始 | 自动选择合格 STREAM，客机有同会话画面和输入；STREAM 本身不传 ROM；都不合格则阻止开始并给原因 |
| AUDIO-01 / D2 | 双方播放；分别静音/取消；切换支持的音频路由，制造回调欠载 | 静音只改本机 gain，不停 canonical PCM；无双音频或持续欠载；保存采样序号/digest/本机 A/V |
| LIFE-01 / D2 | 暂停、后台/前台、锁屏、来电/音频焦点、surface 重建；各端分别操作 | 按状态机暂停/恢复；无粘键、崩溃、重复 authority；归零输入有对应记录 |
| REC-01 / D3 | 在长按时分别断链 1、5、29 秒；双方各做一次主机失联；制造旧连接 callback | 最后认证活动后 350 ms 内冻结/归零；恢复不丢 committed frame；旧 generation 不生效 |
| REC-02 / D3 | 超过恢复时限、重同步失败、切断 tail repair；由用户选择保存结束或符合条件的接管 | 保持暂停，不静默迁移 authority；DUAL 降级按事务保进度单向到 STREAM；不重复询问模式 |
| SAVE-01 / D3 | 暂停后保存、保存结束、持久化边界杀进程、重新入局与分支相遇 | 只发布完整已确认存档；不将预测帧写入；无隐式分支合并；依据规格 §25.4 执行事务故障子集 |
| FILE-01 / D2/D3 | 接受/拒绝文件补齐；注入同名异 hash、损坏 ZIP、超限、取消/断线 | 双方明确确认；仍走通用内容校验和目录导入；拒绝不影响正常 STREAM 权限边界 |
| FRIEND-01 / D3 | 保存后重连、重命名/删除/拉黑、重装换钥、身份重置 | 好友不自动授权本局加入、ROM 接收或主机变化；旧身份不静默继承信任 |
| RADIO-01 / D3 | 权限拒绝/永久拒绝、蓝牙/Wi-Fi 关闭、建网失败、路由变化；有第二 bearer 时切换 | 具体失败阶段可见；安全回退或停止；记录系统确认次数，既有网络恢复按规格处理 |
| FRAG-01 / D3 | GATT value cap 20/182/244 不对称、角色交换、乱序/重复/缺片、4096/4097 边界、迟到 callback | 同一 logical bytes/hash 重组，类型混淆拒绝；5 秒无进展超时，初次 60 秒/重连 30 秒总预算不延长 |
| PERF-01 / D3 | 每个宣称模式按下一节测量，随后至少 30 分钟压力运行 | 延迟、A/V、有效帧、PCM、队列、稳定性全部达标；任一项失败该支持配置不发布 |

这张表是执行索引，不替代规格 §25.4–25.6 的完整事务/安全向量。正式 D3 报告必须给每条规格映射到执行 case ID；暂时不能注入的故障标记 BLOCKED_TOOLING，不能记通过。

### 5.5 性能、恢复与音频的硬门槛

建链后预热 2 分钟，持续测量 10 分钟，至少注入 1000 个唯一 input sequence 边沿。采用客机同一 monotonic clock 关联触控事件与真实 presentation，不相减两台设备原始时钟；240 fps 摄像可作外部交叉验证，不替代 canonical 关联。

| 指标 | 通过条件 |
|---|---|
| DUAL 触控到画面 | p95 ≤ 80 ms；终点必须是最终成为 canonical 的画面 presentation，被回滚预测帧不能提前计入 |
| STREAM 触控到画面 | p95 ≤ 150 ms；关联 event/input_seq → authority applied_frame → video metadata → 客机 presentation |
| 样本完整性 | 所有至少 1000 个 edge 进入分母；任一无法关联、永久消失或超过 1 秒无 canonical 呈现，本轮直接失败 |
| 本机 A/V | 两台设备各自绝对误差 p95 ≤ 50 ms，不要求扬声器声学相位一致 |
| 断链冻结 | 最后认证活动后 ≤350 ms 冻结并归零本机输入 |
| 短断链恢复 | 1–29 秒恢复不丢 committed frame，无双 authority / 双音频 |
| 视频 | encoder queue ≤2 帧；所有队列有上限；guest 有效呈现 ≥声明源帧率的 92%（NTSC 至少 55 fps、PAL 至少 46 fps） |
| 音频 | 无持续 100 ms 以上欠载；DUAL committed PCM rolling digest 两端一致，STREAM PublishedAudioBlock 与 authority 源同 bytes/hash、无重复乱序 |
| 音频修正预算 | gain 前非 canonical/插入静音总时长 ≤200 ms 且 ≤总 sample 的 0.1%；单次 correction ≤200 ms；静音不能豁免统计 |
| 稳定性 | 至少 30 分钟无 crash、ANR、OOM、持续内存增长；严重热状态降视频或暂停 |

报告注明 p95 算法（建议 nearest-rank：排序后取 `ceil(0.95*N)`）、全部样本数、超时数、correction 数、测量工具精度。保存温度状态、帧时间、RSS/队列高水位、耗电起止与外接电源状态。规格未定义统一瓦数/摄氏度阈值，因此功耗和温度记录用于组合评估，不编造新的发布数字门槛。

## 6. 证据模板、排程与退出标准

每次独立运行写入 ignored `out/evidence/nearby-device/<日期>/<run-id>/`，不同修订、模式、角色或设备不得覆盖同一个目录。原始设备日志先留本地，导出前去除凭据、私有路径和个人身份内容。

```text
run-id / case-id / operator / start-end timezone
source revision / dirty state / VERSION / platform package SHA-256
device A/B alias / model / OS / display / battery / thermal / codec / backend
fixture id / content SHA-256 / source timing
inviter / network owner / QUIC listener / authority / P1-P2 mapping
pair route / certified bearer / exact plan hash / system confirmation count
mode / capability decision / input delay D / timeline epoch
steps / expected / actual / PASS|FAIL|BLOCKED_IMPLEMENTATION|BLOCKED_ENV|BLOCKED_TOOLING
sample count / missing-timeout count / p50-p95-max / A-V / PCM corrections
freeze time / recovery committed-frame delta / queue high-water / crash-memory data
logs / videos / screenshots / JUnit-Hypium-xcresult / issue reference
```

执行排程按进入条件推进，不承诺未实现能力的完成日期：

1. **基线恢复批次：**执行 F1/F2/F5，再处理 F3/F4，形成当前 SHA 的可信平台报告。
2. **D0 批次：**三端实机页面、阻塞态、单机回归；可独立于联机后端进行。
3. **D1/D2 批次：**先选实际可用的一对实体机验证传输与产品纵向链路，两种证据分开；任何认证或同步缺失都停止相应正向用例。
4. **D3 批次：**展开九方向、至少 18 座位配置及额外硬件方向交换，再做安全、恢复、30 分钟稳定性与支持项复测。

D3 不可只抽一个方向做性能测试后覆盖全矩阵。保守按每个角色配置每个宣称模式各一轮 2+10 分钟基准，再单独一轮 30 分钟稳定性估算：18×2×42 分钟约 **25.2 小时纯运行时间**，尚未包含准备、额外机型方向、bearer、故障注入和失败重测。此估算是资源规划，不是规格新增次数要求。

只有本轮所有必需门禁通过、未执行项明确且不在宣称范围、每条支持组合均有实机证据时，才能发布对应支持项。现阶段可以交付“测试失败修复”和“联机 UI 阶段验收”，不能对外宣称三端多人联机首版已完成。

## 7. 本文档交付核对

- [x] 固定 main SHA 与版本，区分本地引用和远端状态。
- [x] 列出当前源码边界并纠正过时交接描述。
- [x] 复现两项确定性失败，记录最小修复方案；未修改产品或测试代码。
- [x] 区分历史失败、环境阻塞、未运行、跳过与真实通过。
- [x] 提供阶段门禁、设备/角色矩阵、操作用例、量化指标、证据模板和排程。
- [ ] 后续实现修复、完整平台回归、实际双机测试：按用户本轮“文档即可”要求，均未执行。
