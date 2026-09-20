# 多人联机后端实现探查

日期：2026-09-13。代码基线：`codex/main-test-repair` / `3475d5c`，工作目录为 `.worktrees/main-test-repair`。本次只探查、运行现有共享测试和编写设计，没有实现产品功能。

## 1. 结论

当前是**单机运行基础 + 部分共享协议验证器/初连 reducer + 联机 UI 骨架 + 独立 QUIC 实验**。生产应用尚无从配对到双人输入、画面、声音及恢复的闭环。缺口横跨公共会话接口、认证、传输执行器、模拟调度、媒体管线和持久事务，不能通过解除按钮禁用或让接收函数返回成功来补齐。

用户最新架构要求：**多人联机属于公共代码，系统依赖通过接口注入**。对应的[修复设计](../superpowers/specs/2026-09-13-nearby-backend-repair-design.md)以此为硬约束。

## 2. 按真实调用路径核查

| 环节 | 当前实现与证据 | 实际边界 / 设计影响 |
|---|---|---|
| 公共 Session ABI | `shared/include/flynes/flynes_session.h`：event 只有头部；command kind 只有 `NONE`；snapshot 只有状态、角色、模式与两个水位 | 无法携带用户选择、系统结果、认证身份、输入、媒体 readiness；必须扩展版本化接口 |
| 公共事件入口 | `shared/src/session/flynes_session.cpp:283` 附近 `fly_session_submit_event` 校验参数后返回 `INVALID_STATE` | 用户点击不能推动真实联机状态 |
| 公共网络入口 | 同文件 `fly_session_receive_stream` / `receive_datagram`，约 301–333 行 | 只记诊断，最后仍 `INVALID_STATE`；不是已接通的 decoder→auth→reducer |
| 接收分帧 | `session_receive.hpp`、`flynes_session.cpp` 的 `record_received_stream` | 只识别传入完整缓冲区的第一个 record；没有按 stream_id 保存跨回调的半包状态，不能处理实际 QUIC 任意分段 |
| 已有 framing | `shared/src/session/wire/app_frame.{hpp,cpp}` | 已有 `u32be(length) + u16be(type) + body`、类型映射、通道白名单、上限、解析测试；早期文档“没有帧类型标记”已过时 |
| wire 覆盖 | `shared/schema/flynes_session_v1.schema` 实际有 59 个 kinds、6 个 messages；`session_codec.cpp` 是部分类型的校验/哈希分发 | 类型登记、字节可解码、密码学认证、语义可执行是四件事；不能以 registry 数量代表状态机覆盖 |
| 空通道 | `app_frame.cpp:173` 附近：Input、ROM、Video、Audio 白名单均为空 | 四类生产消息尚不能进入公共接收链。CanonicalInputBundle 的 validator 不等于实时输入链完成 |
| 初连承载锁定 | `initial_plan_lock.{hpp,cpp}`、`wire/pair_capability.*`、`session_initial_plan.hpp` | 有共同计划选择、persist-before-act、代次、系统提示预算等可复用规则；`Verified*Evidence` 是私有测试接缝，生产没有真实认证来源 |
| command 完成 | `InitialPlanLock::complete`，以及公共 `complete_command` | 重复/乱序完成可能使当前初连尝试失效；原规范要求重复回报幂等，未来生产接口需明确区分相同重复、过期与冲突 |
| 身份与好友 | `harmony/nearby/nearby_adapter.cpp`、`friend_store.cpp` | host-only stub，假 `host-fake` / `InMemoryFriendStore`，不能作为设备身份或安全持久化实现 |
| 平台联机接线 | Android JNI、Harmony N-API、iOS bridge 与产品 CMake | 未发现生产 `fly_session_*` 调用或 session 链接。Harmony `NearbyService.ets` 名称虽为 Service，当前主要是 UI 文案/策略，不是网络服务 |
| QUIC | `tools/nearby-quic-spike` | 独立 Quinn/rustls 实验。C ABI 是 worker 上运行、固定 20 秒的阻塞 probe，手工 endpoint/pin；不是产品异步 TransportPort |
| 公共模拟 | `shared/include/flynes/flynes_runtime.h`、`shared/src/runtime/flynes_runtime.cpp` | 已有四路 buttons/sequence、逐帧 epoch/index 校验、12 槽回滚、frame/PCM 元数据、checkpoint。没有公共联机输入调度、提交日志和恢复事务 |
| Android 模拟入口 | `app/src/main/cpp/nes_jni.cpp`、`AudioThread.java`、`AudioPump.java`、`session/EmulationSession.java` | 仍直接调用 `nes_*`；音频 pump 调 core frame-step，输入是本机单 mask。虽然产品链接 runtime，当前播放路径未因此统一 |
| Harmony / iOS 模拟入口 | `harmony/entry/src/main/cpp/play_session.cpp:39`、`ios/app/bridge/FlyNesRuntimeBridge.mm:96` | 都使用 runtime，但本地设置 `buttons[0]`、本地推进 frame；不是双方 seat→ports[4] 的公共调度 |
| PCM 读取 | `flynes_runtime.cpp:900` 的 `fly_runtime_pull_pcm` | destructive 单消费者 ring。竞争/欠载返回零填充、`sample_count=capacity`、sequence/time=0；这是本机回调兜底，不能作为可认证的 canonical 音频块 |
| 媒体路径 | 产品目录搜索编码器、decoder、QUIC API；现有 iOS `PlaybackClock.hpp` / `PlaybackAudioQueue.hpp` | 有本地播放时钟/队列，没有生产 H.264 编解码适配、网络媒体分片/重组、A/V jitter 或共享发布音频分发 |
| 恢复与存档 | runtime checkpoint/rollback、部分 wire recovery object validator | 原规范定义的 RecoveryPoint→canonical tail→durable head→ReplayableProof→transition WAL 尚未构成生产执行路径；普通单机 checkpoint 不能替代这条链 |

