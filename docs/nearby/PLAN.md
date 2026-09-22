# 附近联机最小可玩执行计划

## 2026-09-23：三端双向同步增量

基线：`origin/main@612f76d`，版本线由版本化 worktree 预留为 `1.9.x`。本增量不重做
下文已经完成的 Android 房主 → HarmonyOS 客机功能，也不改变 UX；只把同一 LAN MVP
owner 同步到三端两个角色。

### T0 — 文档与不可变契约

- [x] 更新 README、DESIGN、PLAN、STATUS，明确三端双向仅是现有最小可玩的同步。
- [x] 用共享层回归锁定邀请/线协议不变、host=P1、guest=P2，以及反向选择游戏、暂停、
  回大厅、同连接换游戏、错误和重连边界。
- [x] 保留 Android 房主 → HarmonyOS 客机已有门禁，不修改现有二维码和消息格式。

### T1 — Android 与 Harmony 补齐缺失角色

- [x] Android `NearbyMvpOwner`/JNI 增加 guest 启动；已有扫码页消费真实邀请并进入原大厅，
  大厅和游戏页从 snapshot role 决定本地座位与房主权限。
- [x] Harmony `NearbyService`/N-API 增加 host 启动与邀请读取；已有创建页显示二维码并进入
  原大厅，客机路径保持兼容；大厅和游戏页从 snapshot role 决定座位与房主权限。
- [x] Android instrumentation 已完成红绿；Harmony 新增 N-API host Hypium 断言，但完整
  ohosTest 仍被既有测试文件的 ArkTS 严格类型错误阻塞，见 STATUS 与验证记录。

### T2 — iOS 接入同一 owner

- [x] iOS 构建把 Rust QUIC provider 交叉编译为所选 iPhoneSimulator/iPhoneOS 架构，
  `FlyNES` 链接 `flynes_lan_mvp`；不复制协议实现。
- [x] 新增薄 ObjC++ bridge 管理一个进程级 session，向 Swift 暴露 host/join、invite、snapshot、
  选 ROM/确认、输入、帧、PCM、暂停、回大厅、取消。
- [x] 现有 Nearby 创建/扫码/大厅/游戏页接到该 bridge；不改变页面层级、按钮集合或暂停 UX。
- [x] iOS 邀请格式断言完成红绿；官方脚本产品构建、定向 Runtime、Nearby UI 与安装启动通过。

### T3 — 后续组合验收（不扩入本次同步提交）

- [ ] 使用真实三端 App、真实 QUIC、真实 ROM/核心验证 A→H、H→A、A→I、I→A、H→I、I→H。
- [ ] 每个组合覆盖同 ROM、P1/P2 输入；每个平台至少一次完整覆盖暂停/继续、回大厅、
  同连接换游戏、主动退出、网络错误后旧会话不复活及重新建房/加入。
- [x] 完成当前改动可运行的 host CTest、Android 单元与 emulator instrumentation、Harmony
  host CTest、iOS Simulator Runtime/UI，并记录阻塞与证据；不把未运行的 Hypium/跨端组合写成通过。
- [ ] 仅把摄像头实际取景、真实热点、物理触控/扬声器、功耗温度与物理端到端延迟留给真机。

### T4 — 提交与推送

- [ ] 检查内容/版本/ABI 门禁与 tracked tree，确认没有签名材料、凭据、私有 ROM 或生成包。
- [ ] 提交并推送 `codex/nearby-three-platform-bidirectional`，不合并 `main`。

以下章节保留 2026-09-22 已完成工作的原始计划与证据，作为回归基线而非新增范围。

更新：2026-09-22。设计：[DESIGN.md](DESIGN.md)。当前基线：`main@8b071ad` 加工作区中的 P1 接线与 UX 修复。
目标：Android 房主/P1 开个人热点，HarmonyOS 客机/P2 手动加入后扫码，在两个 App 中玩一款真实游戏。

本轮已在当前 main 工作区执行下面的软件方案，保留已整合的 UX 和未提交改动。
主机、Android 模拟器、Harmony 模拟器与跨 App 模拟器门槛已通过；结果见
[STATUS.md](STATUS.md) 和 [模拟器验证记录](../verification/2026-09-22-nearby-simulator-gate.md)。

## 2026-09-22 调整：先完成本地与模拟器验证

### 当前判断与证据

1. 30 秒 `accept()` 提前结束邀请是已复现的软件缺陷；Rust transport 红灯证明问题，
   修复后 32 秒延迟加入、真实 120 秒过期和 provider 生命周期回归均通过。
2. Android/Harmony 使用专用模拟器和同一工作区构建的包。runner 固定目标并拒绝物理
   serial，避免再次被旧包版本或真机安装确认干扰。
