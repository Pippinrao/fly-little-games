# t2b — NestopiaUE 字段级 C ABI 草案（基于 vendor 1.53.2 @4470a2e 真实源码）

> 依据：`core/vendor/nestopiaue`（git `describe` = 1.53.2，HEAD = 4470a2e）。所有 API 签名逐条对照 `source/core/api/*.hpp`、`NstBase.hpp`、`NstApu.cpp`、`NstFile.cpp`、`NstInpPad.cpp`。凡与 captain 提示词不符者，均已标注「⚠️ 以源码为准」。

---

## 0. 关键事实与「以源码为准」的修正（先读，否则 ABI 会画错）

| # | captain 提示 / 常见假设 | 1.53.2 源码事实 | 对 ABI 的影响 |
|---|---|---|---|
| 0 | 输入用 `Machine::SetInput` | ⚠️ **没有 `SetInput`**。输入是**拉取式回调**：`Core::Input::Controllers::Pad::callback`（`bool(void*, Pad&, uint padIndex)`），每帧由核心回调索取当前按键（`NstInpPad.cpp:102`） | ABI 对外必须仍是「推」式 `nes_set_input()`（触屏/手柄天然推式），C++ 壳内部用 `std::atomic<uint32>` 桥「推→拉」 |
| 1 | `load/run/...` 直接吃内存 | ⚠️ **所有 IO 走 `std::istream/std::ostream`**（`Machine::Load/LoadCartridge/LoadState/SaveState`、`Fds::SetBIOS`、`Machine::Patch` 全吃 C++ 流） | C ABI 必须自建 `std::streambuf` 适配器（见 §4），ABI 面仍用字节指针+长度 |
| 2 | 电池存档 = 读/写 SRAM 指针 | ⚠️ 电池走 `Api::User::fileIoCallback`（`File::Action::SAVE_BATTERY/LOAD_BATTERY/...`），核心**反向回调**取/存数据；且有内置脏检测（`NstFile.cpp:380-385` 的 checksum 比对） | ABI 需要 `nes_file_io_fn` 回调；`nes_battery_flush()` 没有干净内核 API（见 §2 该条） |
| 3 | FDS BIOS 是普通文件 | ⚠️ `Fds::SetBIOS(std::istream*)`；`HasBIOS()`；BIOS 不可内置（任天堂版权） | ABI 暴露 `nes_set_fds_bios(字节)`，缺 BIOS 时 `Load` 返回 `RESULT_ERR_MISSING_BIOS`(-12) |
| 4 | 音频格式可自由选 | 采样率 44100–96000（`Sound::SetSampleRate`）；样本是 **16-bit signed**（`FlushSound<iword,...>`，`iword=signed short`）；`SPEAKER_MONO/STEREO`；`Sound::Output.length[]` 单位是**样本帧**（mono=1 样本/帧，stereo=2 样本/帧，`NstTrackerRewinder.cpp:555` 的 `length << stereo` 可证） | ABI 固定 phase0 为 **mono 16-bit signed**，`length`=int16 个数，避免歧义 |
| 5 | 视频格式随意 | `Video::Output` 有 `pixels`+`pitch`（pitch **可为负**=自底向上），`WIDTH=256/HEIGHT=240`；`RenderState.bits.mask{r,g,b}`+`bits.count` 设 bpp；`filter` 可设 FILTER_NONE/NTSC/SCALE/HQ/xBR | ABI 统一为**正 pitch、自顶向下**；phase0 用 RGB565；零拷贝画进 ANativeWindow 是 phase1+ 优化 |
| 6 | 帧循环有「跑 N 帧」API | `Api::Emulator::Execute(video,sound,input)` **每次跑 1 帧**，返回 `Result`；无「跑 N 帧/跑 N 周期」公开 API | ABI 的 `nes_run_frames` 在壳内 `for` 循环调 `Execute` |
| 7 | 回调可多实例 | ⚠️ **Nestopia 所有回调是静态单例**（`static UserCallback`：log/fileIo/question/event/machine-event/input-poll/video-lock/sound-lock） | ABI **进程内单实例**（phase0）；多实例需壳内「单回调 + nes_t 注册表」分派 |
| 8 | 标准 | C++14 | ⚠️ **C++17**（`configure.ac: AX_CXX_COMPILE_STDCXX([17],...,[mandatory])`） | 编译参数用 `-std=c++17`（NDK clang 支持） |
| 9 | 错误码 | — | Nestopia `Result` 值域 `-13..-1, 0..8`（`NstBase.hpp:157-243`） | ABI 错误码**故意与 Result 同值**以直接透传，另加 2 个 ABI 专有负码 |

