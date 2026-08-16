# FlyNES 阶段 1 · 第一批实现计划（技术债清偿 + 合规 + CI）

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development to implement task-by-task. Steps use `- [ ]` syntax.

**Goal:** 清偿阶段 0 最终审查列出的技术债：ABI 冻结补丁、NST 存档安全包装、spike 测试、CI 门禁、合规基线。为阶段 1「完整可玩」铺平地基。

**Architecture:** 全部工作在既有四层上增量进行——`core/include/nes/nes.h`（ABI，本批完成冻结）、`core/src/`（核心实现 + `nes_state.cpp` 从占位变为存档包装器）、`app/`（JNI + assets + 许可页）、`scripts/` + `.github/`（CI）。已合入 main（commit 0f4da75），宿主冒烟测试 PASS、模拟器 60fps 验收通过。

**Tech Stack:** C++17 / NestopiaUE 1.53.2（GPLv2）/ zlib（crc32 可用）/ Java 17 / Gradle 8.14.3 + AGP 8.7.3 / PowerShell 脚本。

**依据:** `docs/superpowers/specs/2026-08-15-nes-emulator-design.md`（§8 存档格式硬承诺、§9.2 合规轨、§10 测试）、最终整体审查报告（本会话）、`docs/nes-arch-review/t2b-c-abi-draft.md`。

---

## 文件结构（本批将创建/修改）

```
core/include/nes/nes.h                    # +nes_load_database；+NES_ERR_STATE_ROM_MISMATCH（追加，ABI 安全）
core/src/nes_core.cpp                     # +nes_load_database 实现（~10 行）
core/src/nes_state.cpp                    # 占位 → NST 安全包装器（magic+version+sha1+crc32）
core/tests/test_core.cpp                  # +DB 加载、GG/PAR 往返、cpu_ram、capabilities、回调注册 spike
core/tests/fixtures/NstDatabase.xml       # 复制自 vendor
core/tests/fixtures/from_below.nes        # 已有
app/src/main/assets/NstDatabase.xml       # 复制（App 随包分发）
app/src/main/cpp/nes_jni.cpp              # +nativeLoadDatabase
app/src/main/java/com/flynes/emu/NesCore.java      # +loadDatabase
app/src/main/java/com/flynes/emu/MainActivity.java # 加载 DB 于 loadRom 前 + 关于对话框（许可页）
app/src/main/assets/licenses/*.txt        # 许可文本（内核/ROM/zlib/自有）
LICENSE                                   # 根：GPLv2 全文 + NOTICE
docs/COMPLIANCE.md                        # SBOM（第三方依赖 + 许可清单）
scripts/ci-check.ps1                      # 本地 CI：裸 core 检查 + 平台头白名单 + ABI 符号 diff + 宿主测试
scripts/abi_symbols.golden.txt            # 导出的 nes_* 符号 golden
.github/workflows/stage0.yml              # CI（推送后生效）
```

---

## Task S1-1: ABI 冻结补丁 — `nes_load_database` + NstDatabase.xml 入库

**Files:**
- Modify: `core/include/nes/nes.h`、`core/src/nes_core.cpp`、`core/tests/test_core.cpp`
- Create: `core/tests/fixtures/NstDatabase.xml`、`app/src/main/assets/NstDatabase.xml`
- Modify: `app/src/main/cpp/nes_jni.cpp`、`app/src/main/java/com/flynes/emu/NesCore.java`、`app/src/main/java/com/flynes/emu/MainActivity.java`

- [ ] **Step 1: nes.h 追加（在"回调注册"节前插入）**

```c
/* ---------------- 数据库（NstDatabase.xml 随包分发，加载于 load_rom 前） ---------------- */
NES_API int nes_load_database(nes_t* nes, const uint8_t* xml, size_t size);
```

- [ ] **Step 2: nes_core.cpp 实现**（放在 `nes_load_rom` 附近；复用 `nes_stream::MemIStream`）

```cpp
nes_result nes_load_database(nes_t* ctx, const uint8_t* xml, size_t size) {
    if (!ctx || !xml || size == 0) return NES_ERR_INVALID_PARAM;
    if (ctx->in_callback) return NES_ERR_REENTRANT;
    nes::MemIStream ms(xml, size);
    Nes::Api::Cartridge::Database db = ctx->cartridge.GetDatabase();
    return (nes_result)db.Load(ms.stream());
}
```

