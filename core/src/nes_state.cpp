/*
 * core/src/nes_state.cpp — NST 存档安全包装器实现 (S1-2)
 *
 * 见 nes_state.hpp 的格式说明。所有多字节字段显式按小端字节序手工拼装
 * (put_u32_le/put_u64_le), 不依赖宿主大小端。
 */
#include "nes_state.hpp"
#include "nes/nes.h"

#include <cstring>
#include <cmath>
#include <limits>
#include <zlib.h>

namespace flynes_state
{
	namespace
	{
		constexpr size_t   kHeaderSize = 81;
		constexpr size_t   kClockSize = 12;
		constexpr size_t   kMagicLen   = 8;
		constexpr uint32_t kVersion    = 2;
		static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
		              "State audio clock requires IEEE-754 binary64");

		// "FLYNST1\0" — 与后续版本区分的稳定魔数
		constexpr char kMagic[kMagicLen] = {'F', 'L', 'Y', 'N', 'S', 'T', '1', '\0'};

		void put_u32_le(uint8_t* dst, uint32_t v)
		{
			dst[0] = static_cast<uint8_t>(v & 0xFFu);
			dst[1] = static_cast<uint8_t>((v >> 8) & 0xFFu);
			dst[2] = static_cast<uint8_t>((v >> 16) & 0xFFu);
			dst[3] = static_cast<uint8_t>((v >> 24) & 0xFFu);
		}

		void put_u64_le(uint8_t* dst, uint64_t v)
		{
			for (int i = 0; i < 8; ++i)
				dst[i] = static_cast<uint8_t>((v >> (8 * i)) & 0xFFu);
		}

		uint32_t get_u32_le(const uint8_t* p)
		{
			return static_cast<uint32_t>(p[0])
			     | (static_cast<uint32_t>(p[1]) << 8)
			     | (static_cast<uint32_t>(p[2]) << 16)
			     | (static_cast<uint32_t>(p[3]) << 24);
		}

		uint64_t get_u64_le(const uint8_t* p)
		{
			uint64_t v = 0;
			for (int i = 7; i >= 0; --i)
				v = (v << 8) | static_cast<uint64_t>(p[i]);
			return v;
		}

		bool all_zero(const uint8_t* p, size_t n)
		{
			for (size_t i = 0; i < n; ++i)
			{
				if (p[i] != 0)
					return false;
			}
			return true;
		}

