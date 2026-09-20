# Nearby 原稿 UX 恢复：逐页执行卡

> For agentic workers: 使用 executing-plans。一次认领一个任务 ID 和一个平台，不启动其他代理；实施前 TDD，完成后提交该卡要求的证据。用户自行安排并行。

**Goal:** 按获批交互稿恢复三端页面及动作，不以“先能玩”为由重设计。

**Architecture:** 原稿决定布局/顺序，共享 session 决定状态/授权；平台只做原生展示与适配。页面恢复与真实后端接线分别验收。

**Tech Stack:** Android XML/Java、ArkUI、SwiftUI、共享 C++ 状态投影。

日期：2026-09-17。本文细化并约束[总计划](2026-09-17-nearby-playable-mvp-correction-plan.md) T1/T5/T7/T8；后端任务见[可玩链路执行卡](2026-09-17-nearby-playable-engine-task-cards.md)。遇到旧计划与本卡不同，以本卡明确的恢复要求为准；不得改变获批设计。

## 1. 防止再次跑偏的规则

1. 权威顺序：用户明确后续决定 → 获批 UX 文档 → 原稿非演示页面 → 本执行卡。原稿模拟控件和模拟状态不得照搬成产品认证；例如 HTML 输入 maxlength 不能代替文档要求的“7 位粘贴拒绝，不能静默截断”。
2. 首先读[原稿](../specs/assets/nearby-ui-parity-review.html)和[获批文档](../specs/2026-09-13-nearby-ui-parity-design.md)，用其内容验收实现。禁止改原稿、合同或截图基线来迁就当前页面。
3. 每卡只允许修改列出的文件组。需要其他产品目录、公开 ABI、协议、安全规则时移交集成人，不在平台任务顺手扩展。
4. 原生同一页面文件的卡必须串行；A/H/I 三个平台可各自并行。UI fixture 只进测试目标；发布构建不能启用“模拟对方接受/确认/连接成功”。
5. “源码看起来符合”“测试编译通过”“字符串齐全”均不能记页面 PASS。必须有原生截图、控件树/几何和动作断言。
6. 每卡结论分 `布局验收`、`真实接线验收`。前者 fixture 通过不能自动通过后者。后端未就绪不伪造数据，不删除原页面结构。
7. 不新增页面、步骤、默认弹窗、强制模式开关；不把 details 放回首屏；不以平台惯例替换双栏或按钮次序。
8. 功能暂不可用只改变该动作 enabled/reason，保持原位置和目的。不能把扫码入口路由到输入码页并仍称“扫码加入”。
9. 开局前选主机/座位属于 N09 原稿；游戏中主机迁移才是延期复杂能力。首个内部可玩样本允许只测默认角色，但不能把固定角色提交为原设计最终行为。
10. 未获批页面设计变更单列提案，不能混入恢复提交；本卡不要求反复确认常规实现选择。

本次依据文件 SHA-256：HTML `C69FF8F5CC3A346071DBF2BC6E0F2291232ECB6732A8F38970FFCE1126577C16`；获批文档工作副本 `00ED879C297CF81EBDFB0327D8B8474722318A289DD9F3511B2AC4ABC82D0E86`。开工若不同，先核对 diff，不能误用另一份设计。

## 2. 当前状态与任务标记

main=`5389585`；W0 HEAD=`c7bcd79` 且 dirty。W1/W2 已集成，W3 tip=`5e8e73f` 尚未全部集成。当前没有本次原生 UI 重验结果，本文件所有验收默认 NOT_RUN。

本次再次读取发现 W0 iOS `NearbyFriendsView.swift` 已出现 `nearby.entry.headline`，此前 N00 的 `PairingStage`/额外扫码/配对快捷块已不在该文件。Android/Harmony 也有 N00 恢复修改。**这些卡要求复核并补齐，不要求回退后重写。** 审计中关于旧 N00 的反例属于更早读取时点。

N01/N02/N03 仍有明确反例：Android MODE_SCAN 显示 `nearby_join_block`；iOS `List` 同时追加 `stagesSection/codeSection/wifiSection`。N09 仍有字段平铺与永久禁用确认的旧测试。文件状态持续变化，任务开始时要保存所读版本。