- [ ] **Step 3: 复制数据库**：`Copy-Item core/vendor/nestopiaue/NstDatabase.xml core/tests/fixtures/` 与 `app/src/main/assets/`。

- [ ] **Step 4: 测试扩展**（test_core.cpp，在 `nes_create` 后、`nes_load_rom` 前）：读 fixture `NstDatabase.xml`，`nes_load_database` → 断言 rc >= 0；再次加载 → 仍 >= 0（幂等）。

- [ ] **Step 5: JNI + Java**：`nativeLoadDatabase(jlong, byte[] xml)` → `nes_load_database`；`NesCore.loadDatabase(byte[])`；`MainActivity.onCreate` 在 `loadRom` 前 `core.loadDatabase(readAsset("NstDatabase.xml"))`（null 安全：失败仅 log，不阻断）。

- [ ] **Step 6: 验证**：宿主测试 PASS；`.\gradlew.bat assembleDebug` BUILD SUCCESSFUL。

- [ ] **Step 7: 提交** `feat: add nes_load_database ABI + bundle NstDatabase.xml`

---

## Task S1-2: NST 存档安全包装器（"升级不丢档"硬承诺的落点）

**Files:**
- Modify: `core/include/nes/nes.h`、`core/src/nes_state.cpp`（占位 → 实现）、`core/src/nes_core.cpp`（接线）、`core/tests/test_core.cpp`
- Modify: `core/src/nes_stream.hpp`（若需 growable 输出缓冲辅助）

**设计（必须遵守）:**

包装格式（固定头 65 字节 + 载荷）：
```
offset 0   : magic[8]   = "FLYNST1\0"
offset 8   : version u32 LE = 1
offset 12  : core_version[16] = "1.53.2" 零填充
offset 28  : rom_sha1[41] = 40 位十六进制 + NUL（来自 Cartridge profile hash；无卡带时全 0）
offset 69  : payload_len u64 LE
offset 77  : payload_crc32 u32 LE（zlib crc32 对载荷）
offset 81  : payload（NST 字节，Machine::SaveState 产出）
```

- [ ] **Step 1: nes.h 追加错误码**（nes_err 枚举末尾，ABI 专有区）：

```c
    NES_ERR_STATE_ROM_MISMATCH = -202   /* 存档与当前 ROM 的 SHA1 不匹配 */
```

- [ ] **Step 2: nes_state.cpp 实现包装/解包**（新增 `namespace flynes_state` 或 static 函数，供 nes_core.cpp 调用）：

```cpp
// 计算 SHA1：Cartridge::GetProfile()->hash 已含（GetSha1() 返回 dword[5] 或 Get() 填 hex）。
// 产出：sha1_hex[41]（全 0 当无卡带）。
// crc32：zlib 的 crc32()（nestopia PUBLIC 链接 zlib，nes_abi 可用）。

// wrap(raw_nst, raw_len, sha1_hex, out, cap, &written, &needed)
//   → 头 81 字节 + 载荷拷贝；cap < 81+raw_len → NES_ERR_BUFFER_TOO_SMALL 且 *needed=81+raw_len。

// unwrap(in, size, sha1_hex, &payload, &payload_len)
//   → 校验 magic/version/len/crc32/sha1；失败返回具体错误码：
//     magic 不符 → 视为旧版裸 NST（见 Step 4 兼容路径）
//     version > 1 → NES_ERR_UNSUPPORTED_VER
//     crc32 不符 → NES_ERR_INVALID_CRC
//     sha1 非全 0 且不匹配当前卡带 → NES_ERR_STATE_ROM_MISMATCH
```

- [ ] **Step 3: nes_core.cpp 重接 nes_save_state / nes_load_state**：
  - `nes_save_state`: 先用**可增长的内部缓冲**产出裸 NST（用 `std::vector<uint8_t>` 包一个 MemOStream 变体，或复用现有 MemOStream 指向一个 4MB 临时缓冲——任选，注释说明；关键：拿到完整裸 NST 字节），再 `wrap` 到调用方 `out`（cap 检查 → BUFFER_TOO_SMALL + *needed）。
  - `nes_load_state`: `unwrap`（magic 不符时走兼容路径：把输入直接当裸 NST 喂 Machine::LoadState）；得到 payload 后 `Machine::LoadState(MemIStream(payload))`，Result 透传。
  - 保持 re-entrancy 守卫与既有错误码语义。