Nestopia `Result` 关键值（供映射）：`RESULT_OK=0, RESULT_NOP=1, RESULT_WARN_BAD_DUMP=2, RESULT_WARN_BAD_PROM=3, RESULT_WARN_BAD_CROM=4, RESULT_WARN_BAD_FILE_HEADER=5, RESULT_WARN_SAVEDATA_LOST=6, RESULT_WARN_DATA_REPLACED=8, RESULT_ERR_GENERIC=-1, RESULT_ERR_OUT_OF_MEMORY=-2, RESULT_ERR_NOT_READY=-3, RESULT_ERR_INVALID_PARAM=-4, RESULT_ERR_INVALID_FILE=-5, RESULT_ERR_CORRUPT_FILE=-6, RESULT_ERR_INVALID_CRC=-7, RESULT_ERR_UNSUPPORTED=-8, RESULT_ERR_UNSUPPORTED_FILE_VERSION=-9, RESULT_ERR_UNSUPPORTED_VSSYSTEM=-10, RESULT_ERR_UNSUPPORTED_MAPPER=-11, RESULT_ERR_MISSING_BIOS=-12, RESULT_ERR_WRONG_MODE=-13`。

输入位掩码（与 `Core::Input::Controllers::Pad::buttons` 完全一致，`NstApiInput.hpp:92-102`）：

```
A=0x01  B=0x02  SELECT=0x04  START=0x08  UP=0x10  DOWN=0x20  LEFT=0x40  RIGHT=0x80
```

---

## 1. C 头文件草案 `include/nes/nes.h`

