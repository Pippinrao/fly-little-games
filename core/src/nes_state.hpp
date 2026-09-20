/*
 * core/src/nes_state.hpp — NST 存档安全包装器 (S1-2)
 *
 * "升级不丢档"硬承诺的落点 (spec §8): nes_save_state 产出的存档不再直接是
 * 裸 NST 字节, 而是带 81 字节 FLYNST1 头的包装格式 —— 头内记录格式版本、
 * 内核版本、源 ROM 的 SHA1 与载荷 CRC32, 使:
 *
 *   - 未来内核版本号升级后旧档仍可被识别 (版本字段), 格式不兼容时显式报
 *     NES_ERR_UNSUPPORTED_VER, 而不是加载出静默损坏;
 *   - 换 ROM 后误加载旧档被 SHA1 校验拦截 (NES_ERR_STATE_ROM_MISMATCH);
 *   - 载荷被篡改/截断被 CRC32 拦截 (NES_ERR_INVALID_CRC);
 *   - 旧版裸 NST 档 (无头) 仍按兼容路径原样喂给 Machine::LoadState。
 *
 * 头布局 (全部小端):
 *   offset 0  : magic[8]        = "FLYNST1\0"
 *   offset 8  : version u32 LE  = 2 (reader also accepts 1)
 *   offset 12 : core_version[16] = "1.53.2" NUL 填充
 *   offset 28 : rom_sha1[41]    = 40 hex + NUL (来自 Cartridge profile hash;
 *                                 无卡带时全 0)
 *   offset 69 : payload_len u64 LE (metadata + raw NST for version 2)
 *   offset 77 : payload_crc32 u32 LE (zlib crc32 over metadata + raw NST)
 *   offset 81 : v2 remainder IEEE-754 binary64 bits, u64 LE
 *   offset 89 : v2 clock mode u32 LE: 0 unset, 1 NTSC, 2 PAL
 *   offset 93 : v2 raw NST bytes (v1 raw NST starts at offset 81)
 *
 * v2 remainder must be finite and in [0, 1); unset requires zero.
 * v1/raw states have no clock metadata and restore the defined initial clock.
 *
 * 错误码复用 nes.h 的 ABI 枚举: NES_ERR_BUFFER_TOO_SMALL / NES_ERR_UNSUPPORTED_VER
 * / NES_ERR_INVALID_CRC / NES_ERR_CORRUPT_FILE / NES_ERR_STATE_ROM_MISMATCH。
 */
#ifndef NES_STATE_HPP
#define NES_STATE_HPP

#include <cstddef>
#include <cstdint>

namespace flynes_state
{
	enum class AudioClockMode : uint32_t { Unset = 0, Ntsc = 1, Pal = 2 };
	struct AudioClock
	{
		double remainder = 0.0;
		AudioClockMode mode = AudioClockMode::Unset;
	};

	// Wrap raw NST bytes with the FLYNST1 header into out[0..cap).
	// Version 2 adds 12 bytes of CRC-protected audio clock metadata.
	// On success *written = 93 + raw_len; short buffers return this in *needed.
	int wrap(const uint8_t* raw, size_t raw_len, const char* sha1_hex /*40+NUL or all-zero*/,
	         const AudioClock& clock,
	         uint8_t* out, size_t cap, size_t* written, size_t* needed);

	// Unwrap: validate magic/version/len/crc32/sha1.
	// - magic mismatch → returns 0 with *is_wrapped=false (caller treats input as legacy raw NST)
	// - version other than 1/2 → NES_ERR_UNSUPPORTED_VER
	// - crc32 mismatch → NES_ERR_INVALID_CRC
	// - sha1 not all-zero and != current_sha1_hex → NES_ERR_STATE_ROM_MISMATCH
	// On success: *payload points to raw NST, *clock contains validated metadata
	// (or the initial clock for version 1/raw), returns NES_OK.
	int unwrap(const uint8_t* in, size_t size, const char* current_sha1_hex,
	           const uint8_t** payload, size_t* payload_len, bool* is_wrapped,
	           AudioClock* clock);

	// zlib crc32 helper (wraps ::crc32 from zlib.h — include <zlib.h>; link via nestopia's PUBLIC zlib)
	uint32_t crc32_bytes(const uint8_t* p, size_t n);
}

#endif /* NES_STATE_HPP */
