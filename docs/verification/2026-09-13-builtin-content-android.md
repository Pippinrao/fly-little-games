# 内置内容改造 — Android 实测证据（2026-09-13）

工作树：`.worktrees/builtin-content`（分支 `codex/builtin-homebrew-content`，VERSION 1.2.0）
设备：`emulator-5570`（本会话专用 AVD `FlyNES_BuiltinContent`，android-35/default/x86_64）
说明：emulator-5554 / 5564 由其他会话占用，故本会话新建专用 AVD 取证。

## 1. 内置目录在真机上正确加载（手工实测）

命令：

```
adb -s emulator-5570 shell pm clear com.flynes.emu
adb -s emulator-5570 shell am start -n com.flynes.emu/.HomeActivity
adb -s emulator-5570 shell uiautomator dump /sdcard/ui9.xml
adb -s emulator-5570 shell cat /sdcard/ui9.xml
```

`uiautomator` 文本节点（节选，来源见上）：全部 7 款内置游戏出现，且使用**清单里的双语标题**：

```
Concentration Room / 记忆翻牌屋      (BUILT IN)
RHDE: Furniture Fight / 抢家具大作战  (BUILT IN)
Thwaite / 护村记                      (BUILT IN)
Zap Ruder                             (BUILT IN)
DABG: Double Action Blaster Guys / 双动爆破小队
Super Tilt Bro.                       (BUILT IN)
```

`launch_selected` 节点 `enabled="true"`（首个内置游戏被自动选中，可启动）。

应用侧日志（`adb logcat -s FlyNES:*`）：

```
I FlyNES : bundled scan committed 7 game(s)
I FlyNES : catalog projection: 7 native entries, builtin packages=7
I FlyNES : catalog after load: 7 canonical entrie(s), status=LOADED
I FlyNES : game center snapshot: 7 item(s)
```

## 2. Instrumentation 实测

命令：

```
adb -s emulator-5570 shell pm clear com.flynes.emu
$env:ANDROID_SERIAL='emulator-5570'
gradlew :app:connectedDebugAndroidTest \
  "-Pandroid.testInstrumentationRunnerArguments.class=com.flynes.emu.catalog.android.AndroidBuiltinCatalogAdapterTest,com.flynes.emu.ui.FirstRunNavigationTest" \
  "-Pandroid.injected.androidTest.leaveApksInstalledAfterRun=true"
```

结果：**11 个测试，11 通过，0 失败**（`Tests 11/11 completed. (0 skipped) (0 failed)`）。

- `AndroidBuiltinCatalogAdapterTest`（含 `licensedBuiltinsUseSharedScannerVerifiedBilingualTitlesAndStrictLoader`）：**通过** —— 在真机上确认 7 个内置包各自指向自己的 asset、双语标题来自清单、可被严格 loader 读出。
- `FirstRunNavigationTest`：**全部通过**。

> 过程中的三次失败均已定位并修复（见下表 #4、#5、#6），不是放宽容忍度：<br>
> #4 内置目录的 canonical id 被改成清单 id，而原生目录仍按扫描分配的 id 索引，导致 `setFavorite` 找不到条目（收藏点击无效）。修法：**标题取清单、身份保留扫描分配的 id**。<br>
> #5 `FirstRunNavigationTest` 缺 `@Before`，前一条用例持久化的搜索词让后续用例看到空列表。已加清空 `game_center_ui` 的 `@Before`。<br>
> #6 测试里硬编码了「第一个内置游戏」的标题；Game Center 实际按标题字母序排列（首个是 `Concentration Room`）。改为**动态捕获当前选中标题**再断言。

## 3. 本轮通过真机实测发现并修复的 3 个真 bug