- [ ] **Step 4: 测试扩展**（test_core.cpp，替换/增强现有存档段）：
  - save → load → save 字节一致（既有断言保持通过，因为包装是确定性的）。
  - 篡改测试：改 1 字节载荷 → load 应返回 `NES_ERR_INVALID_CRC`（或 CORRUPT_FILE，断言 rc < 0 且非 OK）。
  - 版本测试：把 version 字段改成 2 → `NES_ERR_UNSUPPORTED_VER`。
  - SHA1 测试：把 sha1 字段改成别的 → `NES_ERR_STATE_ROM_MISMATCH`（断言 rc == -202）。
  - 兼容测试：把未包装的裸 NST（解包函数暴露测试入口或复用内部逻辑）喂 load → 仍成功（旧档兼容）。

- [ ] **Step 5: 验证**：宿主测试全 PASS（含新断言）；NDK 构建 nes_abi exit 0（zlib crc32 链接验证）。

- [ ] **Step 6: 提交** `feat: wrap NST saves with version+sha1+crc header (upgrade-safe saves)`

> 注：若现有 `MemOStream` 实现不允许"先全量产出再包装"，允许在 `nes_stream.hpp` 增加一个 `GrowableOStream`（vector 后端、无 cap）作为内部工具——这是受控扩展，注释说明用途。

---

## Task S1-3: spike 测试扩展（金手指/CPU RAM/能力/回调）

**Files:**
- Modify: `core/tests/test_core.cpp`

- [ ] **Step 1: 追加测试段**（ROM 加载成功后）：
  - `nes_get_capabilities` → struct_size/version 正确、num_pads==4、has_debugger==0、has_bps_patch==0。
  - `nes_get_cpu_ram` → *ram 非空、*size == 0x800（读首字节不越界）。
  - GG 往返：`nes_cheat_encode(NES_CHEAT_GAME_GENIE, 0x1234, 0xAB, 0, 0, buf9, 9)` → rc>=0；`nes_cheat_decode(..., buf, &addr,&val,&cmp,&use)` → rc>=0 且 addr==0x1234 && val==0xAB。
  - PAR 往返：同 GG，`NES_CHEAT_PRO_ACTION_ROCKY`。
  - `nes_cheat_add(0x1234, 0xAB, 0, 0)` → rc>=0；`nes_cheat_count` → >=1；`nes_cheat_clear` → rc>=0；count==0。
  - 回调注册：`nes_set_log_callback`（记录被调用次数或仅验证可注册）、`nes_set_file_io_callback`、`nes_set_event_callback`、`nes_set_question_callback` → 全部 rc>=0；再以 NULL 注销 → rc>=0（幂等）。
  - `nes_set_input(0, 0x08 /*START*/)` + 跑 5 帧 → rc 正常（输入桥不崩溃）；`nes_clear_input` → 正常。

- [ ] **Step 2: 验证**：宿主测试全 PASS（新增断言不破坏既有）。

- [ ] **Step 3: 提交** `test: extend smoke test with cheats/cpu-ram/caps/callback spikes`

---

## Task S1-4: CI 门禁

**Files:**
- Create: `scripts/ci-check.ps1`、`scripts/abi_symbols.golden.txt`、`.github/workflows/stage0.yml`

- [ ] **Step 1: 生成 golden 符号表**：对 NDK 构建的 `core/build/android/libnes_abi.a` 用 `llvm-nm --defined-only` 提取 `nes_*` 符号排序 → `scripts/abi_symbols.golden.txt`（提交入库）。

