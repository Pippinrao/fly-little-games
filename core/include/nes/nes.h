/*
 * core/include/nes/nes.h — NestopiaUE C ABI (field-level draft, Task 2)
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
    NES_ERR_UNSUPPORTED_VER      = -9,      /* RESULT_ERR_UNSUPPORTED_FILE_VERSION */
    NES_ERR_UNSUPPORTED_VSSYSTEM = -10,     /* RESULT_ERR_UNSUPPORTED_VSSYSTEM */
    NES_ERR_UNSUPPORTED_MAPPER   = -11,
    NES_ERR_MISSING_BIOS         = -12,
    NES_ERR_WRONG_MODE           = -13,
    /* ABI 专有扩展 (不与 Nestopia Result 冲突) */
    NES_ERR_BUFFER_TOO_SMALL     = -100,
    NES_ERR_NOT_IMPLEMENTED      = -200,
    NES_ERR_REENTRANT            = -201,  /* 回调内重入 nes_* 被拒 */
    NES_ERR_STATE_ROM_MISMATCH   = -202   /* 存档与当前 ROM 的 SHA1 不匹配 */
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
    NES_FILTER_NTSC = 1,        /* 保留（后续放开，本次不实现） */
    NES_FILTER_HQ2X = 2,        /* RenderState::FILTER_HQ2X */
    NES_FILTER_HQ3X = 3,        /* RenderState::FILTER_HQ3X */
    NES_FILTER_HQ4X = 4         /* RenderState::FILTER_HQ4X */
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

typedef struct nes_video_snapshot {
    uint32_t struct_size;
    uint32_t version;
    uint64_t sequence;
    uint32_t width;
    uint32_t height;
    nes_pixfmt format;
    int32_t pitch;
    size_t bytes_written;
} nes_video_snapshot;

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
NES_API int    nes_get_capabilities(const nes_t* nes, nes_caps* caps);

/* ---------------- ROM 加载 / 电源 / 复位 ---------------- */
NES_API int nes_load_rom(nes_t* nes, const uint8_t* data, size_t size, nes_rom_info* info_out);
NES_API int nes_load_rom_patched(nes_t* nes,
                                 const uint8_t* rom, size_t rom_size,
                                 const uint8_t* patch, size_t patch_size, /* IPS/UPS */
                                 nes_rom_info* info_out);
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
NES_API const nes_video_frame* nes_get_video_frame(const nes_t* nes);
NES_API int nes_copy_video_frame(const nes_t* nes, void* out, size_t cap,
                                 nes_video_snapshot* snapshot);
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
NES_API int nes_get_rom_info(const nes_t* nes, nes_rom_info* info);

/* ---------------- FDS ---------------- */
NES_API int nes_set_fds_bios(nes_t* nes, const uint8_t* bios, size_t size);
NES_API int nes_fds_insert_disk(nes_t* nes, uint32_t disk, uint32_t side); /* side 0=A 1=B */
NES_API int nes_fds_change_side(nes_t* nes);
NES_API int nes_fds_eject_disk(nes_t* nes);
NES_API int nes_fds_disk_count(const nes_t* nes, uint32_t* disks, uint32_t* sides);

/* ---------------- 数据库（NstDatabase.xml 随包分发，加载于 load_rom 前） ---------------- */
NES_API int nes_load_database(nes_t* nes, const uint8_t* xml, size_t size);

/* ---------------- 回调注册 (进程级单例) ---------------- */
NES_API int nes_set_log_callback(nes_t* nes, nes_log_fn fn, void* userdata);
NES_API int nes_set_file_io_callback(nes_t* nes, nes_file_io_fn fn, void* userdata);
NES_API int nes_set_event_callback(nes_t* nes, nes_event_fn fn, void* userdata);
NES_API int nes_set_question_callback(nes_t* nes, nes_question_fn fn, void* userdata);

#ifdef __cplusplus
}
#endif

#endif /* NES_NES_H */
