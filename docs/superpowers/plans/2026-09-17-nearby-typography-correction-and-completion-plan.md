# 三端字体纠偏与基本功能补齐执行计划

> For agentic workers: 使用 executing-plans 逐任务执行；用户自行启动并行会话，本文件不授权自动启动子代理。用 checkbox 记录证据，不因源码存在而勾选完成。

**Goal:** 按原始交互稿恢复字号层级及缩放，修复当前回归，并继续交付非串流、双端本地运行的可玩 DUAL。

**Architecture:** 原稿是视觉权威；共享文字角色合同、各端原生字体适配与原生测量。复用已有 V2/Quinn/NES，功能任务继续使用现有 ENG/UX 编号，不另造连接状态或重写通信栈。

**Tech Stack:** ArkUI/ArkTS、Android XML/Java/Material、SwiftUI/ObjC++、C++17/NestopiaUE/Quinn。

依据：[本次审计](../../audits/2026-09-17-nearby-typography-and-function-audit.md)、[原稿](../specs/assets/nearby-ui-parity-review.html)、[UX细卡](2026-09-17-nearby-ux-restoration-task-cards.md)、[ENG细卡](2026-09-17-nearby-playable-engine-task-cards.md)。本计划新增的是字体及新回归工作包，不替换旧卡的验收标准。

## 0. 执行边界和任务状态