任务格式：`UX-03.A` 表示 UX-03 的 Android 子任务；`.H` 为 Harmony、`.I` 为 iOS；`.S` 为共享合同。每卡按如下步骤执行并逐项勾选：

- [ ] 保存当前页面截图/结构或明确构建阻塞；核对当前源码而非仅旧报告。
- [ ] 将该卡断言加入原生测试；先运行并记录实际失败。若全部已满足则只登记证据，不造无意义改动。
- [ ] 仅修此卡允许的差异。
- [ ] 重跑此卡断言和受影响回归，保存 after 截图/控件树。
- [ ] 填写证据记录，审阅 diff 是否改了其他页面/流程；再按 repo hook 提交范围内文件。

## 3. 文件责任表

以下为执行 worktree 内相对路径，A/H/I 标号是准确的文件组别；“新建”表示计划新增，不宣称仓库已有。

| 组 | Android A | Harmony H | iOS I |
|---|---|---|---|
| HOME | `app/src/main/java/com/flynes/emu/HomeActivity.java`；`app/src/main/res/layout/activity_home.xml` | `harmony/entry/src/main/ets/pages/GameCenter.ets` | `ios/app/CatalogLibraryView.swift` |
| ENTRY | `app/src/main/java/com/flynes/emu/NearbyFriendsActivity.java`；`app/src/main/res/layout/activity_nearby_friends.xml` | `harmony/entry/src/main/ets/pages/NearbyFriends.ets` | `ios/app/NearbyFriendsView.swift` |
| PAIR | `app/src/main/java/com/flynes/emu/NearbyPairingActivity.java`；`app/src/main/res/layout/activity_nearby_pairing.xml` | `harmony/entry/src/main/ets/pages/NearbyPairing.ets` | `ios/app/NearbyPairingView.swift` |
| CONFIG | `app/src/main/java/com/flynes/emu/NearbyLobbyActivity.java`；`app/src/main/res/layout/activity_nearby_lobby.xml`；`app/src/main/res/layout/view_nearby_lobby_row.xml` | `harmony/entry/src/main/ets/pages/NearbyLobby.ets` | `ios/app/NearbyLobbyView.swift` |
| FRIENDS | `app/src/main/java/com/flynes/emu/NearbyFriendsManageActivity.java`；`app/src/main/res/layout/activity_nearby_friends_manage.xml` | `harmony/entry/src/main/ets/pages/NearbyFriendsManage.ets` | `ios/app/NearbyFriendsManageView.swift` |
| PLAY | `app/src/main/java/com/flynes/emu/MainActivity.java`；`app/src/main/java/com/flynes/emu/NearbyInGameStatus.java` | `harmony/entry/src/main/ets/pages/RunGame.ets` | `ios/app/RunGameView.swift` |
| TEXT | `app/src/main/res/values/strings.xml`；`app/src/main/res/values-zh-rCN/strings.xml` | `harmony/entry/src/main/resources/base/element/string.json`；`harmony/entry/src/main/resources/zh_CN/element/string.json` | `ios/app/en.lproj/Localizable.strings`；`ios/app/zh-Hans.lproj/Localizable.strings` |

共享 S：`shared/schema/nearby_ui_v1.json`、`shared/include/flynes/product/nearby_ui_state.hpp`、`shared/src/product/nearby_ui_state.cpp`、`shared/tests/fixtures/nearby_ui_v1/cases.json`、`shared/tests/test_product_nearby_ui.cpp`。仅集成人写 S；平台新增文案向 S 提交 key/译文清单。

新建每平台页面测试文件：A `app/src/androidTest/java/com/flynes/emu/ui/NearbyUxRestorationTest.java`；H `harmony/entry/src/ohosTest/ets/test/NearbyUxRestoration.test.ets`（注册到该目录 `List.test.ets`）；I `ios/tests/NearbyUxRestorationTests.mm`（注册到现有测试构建）。这三个文件覆盖下列卡，测试名字用卡号+断言名，不伪称这些目标目前已注册。

## 4. 逐页任务卡

