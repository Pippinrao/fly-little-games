# 热点真机测试与一次连接诊断

本记录补充 [模拟器门槛](2026-09-22-nearby-simulator-gate.md)。当前尚未通过真机
十分钟试玩，不能记录最小可玩完成。

## 晚间复验更新（取代下文的当时待部署状态）

- 诊断包已部署；`out/nearby-mvp/gate-20260922-224424/` 的真实热点自动测试通过 600 帧，双方公共第 599 帧摘要相同。
- 22:45–22:46 用户完成一次真实摄像头扫码，双方确认后进入 RUNNING 并持续推进。
- 用户反馈画面同步但不能正常操作、鸿蒙操作页不同；旧 PCM 非空测试也漏掉了实际音频播放。
  因而上述连接通过不代表真实可玩完成。
- 已回到双模拟器修复复用单机页面、输入积压、音频积压、暂停和保连接换游戏，见
  [基本可玩闭环记录](2026-09-22-nearby-basic-play.md)。本轮新代码尚未复验物理触屏及扬声器。
- ADB offline/HDC 消失的 USB 调试问题未确定根因；目前物理设备不在设备列表中。

以下保留首次失败现场及诊断方法用于追溯。

## 本次真机观察

- Android：vivo V2324A / Android 16；HarmonyOS：HBN-AL80 / HarmonyOS 6.1。
- 两端原安装 1.8.1，已使用保留数据的覆盖降级安装当前主干候选 1.7.11；没有卸载或清数据。
- vivo 热点 `ap0=10.244.139.74`；鸿蒙曾获得 `wlan0=10.244.139.138`，实测 ping
  房主 2/2 成功（21–23 ms）。这只证明当时热点 IP 连通，不代表 QUIC 或游戏已通过。
- 为验证离线场景，关闭了 vivo WLAN 和移动数据，保留热点。调试连接断开前未恢复。
- 22:15 的扫描返回后，客机 `state=4 reason=2 transportResult=-4 operation=4`。
  随后检查到鸿蒙已回到原 WLAN（192.168.3.x）。这是已观察到的网络切换，尚不能
  断定后续所有失败都由该原因造成。
- 重新加入热点后的另一次连接最终 `operation=7`（读取）失败。旧日志没有底层错误
  类别、两端消息轨迹与回调时间，根因未确定。
- 用户要求先补全日志和自动定位，不再反复手动扫码。诊断包准备期间 vivo 变为
  ADB offline，鸿蒙物理目标从 HDC 列表消失；新增诊断尚未部署到真机。

## 诊断实现

共享层提供可选诊断 sink，两端分别写入 `FlyNesNearby` 的 logcat/hilog。每行含
本地递增序号、会话创建后毫秒数、角色、状态/原因、帧号、待处理操作/回调/写队列数、
累计收发字节数。记录以下事件：

- 房主绑定地址及实际监听端口、客机绑定地址和目标端点。
- QUIC 操作提交、异步回调到达、主线程处理完成；操作编号可关联三者。
- JOIN/ACCEPT/CONFIG/READY/START/END 的类型与长度，不记录消息正文。
- pump 超过 1 秒未运行、状态切换、每 300 帧进展、终止前状态。
- 传输错误类别 `timeout/tls/closed/reset/io/transport`。类别用于缩小问题范围，
  不替代具体根因；任意远端关闭文本不写日志。
- Harmony 扫码开始、返回长度、扫码错误码、join 结果、页面出现/隐藏及 Ability
  前后台事件；Android 配对页面暂停、恢复和销毁事件。

操作编号：1 临时证书、2 监听、3 接受连接、4 连接、5 打开流、6 接受流、7 读取、8 写入。
二维码、token、pin、ROM 内容和签名材料不进入诊断日志。诊断回调不得重入会话 API。

## 自动复现

`tools/quality/run_nearby_mvp.ps1` 默认仍使用模拟器，显式 `-PhysicalHotspot` 才允许真机。
真机模式仅接受 `-CrossOnly`，核对邀请端点确实来自 Android 热点；原样传递邀请，
不经过 PC 端口转发。先安装同一候选版本的产品包和测试包，手动连好热点后执行：

```powershell
pwsh -NoProfile -File tools/quality/run_nearby_mvp.ps1 `
  -PhysicalHotspot -CrossOnly `
  -AndroidSerial 10ADBP18BQ0011Y -HarmonyTarget 33Z0224A11002736 `
  -CrossDurationMinutes 0 -CrossRounds 1 -CrossFrames 600
```

Android 使用产品配对 Activity，Harmony 使用产品 N-API 的测试入口。测试自动生成并
注入邀请，无需扫码。该路径用于诊断真实热点/QUIC/核心，不认证摄像头、Scan Kit
生命周期、物理触控或扬声器体验。传输问题定位后，再集中做一次真实摄像头产品复验。

每轮成功或失败都保存两端 `FlyNesNearby` 日志、每 2 秒的网卡时间线；失败终止测试 App。
`run.txt` 记录版本、Git 基线、设备目标、包 SHA-256、故障注入标记和结果。工作树仍未提交，
Git SHA 不能单独代表候选包内容。输出目录为 `out/nearby-mvp/gate-时间戳/`。

加 `-DiagnosticFault wrong-pin` 可以主动制造 pin 校验失败。此时预期 runner 报 FAIL，
两端记录 `category=tls`，用于验证失败现场采集，绝不将该次计为连接通过。

## 验证证据

- 日志回归先红后绿：缺失监听/提交事件时两条断言失败；实现后通过。
- Rust 错误分类先红后绿；完整 Rust suite 23 个单元、3 个 FFI、4 个 transport 通过。
- 共享 MVP CTest 11/11；Harmony host CTest 14/14。
- Android 构建、单元测试通过；配对 instrumentation 6/6。
- Harmony 签名产品包/测试包构建通过；扫码生命周期 mock 5/5、服务 10/10。
- 跨模拟器正常连接 600 帧、公共帧哈希一致、双端 PCM 非空：
  `out/nearby-mvp/gate-20260922-222518/`。
- 错误 pin 注入按预期失败，两端分别定位在 Connect/Accept 的 TLS 阶段，失败现场已保存。
- 本轮构建和回归输出：`out/nearby-mvp/device-20260922/`。

以上为首次部署前验证；其后的热点及扫码复验结果见本文开头更新。