```c
/*
 * include/nes/nes.h — NestopiaUE C ABI (field-level draft)
 * 对应内核: core/vendor/nestopiaue (tag 1.53.2, commit 4470a2e)
 * 约定: 所有跨边界结构体首字段为 uint32_t struct_size, 次字段 uint32_t version。
 *       所有 int 返回值为 nes_err 枚举 (0/正=成功, 负=失败)。
 *       UTF-8 文本; 大小端由调用方约定(移动端全小端)。
 */
#ifndef NES_NES_H
#define NES_NES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && !defined(NES_STATIC)
#  define NES_API __declspec(dllimport)
#elif defined(_WIN32) && defined(NES_STATIC)
#  define NES_API
#elif defined(__GNUC__)
#  define NES_API __attribute__((visibility("default")))
#else
#  define NES_API
#endif

#define NES_API_VERSION_MAJOR 0u
#define NES_API_VERSION_MINOR 1u
#define NES_API_VERSION_PATCH 0u
#define NES_STRUCT_VERSION 1u

/* ---------------- 句柄 ---------------- */
typedef struct nes nes_t;   /* 不透明句柄 */

/* ---------------- 错误码 (与 Nestopia Result 同值透传 + ABI 专有扩展) ---------------- */
typedef enum nes_err {
    NES_OK                       = 0,
    NES_WARN_NOP                 = 1,
    NES_WARN_BAD_DUMP            = 2,
    NES_WARN_BAD_PROM            = 3,
    NES_WARN_BAD_CROM            = 4,
    NES_WARN_BAD_FILE_HEADER     = 5,
    NES_WARN_SAVEDATA_LOST       = 6,
    NES_WARN_DATA_REPLACED       = 8,
    NES_ERR_GENERIC              = -1,
    NES_ERR_OUT_OF_MEMORY        = -2,
    NES_ERR_NOT_READY            = -3,
    NES_ERR_INVALID_PARAM        = -4,
    NES_ERR_INVALID_FILE         = -5,
    NES_ERR_CORRUPT_FILE         = -6,
    NES_ERR_INVALID_CRC          = -7,
    NES_ERR_UNSUPPORTED          = -8,
    NES_ERR_UNSUPPORTED_VER      = -9,
    NES_ERR_UNSUPPORTED_MAPPER   = -11,
    NES_ERR_MISSING_BIOS         = -12,
    NES_ERR_WRONG_MODE           = -13,
    /* ABI 专有扩展 (不与 Nestopia Result 冲突) */
    NES_ERR_BUFFER_TOO_SMALL     = -100,
    NES_ERR_NOT_IMPLEMENTED      = -200,
    NES_ERR_REENTRANT            = -201   /* 回调内重入 nes_* 被拒 */
} nes_err;

/* ---------------- 基础枚举 ---------------- */
typedef enum nes_favored_system {
    NES_FAVORED_NES_NTSC = 0,   /* Nes::Core::FAVORED_NES_NTSC */
    NES_FAVORED_NES_PAL  = 1,
    NES_FAVORED_FAMICOM  = 2,
    NES_FAVORED_DENDY    = 3
} nes_favored_system;

typedef enum nes_pixfmt {
    NES_PIXFMT_RGB565    = 0,   /* RenderState bits.count=16, r=0xF800 g=0x07E0 b=0x001F */
    NES_PIXFMT_RGB888    = 1,   /* count=24, r=0xFF0000 g=0x00FF00 b=0x0000FF */
    NES_PIXFMT_RGBA8888  = 2    /* count=32, r=0x00FF0000 g=0x0000FF00 b=0x000000FF */
} nes_pixfmt;

typedef enum nes_video_filter {
    NES_FILTER_NONE = 0,        /* Nes::Api::Video::RenderState::FILTER_NONE */
    NES_FILTER_NTSC = 1         /* phase0 不实现，仅占位 */
} nes_video_filter;

typedef enum nes_cheat_format {
    NES_CHEAT_GAME_GENIE = 0,   /* Cheats::GameGenieEncode/Decode */
    NES_CHEAT_PRO_ACTION_ROCKY = 1 /* Cheats::ProActionRockyEncode/Decode */
} nes_cheat_format;

/* ---------------- 输入位掩码 (与 Controllers::Pad::buttons 一致) ---------------- */
#define NES_BTN_A       0x01u
#define NES_BTN_B       0x02u
#define NES_BTN_SELECT  0x04u
#define NES_BTN_START   0x08u
#define NES_BTN_UP      0x10u
#define NES_BTN_DOWN    0x20u
#define NES_BTN_LEFT    0x40u
#define NES_BTN_RIGHT   0x80u
#define NES_PORT_MAX    4u

/* ---------------- 回调 (全部进程级单例，见 §0 #7) ---------------- */
typedef void (*nes_log_fn)(void* userdata, const char* text, uint32_t len);

typedef enum nes_io_action {
    NES_IO_LOAD_BATTERY = 1,    /* 内核向宿主要电池数据 (对应 File::LOAD_BATTERY) */
    NES_IO_SAVE_BATTERY = 2,    /* 内核给宿主电池数据 (File::SAVE_BATTERY) */
    NES_IO_LOAD_EEPROM  = 3,    /* File::LOAD_EEPROM */
    NES_IO_SAVE_EEPROM  = 4,    /* File::SAVE_EEPROM */
    NES_IO_LOAD_FDS     = 5,    /* File::LOAD_FDS (多盘游戏取盘面) */
    NES_IO_SAVE_FDS     = 6,    /* File::SAVE_FDS (写回盘面) */
    NES_IO_LOAD_ROM     = 7     /* File::LOAD_ROM (VS/多卡) */
} nes_io_action;

#define NES_IO_DIR_HOST_READS   0   /* core->host: data/data_len 有效，宿主消费/持久化 */
#define NES_IO_DIR_HOST_WRITES  1   /* host->core: buf/buf_cap/buf_len 有效，宿主填数据 */

/*
 * 核心发起的 IO 回调。跑在模拟器线程上；禁止在回调内调用任何 nes_* (会返回 NES_ERR_REENTRANT)。
 * direction=HOST_READS:  host 读取 data[0..data_len) 并持久化; 返回 0 成功 / 负 nes_err 中止。
 * direction=HOST_WRITES: host 向 buf 写数据, 置 *buf_len; 返回 0 成功(且 *buf_len>0) / 负 nes_err 中止。
 */
typedef int (*nes_file_io_fn)(void* userdata, int action, int direction,
                              uint8_t* buf, size_t buf_cap, size_t* buf_len,
                              const uint8_t* data, size_t data_len);

typedef enum nes_event {
    NES_EVENT_CPU_JAM            = 1,  /* Api::User::EVENT_CPU_JAM */
    NES_EVENT_DISPLAY_TIMER      = 2,  /* Api::User::EVENT_DISPLAY_TIMER */
    NES_EVENT_CPU_UNOFFICIAL_OPCODE = 3 /* Api::User::EVENT_CPU_UNOFFICIAL_OPCODE */
} nes_event;
typedef void (*nes_event_fn)(void* userdata, int event, const void* context);

typedef enum nes_question {
    NES_QUESTION_NST_CRC_FAIL_CONTINUE = 1  /* Api::User::QUESTION_NST_PRG_CRC_FAIL_CONTINUE */
} nes_question;
/* 返回: 1=继续 0=中止 (对应 User::Answer::ANSWER_YES / ANSWER_NO) */
typedef int (*nes_question_fn)(void* userdata, int question);

/* ---------------- 跨边界结构体 (均带 struct_size + version) ---------------- */
typedef struct nes_config {
    uint32_t struct_size;
    uint32_t version;
    nes_favored_system favored_system;   /* 缺省 NES_FAVORED_NES_NTSC */
    uint32_t sample_rate;                /* 缺省 48000 */
    nes_pixfmt pixfmt;                   /* 缺省 RGB565 */
} nes_config;

typedef struct nes_caps {
    uint32_t struct_size;
    uint32_t version;
    uint32_t has_fds;          /* FDS 盘面模拟可用(仍缺 BIOS) */
    uint32_t has_nsf;          /* NSF 音频播放 */
    uint32_t has_rewinder;     /* NstApiRewinder 存在 */
    uint32_t has_debugger;     /* 恒为 0: 内核无调试器 */
    uint32_t has_bps_patch;    /* 恒为 0: 内核只 IPS/UPS */
    uint32_t max_cheat_codes;
    uint32_t num_pads;         /* =4 */
} nes_caps;

typedef struct nes_video_frame {
    uint32_t struct_size;
    uint32_t version;
    uint32_t width;            /* 256 */
    uint32_t height;           /* 240 */
    nes_pixfmt format;
    int32_t  pitch;            /* 字节/行, ABI 保证 >0 (自顶向下) */
    const void* pixels;        /* 指向壳内后缓冲, 下次 nes_run_frames 前有效 */
} nes_video_frame;

typedef struct nes_rom_info {
    uint32_t struct_size;
    uint32_t version;
    char     title[256];       /* UTF-8, 由 std::wstring 转换 */
    char     publisher[128];
    char     developer[128];
    char     region[16];       /* 例 "NTSC"/"PAL"/"" */
    uint32_t mapper;
    uint32_t submapper;
    uint32_t prg_size;         /* PRG-ROM 字节 */
    uint32_t chr_size;         /* CHR-ROM 字节 */
    uint32_t wram_size;        /* W-RAM 字节 */
    uint32_t vram_size;        /* V-RAM 字节 */
    uint32_t has_battery;
    uint32_t system;           /* Nes::Core::System 枚举值: SYSTEM_* */
    uint32_t cpu;              /* Nes::Core::Cpu 值 (RP2A03=0...) */
    uint32_t ppu;              /* Nes::Core::Ppu 值 */
    uint32_t region_ntsc;      /* 1=NTSC, 0=PAL */
    uint32_t patched;          /* 1=已软打补丁 */
    char     sha1[41];         /* 40 hex + NUL */
    char     crc32[9];         /* 8 hex + NUL */
    uint32_t players;
} nes_rom_info;

/* ---------------- 生命周期 / 版本能力 ---------------- */
NES_API nes_t* nes_create(const nes_config* cfg);
NES_API void   nes_destroy(nes_t* nes);
NES_API uint32_t nes_api_version(void);              /* (MAJOR<<16)|(MINOR<<8)|PATCH */
NES_API const char* nes_core_version(void);          /* 例 "1.53.2" */
NES_API int    nes_get_capabilities(const nes_t* nes, nes_caps_t* caps);

/* ---------------- ROM 加载 / 电源 / 复位 ---------------- */
NES_API int nes_load_rom(nes_t* nes, const uint8_t* data, size_t size, nes_rom_info_t* info_out);
NES_API int nes_load_rom_patched(nes_t* nes,
                                 const uint8_t* rom, size_t rom_size,
                                 const uint8_t* patch, size_t patch_size, /* IPS/UPS */
                                 nes_rom_info_t* info_out);
NES_API int nes_unload(nes_t* nes);                  /* 触发 SAVE_BATTERY */
NES_API int nes_power(nes_t* nes, int on);           /* on=0 触发 SAVE_BATTERY */
NES_API int nes_reset(nes_t* nes, int hard);         /* Machine::Reset(hard) */

/* ---------------- 运行 (帧循环) ---------------- */
/* 跑至多 max_frames 帧; 每帧音频写入 audio_out (mono int16)。
 * audio_cap_samples 为 int16 个数上限; 输出 frames_run / samples_written。 */
NES_API int nes_run_frames(nes_t* nes, uint32_t max_frames,
                           int16_t* audio_out, uint32_t audio_cap_samples,
                           uint32_t* frames_run, uint32_t* samples_written);

/* ---------------- 视频 / 音频 格式 ---------------- */
NES_API int nes_set_video_format(nes_t* nes, nes_pixfmt format, nes_video_filter filter);
NES_API const nes_video_frame_t* nes_get_video_frame(const nes_t* nes);
NES_API int nes_set_audio_format(nes_t* nes, uint32_t sample_rate, int stereo); /* stereo: 0/1 */

/* ---------------- 输入 (推式; 壳内转拉式回调) ---------------- */
NES_API void nes_set_input(nes_t* nes, uint32_t port, uint32_t buttons);  /* port 0..3 */
NES_API void nes_clear_input(nes_t* nes);

/* ---------------- 即时存档 / 电池 ---------------- */
NES_API int nes_save_state(nes_t* nes, uint8_t* out, size_t cap, size_t* written, size_t* needed);
NES_API int nes_load_state(nes_t* nes, const uint8_t* in, size_t size);
NES_API int nes_battery_flush(nes_t* nes);  /* 见 §2 说明: phase0 无干净内核 API */

/* ---------------- 金手指 ---------------- */
NES_API int nes_cheat_add(nes_t* nes, uint16_t addr, uint8_t value, uint8_t compare, int use_compare);
NES_API int nes_cheat_remove(nes_t* nes, uint32_t index);
NES_API int nes_cheat_clear(nes_t* nes);
NES_API int nes_cheat_count(const nes_t* nes, uint32_t* count);
NES_API int nes_cheat_encode(nes_t* nes, nes_cheat_format fmt,
                             uint16_t addr, uint8_t value, uint8_t compare, int use_compare,
                             char* out, size_t cap);   /* 例 "SXEZSKOZ" */
NES_API int nes_cheat_decode(nes_t* nes, nes_cheat_format fmt, const char* code,
                             uint16_t* addr, uint8_t* value, uint8_t* compare, int* use_compare);

/* ---------------- 内存 ---------------- */
NES_API int nes_mem_read(nes_t* nes, uint16_t addr, uint8_t* out);   /* phase0: NOT_IMPLEMENTED */
NES_API int nes_mem_write(nes_t* nes, uint16_t addr, uint8_t value); /* phase0: NOT_IMPLEMENTED */
NES_API int nes_get_cpu_ram(const nes_t* nes, const uint8_t** ram, size_t* size); /* 2KB, Cheats::GetRam */

/* ---------------- ROM 头信息 ---------------- */
NES_API int nes_get_rom_info(const nes_t* nes, nes_rom_info_t* info);

/* ---------------- FDS ---------------- */
NES_API int nes_set_fds_bios(nes_t* nes, const uint8_t* bios, size_t size);
NES_API int nes_fds_insert_disk(nes_t* nes, uint32_t disk, uint32_t side); /* side 0=A 1=B */
NES_API int nes_fds_change_side(nes_t* nes);
NES_API int nes_fds_eject_disk(nes_t* nes);
NES_API int nes_fds_disk_count(const nes_t* nes, uint32_t* disks, uint32_t* sides);

/* ---------------- 回调注册 (进程级单例) ---------------- */
NES_API int nes_set_log_callback(nes_t* nes, nes_log_fn fn, void* userdata);
NES_API int nes_set_file_io_callback(nes_t* nes, nes_file_io_fn fn, void* userdata);
NES_API int nes_set_event_callback(nes_t* nes, nes_event_fn fn, void* userdata);
NES_API int nes_set_question_callback(nes_t* nes, nes_question_fn fn, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* NES_NES_H */
```