### UX-00.S：页面状态合同及导航表

依赖：总计划 T0；文件：S。一次只交付 screen/action 投影，不实现平台 provider。

**具体改动：** 将 N00–N12/G00 和弹层分别建语义 fixture；同一 PAIR 容器可承载多个 screen，不强制创建多个 Activity。screen 来自 session 阶段+明确本地路由，不能把所有模块同时可见。

**断言：**

```text
N00.create → N01          N00.enterCode → N02       N00.scan → N03
N02.submit accepted → N04 joiner / N05 host（异步结果前不预跳成功）
N05.accept → N06(code/candidate) 或 N07(QR 验签路径)
N06.localConfirmOnly → N06 waiting
全部 link 校验成功 → N08 → 用户点击去大厅 → G00 connected
G00.connected.select → N09；不是直接 GAME_RUNNING
N09.back → G00 connected + 清除本局确认
details.close → 原 screen，输入/邀请/连接不重建
```

拒绝/取消终止对应 attempt；N10 展示首个失败原因及合法恢复。正常返回与 CANCEL 不能混为“onDisappear 就销毁 session”。QR 尚未接线时 N03 仍独立，显示能力限制，不进入 N04。

### UX-01.A/H/I：公共页面骨架和几何

文件：ENTRY/PAIR/CONFIG；依赖 UX-00.S。以可用安全区内宽度 W 为输入，三端一致；只最外层消费安全区一次。

**具体改动：** 原稿 `.fn-header/.fn-columns/.fn-side/.fn-foot` 原生等价实现；顶栏基准 64；正文独立可滚；底栏独立测量；统一六个色令牌。不把手机系统 List/Form 当获批布局。

**验收断言：** W=580 为纵排，W=581 为双栏；双栏左轨 224、轨间 18、右侧剩余；测量时必须记录 W 是哪个容器（安全区后的页面宽），不能 Android 减两次 padding 而 iOS 不减。按钮命中≥48；主按钮最小宽 min(160,实际可用宽)、最大不越界；除原游戏网格外无横向滚动；字号 2.0 不缩字挤压。首屏内容顺序遵循后续卡，不因共用组件而挪位置。

### UX-02.A/H/I：G00 原大厅及连接入口

文件 HOME/TEXT；依赖 UX-00.S，真实接线依赖 ENG-03/04。

**保留：** 四分类“最近/收藏/全部/内置”、搜索/工具、既有 30/70 详情网格、两行横向浏览。不得新建联机专属游戏大厅或玩家侧栏。

**修改：** 原右上角可见文字消费真实状态；点击未连接→N00，连接中→当前步骤，已连接→连接概览，中断→故障状态。原详情启动按钮单人走旧路径、联机选择走 N09。

**断言：** 未连接/请求/认证显示“附近联机”；所有验证完成显示“双人联机中”；已建连接断线显示“联机中断”；显式断开后恢复“附近联机”。任何连接事件前后 category/query/selection/multiplayerOnly 不被重置；已连接选不支持游戏显示原因且不启动单人。

### UX-03.A/H/I：G00 独立双人过滤

文件 HOME；依赖 UX-02，共享 registry 行为不在此卡重写。

**修改：** 数量行右侧独立“支持双人”开关；默认 false、用户值持久化；不变第五分类、不改变排序；空结果保留当前分类并给“关闭支持双人筛选”。

**断言：** 四分类分别以同一测试集合覆盖 SUPPORTED/UNSUPPORTED/UNKNOWN×开关×查询；开时只保留 SUPPORTED 的原相对顺序；关只改开关；内置零结果不跳全部；重启恢复偏好；UNKNOWN 文案“支持情况待确认”。不得用真实内置游戏名写产品条件判断。

### UX-04.A/H/I：N00 首页

文件 ENTRY/TEXT；原稿 `nearby()`；依赖 UX-01。

**左列自上而下：** PLAY TOGETHER；“和身边的人／再来一局。”；“一人创建，另一人加入。”；“创建联机”(主)、“输入配对码”、“扫码加入”。

