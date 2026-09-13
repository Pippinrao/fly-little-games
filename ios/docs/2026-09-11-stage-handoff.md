# iOS 安卓复刻阶段交接（2026-09-11）

## 当前停点与用户要求

用户要求达到阶段性成果后停止开发、写交接文档并提交 Git。本阶段是「现有 iOS 分支已能在真实 Xcode/iOS 模拟器构建和运行，关键播放链路、输入与界面循环有自动化证据」，**不是安卓完整复刻完成，也不是 iPhone 16 Pro Max 已通过验收**。

必须保持的目标：

- 安卓现有界面结构、信息层级、布局、操作顺序和功能能力是唯一对照来源。允许使用现有原生 iOS 外观，不能另造产品结构。
- 继续既有 ios/harmony 分支成果，不从头开发。
- 最终兼容和真机目标：**iPhone 16 Pro Max**。用户已接受继续使用现有模拟器做基础回归。
- 先让模拟器版本可用，再接真机。不得把基础模拟器通过说成 16 Pro Max 通过。
- 本次停止后不启动新任务、不继续后台实现、不自行升级 Mac 系统。

## Git、目录与机器

| 项目 | 当前值 |
|---|---|
| 仓库 | `git@github.com:Pippinrao/fly-little-games.git` |
| 工作分支 | `codex/ios-simulator-ready` |
| 原始基线 | `codex/ios-harmony-port` 的 `091dfd8` |
| Windows worktree | `E:\workspace\codes\games\fly-little-games\.worktrees\ios-simulator-ready` |
| 原 ios/harmony worktree | `E:\workspace\codes\games\fly-little-games\.worktrees\ios-harmony-port`，未由本阶段改写 |
| SSH | `ssh apple` |
| Mac 产品镜像 | `/Users/apple/Developer/fly-little-games-ios`，是同步目录，不是 Git checkout |
| Mac 工具目录 | `/Users/apple/Developer/FlyNES-tools` |
| Mac | `MacBookPro14,1`，2017 Intel 双核 i5，8 GiB，macOS 13.0.1 |
| Xcode | `/Applications/Xcode.app`，14.3.1 / 14E300c，已完成签名验证、许可和首次组件安装 |
| CMake | `/Users/apple/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake` |
| 当前运行时 | iOS 16.4 |
| 当前模拟器 | iPhone 14，`988243AC-5704-45B6-9151-FF3A9B7AFD35` |
| 产品 Bundle ID | `com.flynes.app` |
| 产品路径 | `build/ios-simulator/Debug-iphonesimulator/FlyNES.app`（相对 Mac 产品镜像） |

Mac 环境不能直接创建 iPhone 16 Pro Max 模拟器：Xcode 16 要求 macOS 14.5 起，而该 Mac 官方最高支持 Ventura。此限制不妨碍当前代码继续兼容新 iPhone 的适配工作，但后续指定机型模拟、真机调试需匹配手机实际 iOS 版本的工具链。手机的实际 iOS 版本尚未确认。

## 已接入产品的阶段成果

| 范围 | 当前行为及证据边界 |
|---|---|
| 构建与打包 | iOS 16.4 / Swift 5.8 API 兼容，Intel x86_64 模拟器可构建；保留 arm64 device 配置。Metal 默认库、两种语言资源和许可证正确打包。真机签名未配置、未验证。 |
| 游戏中心 | 从「列表进入详情」改为安卓的左 30% 详情、右 70% 横向两行卡片；点击选中，再启动。四分类、搜索、按分类选择状态和恢复、收藏/最近记录。游戏源作为同页 34/66 分区显示并可关闭。大字体压缩标题区域、单行卡片网格。 |
| 来源与运行 | 内置 ROM 通过实际扫描入库；目录/文件选择、书签持久化、重扫、重新授权、删除来源；共享有界 RAW/ZIP 解析与双哈希校验。部分扫描保留记录，规范化重复条目，尝试有效的替代来源。Foundation 主机集成测试已有证据，但模拟器系统 Files 导入全流程仍待验收。 |
| 持续播放 | CADisplayLink 驱动持续帧，不依赖触摸变化；NTSC/PAL 节拍按实际样本计时；Metal 上传显示；48 kHz PCM 与有界系统音频队列；暂停、前后台、音频中断和自动存档/恢复。 |
| 输入 | D-pad、固定/跟随摇杆、浮动捕获、死区保持、扇区迟滞、A/B 滑动组合、多指同键所有权、17ms 短按、局部/全局取消、结束坐标/历史采样、尺寸变化取消、8 个可激活无障碍控件、按下视觉与 80ms 触觉门限。 |
| 设置与布局 | 安卓五分区侧栏、显示预设卡、即时持久化、控制区完整重置、音频/焦点/自动存档/语言、布局草稿保存/取消/撤销/试用/警告、英中语言与六份许可证。触觉实际强度只能在真机上最终确认。 |
| 视频 | Nearest/Sharp/MMPX/完整 ScaleFX 链路、CRT 叠加、RGB565→RGBA8、比例/整数缩放与黑边；MMPX 保留原 RGB565 比较语义。临时没有 drawable 不算永久失败；pipeline/队列/编码/GPU 终端错误会停止运行并提示返回。 |