3. Harmony 扫码隐藏/恢复、取消、迟到结果和重复点击由 mock port 覆盖生命周期；非法
   QR 走 native 服务测试，真实解析/N-API/QUIC 由跨 App 测试覆盖。摄像头边界留给真机。
4. 配对、同一 owner、大厅、真实 ROM/核心、双确认、P1/P2 输入、同帧画面和 PCM 已接通。
5. 最新跨 App 证据为十分钟主回合加两轮新会话，详见
   [模拟器验证记录](../verification/2026-09-22-nearby-simulator-gate.md)。

### 范围与分层结果

产品网络固定为手动手机热点；普通路由器兼容排后。自动开热点、自动配网、Wi-Fi Direct
不进入本轮。模拟器通过可控的虚拟网络验证真实 QUIC；它不需要模拟热点设置界面。

| 验证层 | 要证明的行为 | 通过不能推出什么 |
|---|---|---|
| 主机共享层 | 真实 QUIC、邀请寿命、认证、取消、两个独立进程 | 两个平台 App 接线已完成 |
| Android 模拟器 | 产品 Activity → JNI → 共享协调器；QR、等待、重建、取消与状态展示 | Harmony 已连接、热点射频可用 |
| Harmony 模拟器 | 扫码结果消费 → N-API → 共享协调器；隐藏/恢复/退出 | 摄像头扫描和 Scan Kit 扩展实测通过 |
| 跨平台模拟器 | 两个 App 真实收发 JOIN/ACCEPT，同一会话 ID，双方可见连接/断开 | 物理热点兼容、续航、实际延迟 |

测试优先级按这四层顺序。物理设备不作为当前开发、定位或 P2 共享层工作的前置条件。
最终物理热点体验单独待验收；下方覆盖 P1–P4 的所有软件门槛通过后才安排真机，
不能只完成 P1 就回到真机反复调试。实施本方案的软件阶段不操作真机。

### S0 — 先更新并统一模拟器测试环境

用户已明确允许更新过旧的模拟器环境。实施时直接完成环境准备，不把升级问题退回用户。

- [x] 分别记录 Android Emulator/系统镜像 API/ABI、Harmony 模拟器/API/ABI，以及两端
  SDK、主包和测试包版本。API/ABI 不满足项目构建要求时更新 SDK/镜像或新建兼容实例，
  以仓库 DEVELOPMENT 工具链和项目实际要求为准，不为追逐版本盲目升级项目依赖。
- [x] 区分系统版本与 App 版本：本次 `INSTALL_FAILED_VERSION_DOWNGRADE` 是 App
  `1008001 → 1007011`，升级系统镜像本身不会解决它。优先新建本轮专用模拟器并安装
  当前同源主包/测试包；保留旧 UX 实例及数据。后续版本按仓库 VERSION 策略统一生成，
  不手改 versionCode，不反复请求用户处理测试安装。
- [x] Android 和 Harmony 均使用专用实例。执行前确认目标确为模拟器，runner 拒绝物理
  序列号；软件阶段不访问真机设置、不安装真机包、不发起真机扫码。
- [x] 验证原生 QUIC provider 实际加载且未被构建选项关闭；主包与测试包来自同一工作区。
  记录构建哈希和差异清单，不能用旧 UX 包承接新后端测试。
- [x] 验证 UDP 可达、时钟和测试 runner 可用。Android 使用明确的 ADB serial；Harmony
  使用明确的 HDC 模拟器 target。安装失败、用例未发现、进程崩溃都算未通过。

### S1 — 先修邀请等待时间，建立共享层可靠基线

涉及：`shared/nearby-quic-provider/src/runtime.rs`、`shared/nearby-quic-provider/tests/transport.rs`、
`shared/src/session/lan_mvp/session.cpp`、`shared/tests/nearby/mvp/test_lan_mvp_session.cpp`、
`shared/CMakeLists.txt`。

- [x] 增加 `--delayed-join` 主机回归并观察失败：真实等待 32 秒、原邀请不变、双方仍能加入同一会话。
- [x] 分开“等待用户加入”和“连接开始后的握手超时”：邀请寿命由协调器的 120 秒期限控制；
  provider 保留握手超时、取消和关闭能力。先检查旧 provider 调用点对等待期限的依赖，
  不直接把 30 改成一个更大的常量，也不对所有连接错误无条件重试。
- [x] 添加短时间 provider 回归覆盖上述语义，再作最小修复；重跑已有取消/关闭回归，
  确认无客机时取消不会挂住、销毁不泄漏后台任务。
- [x] 注册延迟加入为独立 CTest 用例；增加一次真实 120 秒过期用例，断言
  `ENDED/EXPIRED`、QR 清除、旧邀请不能加入。慢用例单独标记，避免每条测试重复等两分钟。