**右列：** “附近设备/好友”页签、对应工具（寻找/管理）；真实匿名设备或真实已存好友行/空态。设备页有“寻找设备”；好友页初始规则按实际保存数量，不记上次 tab。底部保留流程说明。

**删除的偏离：** N00 七阶段流水线、右侧重复“扫描房主二维码”、额外“打开配对”入口、工程接口待办列表。已删除则不重复改。

**断言：** 两 tab 都能看到三个左列动作；无游戏也能进入；真实设备未经认证不显示昵称/长期指纹；空好友不展示示例小林；右列不因 tab 切换换整页。寻找权限只在所需阶段申请，相机只因扫码使用申请。

### UX-05.A/H/I：N01 邀请页

文件 PAIR/TEXT；原稿 `screen==='invite'`；依赖 UX-01、真实接线 ENG-04。

**布局/动作：** 左列“让朋友加入”、说明、“本次配对码”、六位码、真实剩余时间、“重新生成邀请”；右列同 invitation 的 QR、说明；底栏“正在等待朋友 · 尚未选择游戏”和“取消邀请”。标题“创建联机”。重生成弹层解释旧码/QR 失效，取消弹层不更改 invitation。

**断言：** 不混入身份校验码、Wi-Fi 按钮或阶段表；倒计时基于 shared continuous deadline，退后台不延长；重生成/取消后旧代迟到结果无效；QR 未交付区域显示“扫码加入暂不可用，可使用配对码”，不画可误认真的伪 QR；正式码绝不是原稿 `482619` 常量。码过期能看到新邀请动作。

### UX-06.A/H/I：N02 输入码页

文件 PAIR/TEXT；原稿 `joincode`；依赖 UX-01、ENG-04。

**布局/动作：** 左列提示朋友创建及“改用扫码加入”；右列“六位配对码”、输入框、紧邻字段错误、用途说明；底栏“尚未认证”与“请求加入”。无 SAS 输入和 Wi-Fi 模块。

**断言数据：** `012345` 保留前导 0；` 012345 ` 提交规范化为 `012345`；空、5 位、7 位、`12a345`、全角数字不发网络请求；粘贴 7 位不得截成可提交 6 位；未完成输入不每键报错；提交中重复点击请求计数不增；错误区分错码/过期/失效/未发现，不展示未验证身份。键盘开启后输入、错误、提交可达。

### UX-07.A/H/I：N03 扫码页面（布局必做、相机能力独立）

文件 PAIR/TEXT；原稿 `scan`；依赖 UX-01。

**修改：** 创建真正独立 scan screen：左列“扫描房主的二维码”、创建说明、“改用输入配对码”；右列取景区域/权限或能力说明；底栏取消。Android 不再以 MODE_SCAN 显示 join block，iOS 不再 `.scan || .joinCode` 共享整页表单。

**断言：** 点扫码先到 N03；改用码才到 N02；相机拒绝不禁用码路径；取消返回 N00 且停止相机；没有生产相机 provider 时不模拟解码、不声称请求已发。布局 PASS 不等于 C08 功能 PASS；后置 QR 全链仍单列。

### UX-08.A/H/I：N04/N05 等待和房主许可

文件 PAIR/TEXT；原稿 `waiting/request`；依赖 UX-00/ENG-04。

**N04：** 左“等待房主接受”和入口来源；右“加入请求已发出”、另一设备操作说明、匿名提示；底栏“取消请求”。只在提交真实接受后出现。

**N05：** 左“附近设备请求加入”、码/QR/候选来源及未验证提示；右确认邀请说明，“拒绝”在前、“接受，继续验证”在后；底栏匿名请求说明。

**断言：** 没有假好友名；接受只授权本次 attempt，不启动游戏、不接收文件；拒绝/取消后两端结束 attempt，旧接受事件不能复活；房主仅展开页面绝不自动接受；窄屏并列操作换行/堆叠不遮挡。

### UX-09.A/H/I：N06 身份校验

文件 PAIR/TEXT；原稿 `sas`；依赖 ENG-04。

**修改：** 双端显示 shared 提供的同一六位“身份校验码”，明确与邀请配对码不同；动作“数字不一致，取消”及底栏主动作“身份校验码一致”。本端确认后改为“你已确认，等待另一端确认。”和禁用等待动作。

