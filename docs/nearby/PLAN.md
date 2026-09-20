# 附近联机最小可玩执行计划

日期：2026-09-21。设计：[DESIGN.md](DESIGN.md)。起点：已收束的主干 `dfd23f9`。
目标：Android 房主/P1 ↔ HarmonyOS 客机/P2，经同网扫码，在两个 App 中玩一款真实游戏。

本轮文档交付不执行以下功能开发或测试。下一次实施从 P1 开始，逐步更新
[STATUS.md](STATUS.md)，不再新建第二份计划或复制旧接管提示。

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
4. 先验证两个独立进程通过真实 socket 往返，再安装同一主干构建的两个原生 App，
   用扫码入口完成 JOIN/ACCEPT。无设备时记录该物理步骤未运行，保留其后可独立推进的工作。
5. 错 pin/错 token 拒绝、取消建房能结束连接各做一个定点用例；通过后提交。

**通过证据：** 同一会话 ID 的 Android/Harmony App 日志和已连接界面；真实网卡地址；
双方消息确经 QUIC。测试入口注入二维码文本只算解析/接线测试，不算完成扫码旅程。
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
3. 打开 Super Tilt Bro. 的双人对战，两端轮流按方向、攻击、Start，观察对方设备
   显示相同玩家动作。记录游戏帧与输入端口，避免“两个单机画面同时动”的假通过。
4. 运行本轮修改涉及的 Android UI instrumentation 和 Harmony Hypium 用例，
   只修阻断输入、显示、声音或该入口的实际失败，然后提交。

**通过证据：** 两个 App 中 P1/P2 均可操作、两端有画面和声音的一段录屏及对应日志。
不顺带重做字体、渲染模式、全游戏兼容或三端单机验收。

## P4 — 完成一局试玩并给出可安装成果

文件：补齐同一个 coordinator 的 END/失联处理；新增
`tools/quality/run_nearby_mvp.ps1` 作为唯一双机验收入口；结果记入 STATUS。

1. 为主动退出、对端断网写两个定点失败用例，再实现停止核心、清按键、停止音频
   和关闭连接。等待已接纳的 provider 回调结束后销毁 owner。
2. 在 Android/Harmony 真实设备上，通过唯一扫码路径玩同一游戏 10 分钟。
   分别操作两个玩家，核对周期状态摘要；最后断网确认双方停止。
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