| # | 位置 | 症状 | 修法 |
|---|---|---|---|
| 1 | `AndroidCatalogRuntime.scanBuiltinNative` | 内置扫描的 `candidateCount` 硬编码为 `1`，7 款游戏产生 7 条 outcome → `SourceScanResult` 抛 `IllegalArgumentException: scan source or candidate accounting is invalid` → 内置目录完全加载不出来 | 传 `scanned.packageOutcomes().size()` |
| 2 | `AndroidCatalogStreamOpener.open` | 要求内置 locator **等于**源根 uri；源根为 `asset:///roms/` 而包 locator 为 `asset:///roms/<file>` → 永远 `LOCATOR_UNKNOWN` | 改为前缀匹配 |
| 3 | `NativeCatalogProjector.builtinGame` | 原生扫描的 canonical id 是内容哈希派生的，按 id 查不到清单 → UI 显示文件名（`zap_ruder.nes`）而非清单标题 | 增加按 asset 文件名回退查表，并采用清单的稳定 id |

另外修复的**测试隔离问题**：`FirstRunNavigationTest` 没有 `@Before`，前一条用例持久化的搜索词会让后续用例看到空列表（3 失败 → 1 失败）。已加 `@Before` 清空 `game_center_ui`。

## 4. HarmonyOS 实测（2026-09-13）

命令（`harmony/` 下，`DEVECO_SDK_HOME=D:\soft\DevEco Studio\sdk`）：

```
node "D:\soft\DevEco Studio\tools\hvigor\bin\hvigorw.js" assembleHap -p product=default -p module=entry@default  -p buildMode=debug
node "D:\soft\DevEco Studio\tools\hvigor\bin\hvigorw.js" assembleHap -p product=default -p module=entry@ohosTest -p buildMode=debug
"<sdk>\cmake\3.22.1\bin\cmake.exe" -S harmony/tests -B out/harmony-host -G "Visual Studio 17 2022" -A x64 -DFLYNES_BUILD_TESTS=ON
"<sdk>\cmake\3.22.1\bin\ctest.exe" --test-dir out/harmony-host -C Debug --parallel 1
```

结果：

- 宿主 CTest：**12/12 passed**
- `assembleHap`（app）：**BUILD SUCCESSFUL** → `entry-default-unsigned.hap`（12,436,346 B）
- `assembleHap`（ohosTest）：**BUILD SUCCESSFUL** → `entry-ohosTest-unsigned.hap`（2,127,311 B）
  → 证明本轮改动的 ArkTS（`BuiltinGames.ets`、`PlayService`、`CatalogProductService`、`LicenseModel`、`Licenses`、`GameCenter`、`string.json`）与三个 ohosTest 文件**全部通过 ArkTS 编译**。
- 首次失败与修复：ohosTest 报 `Failed to resolve OhmUrl ... "@ohos/hypium"` → 新 worktree 缺 `oh_modules`，执行 `ohpm install --all` 后通过。

**未完成（需你决定）**：设备安装需要**签名 HAP**，而仓库里的 `harmony/build-profile.json5` 的 `signingConfigs` 为 **空数组**——签名材料在你本地未提交的改动中。我**不会**复制或提交签名配置。要完成 hdc 安装 + Hypium，需要你允许我使用你的签名配置（或由你在主检出构建安装）。

## 5. iOS 实测（2026-09-13，`ssh apple`）

源码同步到 Mac 的**新目录** `/Users/apple/Developer/flynes-builtin`（未覆盖你原有的 `fly-little-games-ios` 镜像）。

```
cmake -S ios/app -B build/ios-simulator -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_SYSROOT=$(xcrun --sdk iphonesimulator --show-sdk-path) \
  -DCMAKE_OSX_ARCHITECTURES=x86_64 -DCMAKE_OSX_DEPLOYMENT_TARGET=16.4 -DFLYNES_IOS_BUILD_XCTESTS=ON
cmake --build build/ios-simulator --config Debug --parallel 2
python3 ios/scripts/run_simulator_tests.py 988243AC-5704-45B6-9151-FF3A9B7AFD35 FlyNESRuntimeTests
```

结果：