- [x] 成功连接后保持超过 transport idle 窗口并继续轮询，验证待机不自行结束；主动取消后
  两端结束。保留已有错 pin、错 token 用例，不展开完整旧协议故障矩阵。

执行已有红灯：

```powershell
cmake --build out/nearby-mvp/host --config Debug --target flynes_lan_mvp_session_test
out/nearby-mvp/host/Debug/flynes_lan_mvp_session_test.exe --delayed-join
cargo test --manifest-path shared/nearby-quic-provider/Cargo.toml
ctest --test-dir out/nearby-mvp/host -C Debug -R '^flynes_lan_mvp_' --output-on-failure
```

通过条件：32 秒后仍可使用同一个邀请，120 秒后明确过期；正常/拒绝/取消均收敛。

### S2 — Android 模拟器覆盖产品行为和真实连接

涉及：`app/src/androidTest/java/com/flynes/emu/ui/NearbyPairingTest.java`、
`app/src/androidTest/cpp/CMakeLists.txt`、必要的测试专用 native peer、
`app/src/main/java/com/flynes/emu/NearbyPairingActivity.java`、`NearbyMvpSession.java`；
复用 `NearbyLandscapeTest` 与已整合的 Nearby UX 回归。

- [x] 使用 S0 准备的兼容 AVD/测试安装，记录序列号和基线；保留现有 UX 安装与数据。
- [x] 运行已新增的 32 秒等待 → 原 QR 不变 → 重新建房生成新邀请 → 取消返回用例，
  先保留红灯证据再验证 S1 修复。补取消后旧邀请拒绝和重新进入可建新房。
- [x] 从产品页面实际生成的 QR 位图解码，测试专用客机调用同一个共享 ABI 做真实 QUIC JOIN。
  首先在同一模拟器内运行独立 native 客机进程，避免双 AVD NAT 干扰；产品不能增加
  面向用户的调试配对入口。断言产品显示已连接、会话 ID 一致、二维码清除。
- [x] 验证客机退出后房主页面结束；Activity 离开/销毁后会话资源释放。
  页面重建是否终止旧会话按现有首版规则验证，不在本轮引入恢复功能。
- [x] 运行 Nearby 的已合入 UX/布局/确认回归；保留已通过矩阵，只为实际修改与失败重跑相关项。

构建与定点运行：

```powershell
.\gradlew.bat :app:testDebugUnitTest --tests '*Nearby*' :app:assembleDebug :app:assembleDebugAndroidTest
# 从 adb devices 中选择并确认本轮专用 emulator 序列号，写入 $testSerial。
adb -s $testSerial install -r app/build/outputs/apk/debug/app-debug.apk
adb -s $testSerial install -r app/build/outputs/apk/androidTest/debug/app-debug-androidTest.apk
adb -s $testSerial shell am instrument -w -r -e class com.flynes.emu.ui.NearbyPairingTest com.flynes.emu.test/com.flynes.emu.test.SingleDeviceCertificationRunner
```

通过条件：断言真实 native 会话和 UI 同步；不能仅靠文字出现或注入 LOBBY 状态判定连接。

### S3 — Harmony 模拟器验证扫码之后的完整接线

涉及：`harmony/entry/src/main/ets/pages/NearbyPairing.ets`、必要的轻量扫码结果消费函数、
`harmony/entry/src/main/cpp/napi_init.cpp`、`harmony/entry/src/ohosTest/ets/test/NearbyService.test.ets`、
`List.test.ets`；增加专用 MVP Hypium 测试文件。

- [x] 启动/确认 DevEco Harmony 模拟器，单独核对 provider 对该模拟器 ABI 是否实际可用；
  native 缺失应作为构建问题解决，不能以 HAP 编译成功代替运行，也不能切回真机绕过。
- [x] 让真实 Scan Kit 返回与测试返回进入同一个 QR 消费函数；生命周期测试 mock port，
  N-API、解析、TLS pin/token 校验与共享状态机全部使用产品实现。
- [x] 将新增 MVP 用例放入明确的 `describe` 测试组，提供定点测试选择，避免其他 ROM/UI
  初始化干扰。现有 MVP invalid-QR 用例位于 `describe` 外，需验证其注册与实际执行数。
- [x] 依次验证：合法结果进入 JOINING；非法结果不创建连接；扫描取消可重试；扫描期间页面
  隐藏后恢复轮询；明确退出后迟到结果被丢弃；重复点击只产生一个连接尝试。
- [x] 连接真实 host peer，观察 N-API 与页面均进入同一会话；失败后允许新邀请重试。
  每个实际发现的问题先写失败断言，再改生命周期，不猜测 Scan Kit 会触发哪种回调。
