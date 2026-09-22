# 最小可玩状态

更新：2026-09-23。当前实现 worktree 为 `codex/nearby-three-platform-bidirectional`，
基线 `origin/main@612f76d`，预留版本线 `1.9.x`。
目标：把现有最小可玩同步到 Android、HarmonyOS NEXT、iOS，三端均可作为房主/P1
或客机/P2；不改变 UX，不扩展为完整 Nearby 功能。

## 当前结论

- 已有共享 LAN MVP 协议与 runtime 本身按 host/guest 工作，且 host=P1、guest=P2。当前
  worktree 已补齐 Android guest、Harmony host，并把 iOS 接到同一个 LAN MVP owner。
- 本增量保留现有三端 Nearby 页面、游戏页和暂停交互。不会增加好友、六码发现、ROM
  传输、自动组网/恢复、STREAM、预测回滚或新的 UX。
- 对称性目标为 A→H、H→A、A→I、I→A、H→I、I→H。尚未产生的新组合证据不得
  从共享 ABI、主机双实例或构建成功推断。

- 真实热点连接和一次摄像头扫码已进入双方 RUNNING；旧版存在操作页不一致、输入积压及鸿蒙音频未播放的问题，不能据此前画面同步记录为可玩完成。
- 当前两端改用原有单机页面：Android `MainActivity`、Harmony `RunGame`。复用手柄、暂停按钮、暂停抽屉。
- 暂停/继续同步双方核心；从暂停抽屉回联机大厅，保留已认证连接及会话 ID。
- 房主从原有游戏库换游戏，客机读取自己已有的同一 ROM，双方确认后重新开局，不重新扫码。
- 模拟器已通过《雪人兄弟》→暂停/继续→大厅→内置游戏→大厅→《雪人兄弟》的跨 App 闭环。
  最终证据：`out/nearby-mvp/gate-20260922-235136/`。Android 从窗口注入触摸，Harmony 使用系统 UiTest 触摸；测试只注入邀请，两端仍使用真实 QUIC、真实 ROM 和真实核心。
- 新增日志观察已确认两端可进入《雪人兄弟》双人关卡。仍不把模拟器结果当成物理触屏、扬声器或热点端到端延迟认证。

## 本轮修复与验证

| 项目 | 结果与证据 |
|---|---|
| 输入积压 | 本地最多提前 2 帧；快速端不能排满 256 帧窗口。先红后绿 |
| 实际输入 | 测试断言已完成核心帧中的 P1/P2 按键，覆盖 Start、Select、A、B、方向与释放 |
| 操作页 | 两端复用单机页面；无独立联机手柄页 |
| 暂停、回大厅、换游戏 | 产品按钮实际点击；三次开局保留同一会话 ID；旧局输入通过双方屏障清空 |
| 游戏身份 | 修复内置游戏扫描 ID 与清单 ID 的映射；私人 ZIP 使用既有加载器与 ZIP 条目定位 |
| 声音积压 | 空轮询不再把大块静音塞入音频队列；`pcm-red.txt` 失败、`pcm-green.txt` 通过 |
| 软件输入延时 | 最终轮 Android 同进程日志中，14 次按下/释放到核心应用输入为 12–32 ms，平均 23.8 ms；不含屏幕扫描与物理触屏延迟 |
| 共享层 | 完整 MVP CTest 11/11；PCM 最后修复后受影响子集 9/9，含暂停/换局/内容不匹配/停顿 |
| Android | 构建、单元测试通过；单机/手柄/暂停 instrumentation 18/18 |
| Harmony | 签名产品/测试 HAP 构建通过；host CTest 14/14；单机 Hypium 3+15/18 |

构建与单项输出：`out/nearby-mvp/device-20260922/`。
完整记录：[基本可玩闭环](../verification/2026-09-22-nearby-basic-play.md)。
私人 ROM 来源为用户指定的本地目录，仅导入模拟器，不进入内容包或测试夹具。

## 真机事实与剩余边界

- `gate-20260922-224424/`：vivo 热点直连 Huawei，600 帧及公共帧摘要通过。
- 22:45–22:46 的一次真实摄像头扫码已连接，并在两边确认后持续运行；用户随后的触控反馈促成了上述修复。
- ADB offline/HDC 消失属于调试连接问题，未证明根因已解决；与游戏热点连接分别记录。
- 本轮软件修复先在两个模拟器验证。最新可玩改动尚未通过真机触屏、扬声器、十分钟试玩及物理延迟验收。
- 目前 ADB/HDC 列表只有模拟器；不再反复要求用户扫码。

## 2026-09-23 本次同步证据

- 共享 LAN MVP CTest：11/11；Rust QUIC provider：30/30；内容单一来源门禁通过。
- Android：`testDebugUnitTest`、产品/测试 APK 构建通过；排除需要外部 Harmony 对端的
  专用驱动后，Nearby pairing instrumentation 41/41，通过真实本机 host 邀请验证 guest=P2，
  并验证客机不能进入房主游戏选择。
- Harmony：产品 HAP 构建通过，host CTest 14/14；新增 N-API host Hypium 用例已编写。
  完整 ohosTest 打包被既有测试中的 29 处 ArkTS 严格类型错误阻塞，因此未声明 Hypium 通过；
  当前工程也未配置签名，未安装本次 HAP。
- iOS：官方脚本产品构建通过；新增 bridge Runtime 用例通过；Nearby/UX 定向 UI 14/14；
  产品已安装并启动于 iOS Simulator。完整 Runtime 52 条中 51 条通过，唯一红灯是仓库已有、
  明确等待 `nearbySessionSnapshotV2` 的未实现测试，不在本次 LAN MVP 同步范围。
- 详细命令与边界见 [验证记录](../verification/2026-09-23-nearby-three-platform-sync.md)。

## 本 worktree 待完成

- 完成提交并推送目标分支；不合并回 main。
- 六个有向跨端组合、异常后重建边界和真机体验作为后续验收，不在本次同步中补做完整功能。
- Android/iOS 尚未接入真实相机取景，只验证平台扫码结果进入共享邀请解析器；新增的
  Harmony 房主与 iOS 方向目前只选择双方已有的合格内置游戏，导入 ROM/新方向换游戏后续再做。

## 后续范围

自动组网/恢复、ROM 传输、STREAM、预测回滚、换座、真实相机接线、新方向的导入 ROM/
换游戏、全 ROM 兼容、功耗温度和硬件刷新率认证仍是后续事项。
历史软件门槛见 [早期模拟器记录](../verification/2026-09-22-nearby-simulator-gate.md)，不能替代本轮产品交互验证。