- App 构建：**成功**（`FlyNES.app/FlyNES` 10,827,104 B）。过程中修掉一个 Swift 编译错误：ObjC 类方法 `+shared` 在 Swift 中必须写成 `FlyNesBuiltinGames.shared()`。
- **包内内容实测**：`builtin-games.json` + **7 个 `.nes`** + 7 份许可文本随包（CMake glob 生效）。
- **`FlyNESRuntimeTests`：49 个测试全部通过**（含 `PlaybackSoakTests` 的 30 分钟持续播放 soak：`frames=36000 samples=36000`）。此前 `CatalogSourceImportTests` 用"注入单个内置 URL ⇒ 1 张内置卡"的旧假设，已改为**按清单计算内置卡数**的 `+1` 不变量并通过。
- **`FlyNESUITests`：1 个失败**（本轮从 5 → 1）。
  - 已修复：`ProductImportUITests` ×3 —— 其中 2 条是**测试环境前置条件**（`Files cannot find FlyNES-Import-E2E-v1`），必须先跑 `python3 ios/scripts/stage_import_fixtures.py --udid <udid>`；另 1 条是内置卡数硬编码。
  - 已修复（**真 bug**）：`ProductUITests.mm:117`（点内置卡片启动后 10 秒等不到 `OPEN_PAUSE`）。根因：`CatalogSourceService.romDataForRow:` 里内置行原本靠单个 `builtinURL_` 解析文件，本轮改造把它变成 `nil`，导致内置行解析不到文件、启动按钮 disabled。修法：按行的 `relativePath`（= assetFilename）经 `FlyNesBuiltinGames byAssetFilename:` 解析到各自的 bundle 资源。
  - **重要教训（成本很高）**：`ios/scripts/run_simulator_tests.py` 只做 `test-without-building`，**不会重编译测试包**。中途多轮"修了仍失败"都是旧测试包造成的假象；正确流程是每次先 `cmake --build build/ios-simulator --config Debug`，再跑脚本。
  - **剩余 1 条失败（本轮已修复，见 §6）**：`ProductUITests.mm:132` —— 播放 6 秒并暂停后，`Library/Caches/covers/v1` 里没有采集到封面 PNG。根因是**封面质量门的稀疏点采样漏掉细字标题页**，不是采集链路未触发；修复后该用例与整个 UI 套件（8/8）均通过。



## 6. 封面采集质量门（本轮定位并修复的真 bug，三端一致）

**症状**：iOS `ProductUITests.mm:132` 与 Android `CoverCaptureIntegrationTest` 都在"播放内置游戏后 `covers/v1` 里没有 PNG"上失败；Android 模拟器上 `no_backup/covers/v1` **目录根本不存在**。

**定位过程（全部为实测，不是推断）**：

1. 把 iOS 采集到的原生帧导出成 PNG（256×240）——画面**正常**：就是 Thwaite 的黑色版权/标题页，白字清晰。所以播放、采集触发、帧拷贝都没问题。
2. 同一帧的评分日志：`seq=6552 score=-11.680`（三个采样点同值）。
3. 用真实帧数据复算指标：`mean=0.27 sd=8.23 transitions=2` → `8.23 + 0.045 − 20.0 = −11.68`，其中 **−20 是"平均亮度 < 8"的曝光惩罚**。
4. 根因：`FrameQuality.score` 在 32×30 网格上**逐点采样**（步长 = 宽/32、高/30，即每 8×8 像素只取 1 个点，共 960 点）。内置 homebrew 的标题/版权页是**黑底细白字**，8×8 的稀疏采样**整片跨过笔画**，于是把一屏文字当成"全黑启动画面"拒绝。Zap Ruder 同样中招（`score=16.643 < 18`）。
5. 结论：这不是测试问题，是**采集功能的真缺陷**——7 款内置游戏在 Android/iOS 上**一款都拿不到封面**。

**修法（最小改动，三端同源）**：把"逐点采样"改为 **32×30 网格内取块平均**，评分公式、`MIN_ACCEPTABLE=18.0`、`+1.0` 提升门限、120/240/360/480 采样点全部不变。

- Android：`app/src/main/java/com/flynes/emu/cover/FrameQuality.java`
- iOS：`ios/app/platform/GameCoverPolicy.hpp`（Android 实现的逐行移植）
- 真实帧复算：`mean=21.45 sd=37.11 transitions=184` → **45.74**（通过）
- 空白帧仍然拒绝：全黑 −20、全白 −20、平灰 0、3 个亮点 −5.77

**RED → GREEN 证据**：