- [x] 运行相关 host CTest、MVP Hypium 和受影响 UX 回归。按 DEVELOPMENT 构建主/test HAP，
  用明确的模拟器 target 安装与执行；记录 Hypium 用例数、失败数和原始输出。

通过条件：扫码结果之后的产品链路在模拟器可重复运行，隐藏/恢复/取消不留下悬挂会话。
摄像头识别与系统扫码扩展本身另记为未测，不能成为以上测试的前置障碍。

### S4 — 两个平台 App 闭环，再进入 P2

涉及：`tools/quality/run_nearby_mvp.ps1`（提前建设原计划 P4 的同一个 runner），
以上两端测试与必要的测试专用入口。

- [x] runner 先完成两个独立进程的真实 QUIC 往返，再探测 Android/Harmony 模拟器之间
  UDP 的双向可达性。ADB 的 TCP forward 不能用于证明 QUIC/UDP 连通。
- [x] 优先直接使用模拟器可达地址；若 NAT 必须做 UDP 端口映射，映射只能放在测试环境，
  记录原端点/映射端点。测试可适配 endpoint，但不得修改 pin/token、关闭验证或伪造连接状态。
- [x] Android 产品页建房并实际生成 QR，runner 将载荷交给 Harmony 产品 N-API；
  两端真实 QUIC 握手、JNI/N-API 快照会话 ID 相同、页面显示连接。先用测试接口读取 ID，
  仅在产品后续流程确有需要时扩展产品 DTO。
- [x] 自动执行跨 App 立即加入和三轮重建；32 秒延迟、取消重建、错误邀请和结束由同一
  共享/平台测试集覆盖。证据只记状态、帧和结果，不记录 QR 全文、token 或签名材料。
- [x] 输出逐层结果：共享层、Android 模拟器、Harmony 模拟器、跨 App 模拟器。
  缺少某个模拟器时只把对应层记为未运行，继续完成可独立验证的层。

软件 P1 通过后开展 P2 的真实 ROM、双确认与双输入工作。即使跨平台模拟器环境暂不可用，
已通过 S1/S2 后仍可独立推进 P2 共享层测试与实现，产品接线和跨平台结果继续单独标注。
不再以未做真机扫码为由冻结全部开发。物理热点是否可用仍为独立的最终验收项。

### 本轮方案交付状态

S0–S4 已执行，软件红灯已修复。完整计数、29 组映射与边界记录在
[模拟器验证记录](../verification/2026-09-22-nearby-simulator-gate.md)。本轮未操作真机、
未提交当前混合工作区。

## P1–P4 软件测试完整清单：全部先于真机

覆盖范围是当前最小可玩路径的全部软件行为，以及改动影响到的既有单机路径。
不扩展到 iOS、自动组网、反向建房、STREAM、完整旧 engine 协议矩阵或全 ROM 兼容。
本清单的执行结果逐组记录在模拟器验证记录中；历史 PASS 未直接充当本轮结果。

层级缩写：H = 主机共享/native 测试；A = Android instrumentation；
O = Harmony Hypium；X = Android/Harmony 两个模拟器 App 的真实网络闭环。
标注多个层级时需完成各自断言；H 通过不抵消 A/O/X 的缺项。

### P1：邀请、连接、页面与错误恢复

| 编号 | 层级 | 必测场景与明确断言 |
|---|---|---|
| C01 | H、A、O | 合法 QR 完整解析；破损/截断/超长/未知版本/非法地址端口/pin/token 长度错误拒绝；无崩溃、无连接、无假 LOBBY。二维码解码读取真实图像，载荷不写日志。 |
| C02 | H、A | 有热点与 WLAN 时发布热点地址；只有 WLAN/模拟器网卡时选择可用私网地址；无可用地址时显示原因且不生成 QR。监听失败不得显示等待中。 |
| C03 | H、A、X | 即时加入和延迟 32 秒加入成功；120 秒过期后 QR 消失、原因 EXPIRED、旧邀请拒绝；验证临近过期边界采用共享可控时钟，另保留一次真实计时。 |
| C04 | H、A、O、X | 错 pin 在 TLS 阶段拒绝、错 token 在 JOIN 阶段拒绝；旧邀请/已消费邀请不能再次加入；成功邀请只接纳一个客机。 |
| C05 | H、A、O、X | 重新建房替换 token/邀请并关闭旧 listener；取消期间到达的回调不恢复房间；重复点击不产生并行 owner；取消、失败后重新建房可成功。 |
| C06 | H、A、O、X | 主机不可达、端口关闭、握手超时显示终止原因并可退出/重试；已连接空闲大厅不因等待用户确认自行失效。 |
| C07 | O | 合法/非法扫码结果、扫码取消、重复扫描、隐藏后恢复、退出后迟到结果；生命周期用 mock port，native 非法 QR 与跨 App 测试补足真实解析/N-API。真实摄像头返回留给真机。 |
| C08 | A、O、X | 返回键、工具栏返回、取消、切后台、页面/Activity 重建、进程终止；按首版规则结束旧会话，远端最终结束，回来后不显示伪连接或残留 QR。扫描扩展临时覆盖与用户明确离开分别处理。 |
| C09 | A、O、X | 配对 → 大厅使用同一个 MVP owner 和会话 ID；转场不能误销毁连接；所有页面状态来源于该 owner；结束后大厅不能继续确认/开始。 |
| C10 | A、O | 新版 Nearby 入口/配对/大厅/管理/局内状态可见可操作，保留横屏、中文/英文、已有字体与窄屏矩阵；错误文案、加载反馈、权限拒绝后的返回不遮挡主要操作。系统扫码权限弹窗实测单列真机。 |