**断言：** SAS 不可手输；一端确认仍留 N06，未连接；任一端拒绝当前 generation 失效；两端确认只允许继续 N07，不直接 N08；合法 QR 不强插 SAS（其签名握手仍验证）。

### UX-10.A/H/I：N07 建网与连接详情

文件 PAIR/TEXT；原稿 `network/diagnostics`；依赖 ENG-04/ENG-09。

**首屏：** “正在建立连接”、路线对应说明、“连接详情”；右列用户可理解的已接受/验证身份与 Wi-Fi/安全通道及兼容性阶段；底栏取消。

**详情：** 顺序固定 权限→发现→认证→Wi-Fi→QUIC→版本→codec。共享状态决定 passed/current/not started；不为 DUAL 去执行 STREAM codec 协商，非适用项明确说明，不伪造硬件门禁通过。

**断言：** 连接中大厅仍“附近联机”；Wi-Fi 系统拒绝同 session 不循环弹窗；详情关闭不撤销/重建连接；取消后迟到成功不进 N08；所有阶段不能同时红。

### UX-11.A/H/I：N08 已连接和连接概览

文件 PAIR/HOME/TEXT；原稿 `connected/connection/disconnect`；依赖 ENG-04。

**修改：** 已验证对端真实名字/身份短码（无备注用真实设备通用显示名）、本机/对端两行、连接通过、尚未选游戏；按钮“断开连接”；底栏“去游戏大厅选游戏”。只有持久保存成功才能说“好友已保存”。

**断言：** 不是自动进入游戏/配置；去大厅保留 session 和筛选；点断开确认层含“保持连接/断开连接”，前者无副作用；断开生效后双方状态终止且本机回单人入口；未收到对端确认/网络异常时不得伪称两端已完成断开。

### UX-12.A/H/I：N09 本局确认首屏

文件 CONFIG/TEXT；原稿 `config`；依赖 UX-01、ENG-05/06/07。

**左列：** 游戏名→待确认配置短码→文件状态卡→“游戏与连接详情”（缺文件时原对应补齐入口）→“本机游戏声音”。

**右列：** “本局主机”候选选择/具体不可用原因→“我的座位”P1/P2与对端座位→只读“自动运行方式：双端运行 DUAL”/不可用原因→双方确认状态及配置变化提示。

**底栏：** “返回大厅换游戏会保留连接”与本端唯一“确认入局”。本端确认后“等待另一端确认”，不再给第二个“双方确认”按钮。

**移入详情：** network owner、完整 ROM 身份、profile 验证、候选资源风险明细、传输细节；不可继续平铺 14 个 blocked row 当首屏。

**断言：** 改主机/座位/游戏/能力计划→双端旧确认撤销、同一新短码；声音只本机改变，不影响对方声音/开局确认；缺文件禁止开局但不自动传输/断开；支持双人≠任意设备可开局；返回大厅清确认保连接。上层 UI 不能直接写 START_DUAL 绕过授权。

### UX-13.A/H/I：N09 游戏与连接详情

文件 CONFIG/TEXT；原稿 `content-details`；依赖 UX-12。

**修改：** 用原稿详情层承载游戏身份、两端文件状态、网络提供设备（与主机独立）、已验证好友短码、profile/运行方式说明和必要风险。补齐文件动作可达但未交付时明确不可用，说明双方先导入同一游戏；不得出现“预览缺少文件”模拟动作。

**断言：** 默认不展开；关闭回相同 N09，配置和已确认状态不变；缺文件页不能悄悄选择 HOST_STREAM；拒绝/关闭不隐含发送/接收/导入许可。

### UX-14.A/H/I：N10 失败与中断

文件 PAIR/HOME/PLAY/TEXT；原稿 `failed` + 获批 §4.2/§5；依赖真实 reason 投影。

**修改：** 左列明确失败阶段、原因与合法恢复动作；右阶段表仅首失败突出、此前通过、此后未开始；底部回大厅。首次连接失败标题“未能建立连接”；已连后断线则“联机中断”，不要混称尚未建立。

