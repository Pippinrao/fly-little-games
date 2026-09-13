# iOS 安卓复刻阶段交接（2026-09-11 第二轮）

## 本轮做了什么

承接 `ios/docs/2026-09-11-stage-handoff.md` 的停点继续推进。上一轮的第 1、3 项已落地并
在真实模拟器上验证；第 8 项的设备构建链路已打通。**这不等于安卓完整复刻完成，也不等于
iPhone 16 Pro Max 已通过验收。**

## 已接入产品并验证的成果

### 1. 受信标题与别名搜索（上一轮待办 1）

- `FlyNesAppBridge.mm::catalog_row_dictionary` 改为调用已有的
  `FlyNesCatalogPresentation`：内置来源用受信清单，外部文件只用文件名（去掉最后一个扩展名），
  ZIP 用外层包名与解码后的条目路径两个候选。
- 行字典补齐 `titleEn` / `titleZhHans` / `titleUnknown` / `searchAliases` /
  `entryPath`；`GameCenterItem` 的别名参数改收聚合别名（文件名、相对路径、包名、条目名、
  两个语言标题），共享 `GameCenterState::filtered` 因此能同时按文件名和任一语言标题命中。
- Swift 侧新增 `CatalogGameFactory`，用 `title(forFields:locale:)` 按当前界面语言取主/副标题；
  `CatalogGame` 携带 `titlePrimary` / `titleSecondary` / 两个语言字段。
- 卡片与左侧详情显示本地化标题，第二语言作为副标题/元信息；内置徽标只在没有副标题时出现。
  卡片无障碍标签也带副标题。
- **`CatalogPresentation.mm` 此前没有加入产品 target**，本轮补上（否则链接失败）。

证据：`CatalogTitleCoverTests` 10 项通过（受信双语标题、外部文件名不成为翻译、ZIP 外层优先、
别名搜索、未分类文字兜底）；XCUITest `testAndroidBilingualTitlesAndAutomaticCoverCapture`
在模拟器上确认英文界面显示 `From Below` + `来自下方`，中文界面显示 `来自下方` + `From Below`，
且**不再出现 `from_below.nes`**。

### 2. 自动封面（上一轮待办 1）

- 新增 `platform/CoverCapturePolicy.hpp`：`CoverCaptureSession` 组合已有的
  `GameCoverPolicy.hpp`（120/240/360/480 偏移、阈值 18、改进 > 1），每个游玩会话重新开始评分门限，
  与 Android `CoverCaptureCoordinator` 一致。
- 新增 `platform/FlyNesCoverStore.h/.mm`：`<cacheRoot>/covers/v1/<SHA256(canonicalID)>.png`，
  320x240 最近邻转换、`rename(2)` 原子写入、24 张内存缓存、目录标记为不备份，
  写入后发 `FlyNesCoverStoreDidChange` 通知。文件名不泄露标题或设备路径。
- `FlyNesRuntimeBridge` 新增 `copyLatestRgb565FrameWithSequence:width:height:`，暴露运行时权威的
  `frame_sequence` 与几何，避免用视图本地计数器。
- `RunSurfaceViewController` 在每次产生帧后采样（包括显示跳过的步进），在串行后台队列评分并持久化；
  换游戏时重置会话。**绝不采样 UI、控件、CRT 或暂停抽屉。**
- 新增 `CatalogCoverModel`（SwiftUI 可观察缓存）与卡片/左详情封面显示，未采样成功时回退为标题占位。

证据：`CatalogTitleCoverTests` 覆盖采样偏移、黑屏/曝光拒绝、改进门限、320x240 持久化与最近邻正确性、
文件名不泄露、异常输入拒绝；XCUITest 在模拟器上确认 6 秒游玩后容器内出现 PNG 封面。

### 3. 启动失败留在游戏中心（上一轮待办 3）

- Play 不再是自包含 `NavigationLink`。`CatalogSourceModel.launch(canonicalID:title:onReady:)`
  先解析 ROM，成功才由 `CatalogLibraryView` 把 `LibraryRoute.run(canonicalId:rom:)` 推入路径。
- 解析中状态栏显示「正在启动 %@…」，失败显示 Android 原文
  「无法启动所选游戏，请重新扫描它的来源。」并保留网格与选中态。
- 新增 `library.launching_game` / `library.launch_failed`（英/中）。

### 4. 来源导入（上一轮待办 2，部分完成）

新增 `ios/tests/CatalogSourceImportTests.mm`，用真实 `CatalogSourceService` + `FlyNesAppBridge`
驱动测试：目录导入（中文文件名 + 含中文条目名的 ZIP + 不支持的 `.txt` 被忽略）、
ZIP 外层/条目标题候选、两个游戏都能按持久化来源重新读出原始字节、收藏与最近记录跨重启保留、
重扫保持不变、删除来源清空目录、重复载荷共享 canonicalId 但保留独立 variant 且游戏中心只显示一张卡、
不支持扩展名/类型不符/取消导入被拒绝。