实现/测试位置：S1–S4 已列文件；新 MVP 用例放 `shared/tests/nearby/mvp/`，Android 放
`app/src/androidTest/java/com/flynes/emu/ui/`，Harmony 放 `harmony/entry/src/ohosTest/ets/test/`。
现有 UX、旧配对 reducer 测试保留，但不能代替上述 MVP owner 的真实状态断言。

### P2：真实 ROM、配置确认与确定性双输入

| 编号 | 层级 | 必测场景与明确断言 |
|---|---|---|
| G01 | H、A、O、X | 从真实目录选择验收 ROM；双方相同内容可加载；客机缺文件/不同内容/损坏 ROM/不可读文件显示原因并不进入 RUNNING。相同名字不同内容按摘要拒绝。 |
| G02 | H、X | 核心兼容 ID、源帧率、采样率或配置摘要不一致时拒绝开始；正常路径双方确认的是同一配置摘要。 |
| G03 | H、A、O、X | 无确认/仅一方确认均不开始；双方确认才开始；改选游戏清除旧 READY；重复确认/迟到确认不会重复加载或开局。 |
| G04 | H、X | 使用真实 Nestopia 核心从 frame 0 启动；不继承单机保存、旧按键或旧 PCM；Android 固定 P1、Harmony 固定 P2，越权座位输入拒绝。 |
| G05 | H、X | 两端使用不同按键序列；2 帧输入缓冲按约定填零；只收到一方输入不能推进，两方齐全只推进一次；持续比较同一完成帧的状态摘要。 |
| G06 | H | 输入重复/迟到/越窗、消息分包/粘包/非法长度、旧会话消息；不会重复推进、越界或无限堆积；可靠流不凭空假设丢失某条已交付消息。 |
| G07 | H、X | 传输延迟/抖动/短暂停顿时缺输入就等待，按现有规则恢复推进；超过 2 秒无所需进展则结束；每 60 帧摘要一致，人为注入摘要不一致时两端终止。 |

实现/测试位置：`shared/src/session/lan_mvp/{wire,lockstep,session}.*`、
`shared/tests/nearby/mvp/`、两端大厅与各自测试。复用既有 runtime 测试工具和真实内容
清单；不把旧 engine 的通过结果改名充当 MVP 测试。配置与故障注入限测试设施，正常
路径必须使用真实 socket、真实 ROM、真实核心；不注入“成功”或伪造帧推进。

### P3：原生触控、画面、声音与单机回归

| 编号 | 层级 | 必测场景与明确断言 |
|---|---|---|
| U01 | A、O、X | 通过各自真实触屏控件按下/释放方向、动作、Start，以及本游戏需要的组合按键；记录进入共享层的本地 mask，验证只控制对应座位、松开后无卡键。 |
| U02 | H、A、O、X | 联机启动后只有一个 owner/runtime 推进核心；UI、渲染、音频回调仅消费输出；无残留单机线程造成双步进。 |
| U03 | A、O、X | 两端实际显示真实核心帧；每个玩家操作都在两端产生相同游戏结果；用相同 frame ID/核心摘要关联画面，不能把两个独立单机画面当作同步通过。 |
| U04 | H、A、O、X | 双方实际产出非空 PCM 并交给平台音频消费路径；无数据时安全静音、退出后停止生产、不重播旧队列；模拟器能捕获音频时确认可播放，不能仅凭播放器对象创建成功。 |
| U05 | A、O | 返回、后台、音频焦点/音频中断等模拟器可触发事件按首版结束规则处理；断开提示可见；退出清理控制、画面/音频任务，重新进入可正常开局。 |
| U06 | H、A、O | 联机退出后原有单机加载、触控、画面、声音、保存/读取冒烟通过；测试使用隔离数据，不改用户 ROM/存档。仅执行本次改动影响到的单机回归。 |