---

## 2. 逐函数 → NestopiaUE API 映射（类名+方法名+关键参数，签名与 `source/core/api/*.hpp` 一致）

> 命名空间约定：`Nes::Api::Machine` 简写为 `Machine`，`Nes::Api::Video` 为 `Video`，`Nes::Core::Input::Controllers` 为 `Controllers` 等。

### 生命周期 / 版本
- `nes_create(cfg)`：`new` 内部壳对象；持有 `Nes::Api::Emulator emulator`（构造 `Api::Emulator()`）；按 `cfg` 调 `Machine::SetMode(Machine::NTSC/PAL)`、`Video::SetRenderState(...)`、`Sound::SetSampleRate(...)`/`Sound::SetSpeaker(...)`；注册 4 个静态回调到「单实例分发器」（见 §0 #7）。`cfg==NULL` 用缺省。
- `nes_destroy`：`Machine::Unload()`（触发 SAVE_BATTERY）→ 注销回调 → `delete`。`Api::Emulator::~Emulator()`。
- `nes_api_version`：返回本头文件 `NES_API_VERSION_*` 组装值，与内核无关。
- `nes_core_version`：壳内编译期常量 `"1.53.2"`（可读 `NST_VERSION` 宏，若有）。
- `nes_get_capabilities`：填 `nes_caps`。`has_fds`/`has_nsf`/`has_rewinder` 由编译期特性决定；`has_debugger=has_bps_patch=0`；`max_cheat_codes=...`；`num_pads=Core::Input::NUM_PADS`(4)。

