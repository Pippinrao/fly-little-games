# 多人联机交接 — 2026-09-11

## 1. 先读结论

**当前不能在手机游戏应用内发起多人联机。好友页、附近联机入口、大厅、双机游戏画面与输入共享尚未接入产品。** 用户在手机上看不到相关按钮不是操作遗漏，也不能归因于签名：这些产品功能确实未实现。

已完成的是部分共享内部模块及独立 QUIC 连接实验。真实成功证据是 **Android 手机 ↔ Windows 电脑**；尚无 Android ↔ Harmony 手机 QUIC 成功证据，更无可玩双机验收。交叉编译、单元测试、ping、独立测试页均不能替代产品验收。

用户最新要求：先编写交接文档并提交 Git。本次仅归档文档，不继续安装、修改产品或推送远程。后续恢复实现时，不要再以单个底层模块通过作为整体完成或任意停止点。

## 2. 工作位置与权威文档

- Worktree：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer`。
- 分支：`codex/nearby-multiplayer`；本交接前代码 HEAD：`b1a4487a547d5d6a7bb75d34779aa19393357871`。
- 本机工具默认 cwd 可能仍是仓库根目录；每条实施命令显式指定上述 worktree。
- 唯一获批设计：[nearby multiplayer design](../superpowers/specs/2026-09-04-cross-platform-nearby-multiplayer-design.md)，重点 §22 用户界面、§26–28 实机/安全门禁、§30 跨任务契约。
- 获批设计未修改，normalized Git blob：`c88683050f52cb72773917bb6c97573bdddae8af`。
- [详细实施进度](../superpowers/plans/2026-09-09-nearby-multiplayer-progress.md)。较早“设备缺失”记录是历史状态，不代表交接时设备状态。
- [连接实验计划](../superpowers/plans/2026-09-10-nearby-quic-spike.md)、[实际证据与产物哈希](../acceptance/2026-09-10-nearby-quic-spike.md)、[工具构建及运行说明](../../tools/nearby-quic-spike/README.md)。

## 3. 不得重新擅自决定的用户要求

- 第一版覆盖 Android、HarmonyOS NEXT、iOS；两台手机同一游戏画面，各自操作自己的角色，内部四端口输入不能退回隐式 port 0。
- 两端播放同一会话音频，各自可以静音；要求各端本机 A/V 同步，不要求扬声器声学相位同步。
- BLE 发现与六位码核对、房主二维码，是同一身份/临时加密握手的两种入口，不建立两套协议。
- 成功认证进入同一大厅后保存好友身份；好友记录不自动授权本局加入、ROM 接收、主机或座位变更。
- 同 ROM 且确定性/资源门禁通过时自动 DUAL_SIMULATION，否则 HOST_STREAM；不新增用户强制模式开关。
- DUAL 异常先暂停和重同步，失败后按已批准事务保留进度、单向降级 HOST_STREAM，不再次询问模式选择。
- 邀请者只是默认主机；开局前显示能力和风险，允许手动指定主机，最终主机和座位双方确认，不静默改选。
- 主机重连失败保持暂停；接管或保存结束需用户明确选择，不静默迁移 authority。
- HOST_STREAM 本身不传 ROM；独立且双方确认的用户文件补齐流程可以传 ROM，仍走通用校验/导入，不豁免来源。
- 近场延迟验收口径为 guest touch 到 guest 对应画面，p95：DUAL ≤80 ms，STREAM ≤150 ms；当前没有此项实测证据。
- Wi-Fi 路径与最多一次系统确认按获批规范实机认证；不能为“先通”改为明文、跳过身份或绕过平台安全机制。

## 4. 产品界面设计与实现缺口

| 用户看到的页面 | 获批设计 | 当前产品状态 |
|---|---|---|
| 游戏中心 | “附近联机”入口 | 未接入 |
| 联机页 | 好友/附近设备、寻找设备、扫描房主二维码 | 未接入 |
| 配对 | 匿名入局许可、六位码或扫码确认、失败阶段说明 | 未接入 |
| 大厅 | 游戏、主机能力、P1/P2、ROM 状态、双方确认、本地声音开关 | 未接入 |
| 游戏覆盖层 | 各自控制角色、模式/网络/暂停状态、独立静音 | 未接入联机会话 |
| 好友管理 | 保存身份、重命名、删除、拉黑、身份重置 | 未接入 |

产品源位置：Android `app/src/main/`（不是 `android/`）、Harmony `harmony/entry/src/main/ets/`、iOS `ios/app/`。`tools/nearby-quic-spike/harmony/` 的页面是独立测试工具，绝不能当成上述产品页面。

## 5. 已有代码与边界

| 提交/模块 | 已有能力 | 不能据此声称 |
|---|---|---|
| `c51fcf9` pair_capability | 512-byte summary/48-byte entry 校验、精确共同方案选择 | 设备认证、BLE 身份握手 |
| `e1ea62d` InitialPlanLock | PLAN/ACK/FINAL、持久化和单次提示的内部 reducer | 实际 Wi-Fi 已建立 |
| `b58851c` session_initial_plan | 实际 session handle 拥有状态、原始 60 秒 deadline、generation fencing | public session 网络接入已完成 |
| `5e86fcf` runtime PCM | try-lock 欠载静音，不阻塞等待 core mutex | canonical 网络音频、设备 A/V 已达标 |
| `887852a`、`ca8c711` QUIC probe | TLS 内精确 SPKI pin、STREAM/DATAGRAM/exporter、修复完成与重试竞争 | 产品配对/ChannelBind、正式三端传输后端 |
| `63dc22b`、`b1a4487` mobile runner | 实际 OHOS archive/C ABI/NAPI worker、隔离 HAP、启动请求去重及 latest-Want 修复 | 手机游戏产品接通 |

共享目录保持唯一：`shared/include/flynes/{flynes_app.h,flynes_runtime.h,flynes_session.h}`，实现分别在 `shared/src/{app,runtime,session}/`。当前 public session 的原始网络接收、事件/命令和 snapshot 路径仍有未实现桩；不能把内部 seam 测试当作公开 API 已贯通。原有四端口 runtime/checkpoint/rollback 基础不是端到端联机。

QUIC 工具使用锁定 Quinn 0.11.11 / rustls 0.23.43 / ring 0.17.14 与 Cargo.lock，独立 ALPN `flynes-m0a-quic-v1`。监听者公钥由实验操作者传递，**不是** BLE/QR 身份认证；无 mTLS、无会话恢复/0-RTT、无明文降级或私钥日志。单次默认 20 秒，仅显式本地地址，既不建网也不认证正式 bearer。

## 6. 实测、复核与已知小问题

- 最近父任务验证：20 项 Rust 测试、6 项 Python runner/script 测试、5 项实际 TypeScript gate 行为测试通过；fmt、严格 clippy 通过；Android/OHOS 原生构建通过。
- 既有共享构建最近重跑 41/41 CTests 通过。这是历史已记录结果，不意味着本次文档提交重新执行了全部构建。
- Android listener → Windows connector：真实双向 STREAM/DATAGRAM 成功，两端 exporter SHA-256 一致、退出码均 0；最新二进制主机/手机哈希一致。具体地址、摘要与哈希见证据文档。
- 另一个有效公钥作为错误 pin 的实机负测在 TLS 内拒绝；Windows listener 反向角色测试超时，原因尚未证实。未修改防火墙。
- Harmony HAP 实际编译、原生链接、打包成功，但只有 unsigned 包。没有 Harmony QUIC 执行证据；Android↔Harmony ping 成功不算。
- 启动请求规格复核 PASS；独立质量复核对测试工具无 Critical/Important 问题。质量复核重跑 20 Rust、5 Node，未代替父任务的 Python/mobile 验证。
- 质量复核记录一个 Minor：`tools/nearby-quic-spike/build-mobile.ps1:8` 在 dot-source 调用时把调用方 `$ErrorActionPreference` 留为 `Stop`。编译环境变量虽恢复，这项偏好未恢复。后续补保存/恢复及回归测试；当前用独立 PowerShell 进程运行可避免污染父进程。该问题尚未修复。
- SDK 存在 0.x SemVer 提示（其校验正则不接受 major 0）；未弱化 SDK 检查，不宣称无警告构建。

## 7. 设备与签名：当前具体卡点

交接前最近检测：Android ADB `10ADBP18BQ0011Y`（V2324A），Harmony HDC `33Z0224A11002736`。都已连接现有路由器 LAN；后续重查地址和状态，不写死旧地址作为产品配置。

- Android 已有独立实验 ELF：`/data/local/tmp/flynes-nearby-quic-m0a-20260910/probe`。没有安装/替换 Android 产品 APK。
- Harmony 已安装产品 `com.flynes.emu` version 1000001，必须保留。
- 新测试 bundle 是 `com.flynes.nearbyprobe`，仅 INTERNET 权限。未安装成功，不能借旧 bundle 的身份或 profile 签它。
- **需要签名的工程**：`E:/workspace/codes/games/fly-little-games/.worktrees/nearby-multiplayer/.artifacts/nearby-quic-harmony-app`。
- 用户已回复“已完成自动签名”，但随后在此 exact 工程执行 Hvigor 仍提示 `No signingConfig found for product default`。交接检查 `entry/build/default/outputs/default/entry-default-signed.hap` 不存在；unsigned 包 SHA-256 与证据文档一致。
- 已请用户确认打开的是上述隔离工程而非产品 `harmony`，并保存 default 产品签名设置。不能仅凭配置文件修改时间或用户回复就声称签名包已生成。
- 签名操作只走 DevEco Project Structure → Project → Signing Configs。不要读出/提交密码、证书、私钥、profile 内容；不要绕过签名/SELinux。不要重跑 staging 覆盖现有用户配置。

## 8. 接手后的推进顺序与验收口径

1. 先复查 Git/设备和 exact 隔离工程签名状态。签名就绪后正常构建并仅安装新 probe bundle；按 README 启动 Android listener，再将本次 READY 的端点/public pin 传给 Harmony Want，严格限定当前运行日志，做正测和错误有效 pin 负测。
2. 将这一步仅记作“手机传输实验成功”，不能宣布多人联机完成；现有路由器 LAN 也不能认证无路由器附近 Wi-Fi。
3. 恢复产品开发时，把“游戏中心入口 → 好友/附近页 → 配对 → 双方大厅 → 游戏”作为连续交付主线，与真实服务逐段接入。未完成的能力必须明确显示状态，不能用假好友、假连接或只跳转的按钮冒充已实现。
4. 补全获批身份/安全存储、BLE/QR 同一认证握手、选定 Wi-Fi bearer、生产 QUIC ChannelBind 与 session 收发，再连接四端口输入、authority timeline、视频/PCM 和本地播放策略。未实现门禁不能用实验手工 pin 绕过。
5. 完成两手机实际同屏各自操作、双方声音/独立静音，再验收暂停、断连、重同步、降级、保存/接管及 ROM 补齐；按设计测延迟和资源边界。
6. iOS 需要真实 Apple SDK 构建和设备证据；现有 schema 检查不是 native 或跨端互通。三平台及角色组合逐项实测，不根据 API 名称填支持矩阵。

**产品验收必须由用户能执行的动作证明：能看到入口、找到并认证对方、双方确认入局、同一游戏画面且各自控制角色、声音策略正确、异常不丢失或偷偷改变授权。** 真正缺设备/开发者签名/权限时说明 exact 卡点；不能把某项实机卡点说成所有产品编码都无法继续。

## 9. 并行任务、脏文件与提交安全

- 三端移植任务 ID：`01a06cd1-ea44-73d3-a497-a0308724fa55`。用户要求完成后统一对齐，不反复发同步消息。之前一次消息发送失败，不得声称已经送达；以唯一规格 §30 和已提交代码为交接依据。
- 不复制/改写 `.worktrees/ios-harmony-port` 的未提交内容。来源接缝最新约定是通用 borrowed FD + optional expected physical SHA-256，不恢复旧 `session-receive` 枚举或泄露平台 locator。
- 本 worktree 下 `harmony/build-profile.json5`、`harmony/entry/oh-package-lock.json5` 已修改，`harmony/.clang-tidy`、`harmony/.clangd` 未跟踪：属于其他/用户操作，本任务不读取签名详情、不还原、不纳入提交。
- `docs/acceptance/2026-09-09-nearby-device-audit.md` 是既有未跟踪文件，保留并排除本次提交。
- `.artifacts` 的签名工程、native archive、HAP 都是本机产物，不进 Git；新机器需要按 README 重建和配置自己的签名。
- 本次提交只包含本交接及三个已有的本任务进度/证据/实验计划文档更新。提交后核对文件清单；不合并、不 push、不清理工作树。
