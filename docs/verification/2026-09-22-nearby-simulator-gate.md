# 附近联机模拟器软件门槛记录

日期：2026-09-22。基线：`main@8b071ad` 加当前工作区改动，版本 `1.7.11`。

目标是证明当前最小可玩路径的软件部分已经在主机测试、Android 模拟器和 HarmonyOS
模拟器中闭环。本文不把模拟器结果解释为物理热点、摄像头、触屏手感或扬声器体验。

## 测试目标与证据

| 代号 | 目标 | 结果 | 原始证据 |
|---|---|---|---|
| R29 | Rust QUIC provider：22 单元 + 3 FFI + 4 transport | 29/29 PASS | `out/nearby-mvp/gate-20260922-042022/nearby-quic-provider.log` |
| H11 | MVP 邀请、协议、会话、32 秒延迟、空闲、真 ROM、双输入、停顿、120 秒过期 | 11/11 PASS | `out/nearby-mvp/gate-20260922-035319/shared-ctest.log` |
| O14 | Harmony native 播放、owner、内容、渲染、音频等受影响主机回归 | 14/14 PASS | `out/nearby-mvp/gate-20260922-035319/harmony-host-ctest.log` |
| A40 | Android Nearby 产品页、QR、同进程真 QUIC 客机、32 秒等待、布局 | 40/40 PASS | `out/nearby-mvp/gate-20260922-035319/android-nearby-ui.log` |
| A16 | Android 原有触控、播放与单机冒烟 | 16/16 PASS | `out/nearby-mvp/gate-20260922-035319/android-single-regression.log` |
| O10/O5/O7 | Harmony 服务 10、扫码消费 5、UX 7 | 22/22 PASS | `out/nearby-mvp/gate-20260922-035319/harmony-*.log` |
| O18 | Harmony 原有播放与游戏操作回归 | 18/18 PASS | `out/nearby-mvp/gate-20260922-035319/harmony-Builtin*.log` |
| X3 | Android 产品 host/P1 与 Harmony N-API guest/P2，真实 QUIC、真 ROM/核心、双输入、帧和 PCM | 3/3 PASS | `out/nearby-mvp/gate-20260922-042022/` |

`gate-20260922-035319` 的平台和主机分项均通过；该次旧跨端取证比较了不同结束时刻的
最新帧，因此总 gate 没有写 PASS。修正后的 X3 在双方完成的同一第 599 帧取证，并继续
运行到共同截止时间。首轮持续 10 分 15 秒，Android/Harmony 最终分别完成 48,979/
49,008 帧；两端第 599 帧 SHA-256 相同，且均有非空 PCM。随后两轮各完成 600 帧，
会话 ID 在每一轮两端一致，三个会话均为重新建立。`run.txt` 最终记录 `result=PASS`。

## PLAN 29 组软件场景映射

每组由所列层级共同证明，不用主机测试代替平台行为，也不用 UI 文案代替真实会话。

| 组 | 主要自动化证据 | 结果 |
|---|---|---|
| C01 | H11 invite/protocol；A40 真实 QR 图像解码；O10/O5 非法扫码拒绝 | PASS |
| C02 | Android `NearbyMvpLanAddressTest` 与 A40 产品建房；runner 校验模拟器 UDP 映射 | PASS |
| C03 | H11 `delayed_join`、`expiry`；A40 32 秒原邀请；X3 实际加入 | PASS |
| C04 | R29 错 pin；H11 错 token、单客机；X3 保留 pin/token 完成真实握手 | PASS |
| C05 | R29 生命周期/取消；A40 重建邀请；O5 重复扫码；X3 三轮新会话 | PASS |
| C06 | R29 连接期限；H11 `idle`、`stall`；平台失败/重试 UI 回归 | PASS |
| C07 | O5 用 mock port 覆盖扫码生命周期；O10 覆盖 native 非法 QR；X3 覆盖真实 N-API/解析/QUIC | PASS |
| C08 | R29 close/cancel；A40 页面退出；O5 隐藏恢复/迟到结果；X3 每轮强制清场 | PASS |
| C09 | A40 lobby/同 owner；O14 owner；X3 同一会话 ID 跨配对、确认和运行 | PASS |
| C10 | A40 40 项 Nearby UI；O10/O7 17 项服务和横屏 UX | PASS |
| G01 | H11 `invalid_rom`、`rom_mismatch`；X3 两端从同一内容清单加载真 ROM | PASS |
| G02 | H11 配置摘要协商与拒绝路径；X3 两端确认同一真实核心配置摘要 | PASS |
| G03 | H11 `ready`；A40 lobby 确认；X3 双确认后仅启动一次 | PASS |
| G04 | H11 `play` 从 frame 0 启动并固定座位；X3 Android P1/Harmony P2 | PASS |
| G05 | H11 `play`、protocol 的两帧缓冲/单边等待；X3 不同 P1/P2 输入 | PASS |
| G06 | H11 protocol 的分包、粘包、非法长度、重复、冲突、迟到和越窗 | PASS |
| G07 | H11 `stall` 与每 60 帧摘要；X3 十分钟持续推进和公共帧一致 | PASS |
| U01 | A16/O18 真实控件按下释放；X3 平台桥提交固定座位的不同 mask | PASS |
| U02 | H11/O14 单 owner/runtime；X3 每端只消费联机 runtime 输出 | PASS |
| U03 | H11 真核心同帧画面；X3 双端第 599 帧完整 RGB565 哈希一致 | PASS |
| U04 | H11/O14/A16 音频生产与消费回归；X3 两端 PCM 样本数均大于零 | PASS |
| U05 | R29 关闭/取消；A40/O5 页面和扫码生命周期；重新进入可运行 | PASS |
| U06 | A16 Android 单机；O18 Harmony 单机；O14 native 播放/音频 | PASS |
| E01 | X3 首轮 10 分 15 秒，持续双输入、帧和 PCM，无意外结束 | PASS |
| E02 | H11 `stall` 故障注入；R29 close/cancel；平台退出清理回归 | PASS |
| E03 | X3 三轮建房到运行；R29 repeated connections 回到资源基线 | PASS |
| E04 | R29 admission/worker/late callback/close 矩阵；两端 owner 取消回归 | PASS |
| E05 | X3 只使用数值局域端点和模拟器 UDP，不查询 DNS/云服务 | PASS |
| E06 | runner 拒绝物理序列号，严格解析用例数/失败/超时/崩溃并保存证据 | PASS |

## 仅剩真机验收

1. vivo 系统的安装确认、权限与厂商安全策略。
2. 真实摄像头对焦、二维码识别和 Scan Kit 系统扩展返回 App。
3. Android 个人热点与 HarmonyOS WLAN 的真实 AP、路由和射频路径。
4. 两人实际触屏操作、物理屏幕、扬声器、可感知延迟和十分钟体验。

以上四项使用同一候选版本集中验收。真机若暴露软件缺陷，先补成主机或模拟器回归，
再修复和复验。