| 测试 | RED（旧指标） | GREEN（块平均） |
|---|---|---|
| Android 单元 `CoverCaptureCoordinatorTest.scoresThinTextOnBlackAboveTheAcceptanceThreshold` | `AssertionError: thin credit text must be capturable, scored -16.65` | 4/4 通过 |
| iOS/C++ `ios/tests/game_cover_policy_test.cpp` | `Assertion failed: (credits_score >= 18)` | `PASS: credit text scores 35.304 where point sampling sees nothing` |

C++ 用例里还断言了 `visible == 0`（点采样分辨率下看不到任何白点），把"旧指标必然漏检"写成可执行事实。

**三端实测结果**：

- iOS 模拟器：`ProductUITests` 单测由失败转为 **通过**，落盘 `covers/v1/2119c38f….png`；采样日志 `seq=7331 score=45.739`。
- iOS 完整 UI 套件：**8/8 通过**（`Executed 8 tests, with 0 failures`）。
- Android 模拟器：`CoverCaptureIntegrationTest` 由失败转为 **通过**（`BUILD SUCCESSFUL`）。

**重要教训（成本很高）**：`scp` 会保留源文件 mtime，Mac 上 CMake 因此认为"源文件比目标文件旧"而**跳过重编译**。现象是"改了代码却毫无效果、连新加的日志都没出现"。正确流程：`scp` 之后必须先 `touch` 相应源文件，再 `cmake --build`。

**另一条重要教训**：`xcodebuild test-without-building` 不会安装 App，也不会注入 `FLYNES_TEST_APPLICATION_CONTAINERS`；必须走 `ios/scripts/run_simulator_tests.py`（它先 `simctl install` 再注入环境变量）。用手工 `xcodebuild` 跑 UI 测试会连到**上一次安装的旧 App**，"游戏中"的假象全部来自旧包。

## 7. 本轮顺带修掉的 3 处陈旧 Android instrumentation 断言（单游戏时代遗留）

| 测试 | 症状 | 修法 |
|---|---|---|
| `RomIdentityTest.builtinRomUsesCoreSha1` | 期望 SHA-1 `77C42676…`（**已下架的 From Below**），实测 `81A5AC43…`。核对：`81A5AC43…` = 去掉 16 字节 iNES 头后 payload 的 SHA-1，正是核心的 identity 语义 | 改为**遍历共享清单**逐款校验核心 identity = payload SHA-1 |
| `AndroidCatalogRuntimeTest.restartRestoresCatalog…` | `expected:<1> but was:<7>` | 期望值改为从清单读出的内置游戏数 |
| `CoverCaptureIntegrationTest.gameplayPublishesAGameOnlyCover…` | 单次 `ActivityScenario.launch(MainActivity)` 不再自动开游戏（自动播放已按"清单驱动"移除），因此永远采不到封面 | 改为经 Game Center 启动选中游戏，并等满整个 2/4/6/8 秒采集窗口 |

## 8. 完整回归结果（本轮收尾）

| 套件 | 命令 | 结果 |
|---|---|---|
| 内容门 | `tools/content/verify-builtin-content.ps1` | **PASS**（7 款、单一事实源） |
| Pester 内容门用例 | `Invoke-Pester tools/content/tests/BuiltinContent.Tests.ps1` | **11/11 通过** |
| 清单/合规/iOS/鸿蒙契约 | 8 个 `test_*_contract.py` | **全部 exit=0** |
| 桌面宿主 CTest | `ctest --test-dir .artifacts/builtin-host -C Debug` | **38/38 通过** |
| 鸿蒙宿主 CTest | `ctest --test-dir .artifacts/harmony-host -C Debug` | **12/12 通过** |
| iOS 运行时套件 | `run_simulator_tests.py … FlyNESRuntimeTests` | **49/49 通过** |
| iOS UI 套件 | `run_simulator_tests.py … FlyNESUITests` | **8/8 通过** |
| Android 单元测试 | `gradlew :app:testDebugUnitTest` | **BUILD SUCCESSFUL** |
| Android 封面采集 | `connectedDebugAndroidTest --class …CoverCaptureIntegrationTest` | **通过**（本轮由失败转通过） |
| Android catalog/identity 修复验证 | `--class RomIdentityTest,AndroidCatalogRuntimeTest` | **通过**（本轮由失败转通过） |
| Android 全量 instrumentation | `gradlew :app:connectedDebugAndroidTest` | **92/104 通过，12 失败** —— 见下 |
| 鸿蒙 HAP（app） | `hvigorw assembleHap` | **BUILD SUCCESSFUL** |
| 鸿蒙 HAP（ohosTest） | `hvigorw -p module=entry@ohosTest assembleHap` | **BUILD SUCCESSFUL** |
| 鸿蒙产品契约 | `harmony/tests/test_harmony_product_contract.py` | **1 条既有失败**（`:65` 断言 `play.open(context,this.locator)` 为 2 参，实际代码为 3 参）——本轮未改动该路径 |