**断言：** 认证篡改不能同请求自动重试/降级明文；Wi-Fi 拒绝只给合法新邀请/返回动作；运行断线不默默继续单人；恢复后置时不展示可点击但无效“恢复”，仍能明确结束/断开；返回大厅保留真实中断状态，不能仅因导航清掉。

### UX-15.A/H/I：游戏内暂停、继续、结束

文件 PLAY/TEXT；依据获批 §5、旧设计 D6–D10；依赖 ENG-08/10。

**修改：** 正常联机信息放既有暂停抽屉；不新增永久 HUD。冻结/断线用顶部不可忽略条，正文说明原因和真实可执行退出动作。健康连接暂停/继续接引擎帧边界，不当作断线恢复。

**断言：** 正常游戏无遮挡状态条；暂停帧不增、继续双方恢复；本机静音不静音对方；结束返回旧大厅、连接保留、下局重确认；中断条不能关闭伪装运行；没有接管/保存能力时给禁用原因，不声称已保存。

### UX-16.A/H/I：N11/N12 后置能力入口与边界

文件 CONFIG/FRIENDS/TEXT；原稿 `file-*`、`manage-friends/friend-actions/identity-reset`。

**本批只做：** 保留文件补齐和好友管理入口/标题/返回；尚未实现功能给真实原因，不伪造样例好友、传输进度或保存结果。已验证好友基本保存由 ENG-04 负责，高级改名/删除/拉黑/重置可后置。

**后续完整验收预留：** 发送/接收许可各自独立，单端同意字节数 0，接收后校验+导入仍需确认；好友操作不同授权，重置身份不能继承旧信任。当前 NOT_RUN/DEFERRED，不因入口恢复通过 C12/C15。

### UX-17.A/H/I：替换锁死旧布局的测试

文件：上述新原生测试和现有 `NearbyLobbyTest.java`、`NearbyUiParityTest.java`、`ios/tests/NearbyUiParityTests.mm`、`harmony/entry/src/ohosTest/ets/test/NearbyService.test.ets`。

**具体替换：** Android `everyLobbyFieldIsRendered` 改为首屏/详情各自内容和顺序；`eachFieldStatesWhyItIsUnavailable` 改为真实 reason 随 fixture 变化；`confirmIsOneDisabledPrimaryActionWithAReason` 分未就绪禁用、就绪可点、单端确认等待三个用例。Harmony `renders_every_lobby_field_with_its_blocked_key_and_one_confirm_control` 不再要求首屏平铺；`falls_back_to_permission_because_no_session_stage_exists_yet` 只适用于明确无 provider 的 fixture，不是产品永远权限失败。

**保留：** N07/N10 阶段顺序/首失败语义、安全未就绪禁用、取消和迟到结果隔离等正确断言。禁止批量删 Nearby 测试或降低用例数量作为验收方法。

### UX-18.S + A/H/I：矩阵与证据收口

文件：原生证据输出及 `tools/quality/check_nearby_ui_parity.py`（仅必要的缺失证据校验增强，不能放宽阈值）。

**逐页样本：** N00 两 tab；N01 活跃/过期；N02 空/合法/字段错误/提交中；N03 有权限/拒绝/能力不可用；N04；N05；N06 未确认/单端确认；N07/详情；N08/断开确认；G00 未连/已连/中断/过滤零结果；N09 双待/单端确认/配置失效/缺文件/详情；N10；N11/N12 不可用态；游戏暂停/中断。

**尺寸：** 获批 C17 宽 320/375/550/580/640/736/900/1024；C18 640×360/736×414/844×390×字体1.0/1.3/2.0；zh-CN/en；输入页键盘开/关，安全区记录一次。新增581边界样本验证分栏切换。矩阵未跑完写实际覆盖，不能写“全覆盖”。

**验收：** 每个规定 screen/state/尺寸/语言/倍率组合三端均有记录；控件 bounds 键集合必齐，缺 key 失败（现有 checker 只比较共同 key，不能利用缺键逃验收）；containerOrder/actions/reasons 一致；非网格 overflow=false；相同 bounds 偏差≤2；截图可读且关键动作无遮挡。

