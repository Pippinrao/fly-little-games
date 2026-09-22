# 附近联机基本可玩闭环

日期：2026-09-22；工作区 `main`，版本 `1.7.11`，未提交改动。包哈希见各轮 `run.txt`。

## 修复

1. Android 使用既有 `MainActivity`，Harmony 使用既有 `RunGame`；手柄、暂停按钮、暂停抽屉复用单机实现。
2. 将本地输入提前量限制到约定的 2 帧；旧实现允许较快端填满 256 帧窗口，积压数秒旧输入。
3. 鸿蒙联机输出接到原生播放运行器及 OHAudio；修复空 PCM 轮询补整块静音导致的播放队列积压。轮询仅消费核心实际产出的样本。
4. 暂停同步冻结两端核心，不触发游戏停顿超时；回大厅先清空双方旧局输入，保持 QUIC 连接及会话 ID。
5. 房主从原有游戏库换游戏；客机读取本地内容，重新双确认后开局。修复内置目录身份映射与私人 ZIP 条目定位。缺少/不匹配 ROM 不主动关闭连接。

## 最终跨模拟器证据

`out/nearby-mvp/gate-20260922-235136/`，Android instrumentation 1/1、Harmony Hypium 1/1。

- Android `emulator-5554` 与 Harmony `127.0.0.1:5557` 两个真实应用、真实 QUIC/核心。
- 邀请由测试注入，跳过人工扫码；ROM 使用用户指定目录中的《雪人兄弟》，通过原有来源导入。
- Android `sendPointerSync` 从应用窗口注入触摸，Harmony UiTest 操作现有 Canvas 控件。
- Start、Select、A、B、方向及释放到达对应已完成核心帧；P1/P2 不串座。
- 实际点击房主暂停/继续，暂停超过 2 秒后保持连接，继续后推进。
- 实际点击大厅、原游戏库、更换游戏、双方确认；《雪人兄弟》→内置游戏→《雪人兄弟》，三局保持同一个会话 ID。
- 鸿蒙音频运行、消费样本断言通过；最终日志无音频队列丢弃，但物理播放延迟没有在本轮测量。
- Android 同一进程日志中的 14 次按下/释放，触摸到核心应用输入最小 12 ms、最大 32 ms、平均 23.8 ms。原始计算结果为 `cross-round-1/input-latency.txt`，不是屏幕端到端延时认证。

`gate-20260922-234448/` 是较早的三局通过轮。`234613` 暴露测试保留搜索条件导致选游戏为空，测试现已清除旧搜索；`234913` 被人工操作与自动暂停操作相互干扰，不计通过。最终 `235136` 无人工操作干扰。

较早的人工观察已在两边看到《雪人兄弟》的 1P/2P 关卡，截图保留在
`device-20260922/android-snow-play.png` 与 `device-20260922/harmony-snow-play.jpeg`。
截图不是同帧同步证明；同步由共享摘要及核心帧断言验证。

## 定点回归

输出均在 `out/nearby-mvp/device-20260922/`：

- 共享 MVP 完整 CTest 11/11：`basic-host-test.txt`；最后 PCM 修复后受影响子集 9/9：`basic-final-host.txt`。
- 暂停/回大厅/换局、输入窗口、实际端口和 PCM 空轮询分别保留失败输出与修复后输出；PCM 为 `pcm-red.txt` → `pcm-green.txt`。
- Android 构建及单元测试：`flow-fixed-android.txt`；单机/手柄/暂停 instrumentation 18/18：`basic-single-android.txt`。
- Harmony host CTest 14/14：`basic-harmony-host.txt`；单机 Hypium 3/3 与 15/15：`basic-single-harmony-smoke.txt`、`basic-single-harmony-play.txt`。
- Harmony 产品 HAP 与测试 HAP 构建成功；使用本地测试签名，不是商店证书。

## 边界

最新修复已安装两个模拟器；目前 ADB/HDC 未列出物理设备，所以未对新版重复扫码或安装真机。
旧版真实热点、摄像头扫码已连接成功；新版物理触屏、扬声器、热点延迟及十分钟试玩仍未验收。
本轮不包含 iOS、自动恢复、ROM 传输、全游戏兼容或硬件性能认证。
私人 ROM 未进入仓库内容包；临时导入服务器已停止，端口映射已移除。
