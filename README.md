# FlyNES

FlyNES 是面向 Android、HarmonyOS NEXT 和 iOS 的离线 NES/Famicom 模拟器。仓库包含共享模拟核心、跨平台目录与设置模型，以及各平台原生界面。应用内置一款取得 MIT 授权的 homebrew 游戏 *From Below*；用户自行导入的 ROM 不会上传到网络。

当前稳定范围包括中文游戏目录、来源扫描、收藏与最近游戏、独立存档、可编辑虚拟手柄，以及 Nearest、Sharp Bilinear、MMPX、ScaleFX、CRT 等显示路径。高刷新率和运动补偿只在设备能力与实测证据满足门禁时开放。

## 仓库结构

- `app/`：Android 应用与 instrumentation 测试
- `harmony/`：HarmonyOS Stage 应用、NativeWindow/EGL 渲染与 Hypium 测试
- `ios/`：iOS 应用
- `core/`：NestopiaUE 模拟核心封装
- `shared/`：跨平台目录、设置、会话和渲染公共代码
- `tools/quality/`：主机、模拟器与真机质量门禁
- `tools/versioning/`：版本同步、提交自增和 worktree 分配

## 构建与测试

Android 需要 JDK 17、Android SDK 36、NDK 27 和 CMake 3.22.1：

```powershell
.\gradlew.bat :app:testDebugUnitTest :app:assembleDebug
$env:ANDROID_SERIAL = 'emulator-5554'
.\gradlew.bat :app:connectedDebugAndroidTest
```

HarmonyOS 需要 DevEco Studio 6.0 与 API 20 SDK。详细命令、签名边界和分阶段门禁见 [harmony/README.md](harmony/README.md)。本地签名材料、ROM、APK/HAP 和测试证据均不得提交。

## 版本规则

唯一版本源是 `VERSION`。`MAJOR` 只在仓库所有者明确要求时修改；每次 Git 提交由 `.githooks/pre-commit` 自动增加 `PATCH`；每个新 worktree 必须通过分配脚本取得新的 `MINOR`，避免并行分支版本重叠。

```powershell
.\tools\versioning\Install-GitHooks.ps1
.\tools\versioning\New-VersionedWorktree.ps1 `
  -Path .worktrees\my-feature -Branch codex\my-feature -StartPoint main
```

Android 与 HarmonyOS 的 `versionName` 和数字 `versionCode` 由脚本同步，映射规则为 `major * 1,000,000 + minor * 1,000 + patch`。

## 许可与 ROM

项目整体按 [GNU GPL v2](LICENSE) 发布。第三方组件、随包许可和 SBOM 见 [docs/COMPLIANCE.md](docs/COMPLIANCE.md)。请只使用你有权运行的 ROM；仓库不接受商业 ROM、BIOS、密钥或签名材料。