额外协议风险：`app_frame.cpp` 目前把 1200 称为 QUIC 应用 datagram 的保守下限。RFC 9000 的 1200 是 UDP payload 相关约束，**不保证应用 DATAGRAM 有 1200 bytes 可用**。当前空白名单只会拒绝，因此尚无已工作的媒体回归；接通前必须改为取 backend 当下可发送应用 payload 的实际上限，再扣除全部应用头。Audio 的 960 bytes 也要计算完整头部/FEC 长度。[RFC 9221 §3–5](https://www.rfc-editor.org/rfc/rfc9221.html)；[Quinn Connection API](https://docs.rs/quinn/0.11.11/quinn/struct.Connection.html)。

## 3. 本次实际验证

重新构建现有 `out/main-repair-shared` Debug build，再运行全部 49 项 CTest：**49/49 通过，0 失败，CTest 用时 19.84 秒**。

```powershell
$cmakeBin = 'C:/Program Files (x86)/Microsoft Visual Studio/2022/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin'
& "$cmakeBin/cmake.exe" --build out/main-repair-shared --config Debug --parallel 4
& "$cmakeBin/ctest.exe" --test-dir out/main-repair-shared -C Debug --output-on-failure
```

缓存中的 `CMAKE_HOME_DIRECTORY` 已核对指向本 worktree 的 `shared`，不是另一工作树。命令行 PATH 未提供 cmake，改用了缓存记录的 Visual Studio CMake；首次找不到命令不算构建结果。构建输出为忽略目录中的 `out/main-repair-shared/nearby-design-audit-build.log`，CTest 记录在该目录 `Testing/Temporary/LastTest.log`。

测试结果的含义：

- runtime 测试证明已有本地逐帧、回滚、checkpoint 等基础行为；PCM contention 测试证明实时读取竞争时不阻塞，并保留 canonical producer 游标。
- `test_session_public_path.cpp`、`test_session_receive_seam.cpp` 明确要求未认证公共接收返回 `INVALID_STATE`，没有使用真实双端握手。
- `test_session_codec_loopback.cpp` 是字节/对象闭环，不是三端产品 QUIC 闭环。
- 本轮没有运行 Android/Harmony/iOS 产品原生测试，没有安装或操作手机，没有获得新的物理联机、延迟或发热证据。

## 4. 已有设备证据与 CI 边界

[2026-09-10 QUIC 验收记录（含 09-11 追加）](2026-09-10-nearby-quic-spike.md)明确记录 Android listener ↔ Windows connector 成功、错误 pin 被 TLS 拒绝；反向角色超时。Harmony native/HAP 构建不代表手机执行成功，记录中 Android↔Harmony QUIC、iOS native、离线路由器无关承载、配对及 gameplay 均未确立。本轮没有重新探查设备，不能把历史“未完成”当成设备今天是否连接或授权的判断。

当前 `.github/workflows` 只有 `stage0.yml`、`ios-stage1.yml`、`ios-product.yml`。stage0 构建 core smoke；iOS 工作流包含主机合同、平台构建及 stage1 smoke。没有 shared Session 全量 CTest、真实三端联机和跨端媒体协议的持续门禁。Harmony 的 `tools/quality/run_harmony_completion_gate.ps1` 和 Hypium 已存在，因此旧计划“没有 Hypium”同样不再准确；现有它们也不代表端到端多人联机验收。

## 5. 修复分类

| 分类 | 内容 | 优先级 |
|---|---|---|
| 必须先补的公共合同 | 系统依赖注入、版本化事件/command/snapshot、连接与游戏生命周期、wire 覆盖/认证边界 | P0 |
| 可复用并接通 | capability 选择、InitialPlanLock、framing/validators、四路 runtime、checkpoint、内容身份扫描 | P0–P1 |
| 必须新建的闭环 | 身份认证与好友存储执行、承载/QUIC ports、模拟调度、双人输入、媒体发布/接收、持久恢复 | P0–P2 |
| 必须纠正的误接风险 | 以 hash 当认证、直接放行公共入口、复用 probe/fake store、两个消费者拉 PCM、Android audio pump 推 canonical core、硬编码 1200 | P0 |
| 必须补齐的验收 | 公共仿真网络/崩溃注入、三端原生接线、九个 authority 方向与 18 个 seat 配置、物理延迟与断链恢复 | 发布阻断 |

此清单是设计输入，所有项的修复完成状态仍为“未实施”。

## 6. 工作树归属说明

本轮开始时工作树干净。共享测试结束于本地 19:15；收尾检查时，另外出现 `shared/CMakeLists.txt`、`shared/include/flynes/product/nearby_ui_state.hpp`、`shared/tests/test_product_nearby_ui.cpp` 的改动，文件时间为 19:24–19:26，未由本轮操作产生。本任务未修改、回退、暂存或提交这些文件。现状结论锁定上述代码基线；49 项结果不包含随后新增的 UI test target，也不替这些并发改动背书。

本次只交付本记录和修复设计两个 Markdown 文件，不提交版本 hook 会连带修改的版本元数据。