实现/测试位置：Android 的 `NearbyLobbyActivity.java`、owner/JNI、播放入口和现有触控
instrumentation；Harmony 的 `NearbyLobby.ets`、`napi_init.cpp`、`native_play_runtime.*`、
Hypium；主机复用 `harmony/tests/native_play_runtime_audio_test.cpp` 等受影响回归。
若模拟器某项输出不可采集，先补测试后端/证据采集能力；将“已产出 PCM”和“实际平台
播放”分别报告。硬件扬声器、触屏手感和实际音频延迟才属于真机项。

### P4：软件连续试玩、故障收敛与资源释放

| 编号 | 层级 | 必测场景与明确断言 |
|---|---|---|
| E01 | X | 在两模拟器 App 真实运行验收游戏 10 分钟，持续分别输入 P1/P2；记录完成帧与周期摘要、画面与音频证据，无意外结束、崩溃或各自独立推进。 |
| E02 | H、A、O、X | 任意一方退出/关闭进程、虚拟网络中断、单向黑洞；双方在约定超时内停止核心与音频，清输入，显示原因。恢复网络不会复活旧会话，重新建房才能开始。 |
| E03 | H、A、O、X | 建房→加入→确认→游戏→退出连续 3 轮；无残留 listener/连接/运行线程/未完成回调；测试 owner/provider 资源计数回到基线。内存稳定性看资源与趋势，不对模拟器 RSS 作不可靠的严格相等断言。 |
| E04 | H、A、O | 连接中/确认中/运行中取消以及结束时迟到回调；无重复释放、悬挂调用或 UI 重新变为已连接；实际修改到 native 生命周期时执行对应取消/关闭测试与可用的内存检查。 |
| E05 | H、A、O、X | 网络不具备互联网出口时，上述建房、配对、ROM 加载、游戏不依赖云服务/DNS；保留 peer UDP 可达性，模拟器断网测试不能同时误切断被测虚拟局域网。 |
| E06 | 全部 | runner 识别测试未运行、跳过、超时、崩溃、非零失败数；保存版本/构建哈希/测试目标/用例数/结果，不将缺模拟器、缺 provider、只构建成功写成 PASS。 |

runner 沿用 `tools/quality/run_nearby_mvp.ps1`，原始证据放 `out/nearby-mvp/`。
延迟与黑洞由测试调度/虚拟 UDP 网络控制；不修改正常应用协议来制造通过结果。

### 上真机前的硬门槛

- [x] C01–C10、G01–G07、U01–U06、E01–E06 共 29 组软件场景，逐项列出实际测试名、
  覆盖层、构建版本、结果与证据路径。主机部分在主机测，平台行为在相应模拟器测。
- [x] 所有要求的软件层均通过；没有未解释失败，没有以跳过/仅构建充当通过。
  缺模拟器就先更新/准备模拟器，暂不因此切到真机。
- [x] 最新组合版本的跨平台模拟器 10 分钟试玩、3 轮完整重开与退出/故障注入通过。
- [x] 所有本轮实际改动的必要回归通过，更新 STATUS 的软件结果后才安排物理验收。

### 仅留给真机的项目

| 真机项目 | 必须使用真实硬件的原因 | 执行方式 |
|---|---|---|
| vivo 系统安装/授权 | 厂商安装拦截和系统确认属于设备 ROM 行为 | 软件门槛通过后集中安装同一候选版本 |
| 摄像头识别与 Scan Kit 系统扩展 | 摄像头对焦、二维码取景、真实扩展 UI/授权与回到 App | 以已通过的扫码结果消费链为基础做完整一次扫码旅程 |
| Android 热点 ↔ Harmony WLAN | 真实 AP 接口、系统路由、射频与厂商 Wi-Fi 策略 | 关闭互联网依赖条件下手动组网，验证扫码连通、断开与重新建房 |
| 触屏/屏幕/扬声器实际体验 | 物理触控、硬件音频与可感知延迟不能由模拟器认证 | 两人实际操作验收游戏，连续试玩 10 分钟并检查声音和卡键 |

物理热点的离线测试在地面完成；不把真实乘机作为开发验收条件。刷新率、功耗、温度、
全机型覆盖与弱网性能调优属于后续硬件专项，不加入本轮最小可玩交付。
真机暴露的软件缺陷必须先补成本地/模拟器回归，再修复后复验，避免反复人工扫码定位。

以下 P0–P4 保留功能实施顺序；执行与验收方式以上面的软件优先方案和 29 组场景为准。
凡提到物理设备操作，统一推迟到“上真机前的硬门槛”全部通过后。