- [ ] **Step 2: 写 `scripts/ci-check.ps1`**（本地一键，全部通过返回 0）：
  1. **平台头白名单检查**：扫描 `core/src/*.cpp` `core/src/*.hpp` `core/include/nes/nes.h`，禁止出现 `jni.h`、`android/`、`ANativeWindow`、`AAudio`、`JNIEnv`、`#include <windows.h>` 等（正则匹配，命中即失败并列出文件:行）。
  2. **ABI 符号 diff**：对最新 NDK 构建的 libnes_abi.a 提取符号与 golden 比对（缺失/新增皆告警；新增需人工确认后更新 golden——脚本对"缺失"失败、"新增"警告）。
  3. **宿主测试**：构建并运行 nes_core_test（复用 core/build/host 或重建），断言 exit 0。
  4. **Android 构建**：`gradlew assembleDebug`，断言成功。

- [ ] **Step 3: 写 `.github/workflows/stage0.yml`**（推送后生效，先备好）：
  - jobs: `host-test`（ubuntu-latest：装 zlib、CMake 构建 NES_BUILD_TESTS=ON、跑测试）、`bare-core`（macos-latest：仅编译 `core/include/nes/nes.h` + 一个最小 consumer，验证无平台依赖 + macOS clang 门禁）、`abi-diff`（ubuntu：构建后符号比对，可简化为复用 windows/ubuntu 的产物或跳过——写明备注）。
  - 简洁为主，允许占位注释说明"阶段 1.5 完善"。

- [ ] **Step 4: 本地跑 `scripts/ci-check.ps1`** → 全部通过。

- [ ] **Step 5: 提交** `ci: add local CI gate (platform-header whitelist, ABI symbol golden, host test)`

---

## Task S1-5: 合规基线

**Files:**
- Create: `LICENSE`（GPLv2 全文 + NOTICE）、`app/src/main/assets/licenses/{nestopiaue-gplv2.txt,from-below-mit.txt,zlib-license.txt,own-gplv2.txt}`、`docs/COMPLIANCE.md`、`app/src/main/java/com/flynes/emu/LicensesActivity.java`（或 MainActivity 内关于对话框——选更简单的）
- Modify: `app/src/main/AndroidManifest.xml`（注册 LicensesActivity 若新建）、`app/src/main/java/com/flynes/emu/MainActivity.java`（菜单/按钮打开许可页）

- [ ] **Step 1: 根 LICENSE**：GPLv2 全文（从 `core/vendor/nestopiaue/COPYING` 复制即可——就是 GPLv2 标准文本）+ 文件头 NOTICE 段（项目 FlyNES 自身 GPLv2；注明组件：NestopiaUE GPLv2、From Below MIT、zlib zlib 许可、测试夹具许可）。
- [ ] **Step 2: assets/licenses/**：从各来源复制许可文本（NestopiaUE 的 COPYING、From Below MIT 见 core/tests/fixtures/LICENSE-from-below.txt、zlib 的 LICENSE）。
- [ ] **Step 3: docs/COMPLIANCE.md（SBOM）**：表格列出所有第三方组件（名称、版本、来源、许可、用途、本项目中的位置），含 NestopiaUE 1.53.2、zlib 1.3.1（vendored in core/build 但构建期）、From Below、AGP/Gradle（构建期）。
- [ ] **Step 4: 许可页**：新建 `LicensesActivity.java`（读取 assets/licenses/*.txt 拼成一个滚动 TextView + "源码: https://github.com/（占位）"），Manifest 注册；MainActivity 加一个"关于/许可"入口（简单 Button 或 menu）。
- [ ] **Step 5: 验证**：`gradlew assembleDebug` BUILD SUCCESSFUL；`adb` 安装后能打开许可页（可用模拟器验证，或注明未验证）。
- [ ] **Step 6: 提交** `chore: add GPLv2 compliance baseline (LICENSE/NOTICE, licenses assets, SBOM, about page)`

---

## 自检要点（执行时逐项核对）

- **ABI 兼容**：本批只**追加**（nes_load_database、NES_ERR_STATE_ROM_MISMATCH、GrowableOStream），不修改/删除既有签名与结构体字段 → struct_size/version 机制不变。
- **Spec 覆盖**：§3.8 NstDatabase 入库 ✓（S1-1）、§8 NST 版本/SHA1 硬承诺 ✓（S1-2）、§9.2 合规轨 ✓（S1-5）、§10 CI/ABI 稳定性 ✓（S1-4）、D6 spike ✓（S1-3）。
- **测试**：每步改完跑宿主测试；全部提交后跑 `scripts/ci-check.ps1` 全绿。