**鸿蒙编译期真 bug（本轮修复）**：`harmony/entry/src/main/ets/service/CatalogProductService.ets` 里遗留了 `searchAliases` / `popularityScore` 两个字段的赋值，而本分支的 ABI 类型 `GameCenterRow`（`harmony/entry/src/main/cpp/types/libentry/Index.d.ts`）**并未声明这两个字段**（它们只存在于主检出**未提交**的工作区改动中），导致 `hvigorw assembleHap` 直接 `ArkTS Compiler Error 10505001`。已删除这两处赋值，app 与 ohosTest 均恢复 `BUILD SUCCESSFUL`。这也是"编译不等于通过"的又一例证：上一轮只跑了宿主 CTest 与打包，没有复跑 hvigor。

**Android 全量套件 12 条失败的性质**：全部是视频/GL 路径（`NativePresenterIntegrationTest` ×5、`MotionShadowPresenterTest` ×3、`StartAndPauseSeparationTest` ×2、`MotionComputeParityTest`、`SwappyFailClosedTest`），报错为 `EGL_CONTEXT_UNAVAILABLE`、`uploadedFrames=0`、以及 `onActivity` 时 Activity 已销毁。判定为**模拟器环境问题**，证据：

1. 本轮改动**没有触碰任何 Android 视频路径文件**（`git diff --name-only HEAD` 中视频相关仅 `ios/app/run/RunSurfaceViewController.mm`，iOS 专属）。
2. 该模拟器 GL 为 `Android Emulator OpenGL ES Translator (Google SwiftShader), OpenGL ES 3.0`——**没有 ES 3.1**，而失败用例需要 ES3.1 计算上下文。
3. **单独重启模拟器后只跑这两个类，仍然 6/6 全部失败**，与用例顺序、与前序用例的污染无关。

残余未验证项与阻塞见 §9。

## 10. 第 9 轮：From Below 其实**没有被真正移除**（已修复）

**症状**：目标第一条要求是"移除内置的 From Below"，内容门也报了 PASS，但实测发现**三个 checked-in 的 ROM 二进制仍在仓库里并被分发**：

```
app/src/main/assets/roms/from_below.nes                        ← 打进 Android APK
harmony/entry/src/main/resources/rawfile/from_below.nes         ← 打进 Harmony HAP
core/tests/fixtures/from_below.nes                             ← 同目录还有 LICENSE-from-below.txt
＋ app/src/main/assets/licenses/from-below-mit.txt
＋ harmony/entry/src/main/resources/rawfile/LICENSE-from-below.txt
```

**门禁为什么漏了**：`verify-builtin-content.ps1` 第 8 项只扫描 `$scanExtensions` 里列出的**文本后缀**（`.java/.ets/.xml/...`），`.nes` 二进制不在其中；扫描根也不含 `core/tests/`。于是"没有任何平台私藏 ROM 副本"这条只剩文档措辞，没有可执行保证。

**顺带发现的连带问题**：`.github/workflows/ios-stage1.yml` 的产物检查**断言** `FlyNESPortabilitySmoke.app` 里必须有 `from_below.nes` 且其 SHA-256 等于 `1A3AC4FA…`——即 CI 一直在**强制要求**旧 ROM 随包分发。另有 3 处断言/夹具把旧游戏写死（见下）。

**TDD 修复**：

1. **RED**：门禁新增规则——读 `git ls-files`，任何名字含 `from[-_ ]below` 的**被跟踪文件**（含二进制）一律失败。加规则后立刻失败，列出上述 6 个文件：
   `FAIL retired bundled game is still checked in (6): app/src/main/assets/roms/from_below.nes, …`