### ROM 加载
- `nes_load_rom`：`data/size` → `mem_input_streambuf` → `std::istream`；调 `Machine::Load(stream, favSystem)`（`Machine::Load(std::istream&, FavoredSystem, AskProfile=DONT_ASK_PROFILE)`，`NstApiMachine.hpp:184`）。成功后调 `Input::AutoSelectControllers()` + `Input::AutoSelectAdapter()`（`NstApiInput.hpp:884-896`）。若 `info_out` 非空，调 `nes_get_rom_info` 填充。返回 `Result` 透传。
- `nes_load_rom_patched`：rom 与 patch 各包一个 `std::istream`；构造 `Machine::Patch patch(patch_stream, /*bypassChecksum=*/false)`（`NstApiMachine.hpp:172`）；调 `Machine::Load(rom_stream, favSystem, patch, DONT_ASK_PROFILE)`（`NstApiMachine.hpp:195`）。⚠️ 只支持 IPS/UPS（`NstPatcher.hpp` IPS/UPS；BPS 不支持）。
- `nes_unload`：`Machine::Unload()`（`NstApiMachine.hpp:241`；内部 `PowerOff()` → 触发 SAVE_BATTERY）。
- `nes_power`：`Machine::Power(bool)`（`NstApiMachine.hpp:249`）。`Power(false)` 走 `Machine::PowerOff` → `image->PowerOff()` → `File::Save`（触发 SAVE_BATTERY，`NstFile.cpp:518-528`）。
- `nes_reset`：`Machine::Reset(bool hard)`（`NstApiMachine.hpp:257`）。

