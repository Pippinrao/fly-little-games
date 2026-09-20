# 三端字号比例与可玩链路复核（2026-09-17）

## 结论

**需要纠偏，且不是只有鸿蒙。** 鸿蒙存在可由源码确认的字号层级偏差；Android 使用了与原稿不等价的 Material 标题样式；iOS 混用语义动态字体与固定字号。三端均不能标记“已与原稿一致”。这不是需要重新设计 UI，而是需要恢复已批准的文字角色、缩放与布局合同。

本次是源码/依赖资源复核，不是最新安装包的视觉验收。没有本次三端同尺寸、同语言、同缩放的原生截图，不能判断用户当前安装包、系统字体设置和显示缩放各占多少影响。下列确定问题不需要等待真机才能修复；实际渲染需后续模拟器验证，真机继续 NOT_RUN。

执行文件：[字体纠偏与功能补齐计划](../superpowers/plans/2026-09-17-nearby-typography-correction-and-completion-plan.md)。继续沿用已有 UX/ENG 卡，不重新启动一套后端重写。

## 1. 基线与证据边界

- main：`5389585f8e3fa7ddc3e3dcd125d9b0a299c6c79c`；不是当前产品集成基点。
- 产品 W0：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-ui-acceptance-fixes`，HEAD `6ec6605de0f5275295fc08b5905fed3d329f37a4`，**包含未提交 UI 改动**。本文文件定位均以该 worktree 为准，行号是检查时位置。
- 设计权威：[原始 HTML](../superpowers/specs/assets/nearby-ui-parity-review.html)、[获批 UX](../superpowers/specs/2026-09-13-nearby-ui-parity-design.md)。HTML SHA-256：`C69FF8F5CC3A346071DBF2BC6E0F2291232ECB6732A8F38970FFCE1126577C16`。
- W1/W2 已集成；W3 篡改负例已选择接收，不能再次把整个 W3 当新功能合入。
- 本次未改产品、未运行真机、未重跑三端原生 UI 全套。之前本会话重建并复跑 `flynes_two_engine_dual_mvp` 的失败仍有效；完整 102/105 是 ENG-01 台账结果，不是本次新跑的全量结果。

关键 dirty 文件指纹，避免把后续他人修改混作本次结论：

| W0 文件 | SHA-256 |
|---|---|
| `harmony/entry/src/main/ets/pages/NearbyFriends.ets` | `68B43C9760174B89318D549943E5C5658932D9CE11AA7BBDDC424CD08D85A7E0` |
| `harmony/entry/src/main/ets/pages/NearbyPairing.ets` | `3D6B57CF12106CD86E1A290AB68422D34043EE7925E27828012190068D382877` |
| `app/src/main/res/layout/activity_nearby_pairing.xml` | `1B1728E88CEC79BB279DEE8A354562B92516297816740AD6E58ACB438E2E03F6` |
| `ios/app/NearbyFriendsView.swift` | `17DB515D50495084C0B76071ECEE80295D02EB7D508DD28C66D2428F284152BB` |
| `ios/app/NearbyPairingView.swift` | `C7E77700505E916FFB9FC0F1282E5CA611C969BBAD49BA4E33EE9D6BD8BCFA80` |

## 2. 字体比例：原稿与实际定义

下表数字是默认缩放下的逻辑字号，不是截图物理像素。系统字体允许不同，不能因此随意改变字号层级。Android 资源继承结论经过本地实际依赖 `com.google.android.material:material:1.14.0` 的 AAR `res/values/values.xml` 核对，不是凭 Material 印象猜值。

| 文字角色 | 原稿 | Harmony W0 | Android W0 | iOS W0 |
|---|---|---|---|---|
| N00/N01–03 左侧主标题 | 21 / 600，行高 27.3 | 21 / Medium；N00 行高 28 | HeadlineSmall → **24sp / 32sp 行高** | N00 `.title3.semibold`；N01–03 固定 21 / medium |
| 左侧辅助说明 `.fe-muted` | **12**，行高 17.4 | **14**；N00 行高 20 | 多处继承默认 TextView，未显式绑定 12 | N00 `.subheadline`；N01–03 固定 **14** |
| 普通按钮文字 | **14**；主按钮 600 | **16**，多处固定高 48 | Material LabelLarge 基础 14sp/20sp；不可推断所有按钮已一致 | 多处继承系统按钮文字，未统一绑定 14 |
| N00 右侧页签 | **14** | **16** | 需原生测量最终继承值 | 系统 segmented Picker，非原稿下划线样式 |
| 邀请码 | 29 / 600，字距 .17em≈4.93 | 29 / Bold，字距 4 | HeadlineMedium → **28sp**，字距 .12em≈3.36 | 29 / semibold，但固定字号 |
| 输入码 | 28，字距 .16em≈4.48 | 28；仍需测实际字距/行高 | 28sp；固定高 60dp | 固定 28；最小高 60 |
| kicker | 11，字距 .13em≈1.43 | 11，字距 1.6 | 11sp，字距 .13em | 固定 11，字距 1.6 |

鸿蒙辅助说明比原稿大约 **16.7%**，按钮/页签大约 **14.3%**；Android 主标题大约 **14.3%**，指定行高大约 **17.2%**。这些偏差会改变换行、文字密度和主次关系。**这些百分比只描述基础数值，不代表已测得的屏幕视觉差异。**

### F01 / P1：鸿蒙不是整体等比偏大，而是文字角色被改了

定位：`NearbyFriends.ets:109–150`、`:177–195`；`NearbyPairing.ets:282–322`、`:363–379`、`:430–446`、`:496–504`。

标题大致保留 21，但说明改 14、按钮改 16，会让说明与主操作抢占标题层级。全部字号统一乘 0.875 反而会把原本正确的 21、29、11 缩错。应按角色恢复，不做全局缩小。

### F02 / P1：Android 的主题名相似，不代表原稿字号相同

定位：`activity_nearby_friends.xml:32`、`activity_nearby_pairing.xml:29,47,59`；`res/values/themes.xml:3`、`app/build.gradle:73`。

`Theme.Material3.Dark.NoActionBar` 没有 Nearby 专用字体覆盖；实际 `TextAppearance.M3.Sys.Typescale.HeadlineSmall` 是 24sp/32sp，HeadlineMedium 是 28sp/36sp。应建立局部 Nearby TextAppearance，不全局重写 Material 主题、也不让系统默认值决定原稿标题。

### F03 / P1：iOS 同一流程的动态字体策略不一致

定位：`NearbyFriendsView.swift:87–102` 的 `.title3/.subheadline` 与 `NearbyPairingView.swift:135–148,193–218,240–247` 的 `.system(size:)`。

N00 用语义字号，N01–03 大量固定点数；后者未见 `@ScaledMetric` 或等效统一适配。换系统字号时两类页面不能证明同比适配。需要统一“基础角色 + 系统动态缩放”，不是只把 `.title3` 换成一个不缩放的 21。

### F04 / P1：鸿蒙 1.3 倍验收实际无法由现有测试入口施加

定位：`harmony/entry/src/main/ets/entryability/EntryAbility.ets:16`。debug Want 参数只接受 `1/1.5/2`，而 C18 要求 `1/1.3/2`；输入 1.3 被忽略。`AppScope/resources/base/profile/configuration.json` 已声明 `followSystem`、最大 2；不能把问题误诊为全局没有系统缩放。

修复要求：1.3 可设置且读取配置/实际测量证明生效；无效参数明确记录拒绝。保留正常系统无障碍放大，不把 `followSystem` 改为不跟随。

### F05 / P1：容器布局会把字号问题放大

- Android PAIR 仍是固定水平 `224dp + 18dp + 两侧16dp`，无对应窄屏切换；320 宽右侧只剩约 46dp，却放 170dp QR 框。源码 `activity_nearby_pairing.xml:22–24,69–73`。这属于布局问题，缩小字体不能修复。
- Harmony 多个按钮固定高 48，标题/说明设置 `maxLines`；大字可能裁切，需要原生证据判定，不把“风险”写成已观测溢出。
- iOS N00 宽屏分支无 ScrollView，N01–03 窄屏左子视图仍固定 224，底栏固定横向且按钮最小宽 160。横屏大字时需检查可滚动与底栏重排。
- iOS N00 同时有自绘标题和 `.navigationTitle`，存在双标题风险；是否两者实际同时可见需原生导航栈验证。

### F06 / P2：G00 需独立核对，不能随 Nearby 一起换皮

Harmony `GameCenter.ets` 还存在页签16、计数16、详情标题24等定义，与 HTML 对应的14、12、20不一致；但 G00 有原有 compact/large 策略。Android/iOS 游戏大厅尚未在本次取得全角色测量，不作“正确”结论。执行时先区分已有大厅获批适配与本轮新增回归，保留原大厅构图/卡片/分类和独立双人过滤，不能借字体纠偏重做首页。

## 3. 同时发现的交互回归

| ID / 优先级 | 源码证据 | 必须修复的结果 |
|---|---|---|
| R01 / P1 | Android `activity_nearby_pairing.xml:89` 恢复了 `maxLength=6` | 粘贴7位不能悄悄截成可提交6位；保留原始输入校验，错误就不发请求 |
| R02 / P1 | iOS `NearbyPairingView.swift:283–295` 提交按钮无 disabled；`:334` 无重复提交 guard | 非6位/正在提交不可提交；快速双击只有一个 attempt；输入校验仍存在，不能误报为当前必然发送非法码 |
| R03 / P1 | iOS `footerAction():322–331` 扫码取消无动作，创建取消清数据但不返回 | 按原稿退出页面并释放本页请求，迟到事件不能恢复旧页 |
| R04 / P1 | iOS `NearbyFriendsView.swift` rightPane 工具栏始终导航 FriendsManage | “附近设备/寻找设备”不得打开好友管理；未实现发现时明确不可用原因 |
| R05 / P1 | 三端 QR/相机部分仍有框与操作性提示，但无完整能力证明 | 未实现时保留原稿位置、明确不可用原因和输入码替代；不得把占位图当真二维码 |

## 4. 功能进展：不能把字号修好等同可玩完成

| 层级 | 当前证据 | 判定 |
|---|---|---|
| 基础组件与协议 | 已有 V2 engine、DUAL 调度、Quinn 组件、安全负例 | 可复用，不应重写 |
| L1 双 engine/假 runtime | ENG-01 台账 102/105；本会话目标复跑 dual_mvp 仍失败 | 300ms 冻结等5条断言未绿；先诊断时钟/fixture/engine，不预判根因 |
| L2 真核心 + 真网络 | fixture 仍 Fake DualRuntime，Quinn linkage不等于双engine真实通道 | 未验收 |
| L3 原生可玩 | 三端 bridge 仍见 `verify_nearby_v2_composition_contract`，未见完整生产 V2 owner 链路 | 未验收，不能把 ABI 可链接当已接入 |
| 本局同意 | `dual_session_controller.cpp:252` 设置 running；`:596` 用 selected 推导 seats_confirmed | 尚缺双方针对同一配置确认与 runtime-ready 屏障的完成证据 |
| 原稿 UX | N00/N01–03 结构在恢复；N09 仍技术字段平铺 | 部分实现，字体、交互和原生矩阵未通过 |
| 真机 | 按用户要求暂停 | NOT_RUN，不算本轮执行任务 |

最短路线仍为：冻结 UX/接口 → 双方确认 → 真 NES + 真 Quinn → Android 双实例可玩 → Harmony/iOS 接入 → 原稿基本 UX/好友保存/开局前主机与座位选择 → 三端非真机矩阵。STREAM、ROM 传输、自动重连和局中迁移继续后置。

## 5. 字体适配的技术边界

不能把 CSS px、屏幕 px、Android sp/dp、Harmony fp/vp、iOS pt 当成同一个物理单位。基线比较使用可用内容区逻辑尺寸；截图另外记录 density、系统显示缩放、字体缩放及安全区。

- Harmony fp 随系统字体设置变化；保留系统放大，避免在 fp 之外再乘一次倍数。[OpenHarmony 像素单位](https://github.com/openharmony/docs/blob/master/zh-cn/application-dev/reference/apis-arkui/arkui-ts/ts-pixel-units.md)
- Android 14 支持非线性字体放大，不能用 `sp × fontScale` 替代所有实际测量；不得为了跨端截图强制关闭无障碍。[Android 官方说明](https://developer.android.com/about/versions/14/features#non-linear-font-scaling)
- iOS 应统一使用可缩放指标/语义参照；Dynamic Type 类别不能伪称精确等于1.3倍。[Apple ScaledMetric](https://developer.apple.com/documentation/SwiftUI/ScaledMetric)

因此验收分“受控比例的合同对照”和“真实系统设置的可读性”两类证据，两者不能互相替代。既有 C17/C18 和几何差≤2逻辑单位不擅自放宽；未能提供等价配置/完整矩阵时如实标未验收。