## P0 — 仓库收束（已完成）

- [x] 保全原工作树差异和本地证据。
- [x] 所有注册联机分支提交进入 main；保留历史分支引用。
- [x] 活动 worktree 只剩仓库根目录。
- [x] 归档旧联机设计、任务卡和验收报告；README/AGENTS 指向当前入口。
- [x] 停止与仓库整理无关的全量构建/测试。

详见 [REPOSITORY.md](REPOSITORY.md)。P0 不计入可玩功能完成度。

## P1 — Android 与 Harmony 两个 App 真正连接

**先交付可见结果：Android 显示房间 QR，Harmony 扫码后双方显示已连接。**

文件：新增 `shared/include/flynes/flynes_nearby_mvp.h`、
`shared/src/session/lan_mvp/{invite,session}.{hpp,cpp}`、
`shared/src/session/flynes_nearby_mvp.cpp`；修改 `shared/CMakeLists.txt`、
`app/src/main/cpp/nearby/session_owner.*`、`app/src/main/cpp/flynes_app_jni.cpp`、
`harmony/entry/src/main/cpp/napi_init.cpp`、两端产品 CMake 和 Nearby 配对页面。

1. 先写共享邀请解析测试：合法 QR 可读出 endpoint/pin/token；错误版本和破损载荷拒绝。
   运行该新目标得到失败，再实现解析器。二维码编解码使用离线库/平台能力；不自写算法。
2. 在共享协调器中直接调用已有 QUIC C ABI 的临时证书、listen/connect、双向流。
   验证时绑定实际网络地址；保留 pin 校验。共享邀请对象是两端唯一数据源。
3. Android 建房产生 QR；Harmony 扫码把载荷交给同一个解析器。删除这一入口对旧
   GATT discovery、好友存储和 bearer 协商的启动依赖，产品仅创建一个共享 owner。
4. 先验证两个独立进程通过真实 socket 往返，再安装同一工作区构建的两个模拟器 App，
   用产品 QR 与测试扫码结果入口完成 JOIN/ACCEPT；真实摄像头扫码留待最终物理验收。
5. 错 pin/错 token 拒绝、取消建房能结束连接各做一个定点用例；通过后提交。

**软件通过证据：** 同一会话 ID 的 Android/Harmony 模拟器 App 日志和已连接界面；实际
虚拟网络地址/映射；双方消息确经 QUIC。仅注入扫码返回时记录软件 P1 通过，不能声称
摄像头扫码旅程通过；该边界不阻止继续 P2。
P1 未连通时继续处理当前连接阻断，不扩展到 BLE、第二入口或完整安全存储系统。

## P2 — 同一真实 ROM、双方确认、固定双人输入

文件：新增 `shared/src/session/lan_mvp/{wire,lockstep}.{hpp,cpp}`，扩展 P1 的
session/C ABI；复用 `shared/src/runtime/flynes_runtime.cpp` 和目录内容读取接口。
新增定点测试 `shared/tests/nearby/mvp/test_lan_mvp_session.cpp`。

1. 用真实内置 ROM 写失败用例：单边 READY 不推进；不同 ROM/核心标识拒绝；
   同一帧只有 P1 时不推进，P2 到达后恰好推进一次。
2. 实现 CONFIG/READY/START、`load_rom_fresh` 和固定座位的有界输入队列。
   不以源码中的游戏名称选择 ROM；测试从内容清单查找验收游戏。
3. 实现 2 帧输入缓冲，双方输入齐全才驱动真实 runtime。核心调用规则为：

   ```text
   receive INPUT(frame, remote_mask) -> store_remote(frame)
   sample local input for frame + 2 -> send INPUT(frame + 2, local_mask)
   if running && local[frame] && remote[frame]:
       step_frame(P1[frame], P2[frame]); frame += 1
   else:
       wait_for_peer()
   ```

4. 两个真实核心分别注入 P1/P2 的不同按键序列，核对相同帧的状态摘要。
   将其中一个输入停掉，确认另一端不会独立继续游戏；只做这些路径的回归。
5. 两端大厅确认同一游戏后进入 RUNNING，提交该检查点。

**通过证据：** 真 ROM、真核心、真网络、双方确认；两个输入端口实际生效。
主机双实例仍只证明共享层，产品输入和显示在 P3 验收。

## P3 — 接上两个原生 App 的触屏、画面和声音

文件：Android `NearbyLobbyActivity.java`、`NearbySessionOwner.java`、
`app/src/main/cpp/nearby/session_owner.*`、`nes_jni.cpp` 及现有播放入口；
Harmony `NearbyLobby.ets`、`NearbyService.ets`、`napi_init.cpp`、
`native_play_runtime.*` 及现有游戏页面。