### 运行（帧循环）
- `nes_run_frames(nes, max_frames, audio_out, cap, &run, &written)`：
  1. 取内部 `Video::Output vo`：`vo.pixels = 内部后缓冲`, `vo.pitch = 256 * bpp`（正 pitch、自顶向下）。不设 `Video::Output::lockCallback/unlockCallback`（缺省无回调时 `Output::Locker::operator()` 走 `!function && pixels && pitch`，`NstApiVideo.hpp:122-128`）。
  2. 取内部 `Sound::Output so`：`so.samples[0]=audio_out+written`, `so.length[0]=per_frame`, `so.samples[1]=so.length[1]=0`。`per_frame = sample_rate / 60.0988`（NTSC）或 `/50.0070`（PAL）取整；核心会把不足/超额样本内部缓冲（`NstApiSound.hpp:73-79` 语义）。
  3. `for i in 0..max_frames: Result r = emulator.Execute(&vo, &so, &controllers)`（`NstApiEmulator.hpp:Execute`，一次 1 帧）。`written += so.length[0]`（实际产出=请求数，核心保证写满 `length[0]`，见 `NstApu.cpp:847` `streamed = length[0]+length[1]`）。
  4. `*run=i; *samples_written=written;` 返回最后一个非 OK 的 `Result` 或 `NES_OK`。
  - ⚠️ 音频主时钟由**宿主**负责：宿主按 AudioTrack 缓冲下探量反推 `max_frames`，本函数只「跑 N 帧 + 交样本」。

### 视频 / 音频格式
- `nes_set_video_format`：填 `Video::RenderState rs`（`NstApiVideo.hpp:639`）：`rs.width=256; rs.height=240; rs.filter=FILTER_NONE`；按 `pixfmt` 设 `rs.bits.count` 与 `rs.bits.mask{r,g,b}`（RGB565: count=16, 0xF800/0x07E0/0x001F；RGBA8888: count=32, 0x00FF0000/0x0000FF00/0x000000FF）；调 `Video::SetRenderState(rs)`（`NstApiVideo.hpp:767`）。同步重分配壳内后缓冲。
- `nes_get_video_frame`：返回壳内 `nes_video_frame_t`（指针指向后缓冲；`width=256,height=240,pitch=256*bpp`）。
- `nes_set_audio_format`：`Sound::SetSampleRate(rate)`（`NstApiSound.hpp:268`，44100–96000）；`Sound::SetSpeaker(stereo? SPEAKER_STEREO: SPEAKER_MONO)`（`NstApiSound.hpp:282`）。phase0 建议 mono。

### 输入（推 → 拉桥接）
- `nes_set_input(port,buttons)`：写 `g_buttons[port].store(buttons, relaxed)`。
- `nes_clear_input`：`g_buttons[0..3].store(0)`。
- 壳内（`nes_create` 时）：`Controllers::Pad::callback.Set(&on_pad_poll, nes)`；`on_pad_poll(void* u, Controllers::Pad& pad, uint idx)` 里 `pad.buttons = g_buttons[idx].load()` 并 `return true`（回调签名 `bool(void*, Pad&, uint)`，`NstApiInput.hpp:116`；调用点 `NstInpPad.cpp:102` `Controllers::Pad::callback(pad, type - Api::Input::PAD1)`）。⚠️ 无 `SetInput`，这是唯一入口。

### 即时存档 / 电池
- `nes_save_state`：`out/cap` 包成 `mem_output_streambuf`（计数 + 超容截断）→ `std::ostream`；调 `Machine::SaveState(ostream, Machine::USE_COMPRESSION)`（`NstApiMachine.hpp:319`，`Compression` 枚举 `NstApiMachine.hpp:292`）。超容返回 `NES_ERR_BUFFER_TOO_SMALL` 并回填 `*needed`（由 streambuf 计数）。
- `nes_load_state`：`in/size` → `mem_input_streambuf` → `std::istream`；调 `Machine::LoadState(istream)`（`NstApiMachine.hpp:310`）。CRC 不匹配时若注册了 `questionCallback` 会抛 `QUESTION_NST_PRG_CRC_FAIL_CONTINUE`（`NstApiUser.hpp:76`）。
- `nes_battery_flush`：⚠️ **1.53.2 无公开「仅存电池」API**；SAVE_BATTERY 只在 `Unload()`/`Power(false)`（`Machine::PowerOff`→`image->PowerOff`）与脏检测（`NstFile.cpp:380-385` checksum）下触发。**phase0 实现为返回 `NES_ERR_NOT_IMPLEMENTED`**，并在文档写明：宿主想即时安全点请用 `nes_save_state`（NST 是电池的超集）；或接受「`nes_unload` 时统一落盘」。