- 产品基点 W0：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`，检查时 HEAD `6ec6605` 加 dirty UI；开工重读状态并与审计指纹比较。本文代码路径均相对执行 worktree，不是让执行者在 main 改产品。
- 本文新增任务均**未实施**。ENG-01 有基线提交，但其失败测试未闭环；不能因台账写“完成”而跳过红灯。
- 真机不执行；模拟器/host可继续。没有Mac或兼容模拟器时该端写 BLOCKED/NOT_RUN，禁止拿源码审查充作视觉PASS。
- STREAM、ROM传输、自动恢复、局中迁移后置；断链冻结、终止、双确认、真实P1/P2输入不是可删减项。
- 余量低于25%时立即停当前及队列，不自动消耗重置信用。不开自动并行；独立worktree只能由用户启动并使用仓库脚本。
- 原始设计不修改；不全局缩小字体、不关闭系统字体放大、不全局替换Material主题、不更换全应用字体或大厅风格。

## 1. 文件归属与共享边界

| 任务 | 写入范围（新建明确标注） | 不得顺带修改 |
|---|---|---|
| TYPO-00.S | 新建 `shared/schema/nearby_typography_v1.json`；新建 `tools/quality/check_nearby_typography.py`、`tools/quality/tests/test_nearby_typography.py`；按需为现有原生证据校验追加字段 | wire/ABI、原稿、其他worker页面 |
| TYPO-01.H | 新建 `harmony/entry/src/main/ets/ui/NearbyTypography.ets`；`pages/NearbyFriends.ets`、`NearbyPairing.ets`、`NearbyLobby.ets`、`NearbyFriendsManage.ets`；`entryability/EntryAbility.ets` | GameCenter全局风格、native engine |
| TYPO-02.A | 新建 `app/src/main/res/values/nearby_typography.xml`；四个 `activity_nearby_{friends,pairing,lobby,friends_manage}.xml`、`view_nearby_lobby_row.xml`；对应Activity仅布局适配 | 全局主题字号、非Nearby页面 |
| TYPO-03.I | 新建 `ios/app/NearbyTypography.swift` 并注册构建；`NearbyFriendsView.swift`、`NearbyPairingView.swift`、`NearbyLobbyView.swift`、`NearbyFriendsManageView.swift` | Catalog全局风格、bridge engine |
| TYPO-04.A/H/I | 现有 HOME 组文件（见UX责任表）和该端测试，仅已证实的Nearby增量回归 | 无证据的原大厅全面改版 |
| REG-01.A / REG-02.I | Android PAIR；iOS ENTRY/PAIR；各端UI测试 | 协议和加密实现 |
| TYPO-05.S/A/H/I | 原生证据导出、quality测试、验收台账 | 阈值放宽、截图伪造、删除旧负例 |

原生测试继续归已有 UX 卡：Android `app/src/androidTest/java/com/flynes/emu/ui/NearbyUxRestorationTest.java`；Harmony `harmony/entry/src/ohosTest/ets/test/NearbyUxRestoration.test.ets`（注册 `List.test.ets`）；iOS `ios/tests/NearbyUxRestorationTests.mm`（注册 `ios/tests/CMakeLists.txt`）。若文件尚未创建则本任务创建；先查已有实现，不能并行新建两个同名测试。

字体worker与页面恢复worker会改同一文件：**同一平台串行或合并为同一负责人**。共享合同/quality由集成人唯一写入。产品提交前按仓库hook管理版本，仅提交本任务文件及hook要求的版本同步文件。

## 2. TYPO-00.S：冻结文字角色与失败断言

依赖：先读获批UX和本次审计；独立于后端，可先交付。只确定原稿已有值，不引入新视觉语言。

- [ ] 从原稿将以下表写入新 `nearby_typography_v1.json`，逐项附CSS选择器、默认权重和适用节点；标题/辅助说明/按钮分别命名，禁止一个body token套全部。
- [ ] 新quality单测先构造错误数据：subtitle14、headline24、button16、code字距.12；分别必须FAIL。字体文件不存在也FAIL。
- [ ] 再写最小校验逻辑；验证数值和单位/权重齐全，校验每端实际测量记录，不接受仅把JSON常量抄到结果。
- [ ] 交付冻结文件和测试后，三平台才开始消费。新Swift源/ArkTS helper/XML资源必须被产品真实引用，不能只有测试读取。

| role | 默认size | weight | 原稿lineHeight | tracking | 适用 |
|---|---:|---:|---:|---:|---|
| pageTitle | 18 | 600 | 23.4 | 0 | Nearby h2 |
| paneTitle | 21 | 600 | 27.3 | 0 | 左侧h3 |
| sectionTitle | 15 | 600 | 21.75 | 0 | h4 |
| body | 14 | 400 | 20.3 | 0 | 普通标签/正文 |
| muted | 12 | 400 | 17.4 | 0 | 辅助说明/页脚/原因 |
| action | 14 | 400 | 20.3 | 0 | 普通按钮/页签 |
| primaryAction | 14 | 600 | 20.3 | 0 | 主按钮 |
| kicker | 11 | 400 | 15.95 | .13em | 上方小标签 |
| inviteCode | 29 | 600 | 40.6 | .17em | 邀请码 |
| codeInput | 28 | 400 | 40.6 | .16em | 输入码 |

数值是CSS目标行盒；原生lineSpacing、fontPadding、baseline、tracking API不是同义参数，适配后验证实际行盒/容器，不机械把所有API设为同一数字。字形轮廓不做跨系统逐像素相等要求。原稿某节点有局部覆盖时按原选择器记录覆盖，不猜默认权重。

建议测量记录新增字段（新schema，不宣称当前已支持）：

```json
{
  "elementId": "nearby_entry_subtitle",
  "role": "muted",
  "baseFontSize": 12,
  "effectiveFontSizeLogical": 12,
  "weight": 400,
  "scaleMode": "system",
  "requestedFontScale": 1,
  "appliedFontScale": 1,
  "clipped": false
}
```

上面仅是格式示例，不是验收证据。真实输出还须记录平台、系统版本、density、显示缩放、Dynamic Type类别（iOS）、源码指纹、包hash、尺寸/安全区、字体族、原生bounds与screenshotPath。

测试命令（新建后）：`python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py`。预期先红后绿；同时 `python tools/quality/check_nearby_ui_contract.py .` 保持通过。

## 3. TYPO-01.H：鸿蒙分角色恢复，修正缩放入口

对应F01/F04/F05，纳入UX-01.H、UX-04–07.H。先做本卡可直接改善用户感知的问题。

- [ ] 新建失败用例 `TYPO_H_N00_roles`：原生N00主标题21/600、说明12、按钮14；现状说明14/按钮16必须失败。
- [ ] 用统一helper替换Nearby散落数字：主标题Medium改600、说明14改12、按钮/页签16改14；邀请码29保持不变，恢复600及.17em等效字距；不改图标字号充当文字。
- [ ] 新增 `TYPO_H_scale13_applied`：debug传1.3，读取实际config及文本测量证明应用；当前忽略必须先失败。允许集合至少含1/1.3/2，保留现有1.5不影响兼容；release不得接受测试覆盖。
- [ ] 新增 `TYPO_H_scale2_reachable`：中文/英文、640×360、2倍，长原因、主按钮和返回均可完整读取/滚动到并触发；禁止以省略号截掉必要说明或缩字体通过。
- [ ] 修复固定48高和maxLines引起的实际裁切：48为基线最小点击高，大字允许增长；必要内容垂直滚动。确认切系统字号后布局也更新，不只文本更新。
- [ ] 跑host、HAP构建和模拟器Hypium；每组导出截图/实际字号/可用区。真实设备安装不做。

验收：默认角色与冻结合同一致；1→1.3→2没有重复乘倍数；按钮至少48vp且无文字裁切；1.3证据不能实际仍是1。此卡不宣称完整Nearby功能已可用。

## 4. TYPO-02.A：Android局部样式与窄屏布局

对应F02/F05；纳入UX-01.A、UX-04–07.A。

- [ ] `TYPO_A_headline_resolves21` 读取真实TextView尺寸/lineHeight；当前24sp应失败。同时断言说明12、按钮14、邀请码29。
- [ ] 建立Nearby专用TextAppearance，显式绑定角色，覆盖默认HeadlineSmall/Medium；不修改应用全局textAppearance。例：paneTitle基值21sp、权重600、目标行盒27.3逻辑单位，测量后处理fontPadding差异。
- [ ] `TYPO_A_pair_width320` 先证明当前右栏不足：按内容宽<=580切纵向，>580才保留原稿左右结构。断点使用扣安全区后的内容宽，不用设备型号/物理像素。
- [ ] 测320/580/640宽，QR框可完整容纳；输入区/底栏不横向溢出；普通内容不添加横向滚动“绕过”问题。
- [ ] 在真实系统字体设置下检查1/1.3/2与非线性放大实际输出；固定高48/60改为满足原稿基线并可容纳大字的最小高。恢复字号不能降低点击面积。
- [ ] 单元、构建、模拟器instrumentation先红后绿；保留dependency版本，不为了调字号升级/降级Material。

验收：不再继承24sp标题；字体与行盒有原生读取证据；320宽布局可用；与REG-01.A串行避免覆盖PAIR。

## 5. TYPO-03.I：iOS统一动态字体及可达性

对应F03/F05；纳入UX-01.I、UX-04–07.I。

- [ ] `TYPO_I_roles_and_scaling` 在N00与N01使用同一角色测量：默认标题21/600、辅助12；提高Dynamic Type后两页均响应，不出现N00增大但N01固定。
- [ ] 用NearbyTypography封装基值及Dynamic Type参照。自定义点数通过ScaledMetric/UIFontMetrics等原生方式适配；不将所有页面改成固定 `.system(size:)`，也不只替换N00。
- [ ] `TYPO_I_wide_scroll`：640×360、大字、英文，末项操作可达；宽分支也有合法滚动/自适应路径，底栏不遮正文。
- [ ] `TYPO_I_single_header`：实际导航栈只有一套原稿标题/返回，不把系统bar和自绘bar重复堆叠；保持系统返回语义。
- [ ] 窄屏取消左子视图固定224限制；底栏按可用宽度重排并保留按钮顺序/状态；恢复原稿页签外观，避免直接把segmented外观算一致。
- [ ] Mac新鲜构建后跑官方simulator runner和截图；Windows无法跑时写未验证，不沿用旧截图。

验收：角色定义一致、页面间缩放策略一致、主操作在大字横屏可达。不要宣称某个Dynamic Type类别天然就是1.3倍，按第8节双轨记录。

## 6. REG-01.A / REG-02.I：先消除已确认的交互回归

每一行作为独立小提交/检查点；先保留失败断言，再修产品。不要为消除测试红灯删除验收断言。

| 子任务 | 文件 | 可执行测试向量与完成标准 |
|---|---|---|
| REG-01.A.1 | Android PAIR XML/Activity | 粘贴`1234567`，原文不被截成6位；显示格式原因；submit disabled；桥接请求计数0 |
| REG-01.A.2 | Android PAIR测试 | `12345`/空/`12A456`均不发送；`123456`只发送一次；既有归一化允许的输入按C05处理，不能自行改规则 |
| REG-02.I.1 | iOS PAIR footer/submitJoin | 5位时disabled；6位提交后立即disabled；连续触发两次仅一个attempt；失败/取消后按真实快照恢复 |
| REG-02.I.2 | iOS PAIR footerAction | scan取消回N00；create取消先撤销对应generation再退出；迟到回调不能重新显示旧邀请 |
| REG-02.I.3 | iOS ENTRY rightPane | devices工具按钮只触发发现/不可用说明，不导航好友管理；friends工具按钮才进入好友管理 |
| REG-03.A/H/I | 三端PAIR与文案 | QR/相机未接入时不显示伪成功、不暗示正在扫描；有明确原因和输入码替代；相机拒绝不阻塞码路径 |

QR/相机真实能力属于后续独立卡，本轮先把占位状态讲清楚。页面结构仍按原稿保留，不能删掉入口使矩阵“通过”。

## 7. TYPO-04.A/H/I：补查G00与剩余Nearby页面

- [ ] 按UX责任表分别检查HOME/CONFIG/FRIENDS/PLAY文字节点，列出实际角色/基值/缩放来源；不能只查N00就宣布全端字号正确。
- [ ] G00先读取原大厅compact/large策略与获批约束，将历史适配和新Nearby改动分开记录；只修已证实的偏差。保留分类、卡片、详情、过滤的位置和状态。
- [ ] N09仍按UX-12/13恢复“游戏、主机、座位、确认”首屏，技术参数收进详情；应用本次文字角色，不继续美化平铺诊断表。
- [ ] N04–N10的等待/接受/SAS/失败/断线原因同样使用文字角色；真实后端未接入时可做明确fixture布局测试，但不得把fixture当L3。

验收：角色盘点覆盖G00、N00–N12和首版弹层；后置功能标不可用但布局不伪成功。G00范围有设计歧义时停该子项确认，不因此阻塞Nearby已确定的12/14/21修复。

## 8. TYPO-05：字体、几何、系统设置三项分别验收

### 8.1 先跑的小矩阵

N00、N01、N02、N03、N09、G00；中文与英文；640×360、736×414、844×390；1/1.3/2；再补320和580断点。此矩阵是快速回归，不替代完整C01–C18。

双轨证据：

1. **合同对照**：相同逻辑可用区、语言、受控字体比例；记录如何施加及测量。若采用debug测试比例适配，明确`scaleMode=test-controlled`，release不可用，不能混作系统结果。
2. **系统可读性**：保持真实OS字体设置、原生缩放曲线，记录实际字号与iOS类别；断言不裁切、可读、可操作、布局能重排。不得造一个`fontScale=1.3`标签而未测应用结果。

完整C17宽度：320/375/550/580/640/736/900/1024；C18尺寸与比例按既有quality定义。跨端容器几何仍≤2逻辑单位；字体族可不同。缺平台、缺配置、缺截图为未通过/未运行，不能降低阈值。现有checker未区分双轨时由S负责人追加分组/校验，不能把不同轨记录混组。

- [ ] quality负例：缺一端截图FAIL、1.3请求实际1FAIL、一个角色未测FAIL、三个端复制同一fixture常量不能当原生测量、溢出FAIL。
- [ ] 原生导出字段由布局/字体读取获得；源码contract只是期望值，不是actual。
- [ ] 证据存 ignored `out/evidence/nearby-typography/`；报告写源SHA+dirty指纹、包hash、环境、命令、测试数量、失败详情、截图路径。
- [ ] 运行现有parity checker；因完整后置页面缺证据而失败时如实保留，同时可单独报告首版子集，不冒充完整验收通过。

### 8.2 验证命令与环境

以下在对应执行worktree运行，依照 [DEVELOPMENT](../../DEVELOPMENT.md)。连接设备必须确认为模拟器；本计划不授权物理设备安装。

```powershell
python tools/quality/check_nearby_ui_contract.py .
python -m unittest discover -s tools/quality/tests -p test_nearby_typography.py
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"
python tools/quality/check_nearby_ui_parity.py out/evidence/nearby-typography/native
```

Harmony：按DEVELOPMENT第6节构建entry/ohosTest HAP，跑host CTest和**模拟器**Hypium；注册新测试到List。构建HAP成功不等于Hypium通过。iOS：在Mac先`cmake --build build/ios-simulator --config Debug`，再`python3 ios/scripts/run_simulator_tests.py <SIMULATOR_UDID> FlyNESUITests`；UDID须取实际模拟器，不执行物理安装。新增测试未被发现、0 tests、使用旧bundle均不算PASS。

## 9. 功能继续补齐：不扩大首版范围

以下引用已有详细文件和验收卡，不重新派已合入的W1/W2。每一批结束登记实际状态再进入下一批；字体卡可与不改同文件的共享engine工作并行。

| 顺序 / 原任务 | 当前状态 | 下一步唯一目标 / 放行标准 |
|---|---|---|
| GATE-0（ENG-01补遗、ENG-10前置） | dual_mvp冻结5断言失败 | 先复现300ms无对端活动/30秒deadline，区分fixture时钟和engine根因；修后负例仍有意义，目标测试通过；PCM构建与ZIP fixture分别诊断，不删测试 |
| UX-00.S + ENG-02 | 台账未开始 | 页面/动作/快照与最小wire缺口冻结；不能让平台用本地bool填已连接/已确认 |
| ENG-05、06 | 未验收 | 同ROM/core/profile/options/seat/authority/epoch/revision；仅A确认不步进，变配置撤销旧确认，双端ready才启动 |
| ENG-08 + ENG-09 | 未验收 | 真NES双实例与真实Quinn双engine分项测试；600帧假runtime不复用为真核心证据；P1/P2分别影响真实状态 |
| ENG-03.A、04.1–5.A | 未接入完整owner | Android码加入→房主接受→SAS→真实link，生命周期只有一个V2 owner；未支持的无线能力明确边界 |
| ENG-10、11.A、12/M0 | 未验收 | 两个原生实例显示/听到本地核心输出，两端可操作；暂停继续、断线冻结、结束留连接、再次开局均通过 |
| ENG-07、04.6 + UX-08–15、17 | 未验收 | M1恢复开局前主机/座位选择、认证后好友保存、N09首屏和完整基本状态，不能永久保留M0只读默认值 |
| ENG-03/04/11.H/I + UX对应卡 | 未验收 | Harmony/iOS推广同一生产链路与原稿；跨端非真机条件可验证的组合逐项给证据，无硬件能力标阻塞 |
| ENG-12/M2 + TYPO-05/UX-18 | 未验收 | 三端非真机矩阵通过；真实无线/时延/温升等单独留待真机，不写完整端到端真机PASS |

GATE-0复现（现有W0构建目录）：

```powershell
wsl -d Ubuntu-24.04 -- bash -lc 'cd /mnt/e/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes && cmake --build out/nearby-playable/shared-linux --target flynes_two_engine_dual_mvp_test --parallel 2 && ctest --test-dir out/nearby-playable/shared-linux -R "^flynes_two_engine_dual_mvp$" --output-on-failure --no-tests=error'
```

预期当前为FAIL；修复后必须1/1通过并记录日志，再跑相关回归。不能仅将300ms阈值放宽到测试通过，也不能用本地发送刷新对端活性伪装连接仍健康。

## 10. 交付与防跑偏检查

- [ ] 每卡交付：改动文件、失败断言→通过证据、未跑项、源指纹、下一卡；未实施卡保持空框。
- [ ] 主标题21/说明12/按钮14不是经验值，是原稿角色；禁止整体缩放页面来“看起来像”。
- [ ] 无线权限/相机/好友存储缺能力时有真实原因；没有可点击但不做事的占位主操作。
- [ ] UX原生视觉、L1假runtime、L2真核心/网络、L3原生可玩、L4真机分别记录，不互相顶替。
- [ ] 不给无依据的完成百分比/工期；以M0/M1/M2门禁和红灯数量报告进展。
- [ ] 本轮文档编写不授权代码修改、合并、提交或新建worktree；执行由用户指定卡号启动。