1. 分别写产品接线失败用例：Android 触屏只产生 P1，Harmony 触屏只产生 P2；
   开局后没有旧单机线程继续推进第二份核心。
2. 将现有手柄提交到共享会话，将现有显示/音频消费接到同一个联机 runtime。
   复用 `fly_runtime_copy_latest_frame` / `fly_runtime_pull_pcm`；音频回调只消费 PCM。
3. 先在两个模拟器 App 打开用户指定的《雪人兄弟》双人模式，两端轮流按方向、攻击、Start，观察对端
   显示相同玩家动作。记录游戏帧与输入端口，避免“两个单机画面同时动”的假通过。
4. 运行本轮修改涉及的 Android UI instrumentation 和 Harmony Hypium 用例，
   只修阻断输入、显示、声音或该入口的实际失败，然后提交。

**通过证据：** 两个 App 中 P1/P2 均可操作、两端有画面和声音的一段录屏及对应日志。
不顺带重做字体、渲染模式、全游戏兼容或三端单机验收。

## P4 — 完成一局试玩并给出可安装成果

文件：补齐同一个 coordinator 的 END/失联处理；新增
`tools/quality/run_nearby_mvp.ps1` 作为唯一双机验收入口；结果记入 STATUS。

1. 为主动断开、对端断网写两个定点失败用例，再实现停止核心、清按键、停止音频
   和关闭连接。等待已接纳的 provider 回调结束后销毁 owner。
2. 先在 Android/Harmony 模拟器完成 E01–E06。全部软件门槛通过后，再在真实设备上
   通过热点与唯一扫码路径玩同一游戏 10 分钟，核对两位玩家输入与断网收敛。
3. 仅重跑受上述改动影响的用例。记录源码提交、两个包的 SHA-256、安装方式、
   设备/系统版本、入口操作和结果；邀请 token 不进入证据。
4. 验收脚本缺少设备或使用模拟器时返回明确的未完成状态，禁止输出物理可玩 PASS。
   产物和原始日志放 `out/nearby-mvp/`，文档只保存可复核结论。

**停止扩展：** 这一步通过就交付本轮；后续需求另行排期。
可安装开发包沿用仓库本地签名规则；正式发布仍遵守 main、版本和标签规则。

## 实施约束与定点命令

- 每一步采用“目标失败断言 → 最小实现 → 受影响回归 → 提交”，沿 P1→P4 推进。
- 只新增一个共享协调器和一个双机 runner；不新建并行 worktree，不分散出新主线。
- 每个新问题先说明它阻断了哪个 P1–P4 结果。无法对应的事项写入 STATUS 的后续栏。
- 现有旧 engine 测试保留，修改到相关旧代码时才运行其受影响子集。
  不以“所有历史测试先变绿”作为开始产品接线的前置条件。
- 新测试目标统一使用 `flynes_lan_mvp_*`；不要重命名旧测试来冒充新路径通过。

新增目标后，主机执行示例（CMake/SDK 按 DEVELOPMENT 配置）：

```powershell
cmake --build out/nearby-mvp/host --config Debug --target flynes_lan_mvp_session_test
ctest --test-dir out/nearby-mvp/host -C Debug -R '^flynes_lan_mvp_' --output-on-failure
.\gradlew.bat :app:testDebugUnitTest --tests '*Nearby*'
```

首次配置必须显式开启 `FLYNES_ENABLE_RUST_QUIC_PROVIDER=ON` 并确认没有回退为 OFF。
Android UI 按具体测试类运行；Harmony 使用具体 Nearby Hypium 类。
runner 负责 ADB/HDC 设备选择和证据收集，不能把两个主机进程的结果写成跨平台 App 结果。

不在计划中预填完成百分比、固定总工期或尚未产生的 PASS。

## 2026-09-22 真机反馈后的基本功能收束

用户明确要求先修复基本交互，再集中验证：

1. 限制本地输入提前量，避免快端积压整个网络重排窗口。
2. 两端复用单机游戏页面、手柄与暂停菜单；删除独立联机操作页。
3. 暂停菜单返回联机大厅，保留传输连接和会话身份；回大厅需双方确认清空旧局输入。
4. 房主在大厅从现有游戏库选择游戏，从机按本地游戏身份加载同一 ROM，双方确认后开新局。
   缺少 ROM 时显示原因并留在大厅，不传输 ROM；只有主动断开或实际连接错误才销毁连接。
5. 先用两个模拟器直连并自动注入邀请测试操作、暂停、换游戏、输入延迟和同步；
   本地用户 ROM 只用于本地验收，不进入内容包或测试夹具。
6. 真机保留一次最终触屏、扬声器、热点手感验收，当前暂停反复扫码与无关性能优化。