### 金手指
- `nes_cheat_add`：`Cheats::SetCode(Cheats::Code(addr,value,compare,useCompare))`（`NstApiCheats.hpp:104`）。
- `nes_cheat_remove`：`Cheats::DeleteCode(index)`（`NstApiCheats.hpp:133`）。
- `nes_cheat_clear`：`Cheats::ClearCodes()`（`NstApiCheats.hpp:147`）。
- `nes_cheat_count`：`Cheats::NumCodes()`（`NstApiCheats.hpp:140`）。
- `nes_cheat_encode`：GG→`Cheats::GameGenieEncode(code, char(&)[9])`；PAR→`Cheats::ProActionRockyEncode(code, char(&)[9])`（`NstApiCheats.hpp:173,191`，静态 `NST_CALL`）。
- `nes_cheat_decode`：`Cheats::GameGenieDecode(const char*, Code&)` / `ProActionRockyDecode(...)`（`NstApiCheats.hpp:182,200`）。

### 内存
- `nes_get_cpu_ram`：`Cheats::GetRam()`（`NstApiCheats.hpp:164`，返回 `const uchar (&)[0x800]` 只读 2KB CPU RAM）。
- `nes_mem_read/write`：⚠️ 内核无「任意地址总线 peek/poke」公开 API。**phase0 返回 `NES_ERR_NOT_IMPLEMENTED`**；未来要么改 core（加 CPU/PPU 总线访问器），要么只支持 0x0000–0x1FFF 的 CPU RAM 映射。

### ROM 头信息
- `nes_get_rom_info`：`Cartridge::GetProfile()`（`NstApiCartridge.hpp:1324`，无卡带返回 NULL→`NES_ERR_NOT_READY`）。字段来源：`profile.game.title/publisher/developer/region`（`std::wstring`→UTF-8）；`profile.board.mapper/subMapper/GetPrg()/GetChr()/GetWram()/GetVram()/HasBattery()`（`NstApiCartridge.hpp:508-536`）；`profile.system.type/cpu/ppu`（`NstApiCartridge.hpp:456-464`）；`profile.hash.GetSha1()/GetCrc32()`（`NstApiCartridge.hpp:192-199`）；`profile.patched`；`profile.game.players`。`region_ntsc` 由 `Machine::GetMode()==Machine::NTSC` 判定。

### FDS
- `nes_set_fds_bios`：`bios/size` → `mem_input_streambuf` → `std::istream`；调 `Fds::SetBIOS(&stream)`（`NstApiFds.hpp:108`，参数是 `std::istream*`）。判 `Fds::HasBIOS()`（`NstApiFds.hpp:123`）。
- `nes_fds_insert_disk`：`Fds::InsertDisk(disk, side)`（`NstApiFds.hpp:86`）。
- `nes_fds_change_side`：`Fds::ChangeSide()`（`NstApiFds.hpp:93`）。
- `nes_fds_eject_disk`：`Fds::EjectDisk()`（`NstApiFds.hpp:100`）。
- `nes_fds_disk_count`：`Fds::GetNumDisks()` / `Fds::GetNumSides()`（`NstApiFds.hpp:130,137`）。
- ⚠️ FDS 盘镜像本体经 `Machine::LoadDisk(std::istream&, FavoredSystem)`（`NstApiMachine.hpp:225`）加载；多盘/多面游戏在运行中经 `User::fileIoCallback` 的 `LOAD_FDS/SAVE_FDS` 取/存盘面。

### 回调注册
- `nes_set_log_callback`：`Api::User::logCallback.Set(fn, userdata)`（`NstApiUser.hpp:390`）。
- `nes_set_file_io_callback`：`Api::User::fileIoCallback.Set(fn, userdata)`（`NstApiUser.hpp:411`）。壳内包装：收到 `File& file` 后读 `file.GetAction()`，按 `SAVE_*` 调 `file.GetContent(const void*& mem, ulong& size)`（`NstApiUser.hpp:269`）→ 转成 `nes_file_io_fn(direction=HOST_READS, data=mem, len=size)`；按 `LOAD_*` 先 `file.GetMaxSize()`（`NstApiUser.hpp:248`）→ 调 `nes_file_io_fn(direction=HOST_WRITES, buf, cap)` → 宿主填好 → `file.SetContent(buf, len)`（`NstApiUser.hpp:304`）。若宿主返回负值，`SetContent` 不调用、内核得到「未加载」结果。
- `nes_set_event_callback`：`Api::User::eventCallback.Set(...)`（`NstApiUser.hpp:397`；事件 `EVENT_CPU_JAM/EVENT_DISPLAY_TIMER/EVENT_CPU_UNOFFICIAL_OPCODE`）。
- `nes_set_question_callback`：`Api::User::questionCallback.Set(...)`（`NstApiUser.hpp:405`；`Question`/`Answer` 枚举 `NstApiUser.hpp:71-100`）。

---

## 3. 阶段0 实现 vs NOT_IMPLEMENTED 标注