## 5. 测试与交付格式

在执行 worktree 运行（以下为计划命令，不是本次实跑结果）：

```powershell
git rev-parse --show-toplevel
python tools/quality/check_nearby_ui_contract.py .
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
# 先将 ANDROID_SERIAL 设置为本次已确认的模拟器；不要让 runner 自动选真机
.\gradlew.bat :app:connectedDebugAndroidTest "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.ui.NearbyUxRestorationTest"
python tools/quality/check_nearby_ui_parity.py out/evidence/nearby-ux-restoration/native
```

Harmony 原生新测试注册后用 DEVELOPMENT 的 ohosTest/Hypium 流程；现有 NearbyService 纯服务测试不能代替 ArkUI 几何。iOS 先新鲜构建，使用官方 simulator runner 跑 UI bundle。真机一律 NOT_RUN。

每卡保存到 `out/evidence/nearby-ux-restoration/<task-id>/<platform>/`：

```json
{
  "taskId": "UX-12.A",
  "sourceRevision": "记录实际完整 SHA",
  "dirtyFingerprint": "记录实际修改文件摘要",
  "screenId": "N09",
  "state": "local_confirmed_peer_pending",
  "providerMode": "fixture 或 production，按实测填写",
  "layoutResult": "NOT_RUN",
  "wiringResult": "NOT_RUN",
  "commandExitCode": null,
  "testCount": 0,
  "screenshots": [],
  "knownGaps": []
}
```

这是字段格式示例，不是证据。native parity JSON 仍使用现有 checker 的 caseId/platform/locale/contentSize/fontScale/screenId/entryText/category/multiplayerOnly/visibleIds/enabledActions/reasons/containerOrder/overflowOutsideGameGrid/bounds 结构，数据必须原生采集，不把预期 fixture 直接复制成实际结果。

每次交付一句话明确“哪页什么状态已恢复、真实接线是否通过、下一张卡是什么”。不汇报模糊整体百分比。

## 6. 获批验收用例覆盖索引

| 原用例 | 本次执行卡 | 通过边界 |
|---|---|---|
| C01 | UX-02/03 | 原大厅和可见入口 |
| C02/C03 | UX-03 | 筛选交集/持久化/零结果，不靠截图独证 |
| C04 | UX-04/05/06/07 | 三入口独立且无需先选游戏 |
| C05 | UX-06 + ENG-04.1 | 格式错误无网络请求，过期真实失败 |
| C06 | UX-08 + ENG-04 | 匿名及房主许可，无凭据提前释放 |
| C07 | UX-09 + ENG-04.5 | 双 SAS，不一致取消 |
| C08 | UX-07布局 + 后续QR功能任务 | 布局通过不代表验签/重放负例通过 |
| C09 | UX-02/10/11 + ENG-04.5/.6 | 全部校验后才连，保存按实际结果 |
| C10 | UX-07/10/14 + ENG-04.1/.4 | 权限独立、Wi-Fi预算、单人保留 |
| C11 | UX-12 + ENG-05/06/07 | 同一配置、变更失效、双方再确认 |
| C12 | UX-13/16 + 后续文件传输 | 本版只验缺文件阻止开局，传输部分后置 |
| C13 | UX-02/12/15 + ENG-10 | 返回/换局保连接、清确认、保筛选 |
| C14 | UX-14/15 + ENG-10 | 中断冻结，无静默迁移/单人/STREAM |
| C15 | UX-16 + 后续好友管理 | 基本保存不等于高级操作验收 |
| C16 | UX-05/08/09/10 + ENG-04/06/10 | 各代迟到事件不复活 |
| C17/C18 | UX-01/18 | 三端实际尺寸/字体/安全区/键盘证据 |

获批 U01–U04 对应 UX-02/03；U05 对应 UX-00/11；U06/U07 对应 UX-04；U08/U09 对应 UX-05–10；U10 对应 UX-12/15、ENG-05–07/10；U11 对应 UX-01/17/18。所有 N00–N12/G00 均有任务；D6–D10 游戏内行为由 UX-15承接。
