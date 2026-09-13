# FlyNES 软件物料清单（SBOM）与合规说明

> 本文件列出 FlyNES 项目（仓库根）分发的全部第三方组件及许可。随包分发的许可文本见 `content/assets/licenses/`（由 `content/assets/builtin-games.json` 清单声明，构建时同步到各端资源目录），App 内许可页展示同一批文本。根 `LICENSE` 文件含 NOTICE 与 GPLv2 全文。
>
> 内置游戏的**唯一真源**是 `content/assets/builtin-games.json`；每款游戏的构建固定信息在 `content/sources.lock.json`。

## SBOM

### 内置自制游戏（7 款，全部从 pinned 上游源码编译）

| 游戏 | 资产文件 | 上游 revision | 上游仓库 | 许可（SPDX） | 用途 | 位置 |
|---|---|---|---|---|---|---|
| Super Tilt Bro. | super_tilt_bro.nes | b132fd25add46f816e04be64c434386743b84b8b | github.com/sgadrat/super-tilt-bro | WTFPL | 内置游戏（UNROM 离线变体） | content/assets/roms/ |
| Twin Dragons | twin_dragons.nes | bcf27d0c5117a2aac19e6c33bdb3c776ada069fb | github.com/Garydos/twin-dragons-nes | Zlib AND CC-BY-3.0 AND CC-BY-4.0 AND LicenseRef-PublicDomain | 内置游戏 | content/assets/roms/ |
| RHDE: Furniture Fight | rhde.nes | 376204907155d15bb13018e5f9b231c8511d5333 | github.com/pinobatch/rhde-nes | FSFAP | 内置游戏 | content/assets/roms/ |
| Zap Ruder | zap_ruder.nes | 1b22babdf324fd1e840dfa26ffcc34c75f4e422f | github.com/pinobatch/zap-ruder | FSFAP | 内置游戏（光枪测试 + 双人曲棍球） | content/assets/roms/ |
| Concentration Room | concentration_room.nes | ed19c3c07ca389b70cf2e0dd2ce0320df28d511d | github.com/pinobatch/croom-nes | GPL-3.0-or-later | 内置游戏 | content/assets/roms/ |
| Thwaite | thwaite.nes | 00e36745188bc165990f60eed6b093c3ce6ad0e3 | github.com/pinobatch/thwaite-nes | GPL-3.0-or-later | 内置游戏 | content/assets/roms/ |
| DABG: Double Action Blaster Guys | dabg.nes | 5ecc60b6af3f726851bfeb1c2555388c5953e0c0 | github.com/NovaSquirrel/DABG | Zlib | 内置游戏 | content/assets/roms/ |

### 其他组件

| 组件 | 版本 | 来源 | 许可 | 用途 | 位置 |
|---|---|---|---|---|---|
| NestopiaUE | 1.53.2 (commit 4470a2e) | github.com/0ldsk00l/nestopia | GPLv2 | 模拟器内核 | core/vendor/nestopiaue (submodule) |
| NstDatabase.xml | (vendored) | 同上 | GPLv2 | 游戏纠错库 | app/src/main/assets/ + core/tests/fixtures/ |
| zlib | 1.3.1 | github.com/madler/zlib | zlib 许可 | 压缩/CRC（构建期） | core/build/host-deps/（不入库） |
| Gradle 8.14.3 / AGP 8.7.3 | 构建期 | gradle.org / developer.android.com | Apache-2.0 | 构建系统 | 构建期依赖 |
| 本项目自有代码 | — | — | GPLv2 | 全部 | core/src, app/src, scripts |

## 合规说明

- **GPLv2 结合作品**：FlyNES 的模拟器内核（NestopiaUE）与 Java 壳经 JNI 链接为单一 `.so` 分发，构成 GPLv2 意义下的「结合作品」，整体以 GPLv2 开源。源码在 GitHub 公开，构建可复现（见 `.github/workflows/stage0.yml` 与 `scripts/ci-check.ps1`），App 内提供许可页。
- **GPL-3.0-or-later 的内置 ROM 属于「聚合分发」，不是结合作品**：Concentration Room 与 Thwaite 两款 ROM 是独立程序（Nintendo 6502 机器码），不与该模拟器链接、不共享地址空间、不通过函数调用交互，仅与 GPLv2 内核一同存放和分发。按 GPLv2 第 2 节与 GPLv3 第 5 节的「mere aggregation」原则，这既不使内核受 GPLv3 约束，也不使 ROM 受 GPLv2 约束。因此 **C++ 依赖仍无 GPLv3 代码组件**，GPLv2/GPLv3 代码层面的不兼容问题不存在。
- **GPL-3.0-or-later 的对应源码**：两款 ROM 均由本项目从固定 revision 的上游源码编译（配方见 `content/sources.lock.json`，含构建环境、命令与工具链版本）。完整对应源码即该 revision 的上游仓库；随包附 GPLv3 全文（`content/assets/licenses/concentration_room.txt`、`content/assets/licenses/thwaite.txt`）并在 App 内许可页给出可点击的源码 URL。书面索取亦予提供。
- **内置 ROM 仅含明确许可的 homebrew**：7 款均为许可明确、允许再分发的自制游戏，逐款许可原文随包分发。不内置任何商业 ROM、BIOS（如 FDS disksys.rom 由用户自备）或版权素材。
- **唯一一款上游构建不可字节复现的游戏如实披露**：`concentration_room` 的上游 `tools/shuffle.py` 会随机打乱内存布局（ASLR 加固）。实测在固定 `-r` 模式、固定 `--seed`、固定 `PYTHONHASHSEED` 且每次全新解包的条件下，四次构建得到三个不同哈希。因此该款的 `hashPolicy` 为 `artifact`：**出厂 ROM 仍被哈希固定并由门禁校验，仍从 pinned 源码编译并校验 iNES 头、mapper 与尺寸**，但本项目不声称其重编字节一致。其余 6 款可由 `tools/content/build-builtin-roms.ps1` 重编得到字节一致的 ROM。
- **内置内容的门禁**：`tools/content/verify-builtin-content.ps1` 校验清单、ROM 哈希、许可文本、构建锁一致性、三端均从共享清单读取，且没有任何平台私藏一份 ROM 副本。门禁同时读取 git 索引，**任何仍被 checked-in 的、名字含 `from[-_ ]below` 的文件（包含二进制 ROM 与许可文本）都会直接失败**——二进制资产不会被文本扫描发现，必须按路径判定。`content/tests/test_builtin_manifest_contract.py`、`harmony/tests/test_builtin_games_contract.py`、`ios/tests/test_builtin_games_contract.py` 分别校验清单结构与三端消费方式。
- **已下架游戏（From Below）的清理**：该 ROM 曾在 `app/src/main/assets/roms/`、`harmony/entry/src/main/resources/rawfile/`、`core/tests/fixtures/` 三处以 checked-in 二进制形式存在，并被 Android/Harmony 打包分发、被 `ios-stage1.yml` 的产物检查断言要求存在。本轮已全部删除（连同上列目录中的 MIT 许可文本），构建产物检查改为**按清单校验**（`ios/scripts/verify_bundled_resources.py`：逐款校验 ROM 存在、SHA-256 与清单一致、iNES 头合法，并校验随包清单与 `content/assets` 字节一致）。
- **依赖许可核查**：C++ 依赖仅 NestopiaUE（GPLv2，兼容）与构建期 zlib（zlib 许可，宽松）；无 GPLv3 代码组件。
- **内容红线**：不内置作弊码数据库/商业补丁库；金手指仅支持手动输入/导入；App 名与图标避开任天堂商标。