| 函数 | 阶段0 | 说明 |
|---|---|---|
| `nes_create/destroy/api_version/core_version/get_capabilities` | ✅ 实现 | 骨架 |
| `nes_load_rom` / `nes_unload` / `nes_power` / `nes_reset` | ✅ 实现 | 最小可玩闭环 |
| `nes_load_rom_patched` | ⏳ NOT_IMPLEMENTED | 阶段1（IPS/UPS 打通后再开） |
| `nes_run_frames` | ✅ 实现 | 核心时序点 |
| `nes_set_video_format` / `nes_get_video_frame` | ✅ 实现 | 仅 RGB565 生效，NTSC 滤波占位 |
| `nes_set_audio_format` | ✅ 实现 | 只 mono=0 生效，stereo=1 可返回 NOT_IMPLEMENTED |
| `nes_set_input` / `nes_clear_input` | ✅ 实现 | 推→拉桥 |
| `nes_save_state` / `nes_load_state` | ✅ 实现 | 压缩=USE_COMPRESSION |
| `nes_battery_flush` | ⏳ NOT_IMPLEMENTED | 无干净内核 API，见 §2 |
| `nes_cheat_*` | ✅ 实现 | 内核原生齐全（GG/PAR 编解码 + Code 增删查） |
| `nes_mem_read` / `nes_mem_write` | ⏳ NOT_IMPLEMENTED | 内核无总线 peek/poke |
| `nes_get_cpu_ram` | ✅ 实现 | Cheats::GetRam 2KB 只读 |
| `nes_get_rom_info` | ✅ 实现 | GetProfile 摊平 |
| `nes_set_fds_bios` / `nes_fds_*` | ⏳ NOT_IMPLEMENTED | 阶段2；但 ABI 形状先占位 |
| `nes_set_*_callback` (log/file_io/event/question) | ✅ 实现 | file_io 是电池存档的必经通道 |

> 结论：阶段0 必须实现的最小面 = 生命周期 + load/unload/power/reset + run_frames + video(RGB565) + audio(mono) + input + save/load_state + cheat + get_cpu_ram + get_rom_info + 4 个回调。其余 `NOT_IMPLEMENTED` 但**签名与结构体先冻结**（size/version 已在字段里，后续加字段不破坏 ABI）。

---

## 4. `std::streambuf` 适配器（内存字节流）实现要点

1. **两类适配器**：只读输入 `mem_input_streambuf(const uint8_t*, size_t)` 与只写输出 `mem_output_streambuf(uint8_t*, size_t cap, size_t* written, size_t* needed)`。分别喂给 `std::istream` / `std::ostream`（`std::istream is(&inbuf)`、`std::ostream os(&outbuf)`，无需 `seek`）。
2. **只读 `underflow()`**：维护 `gptr/egptr` 指向 `[data, data+size)`；首次 `underflow()` 把 `setg(data, data, data+size)` 返回 `*gptr`，读到末尾返回 `traits::eof()`。ROM/存档/补丁/FDS BIOS 全是顺序读，**不需要 `seekoff/seekpos`**（若未来要 seek，返回 `pos_type(off_type(-1))` 即可，Nestopia 顺序读不依赖）。
3. **只写 `overflow()`/`xsputn()`**：`overflow(c)` 把 1 字节写入缓冲；`xsputn(s,n)` 整块 memcpy 并**检查 `written+n > cap`**，超容则置 `overflowed=true` 并返回 0（`std::ostream` 会置 `badbit`）。写满后 `*written` 累计、`*needed` 累计到真实需求。`SaveState` 写坏时（badbit）Nestopia 会返回错误，壳据此映射 `NES_ERR_BUFFER_TOO_SMALL`。
4. **生命周期与异常边界**：`streambuf` 与 `istream/ostream` 必须在调用 `Machine::Load/SaveState/...` 的**同一个栈帧**内活过全程（流对象析构前内核已读完/写完）。Nestopia 内部用 `try/catch(Result)`（`NstFile.cpp:328-339`），streambuf 抛 `std::bad_alloc` 会被捕获为 `RESULT_ERR_OUT_OF_MEMORY`，**不要在 streambuf 里抛自定义异常**，用「截断 + badbit」表达超容。
5. **`fileIoCallback` 的流方向**：`SAVE_BATTERY` 走 `file.GetContent(const void*&, ulong&)`（直接拿指针，**不用流**，`NstApiUser.hpp:269`）最省拷贝；`LOAD_BATTERY` 走 `file.SetContent(const void*, ulong)`（`NstApiUser.hpp:304`）直接喂指针。二者都无需在回调里造 streambuf——只有 `nes_load_rom/state/bios/patch` 与 `nes_save_state` 这四个入口才需要。
6. **线程/重入**：`fileIoCallback`、`Pad::callback`、`logCallback` 都在模拟器线程（`nes_run_frames`/`nes_load_rom` 调用栈内）触发；壳内适配器必须**非阻塞**、**不得重入** `nes_*`（用线程局部 `in_callback` 标志，重入即返 `NES_ERR_REENTRANT`）。
7. **wchar_t 转换**：`rom_info` 里 `std::wstring → UTF-8` 用 `std::wstring_convert`（C++17 已弃用但可用）或手写 UTF-32→UTF-8，**不要**把 `wchar_t*` 直接暴露给 C ABI（Android/Linux 4 字节、Windows 2 字节）。