## 已验证的测试

最终本阶段命令结果和结果包位置记录在同目录 `evidence/2026-09-11-simulator.txt`。不要只引用旧日志或源码检查来判断可玩性。

- 真实 iOS Simulator XCTest：**19 项通过**，包含 13 项多点触控/视觉/无障碍回归、5 项运行时与存档回归、1 项系统音频输出测试。
- 音频测试观察实际 `AVAudioEngine` mixer 的非零样本、播放器 sample time 持续增长及暂停停止，不只是检查 PCM 数组非空；没有声称已在用户真机听感验收。
- 真实 XCUITest：**3 项通过**；覆盖左右结构与选卡留在网格、来源面板开关、控制区重置，以及 **10 轮「暂停→设置→完成→继续→暂停→游戏中心→重开」**，最后退出进程再启动。
- Mac Metal GPU 测试：四种空间模式的方向/颜色/黑边、MMPX 精度、CRT、缩小视口、缺失 Sharp 时 Nearest 回退、永久故障与等待首帧的区别均通过。
- Windows 可运行的 iOS 检查脚本：**9 个通过**（其中源码检查只证明约定，不代替模拟器）。过时的「伪造内置可玩行」约定改为真实扫描约定，ABI baseline 补齐已有 control-layout API。
- 另有 Foundation 来源/包解析、RGB565 全色域、方向状态机和短按、布局草稿与语言辅助测试，见源码和已有测试报告。部分为独立主机可执行程序，尚未统一注册到 CTest。
- 共享主机完整回归此前有 ZIP fixture 生成器与不同 zlib 编码器的字节级不一致；最终复跑状态见证据文件。不要为让测试通过而改写黄金 ZIP。

## 尚未接入产品的已提交准备代码

`de7676a` 只提供 `CatalogPresentation.h/.mm` 和 `GameCoverPolicy.hpp`，附独立主机测试。它们没有加入产品调用链，也没有新增封面存储或 UI 展示。详细接线建议见 [标题与封面交接](catalog-presentation-cover-handoff.md)。

当前用户界面仍可能显示 `from_below.nes`；不能声称已经显示安卓受信标题「From Below / 来自下方」或已经有自动封面。

## 优先待办（恢复工作时按顺序）

1. **名称与自动封面接线**：使用已验证 helper，对照 Android 标题优先级、别名搜索和封面 120/240/360/480 帧采样、质量门限、320x240 持久化及缓存。
2. **系统导入验收**：在模拟器真实 Files picker 完成文件/目录、ZIP、中文名、重复、过期/拒绝授权、取消、来源变更、重启恢复；现有主机测试不能代替此项。
3. **启动错误留在游戏中心**：当前 `RunGameContainer` 解析失败会显示单独错误页，安卓是在库中显示失败并保留界面。需把准备 ROM 与成功导航的时机对齐，避免点击后才离开库显示加载失败。
4. **设置完整验收**：英中切换后 SwiftUI/UIKit 同步、重启持久化、编辑取消/保存/重置警告、大字体和横屏安全区；目前三项 UI 测试尚未逐一覆盖全部设置。
5. **输入外设**：Android 的硬件键盘/游戏手柄接线未迁移；触控主路径已验证。真机还需实际多指与触觉。
6. **30 分钟持续游戏与性能**：尚未执行，不能勾选；采集卡顿、队列、内存、温度/前后台恢复。此阶段 10 轮界面循环已完成。
7. **高刷新质量门限**：当前显示节拍固定 60；高级质量未资格验证。`FlyNesDisplayLinkPacer` 现有 `lastPresentedTime/presentedFrames` 仍记录 targetTimestamp/tick，而不是实际 drawable presentation；后续必须改为真实呈现统计后才可用于 120Hz 放行，不能仅看机型或 maximumFramesPerSecond。现阶段不要启用高级时域功能。
8. **iPhone 16 Pro Max**：用兼容的 Xcode/运行时完成指定模拟器后，再配置真机签名、开发者模式和设备调试，验证大屏横屏/灵动岛/安全区/声音/触控/生命周期。只按系统 view bounds、safeAreaInsets 与 drawableSize 布局，不硬编码旧机型分辨率。
9. 核对 Android 页面和功能逐项清单，做最终代码审查。当前没有合并回原分支、没有 PR、没有发布/分发。

