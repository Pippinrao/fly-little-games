# FlyNES 软件物料清单（SBOM）与合规说明

> 本文件列出 FlyNES 项目（仓库根）分发的全部第三方组件及许可。随包分发的许可文本见 `app/src/main/assets/licenses/`，App 内许可页（LicensesActivity）展示同一批文本。根 `LICENSE` 文件含 NOTICE 与 GPLv2 全文。

## SBOM

| 组件 | 版本 | 来源 | 许可 | 用途 | 位置 |
|---|---|---|---|---|---|
| NestopiaUE | 1.53.2 (commit 4470a2e) | github.com/0ldsk00l/nestopia | GPLv2 | 模拟器内核 | core/vendor/nestopiaue (submodule) |
| NstDatabase.xml | (vendored) | 同上 | GPLv2 | 游戏纠错库 | app/src/main/assets/ + core/tests/fixtures/ |
| zlib | 1.3.1 | github.com/madler/zlib | zlib 许可 | 压缩/CRC（构建期） | core/build/host-deps/（不入库） |
| From Below | 1.0 Final | mhughson.itch.io/from-below | MIT | 内置示例 ROM | app/src/main/assets/roms/ + core/tests/fixtures/ |
| Gradle 8.14.3 / AGP 8.7.3 | 构建期 | gradle.org / developer.android.com | Apache-2.0 | 构建系统 | 构建期依赖 |
| 本项目自有代码 | — | — | GPLv2 | 全部 | core/src, app/src, scripts |

## 合规说明

- **GPLv2 结合作品**：FlyNES 的模拟器内核（NestopiaUE）与 Java 壳经 JNI 链接为单一 `.so` 分发，构成 GPLv2 意义下的「结合作品」，整体以 GPLv2 开源。源码在 GitHub 公开，构建可复现（见 `.github/workflows/stage0.yml` 与 `scripts/ci-check.ps1`），App 内提供许可页。
- **内置 ROM 仅含明确许可的 homebrew**：目前仅内置 From Below（MIT），不内置任何商业 ROM、BIOS（如 FDS disksys.rom 由用户自备）或版权素材。
- **依赖许可核查**：C++ 依赖仅 NestopiaUE（GPLv2，兼容）与构建期 zlib（zlib 许可，宽松）；无 GPLv3 组件，不存在 GPLv2/GPLv3 不兼容问题。
- **内容红线**：不内置作弊码数据库/商业补丁库；金手指仅支持手动输入/导入；App 名与图标避开任天堂商标。