		bool valid_clock(const AudioClock& clock)
		{
			return std::isfinite(clock.remainder) && clock.remainder >= 0.0 &&
			       clock.remainder < 1.0 &&
			       (clock.mode == AudioClockMode::Ntsc || clock.mode == AudioClockMode::Pal ||
			        (clock.mode == AudioClockMode::Unset && clock.remainder == 0.0));
		}
	}

	uint32_t crc32_bytes(const uint8_t* p, size_t n)
	{
		// zlib crc32(uLong crc, const Bytef* buf, uInt len); 载荷为 NST 状态档,
		// 远小于 uInt 上限, 直接窄化安全。
		return static_cast<uint32_t>(::crc32(0L, p ? p : Z_NULL, static_cast<uInt>(n)));
	}

	int wrap(const uint8_t* raw, size_t raw_len, const char* sha1_hex,
	         const AudioClock& clock,
	         uint8_t* out, size_t cap, size_t* written, size_t* needed)
	{
		if (!raw || !sha1_hex)
			return NES_ERR_INVALID_PARAM;
		if (cap > 0 && !out)
			return NES_ERR_INVALID_PARAM;
		if (!written || !needed)
			return NES_ERR_INVALID_PARAM;
		if (!valid_clock(clock) || raw_len == 0 ||
		    raw_len > std::numeric_limits<size_t>::max() - kHeaderSize - kClockSize)
			return NES_ERR_INVALID_PARAM;

		const size_t total = kHeaderSize + kClockSize + raw_len;
		*written = 0;
		*needed  = total;

		if (cap < total)
			return NES_ERR_BUFFER_TOO_SMALL;

		// 头先整体清零 (覆盖 core_version 的 NUL 填充与 sha1 的全 0 情形)
		std::memset(out, 0, kHeaderSize);
		std::memcpy(out, kMagic, kMagicLen);
		put_u32_le(out + 8, kVersion);
		std::memcpy(out + 12, "1.53.2", 7);   // 6 字符 + NUL, 余下保持 0
		std::memcpy(out + 28, sha1_hex, 41);  // 40 hex + NUL
		put_u64_le(out + 69, static_cast<uint64_t>(kClockSize + raw_len));
		uint64_t remainder_bits = 0;
		std::memcpy(&remainder_bits, &clock.remainder, sizeof(remainder_bits));
		put_u64_le(out + 81, remainder_bits);
		put_u32_le(out + 89, static_cast<uint32_t>(clock.mode));
		std::memcpy(out + kHeaderSize + kClockSize, raw, raw_len);
		put_u32_le(out + 77, crc32_bytes(out + kHeaderSize, kClockSize + raw_len));

		*written = total;
		return NES_OK;
	}

	int unwrap(const uint8_t* in, size_t size, const char* current_sha1_hex,
	           const uint8_t** payload, size_t* payload_len, bool* is_wrapped,
	           AudioClock* clock)
	{
		if (is_wrapped) *is_wrapped = false;
		if (payload)    *payload    = nullptr;
		if (payload_len) *payload_len = 0;
		if (clock) *clock = AudioClock{};

		if (!in || !current_sha1_hex || !payload || !payload_len || !is_wrapped || !clock)
			return NES_ERR_INVALID_PARAM;
		if (size == 0)
			return NES_ERR_INVALID_PARAM;

		// 装不下整个头: 若以魔数开头则是被截断的包装档 (损坏), 否则视为旧版裸 NST
		if (size < kHeaderSize)
		{
			if (size >= kMagicLen && std::memcmp(in, kMagic, kMagicLen) == 0)
				return NES_ERR_CORRUPT_FILE;
			return NES_OK; // 兼容路径: *is_wrapped = false
		}

		// 魔数不符 → 旧版裸 NST, 调用方按原样喂 Machine::LoadState
		if (std::memcmp(in, kMagic, kMagicLen) != 0)
			return NES_OK; // 兼容路径: *is_wrapped = false

		// Only explicitly known formats may reach the machine.
		const uint32_t version = get_u32_le(in + 8);
		if (version != 1 && version != kVersion)
			return NES_ERR_UNSUPPORTED_VER;

		// 载荷长度必须落在输入缓冲内, 否则声明长度造假 → 损坏
		const uint64_t len = get_u64_le(in + 69);
		const size_t metadata_size = version == 2 ? kClockSize : 0;
		if (len <= metadata_size || len != size - kHeaderSize)
			return NES_ERR_CORRUPT_FILE;
		const size_t plen = static_cast<size_t>(len);

		// CRC32 覆盖载荷: 篡改/截断在此拦截 (在读载荷前先校验长度, 无越界风险)
		const uint32_t stored = get_u32_le(in + 77);
		if (stored != crc32_bytes(in + kHeaderSize, plen))
			return NES_ERR_INVALID_CRC;

		// SHA1: 全 0 (无卡带时保存) 跳过; 否则必须与当前 ROM 一致
		if (!all_zero(in + 28, 40))
		{
			if (std::memcmp(in + 28, current_sha1_hex, 40) != 0)
				return NES_ERR_STATE_ROM_MISMATCH;
		}

		AudioClock decoded;
		if (version == 2)
		{
			const uint64_t bits = get_u64_le(in + 81);
			std::memcpy(&decoded.remainder, &bits, sizeof(bits));
			decoded.mode = static_cast<AudioClockMode>(get_u32_le(in + 89));
			if (!valid_clock(decoded))
				return NES_ERR_CORRUPT_FILE;
		}
		*clock       = decoded;
		*payload     = in + kHeaderSize + metadata_size;
		*payload_len = plen - metadata_size;
		*is_wrapped  = true;
		return NES_OK;
	}
}