## 继续构建与测试

Windows worktree 是当前源码权威位置；不要把 Mac build 目录或 `.artifacts` 打包回仓库。首次使用新 checkout 先初始化仓库固定的子模块。

在 Mac（`ssh apple` 后）：

```bash
cd /Users/apple/Developer/fly-little-games-ios
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
bash ios/scripts/build_simulator.sh
CMAKE_BIN=/Users/apple/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake
"$CMAKE_BIN" -S ios/app -B build/ios-simulator -DFLYNES_IOS_BUILD_XCTESTS=ON
"$CMAKE_BIN" --build build/ios-simulator --config Debug --target FlyNESRuntimeTests FlyNESUITests --parallel 2
python3 ios/scripts/run_simulator_tests.py 988243AC-5704-45B6-9151-FF3A9B7AFD35
python3 ios/scripts/run_simulator_tests.py 988243AC-5704-45B6-9151-FF3A9B7AFD35 FlyNESUITests
bash ios/scripts/run_product_simulator.sh 988243AC-5704-45B6-9151-FF3A9B7AFD35
```

`run_simulator_tests.py` 会给 CMake 生成的 scheme 补入 Testables，再用 test-without-building 执行；CMake 重新生成之后需再次通过脚本运行。修改 CMake 后主动重新 configure，不能依赖旧 Xcode ZERO_CHECK 恰好更新。

同步本阶段源码（Windows PowerShell，当前 worktree）：

```powershell
tar -cf .artifacts/ios-sync.tar ios/app ios/scripts ios/tests ios/abi
scp .artifacts/ios-sync.tar apple:Developer/FlyNES-tools/ios-sync.tar
ssh apple 'cd ~/Developer/fly-little-games-ios && tar -xf ~/Developer/FlyNES-tools/ios-sync.tar'
```

这只适用于已有完整 Mac 镜像。若 core/shared 代码或子模块也改变，要同步对应源，或在新 Mac 直接 checkout 已推送分支；不要重用来自不同架构的 build 缓存。

本地主机源码检查（PowerShell）：

```powershell
$checks = rg --files ios/tests -g 'test*.py'
$failures = 0
foreach ($check in $checks) {
    python $check
    if ($LASTEXITCODE -ne 0) { $failures++ }
}
if ($failures) { throw "$failures checks failed" }
```

Metal GPU 回归（Mac）：

```bash
xcrun --sdk macosx clang++ -std=c++17 -fobjc-arc -Wall -Wextra -Werror \
  -Iios/app/metal ios/tests/metal_renderer_test.mm ios/app/metal/FlyNesMetalRenderer.mm \
  -framework Foundation -framework Metal -framework QuartzCore -framework CoreGraphics \
  -o /tmp/flynes-metal-check
/tmp/flynes-metal-check ios/app/metal/shaders
```

## 证据位置与阶段提交

- Mac 最终日志：`build/ios-handoff-build.log`、`build/ios-handoff-runtime.log`、`build/ios-handoff-ui.log`。
- Mac `.xcresult`：`build/ios-simulator/evidence/`，准确文件名见证据摘要。
- Windows 本地附件：worktree 下 `.artifacts/`（不入 Git）；旧 `gameplay.png` 带 EXIF 旋转信息，不可把预览工具的旋转/裁切误判成游戏渲染错误。
- 阶段提交链包含播放 `0de9bd3`、来源 `9c25882` 及后续修复、设置 `9b2389f` / `0f5b4d2`、输入 `23780c6`、标题/封面准备 `de7676a`；最终整合及此交接以当前分支最新提交为准。
- 本文随代码一起提交到 `codex/ios-simulator-ready`。交接结束后保持 worktree，等待用户安排下一阶段。