**仍未覆盖**：真实系统 Files picker 的点击与取消（系统 UI 自动化脆弱）。插件本身是
`fileImporter` → `CatalogSourceService.add(url:directory:)`，其逻辑已被上述测试覆盖。
真机验收清单里保留了一条手工勾选项。

### 5. iPhone 16 Pro Max 就绪（上一轮待办 8，构建链路完成）

- `ios/app/CMakeLists.txt` 新增 `FLYNES_IOS_SIGNING_TEAM`：为空时构建未签名 arm64 归档
  （Sideloadly 路径），填写 Apple 开发团队 ID 时启用自动签名并校验签名与描述文件。
- 新增 `ios/scripts/build_device.sh`：arm64 设备构建，校验产物含两种语言、`default.metallib`、
  且 arm64。
- 新增 `ios/scripts/package_device.sh`：生成 `FlyNES-unsigned.ipa` 与 SHA-256。
- 新增 `ios/docs/iphone-device-acceptance.md`：真机安装路径（Sideloadly / devicectl）与逐项
  验收清单（灵动岛与安全区、音频、多点触控、触觉、封面、30 分钟、语言、来源）。

证据：Mac 上执行 `bash ios/scripts/build_device.sh` 成功，输出
`build/ios-device/Release-iphoneos/FlyNES.app`，`lipo -archs` = `arm64`，未签名无描述文件。
**真机未连接、未安装、未验收。**

### 6. 持续游玩与暂停循环（上一轮待办 6，部分完成）

新增 `ios/tests/PlaybackSoakTests.mm`：无输入连续步进（默认 36000 帧，可用
`FLYNES_SOAK_FRAMES` 放大）、逐帧校验序列号单调、几何 256x240、PCM 队列有界、封面恰好命中四个
偏移、常驻内存增长上限；另一项做 30 轮「存档 → 清空音频 → 恢复」并确认目录快照不变。

这**不是** 30 分钟真机游玩。真机 30 分钟与温度/掉帧仍需用户在清单上确认。

## 模拟器最终结果

| 套件 | 结果 |
|---|---|
| FlyNESRuntimeTests | 见 `ios/docs/evidence/2026-09-11-round2-simulator.txt` |
| FlyNESUITests | 同上 |
| Windows 源码/契约检查（9 个 Python） | 同上 |

## 仍未完成（按建议顺序）

1. **真机验收**：签名/安装、灵动岛与安全区、音频听感、多点触控与触觉强度、30 分钟性能、
   系统 Files picker 手工勾选。只按 `view.bounds` / `safeAreaInsets` / `drawableSize` 布局，
   不硬编码旧机型分辨率。
2. **高刷新质量门限**：显示节拍仍固定 60；`FlyNesDisplayLinkPacer` 记录的仍是
   targetTimestamp/tick 而不是真实 drawable presentation，改完才能放行 120Hz。
3. **输入外设**：Android 的硬件键盘/手柄接线仍未迁移（触控主路径已验证）。
4. **最后一项核对**：逐项对照 Android 页面与功能做代码审查；本轮已更新
   `test_product_android_parity_contract.py` / `test_product_catalog_contract.py` 以匹配新行为。
5. 未合并回原分支、未开 PR、未发布。

## 继续构建与测试

Mac 镜像：`/Users/apple/Developer/fly-little-games-ios`（同步目录）。Windows 权威源码：
本 worktree。

```bash
cd /Users/apple/Developer/fly-little-games-ios
export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
CMAKE_BIN=/Users/apple/Developer/FlyNES-tools/cmake-3.31.8-macos-universal/CMake.app/Contents/bin/cmake
"$CMAKE_BIN" -S ios/app -B build/ios-simulator -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT="$(xcrun --sdk iphonesimulator --show-sdk-path)" \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 -DFLYNES_IOS_BUILD_XCTESTS=ON
"$CMAKE_BIN" --build build/ios-simulator --config Debug --target FlyNES FlyNESRuntimeTests FlyNESUITests --parallel 2
python3 ios/scripts/run_simulator_tests.py 988243AC-5704-45B6-9151-FF3A9B7AFD35
python3 ios/scripts/run_simulator_tests.py 988243AC-5704-45B6-9151-FF3A9B7AFD35 FlyNESUITests
bash ios/scripts/build_device.sh
```

新增 XCTest 源文件后必须重新 configure：`xctest_add_bundle` 的源列表只在 configure 时读取。

Mac 限制不变：`MacBookPro14,1` / macOS 13.0.1 / Xcode 14.3.1 / iOS 16.4 模拟器 / x86_64，
无法创建 iPhone 16 Pro Max 模拟器；真机需匹配手机实际 iOS 版本的工具链或用 Sideloadly。