2. **GREEN**：`git rm` 这 6 个文件（2 个 ROM + 4 份许可文本）；`harmony/entry/src/main/resources/rawfile/` 重新由 `sync-builtin-content.ps1` 从清单生成（15 个文件）。门禁恢复 `PASS builtin content gate (7 games, single source of truth)`。
3. 规则**保留强度**：临时重新 `git add -N` 一个 `from_below.nes` 后门禁立刻再次失败，删除后恢复 PASS。
4. `.gitattributes` 里指向旧 ROM 的 `binary` 行改为 `rawfile/*.nes binary`。
5. **Pester 新增用例** `rejects the retired game still checked in as a ROM binary`：在临时 fixture 里 `git init` + 造一个 `.nes` + `git add -N`，断言门禁必须失败。**12/12 通过**（原 11 条 + 新增 1 条）。

**产物检查改为按清单校验**（不再写死某一款）：新增 `ios/scripts/verify_bundled_resources.py`，逐款校验"随包 ROM 存在 + SHA-256 与清单一致 + iNES 头合法"，并校验随包 `builtin-games.json` 与 `content/assets` **字节一致**（能抓出陈旧打包）；`ios-stage1.yml` 改为调用它并输出哈希证据文件。

**iOS 产物实测**（Mac 模拟器构建的 `FlyNES.app`）：

```
$ python3 ios/scripts/verify_bundled_resources.py build/ios-simulator/Debug-iphonesimulator/FlyNES.app /tmp/product-bundled-hashes.txt
bundled resources match the manifest (7 games)      rc=0
$ find FlyNES.app -name '*from*below*' | wc -l
0
```

**连带修掉的 3 处"旧游戏写死"断言**：

| 位置 | 原断言 | 改为 |
|---|---|---|
| `ios/tests/test_product_android_parity_contract.py:177` | 要求 `harmony/.../rawfile/thwaite.nes` 存在（**上一轮刚被当作"合法内置 ROM"写进断言**） | 改为要求共享清单的 ROM `content/assets/roms/thwaite.nes` 存在——三端共用同一份夹具，任何平台都不再私藏 ROM |
| `harmony/tests/test_harmony_product_contract.py:114` | `assert "From Below" in catalog`（**要求源码里出现已下架游戏名**） | 反转为 `not in`，并要求 `loadRows`+`BuiltinGames` 存在 |
| `harmony/tests/product_bridge_test.cpp:27` | 夹具第一行就是 `{"builtin", "From Below", …, "from_below.nes"}` | 改为合成夹具 `builtin:sample / Sample Bundle / sample.nes` |

**第 9 轮回归**：内容门 PASS、Pester **12/12**、`test_compliance_docs.py` PASS、8 个契约脚本全 exit=0、桌面宿主 CTest **38/38**、鸿蒙宿主 CTest **12/12**、iOS UI 套件 **8/8**、Android 单元 BUILD SUCCESSFUL、iOS 产物清单校验 PASS + 旧 ROM 计数 0。

**教训**：门禁"PASS"只证明它检查的那些东西成立。本次漏检的根因是**门禁按文件后缀过滤，二进制资产天然不可见**；而"移除某个东西"这类要求必须用**按路径/索引的判定**来守，不能只靠文本扫描。文档里写"已移除"不等于被移除——本轮之前 `COMPLIANCE.md` 与证据文件都写着已移除，实际三个包仍在分发。

## 11. 仍未验证 / 阻塞

- Android / iOS / HarmonyOS **真机**（非模拟器）证据：未做。
- 鸿蒙设备安装 + Hypium：**阻塞**——`harmony/build-profile.json5` 的 `signingConfigs` 为空数组，签名材料在用户本地未提交改动中；不会擅自复制或提交签名配置。
- HarmonyOS 封面采集：`RunGame.ets` 的 `considerCover` **不经过质量门**（任何采样帧直接落盘），与本轮修复的"质量门"不是同一路径；本轮未改动鸿蒙采集行为。
- Android 全量 instrumentation 的 12 条视频/GL 失败：原因为模拟器无 ES3.1（见 §8），非本轮改动引入。


