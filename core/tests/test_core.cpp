/*
 * core/tests/test_core.cpp — headless smoke test (Task 6)
 *
 * First runtime validation of the whole NestopiaUE core through the C ABI
 * (core/include/nes/nes.h). Loads a real NES ROM ("Thwaite", MIT homebrew),
 * runs 60 frames, and verifies video / audio / save-state behavior:
 *
 *   load ROM -> run 60 frames -> video non-blank -> audio non-silent
 *             -> save state -> mutate (30 frames) -> load state
 *             -> save again (byte-equality / NES_OK round-trip)
 *
 * Self-contained: stdio only, no external deps. Exit code 0 = PASS.
 */
#include "nes/nes.h"
#include "../src/nes_audio_clock.hpp"
#include "../src/nes_state.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

// Test-only allocator history: fill fresh storage before constructors run.
// Outside the scoped create calls below this behaves like ordinary new/delete.
namespace { thread_local int allocation_fill = -1; }
void* operator new(std::size_t size)
{
	void* memory = std::malloc(size ? size : 1);
	if (!memory) throw std::bad_alloc();
	if (allocation_fill >= 0) std::memset(memory, allocation_fill, size);
	return memory;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace
{
	int g_failures = 0;

	// check(): prints ok/FAIL and bumps the failure counter.
	void check(bool ok, const char* what)
	{
		std::printf("  [%s] %s\n", ok ? "ok  " : "FAIL", what);
		if (!ok)
			++g_failures;
	}

	bool read_file(const char* path, std::vector<uint8_t>& out)
	{
		FILE* f = std::fopen(path, "rb");
		if (!f)
			return false;
		std::fseek(f, 0, SEEK_END);
		const long len = std::ftell(f);
		std::fseek(f, 0, SEEK_SET);
		if (len <= 0)
		{
			std::fclose(f);
			return false;
		}
		out.resize(static_cast<size_t>(len));
		const size_t got = std::fread(out.data(), 1, out.size(), f);
		std::fclose(f);
		return got == out.size();
	}

	std::vector<uint8_t> save_bytes(nes_t* nes)
	{
		size_t written = 0, needed = 0;
		check(nes_save_state(nes, nullptr, 0, &written, &needed) == NES_ERR_BUFFER_TOO_SMALL,
		      "save size query succeeds");
		std::vector<uint8_t> bytes(needed);
		check(nes_save_state(nes, bytes.data(), bytes.size(), &written, &needed) == NES_OK,
		      "save bytes succeeds");
		bytes.resize(written);
		return bytes;
	}

	std::vector<int16_t> run_pcm(nes_t* nes, uint32_t count)
	{
		std::vector<int16_t> pcm(count * 2048);
		uint32_t frames = 0, samples = 0;
		check(nes_run_frames(nes, count, pcm.data(), static_cast<uint32_t>(pcm.size()),
		                     &frames, &samples) == NES_OK && frames == count,
		      "PCM replay runs requested frames");
		pcm.resize(samples);
		return pcm;
	}

	std::vector<uint8_t> canonical_bytes(nes_t* nes)
	{
		size_t written = 99, needed = 0;
		check(nes_copy_canonical_state(nes, nullptr, 0, &written, &needed) == NES_ERR_BUFFER_TOO_SMALL
		      && written == 0 && needed > 93, "canonical query reports exact required size");
		std::vector<uint8_t> bytes(needed);
		check(nes_copy_canonical_state(nes, bytes.data(), bytes.size(), &written, &needed) == NES_OK
		      && written == bytes.size() && needed == bytes.size(), "canonical exact-size copy succeeds");
		bytes.resize(written);
		return bytes;
	}

	void put_le(std::vector<uint8_t>& bytes, size_t offset, uint64_t value, size_t size)
	{
		for (size_t i = 0; i < size; ++i)
			bytes[offset + i] = static_cast<uint8_t>(value >> (i * 8));
	}

	uint32_t get_le32(const std::vector<uint8_t>& bytes, size_t offset)
	{
		uint32_t value = 0;
		for (size_t i = 0; i < 4; ++i) value |= uint32_t(bytes[offset + i]) << (i * 8);
		return value;
	}

	size_t find_chunk(const std::vector<uint8_t>& bytes, size_t begin, size_t end,
	                  const char* id)
	{
		for (size_t offset = begin; offset + 8 <= end;)
		{
			const size_t length = get_le32(bytes, offset + 4);
			if (length > end - offset - 8) break;
			if (std::memcmp(bytes.data() + offset, id, 4) == 0) return offset;
			offset += 8 + length;
		}
		return bytes.size();
	}

	// Turn current raw NST into the historical format by removing the optional
	// queue chunk. Merely stripping the new wrapper would not exercise old NST.
	void remove_audio_buffer_chunk(std::vector<uint8_t>& raw)
	{
		const size_t apu = find_chunk(raw, 8, raw.size(), "APU\0");
		check(apu < raw.size(), "locate APU legacy fixture chunk");
		if (apu == raw.size()) return;
		const size_t buffer = find_chunk(raw, apu + 8, apu + 8 + get_le32(raw, apu + 4), "BFR\0");
		if (buffer == raw.size()) return; // already a historical NST
		const size_t removed = 8 + get_le32(raw, buffer + 4);
		put_le(raw, apu + 4, get_le32(raw, apu + 4) - removed, 4);
		put_le(raw, 4, get_le32(raw, 4) - removed, 4);
		raw.erase(raw.begin() + buffer, raw.begin() + buffer + removed);
	}

	void refresh_state_crc(std::vector<uint8_t>& bytes)
	{
		put_le(bytes, 69, bytes.size() - 81, 8);
		put_le(bytes, 77, flynes_state::crc32_bytes(bytes.data() + 81, bytes.size() - 81), 4);
	}

	bool same_pcm(const std::vector<int16_t>& actual, const std::vector<int16_t>& expected)
	{
		if (actual == expected) return true;
		std::printf("  PCM mismatch: actual=%zu expected=%zu samples\n", actual.size(), expected.size());
		for (size_t i = 0; i < actual.size() && i < expected.size(); ++i)
		{
			if (actual[i] != expected[i])
			{
				std::printf("  first PCM difference at %zu: actual=%d expected=%d\n",
				            i, actual[i], expected[i]);
				break;
			}
		}
		return false;
	}

	std::vector<uint8_t> frame_bytes(nes_t* nes)
	{
		const nes_video_frame* frame = nes_get_video_frame(nes);
		const auto* pixels = static_cast<const uint8_t*>(frame->pixels);
		return {pixels, pixels + static_cast<size_t>(frame->pitch) * frame->height};
	}

	void test_canonical_allocation_history(const std::vector<uint8_t>& rom)
	{
		auto create_with_history = [](int fill) {
			allocation_fill = fill;
			nes_t* instance = nes_create(nullptr);
			allocation_fill = -1;
			return instance;
		};
		// Exercise a retired loaded instance as well as deterministic fresh
		// storage patterns; allocator reuse alone is not a reliable regression.
		nes_t* retired = create_with_history(0);
		check(retired && nes_load_rom(retired, rom.data(), rom.size(), nullptr) >= 0,
		      "load retired allocation-history core");
		nes_destroy(retired);
		nes_t* clean = create_with_history(0);
		nes_t* reused = create_with_history(1);
		check(clean && reused, "create cores with different allocation histories");
		if (!clean || !reused) { nes_destroy(clean); nes_destroy(reused); return; }
		check(nes_load_rom(clean, rom.data(), rom.size(), nullptr) >= 0 &&
		      nes_load_rom(reused, rom.data(), rom.size(), nullptr) >= 0,
		      "load allocation-history comparison cores");
		for (unsigned frame = 0; frame < 12; ++frame)
		{
			for (unsigned port = 0; port < 4; ++port)
			{
				nes_set_input(clean, port, 1u << port);
				nes_set_input(reused, port, 1u << port);
			}
			const auto expected_pcm = run_pcm(clean, 1);
			const auto actual_pcm = run_pcm(reused, 1);
			check(actual_pcm == expected_pcm, "allocation history preserves frame PCM");
			check(frame_bytes(clean) == frame_bytes(reused), "allocation history preserves video");
			check(canonical_bytes(clean) == canonical_bytes(reused),
			      "allocation history preserves canonical core state");
		}
		nes_destroy(clean);
		nes_destroy(reused);
	}

	void test_canonical_state(const std::vector<uint8_t>& rom)
	{
		nes_t* nes = nes_create(nullptr);
		nes_t* peer = nes_create(nullptr);
		check(nes && peer, "create canonical comparison cores");
		if (!nes || !peer) { nes_destroy(nes); nes_destroy(peer); return; }
		size_t written = 99, needed = 99;
		uint8_t byte = 0xA5;
		check(nes_copy_canonical_state(nullptr, nullptr, 0, &written, &needed) == NES_ERR_INVALID_PARAM,
		      "canonical rejects null handle");
		check(nes_copy_canonical_state(nes, nullptr, 1, &written, &needed) == NES_ERR_INVALID_PARAM,
		      "canonical rejects null nonempty buffer");
		check(nes_copy_canonical_state(nes, &byte, 1, nullptr, &needed) == NES_ERR_INVALID_PARAM
		      && nes_copy_canonical_state(nes, &byte, 1, &written, nullptr) == NES_ERR_INVALID_PARAM,
		      "canonical requires both output sizes");
		check(nes_copy_canonical_state(nes, nullptr, 0, &written, &needed) == NES_ERR_NOT_READY
		      && written == 0 && needed == 0, "canonical unloaded core reports not ready and zero sizes");

		struct Reentry { nes_t* nes; unsigned calls = 0; bool rejected = true; } reentry{nes};
		auto log = [](void* userdata, const char*, uint32_t) {
			auto& probe = *static_cast<Reentry*>(userdata);
			size_t written = 99, needed = 99;
			++probe.calls;
			probe.rejected &= nes_copy_canonical_state(probe.nes, nullptr, 0, &written, &needed)
			    == NES_ERR_REENTRANT && written == 99 && needed == 99;
		};
		nes_set_log_callback(nes, log, &reentry);
		check(nes_load_rom(nes, rom.data(), rom.size(), nullptr) >= 0, "load canonical core");
		nes_set_log_callback(nes, nullptr, nullptr);
		check(reentry.calls > 0 && reentry.rejected, "canonical export rejects callback reentry without touching outputs");
		check(nes_load_rom(peer, rom.data(), rom.size(), nullptr) >= 0, "load untouched canonical control core");
		run_pcm(nes, 61);
		run_pcm(peer, 61);
		const auto compressed = save_bytes(nes);
		const auto canonical = canonical_bytes(nes);
		check(canonical == canonical_bytes(nes), "repeated canonical exports are byte-identical");
		check(save_bytes(nes) == compressed, "canonical export preserves ordinary compressed save bytes");
		check(canonical.size() > 93 && canonical[8] == 2, "canonical state retains wrapper version 2");
		check(canonical.size() > compressed.size(), "canonical state bypasses zlib compression");
		if (canonical.size() > 93 && compressed.size() > 93)
		{
			const auto ram_chunk = [](const std::vector<uint8_t>& state) {
				const size_t cpu = find_chunk(state, 101, state.size(), "CPU\0");
				return cpu == state.size() ? cpu
				    : find_chunk(state, cpu + 8, cpu + 8 + get_le32(state, cpu + 4), "RAM\0");
			};
			const size_t ram = ram_chunk(canonical), compressed_ram = ram_chunk(compressed);
			check(ram < canonical.size() && get_le32(canonical, ram + 4) == 2049
			      && canonical[ram + 8] == 0, "canonical CPU RAM uses raw NST encoding");
			check(compressed_ram < compressed.size() && compressed[compressed_ram + 8] == 1,
			      "ordinary CPU RAM still uses zlib NST encoding");
			check(std::memcmp(canonical.data() + 28, compressed.data() + 28, 41) == 0
			      && std::memcmp(canonical.data() + 81, compressed.data() + 81, 12) == 0,
			      "canonical state preserves ROM identity and fractional audio clock");
		}
		std::vector<uint8_t> short_buffer(canonical.size() - 1, 0xA5);
		const auto untouched = short_buffer;
		written = needed = 99;
		check(nes_copy_canonical_state(nes, short_buffer.data(), short_buffer.size(), &written, &needed)
		      == NES_ERR_BUFFER_TOO_SMALL && written == 0 && needed == canonical.size()
		      && short_buffer == untouched, "canonical short buffer reports size without partial writes");
		const auto expected_pcm = run_pcm(peer, 1);
		const auto expected_frame = frame_bytes(peer);
		check(same_pcm(run_pcm(nes, 1), expected_pcm), "canonical export leaves next-frame PCM unchanged");
		check(frame_bytes(nes) == expected_frame, "canonical export leaves next-frame video unchanged");
		check(nes_load_state(nes, canonical.data(), canonical.size()) == NES_OK,
		      "canonical state loads through the ordinary load API");
		check(canonical_bytes(nes) == canonical, "canonical load restores complete serialized state");
		check(same_pcm(run_pcm(nes, 1), expected_pcm), "canonical load restores exact PCM and sample cadence");
		check(frame_bytes(nes) == expected_frame, "canonical load restores video pixels");
		if (compressed.size() > 93)
		{
			auto zero = compressed;
			put_le(zero, 81, 0, 8);
			refresh_state_crc(zero);
			check(nes_load_state(nes, zero.data(), zero.size()) == NES_OK, "load positive-zero clock");
			const auto positive = canonical_bytes(nes);
			put_le(zero, 81, UINT64_C(0x8000000000000000), 8);
			refresh_state_crc(zero);
			check(nes_load_state(nes, zero.data(), zero.size()) == NES_OK, "load negative-zero clock");
			check(canonical_bytes(nes) == positive, "canonical clock normalizes equivalent signed zeros");
			check(save_bytes(nes) == zero, "ordinary save preserves negative-zero clock encoding");
		}
		nes_destroy(peer);
		nes_destroy(nes);
	}

	const char* err_str(int rc)
	{
		switch (rc)
		{
		case NES_OK:                      return "NES_OK";
		case NES_WARN_NOP:                return "NES_WARN_NOP";
		case NES_WARN_BAD_DUMP:           return "NES_WARN_BAD_DUMP";
		case NES_WARN_BAD_PROM:           return "NES_WARN_BAD_PROM";
		case NES_WARN_BAD_CROM:           return "NES_WARN_BAD_CROM";
		case NES_WARN_BAD_FILE_HEADER:    return "NES_WARN_BAD_FILE_HEADER";
		case NES_WARN_SAVEDATA_LOST:      return "NES_WARN_SAVEDATA_LOST";
		case NES_WARN_DATA_REPLACED:      return "NES_WARN_DATA_REPLACED";
		case NES_ERR_GENERIC:             return "NES_ERR_GENERIC";
		case NES_ERR_OUT_OF_MEMORY:       return "NES_ERR_OUT_OF_MEMORY";
		case NES_ERR_NOT_READY:           return "NES_ERR_NOT_READY";
		case NES_ERR_INVALID_PARAM:       return "NES_ERR_INVALID_PARAM";
		case NES_ERR_INVALID_FILE:        return "NES_ERR_INVALID_FILE";
		case NES_ERR_CORRUPT_FILE:        return "NES_ERR_CORRUPT_FILE";
		case NES_ERR_INVALID_CRC:         return "NES_ERR_INVALID_CRC";
		case NES_ERR_UNSUPPORTED:         return "NES_ERR_UNSUPPORTED";
		case NES_ERR_UNSUPPORTED_VER:     return "NES_ERR_UNSUPPORTED_VER";
		case NES_ERR_UNSUPPORTED_VSSYSTEM:return "NES_ERR_UNSUPPORTED_VSSYSTEM";
		case NES_ERR_UNSUPPORTED_MAPPER:  return "NES_ERR_UNSUPPORTED_MAPPER";
		case NES_ERR_MISSING_BIOS:        return "NES_ERR_MISSING_BIOS";
		case NES_ERR_WRONG_MODE:          return "NES_ERR_WRONG_MODE";
		case NES_ERR_BUFFER_TOO_SMALL:    return "NES_ERR_BUFFER_TOO_SMALL";
		case NES_ERR_NOT_IMPLEMENTED:     return "NES_ERR_NOT_IMPLEMENTED";
		case NES_ERR_REENTRANT:           return "NES_ERR_REENTRANT";
		case NES_ERR_STATE_ROM_MISMATCH:  return "NES_ERR_STATE_ROM_MISMATCH";
		default:                          return "unknown";
		}
	}
} // namespace

int main(int argc, char** argv)
{
	// Audio sample cadence must preserve the fractional samples that do not fit
	// in a single emulated frame. The accumulated error stays below one sample.
	{
		double remainder = 0.0;
		uint64_t total = 0;
		constexpr uint32_t frame_count = 60u * 30u * 60u;
		for (uint32_t i = 0; i < frame_count; ++i)
			total += nes_samples_for_next_frame(48000, 60.0988, remainder);
		const double expected = static_cast<double>(frame_count) * 48000.0 / 60.0988;
		const double error = total > expected ? total - expected : expected - total;
		check(error < 1.0, "30-minute audio cadence stays within one sample");
	}

	const char* rom_path = (argc > 1) ? argv[1] : "content/assets/roms/thwaite.nes";
	const char* db_path  = (argc > 2) ? argv[2] : "core/tests/fixtures/NstDatabase.xml";
	std::printf("=== FlyNES headless smoke test (Task 6) ===\n");
	std::printf("ROM path: %s\n", rom_path);
	std::printf("DB  path: %s\n", db_path);
	std::printf("core version: %s, api version: %u.%u.%u\n",
	            nes_core_version(),
	            (nes_api_version() >> 16) & 0xFFu,
	            (nes_api_version() >> 8) & 0xFFu,
	            nes_api_version() & 0xFFu);

	// ---- 1. create (default config) -------------------------------------
	nes_t* nes = nes_create(NULL);
	check(nes != NULL, "nes_create(NULL) returns non-NULL handle");
	if (!nes)
	{
		std::printf("FAIL: cannot continue without a core handle\n");
		return 1;
	}

	// ---- 2. load database (NstDatabase.xml, before ROM) -----------------
	// The bundled DB refines ROM profiles; it must load (and reload) cleanly.
	std::vector<uint8_t> db;
	check(read_file(db_path, db), "read NstDatabase.xml fixture");
	if (db.empty())
	{
		std::printf("FAIL: cannot continue without the database fixture\n");
		nes_destroy(nes);
		return 1;
	}
	const int db_rc1 = nes_load_database(nes, db.data(), db.size());
	check(db_rc1 >= 0, "nes_load_database returns NES_OK or positive warning");
	std::printf("  database: rc=%d (%s), %zu bytes\n", db_rc1, err_str(db_rc1), db.size());
	// Idempotence: reloading the same DB must not fail (Load resets state).
	const int db_rc2 = nes_load_database(nes, db.data(), db.size());
	check(db_rc2 >= 0, "nes_load_database is idempotent (2nd load >= 0)");
	std::printf("  database: 2nd load rc=%d (%s)\n", db_rc2, err_str(db_rc2));

	// ---- 3. read ROM + validate + load ----------------------------------
	std::vector<uint8_t> rom;
	check(read_file(rom_path, rom), "read ROM file");
	if (rom.size() < 16)
	{
		std::printf("FAIL: ROM too small to even hold an iNES header\n");
		nes_destroy(nes);
		return 1;
	}

	const bool magic_ok = rom[0] == 'N' && rom[1] == 'E' && rom[2] == 'S' && rom[3] == 0x1A;
	check(magic_ok, "ROM starts with NES\\x1A magic");
	const bool size_ok = rom.size() >= 16 * 1024 && rom.size() <= 512 * 1024;
	check(size_ok, "ROM size within 16KB..512KB");
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "ROM size = %zu bytes", rom.size());
		check(true, buf);
	}
	if (!magic_ok || !size_ok)
	{
		std::printf("FAIL: not a valid iNES ROM fixture\n");
		nes_destroy(nes);
		return 1;
	}

	nes_rom_info info;
	std::memset(&info, 0, sizeof(info));
	const int load_rc = nes_load_rom(nes, rom.data(), rom.size(), &info);
	check(load_rc >= 0, "nes_load_rom returns NES_OK or positive warning");
	std::printf("  rom_info: rc=%d (%s)\n", load_rc, err_str(load_rc));
	std::printf("    title    = \"%s\"\n", info.title);
	std::printf("    publisher= \"%s\"\n", info.publisher);
	std::printf("    mapper   = %u, submapper = %u\n", info.mapper, info.submapper);
	std::printf("    prg      = %u bytes, chr = %u bytes\n", info.prg_size, info.chr_size);
	std::printf("    wram     = %u bytes, vram = %u bytes\n", info.wram_size, info.vram_size);
	std::printf("    region   = \"%s\" (NTSC=%u), system=%u\n", info.region, info.region_ntsc, info.system);
	std::printf("    sha1     = %s\n", info.sha1);
	std::printf("    crc32    = %s\n", info.crc32);
	if (load_rc < 0)
	{
		std::printf("FAIL: cannot continue without a loaded ROM\n");
		nes_destroy(nes);
		return 1;
	}

	// ---- 4. audio format -------------------------------------------------
	const int af_rc = nes_set_audio_format(nes, 48000, 0);
	check(af_rc == NES_OK, "nes_set_audio_format(48000, mono) returns NES_OK");

	// ---- 5. run 60 frames ------------------------------------------------
	const uint32_t kFrames     = 60;
	const uint32_t kAudioCap   = kFrames * 1024; // ~1.25s @48kHz mono, plenty
	std::vector<int16_t> audio(kAudioCap);
	uint32_t frames_run = 0, samples_written = 0;
	const int run_rc = nes_run_frames(nes, kFrames, audio.data(), kAudioCap,
	                                  &frames_run, &samples_written);
	check(run_rc == NES_OK, "nes_run_frames(60) returns NES_OK");
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "frames_run == 60 (got %u)", frames_run);
		check(frames_run == kFrames, buf);
	}
	{
		char buf[128];
		std::snprintf(buf, sizeof(buf), "samples_written > 0 (got %u)", samples_written);
		check(samples_written > 0, buf);
	}

	// ---- 6. video check --------------------------------------------------
	const nes_video_frame* vf = nes_get_video_frame(nes);
	bool vf_ok = false;
	{
		char buf[160];
		if (vf)
			std::snprintf(buf, sizeof(buf), "video frame %ux%u pitch=%d fmt=%d pixels=%p",
			              vf->width, vf->height, vf->pitch, (int)vf->format, vf->pixels);
		else
			std::snprintf(buf, sizeof(buf), "video frame is NULL");
		check(vf != NULL, buf);
	}
	if (vf)
	{
		check(vf->width == 256 && vf->height == 240, "video frame is 256x240");
		check(vf->pixels != NULL, "video frame has a pixel buffer");

		// RGB565: black == 0x0000. Count any non-black pixel.
		const uint16_t* px = static_cast<const uint16_t*>(vf->pixels);
		const size_t row_bytes = static_cast<size_t>(vf->pitch);
		const size_t px_per_row = row_bytes / sizeof(uint16_t);
		uint64_t non_black = 0;
		for (uint32_t y = 0; y < vf->height; ++y)
		{
			const uint16_t* row = px + static_cast<size_t>(y) * px_per_row;
			for (uint32_t x = 0; x < vf->width; ++x)
			{
				if (row[x] != 0)
					++non_black;
			}
		}
		{
			char buf[160];
			std::snprintf(buf, sizeof(buf), "non-black pixels found (got %llu / %u)", 
			              static_cast<unsigned long long>(non_black),
			              vf->width * vf->height);
			check(non_black > 0, buf);
		}
		vf_ok = true;
	}

	// ---- 6a. race-free published frame snapshot -------------------------
	{
		nes_video_snapshot before{};
		before.struct_size = sizeof(before);
		std::vector<uint8_t> pixels(256u * 240u * 2u);
		int rc = nes_copy_video_frame(nes, pixels.data(), pixels.size(), &before);
		check(rc == NES_OK, "copy the current published video frame");
		check(before.sequence > 0, "published frame has a positive sequence");
		check(before.width == 256 && before.height == 240,
		      "published frame describes the native viewport");
		check(before.bytes_written == pixels.size(),
		      "published frame reports the copied byte count");

		uint32_t fr = 0, sw = 0;
		rc = nes_run_frames(nes, 1, audio.data(), kAudioCap, &fr, &sw);
		check(rc == NES_OK && fr == 1, "run exactly one frame before the next snapshot");

		nes_video_snapshot after{};
		after.struct_size = sizeof(after);
		rc = nes_copy_video_frame(nes, pixels.data(), pixels.size(), &after);
		check(rc == NES_OK, "copy the next published video frame");
		check(after.sequence == before.sequence + 1,
		      "published sequence advances exactly once per emulated frame");
		check(after.source_region == NES_REGION_NTSC || after.source_region == NES_REGION_PAL,
		      "v2 snapshot reports an explicit source region");
		check(after.native_monotonic_ns > 0,
		      "v2 snapshot reports a native monotonic copy timestamp");

		std::fill(pixels.begin(), pixels.end(), 0xA5);
		nes_video_snapshot unchanged{};
		unchanged.struct_size = sizeof(unchanged);
		unchanged.sequence = 0x1122334455667788ULL;
		rc = nes_copy_video_frame_if_new(nes, after.sequence, pixels.data(), pixels.size(),
		                                 &unchanged);
		check(rc == NES_WARN_NO_VIDEO_CHANGE,
		      "copy-if-new distinguishes an unchanged published sequence");
		check(unchanged.sequence == 0x1122334455667788ULL,
		      "no-change leaves caller sequence metadata untouched");
		check(pixels.front() == 0xA5 && pixels.back() == 0xA5,
		      "no-change leaves destination bytes untouched");

		nes_video_snapshot legacy{};
		legacy.struct_size = NES_VIDEO_SNAPSHOT_V1_SIZE;
		legacy.source_region = NES_REGION_DENDY;
		legacy.native_monotonic_ns = 99;
		rc = nes_copy_video_frame(nes, pixels.data(), pixels.size(), &legacy);
		check(rc == NES_OK && legacy.sequence == after.sequence,
		      "v1-size snapshot remains ABI compatible");
		check(legacy.source_region == NES_REGION_DENDY && legacy.native_monotonic_ns == 99,
		      "v1-size caller tail is never overwritten");

		nes_video_snapshot undersized{};
		undersized.struct_size = NES_VIDEO_SNAPSHOT_V1_SIZE - 1;
		undersized.sequence = 77;
		rc = nes_copy_video_frame(nes, pixels.data(), pixels.size(), &undersized);
		check(rc == NES_ERR_INVALID_PARAM && undersized.sequence == 77,
		      "undersized snapshot is rejected without tail writes");
	}

	// ---- 6aa. input is timestamped only at the core's pad-read boundary --
	{
		nes_input_sample before{};
		before.struct_size = sizeof(before);
		check(nes_get_last_input_sample(nes, &before) == NES_OK,
		      "read initial sampled input");
		nes_set_input(nes, 0, NES_BTN_A);
		nes_input_sample before_run{};
		before_run.struct_size = sizeof(before_run);
		check(nes_get_last_input_sample(nes, &before_run) == NES_OK
		      && before_run.generation == before.generation,
		      "setter does not pretend the core sampled input");
		uint32_t fr = 0, sw = 0;
		check(nes_run_frames(nes, 1, audio.data(), kAudioCap, &fr, &sw) == NES_OK && fr == 1,
		      "run one frame to cross the pad-read boundary");
		nes_input_sample sampled{};
		sampled.struct_size = sizeof(sampled);
		check(nes_get_last_input_sample(nes, &sampled) == NES_OK
		      && sampled.generation > before.generation
		      && sampled.pad_bits[0] == NES_BTN_A
		      && sampled.native_monotonic_ns > 0,
		      "sampled input records generation, pad bits and native time");
		nes_set_input(nes, 0, NES_BTN_A);
		check(nes_run_frames(nes, 1, audio.data(), kAudioCap, &fr, &sw) == NES_OK,
		      "run with unchanged pad state");
		nes_input_sample same{};
		same.struct_size = sizeof(same);
		nes_get_last_input_sample(nes, &same);
		check(same.generation == sampled.generation,
		      "writing identical pad bits does not create a new generation");
		nes_clear_input(nes);
	}

	// ---- 6b. video filter scaling ---------------------------------------
	{
		char buf[160];
		// hq4x: framebuffer scales to 1024x960, frames still run.
		int rc = nes_set_video_format(nes, NES_PIXFMT_RGB565, NES_FILTER_HQ4X);
		std::snprintf(buf, sizeof(buf), "set_video_format(HQ4X) rc=%d", rc);
		check(rc >= 0, buf);
		const nes_video_frame* f4 = nes_get_video_frame(nes);
		check(f4 != NULL && f4->width == 1024 && f4->height == 960,
		      "HQ4X frame is 1024x960");
		check(f4 != NULL && f4->pixels != NULL && f4->pitch == 1024 * 2,
		      "HQ4X frame has a 1024-px pitch buffer");
		uint32_t fr4 = 0, sw4 = 0;
		const int rc4 = nes_run_frames(nes, 2, audio.data(), kAudioCap, &fr4, &sw4);
		std::snprintf(buf, sizeof(buf), "HQ4X: 2 frames rc=%d fr=%u", rc4, fr4);
		check(rc4 >= 0 && fr4 == 2, buf);

		// HQ4X frame must contain non-black content (not a black screen).
		uint64_t nb4 = 0;
		uint64_t nb4_orig_zone = 0; // first 256x240 (would-be leftover zone)
		{
			const uint16_t* px = static_cast<const uint16_t*>(f4->pixels);
			const size_t row_bytes = static_cast<size_t>(f4->pitch);
			const size_t px_per_row = row_bytes / sizeof(uint16_t);
			for (uint32_t y = 0; y < f4->height; ++y)
			{
				const uint16_t* row = px + static_cast<size_t>(y) * px_per_row;
				for (uint32_t x = 0; x < f4->width; ++x)
				{
					if (row[x] != 0)
					{
						++nb4;
						if (y < 240 && x < 256) ++nb4_orig_zone;
					}
				}
			}
		}
		std::snprintf(buf, sizeof(buf),
		              "HQ4X frame non-black pixels (got %llu/%u; 256x240 zone %llu)",
		              static_cast<unsigned long long>(nb4), f4->width * f4->height,
		              static_cast<unsigned long long>(nb4_orig_zone));
		check(nb4 > 0, buf);

		// hq2x: 512x480.
		rc = nes_set_video_format(nes, NES_PIXFMT_RGB565, NES_FILTER_HQ2X);
		std::snprintf(buf, sizeof(buf), "set_video_format(HQ2X) rc=%d", rc);
		check(rc >= 0, buf);
		const nes_video_frame* f2 = nes_get_video_frame(nes);
		check(f2 != NULL && f2->width == 512 && f2->height == 480,
		      "HQ2X frame is 512x480");

		// Back to NONE: 256x240 restored.
		rc = nes_set_video_format(nes, NES_PIXFMT_RGB565, NES_FILTER_NONE);
		std::snprintf(buf, sizeof(buf), "set_video_format(NONE) rc=%d", rc);
		check(rc >= 0, buf);
		const nes_video_frame* f0 = nes_get_video_frame(nes);
		check(f0 != NULL && f0->width == 256 && f0->height == 240,
		      "NONE frame is 256x240");

		// NTSC is not implemented yet -> NOT_IMPLEMENTED.
		rc = nes_set_video_format(nes, NES_PIXFMT_RGB565, NES_FILTER_NTSC);
		std::snprintf(buf, sizeof(buf), "set_video_format(NTSC) rc=%d", rc);
		check(rc == NES_ERR_NOT_IMPLEMENTED, buf);
	}

	// ---- 7. audio check --------------------------------------------------
	// the fixture ROM's intro is silent for several seconds (verified empirically:
	// sound first appears ~frame 440 without input, ~frame 130 with START).
	// So the non-silence assertion is checked over a tolerance window of up to
	// 600 frames (10s @60fps) while the 60-frame run contract above stays exact.
	{
		uint64_t non_zero = 0;
		for (uint32_t i = 0; i < samples_written; ++i)
		{
			if (audio[i] != 0)
				++non_zero;
		}
		{
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "audio: first 60 frames -> %u samples, %llu non-zero",
			              samples_written, static_cast<unsigned long long>(non_zero));
			check(samples_written > 1000, buf);
		}

		// Scan forward in 60-frame chunks until non-zero audio appears.
		uint32_t audio_total_frames = kFrames;
		uint64_t audio_nonzero_total = non_zero;
		std::vector<int16_t> chunk(kFrames * 1024);
		while (audio_nonzero_total == 0 && audio_total_frames < 600)
		{
			uint32_t fr = 0, sw = 0;
			const int rc = nes_run_frames(nes, kFrames, chunk.data(),
			                              static_cast<uint32_t>(chunk.size()), &fr, &sw);
			if (rc != NES_OK || fr == 0)
				break;
			audio_total_frames += fr;
			for (uint32_t i = 0; i < sw; ++i)
			{
				if (chunk[i] != 0)
					++audio_nonzero_total;
			}
		}
		char buf[224];
		std::snprintf(buf, sizeof(buf),
		              "audio non-silence within 600-frame window: %llu non-zero samples "
		              "across %u frames (tolerance for silent intro)",
		              static_cast<unsigned long long>(audio_nonzero_total), audio_total_frames);
		check(audio_nonzero_total > 0, buf);
	}

	// ---- 8. save-state round-trip ----------------------------------------
	// 8a. save state #1 (validates MemOStream seek back-patching; a broken
	//     seek fails the first SaveState with CORRUPT_FILE).
	const size_t kStateCap = 4u * 1024u * 1024u;
	std::vector<uint8_t> buf1(kStateCap);
	size_t written1 = 0, needed1 = 0;
	const int save1_rc = nes_save_state(nes, buf1.data(), kStateCap, &written1, &needed1);
	check(save1_rc == NES_OK, "nes_save_state #1 returns NES_OK");
	{
		char buf[160];
		std::snprintf(buf, sizeof(buf), "save #1: written=%zu needed=%zu", written1, needed1);
		check(written1 > 0, buf);
	}

	// 8b. mutate the game state (30 more frames).
	{
		std::vector<int16_t> audio2(30 * 1024);
		uint32_t fr2 = 0, sw2 = 0;
		const int rc = nes_run_frames(nes, 30, audio2.data(),
		                              static_cast<uint32_t>(audio2.size()), &fr2, &sw2);
		char buf[128];
		std::snprintf(buf, sizeof(buf), "mutate: 30 frames ran (rc=%d frames=%u)", rc, fr2);
		check(rc == NES_OK && fr2 == 30, buf);
	}

	// 8c. reload the saved state; must be accepted (CRC validates the blob,
	//     so a NES_OK here also proves the back-patched chunk lengths were
	//     written correctly).
	const int load_rc2 = nes_load_state(nes, buf1.data(), written1);
	check(load_rc2 == NES_OK, "nes_load_state #1 returns NES_OK");

	// 8d. determinism: save again right after reload without running frames —
	//     identical machine state must serialize to identical bytes. This is
	//     the strongest proof that load_state restores exactly.
	{
		std::vector<uint8_t> bufA(kStateCap);
		size_t wA = 0, nA = 0;
		const int rc = nes_save_state(nes, bufA.data(), kStateCap, &wA, &nA);
		const bool same = (rc == NES_OK) && (wA == written1) &&
		                  std::memcmp(bufA.data(), buf1.data(), wA) == 0;
		char buf[192];
		std::snprintf(buf, sizeof(buf),
		              "save-after-reload == save#1 (rc=%d, %zu vs %zu bytes, %s)",
		              rc, wA, written1, same ? "identical" : "DIFFER");
		check(same, buf);
	}

	// 8e. as specified: run 1 frame after reload, save to buf2, compare with
	//     buf1. Frame advancement mutates state, so exact equality is not
	//     guaranteed (internal counters); both saves must still succeed.
	{
		std::vector<int16_t> audio3(1024);
		uint32_t fr3 = 0, sw3 = 0;
		nes_run_frames(nes, 1, audio3.data(), 1024, &fr3, &sw3);

		std::vector<uint8_t> buf2(kStateCap);
		size_t written2 = 0, needed2 = 0;
		const int save2_rc = nes_save_state(nes, buf2.data(), kStateCap, &written2, &needed2);
		check(save2_rc == NES_OK, "nes_save_state #2 (after reload + 1 frame) returns NES_OK");

		const bool same = (save2_rc == NES_OK) && (written2 == written1) &&
		                  std::memcmp(buf2.data(), buf1.data(), written1) == 0;
		char buf[192];
		std::snprintf(buf, sizeof(buf),
		              "buf2 == buf1 byte-for-byte (save#2 %zu bytes vs save#1 %zu, %s)",
		              written2, written1, same ? "identical" : "differ (expected: counters)");
		if (same)
			check(true, buf);
		else
		{
			// Allowed fallback: both saves succeeded and load_state was NES_OK.
			check(save2_rc == NES_OK && written2 > 0, buf);
		}
	}

	// 8f. FLYNST1 wrapper: tamper / version / sha1 / legacy-compat.
	// All wrapper tests below slice buf1[0..written1) and poke individual
	// bytes; they need the payload (past the 81-byte header) to exist.
	if (written1 > 81)
	{
		// f1. tamper: flipping one payload byte must be caught by the
		//     wrapper's CRC32 (payload covers offset 81..).
		{
			std::vector<uint8_t> tampered(buf1.begin(), buf1.begin() + written1);
			tampered[81] ^= 0xFF; // first payload byte
			const int rc = nes_load_state(nes, tampered.data(), tampered.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "tampered payload -> load fails (rc=%d %s, expected NES_ERR_INVALID_CRC)",
			              rc, err_str(rc));
			check(rc == NES_ERR_INVALID_CRC, buf);
		}

		// f2. version: setting version (u32 LE at offset 8) to 3 must be
		//     rejected as an unknown wrapper format.
		{
			std::vector<uint8_t> v2(buf1.begin(), buf1.begin() + written1);
			v2[8] = 3; v2[9] = 0; v2[10] = 0; v2[11] = 0;
			const int rc = nes_load_state(nes, v2.data(), v2.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "version=3 -> load fails (rc=%d %s, expected NES_ERR_UNSUPPORTED_VER)",
			              rc, err_str(rc));
			check(rc == NES_ERR_UNSUPPORTED_VER, buf);
		}

		// f3. sha1: altering one hex char of rom_sha1 (offset 28) must be
		//     rejected even though the CRC32 is still valid (it covers only
		//     the payload, not the header).
		{
			std::vector<uint8_t> other_rom(buf1.begin(), buf1.begin() + written1);
			other_rom[28] = (other_rom[28] == 'F') ? '0' : 'F'; // guarantee a change
			const int rc = nes_load_state(nes, other_rom.data(), other_rom.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "rom_sha1 altered -> load fails (rc=%d %s, expected NES_ERR_STATE_ROM_MISMATCH)",
			              rc, err_str(rc));
			check(rc == NES_ERR_STATE_ROM_MISMATCH, buf);
		}

		// f4. legacy compat: the bytes past the header are the raw NST
		//     stream; feeding them directly (as old saves were stored) must
		//     still load. Runs after the ROM is loaded, so the payload is
		//     meaningful for the current machine.
		{
			const size_t raw_offset = buf1[8] == 2 ? 93 : 81;
			std::vector<uint8_t> legacy(buf1.begin() + raw_offset, buf1.begin() + written1);
			remove_audio_buffer_chunk(legacy);
			const int rc = nes_load_state(nes, legacy.data(), legacy.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "legacy raw NST (header stripped) loads (rc=%d %s, expected >= 0)",
			              rc, err_str(rc));
			check(rc >= 0, buf);
			std::vector<uint8_t> v1(buf1.begin(), buf1.begin() + 81);
			v1[8] = 1;
			v1.insert(v1.end(), legacy.begin(), legacy.end());
			refresh_state_crc(v1);
			for (const auto& old : {legacy, v1})
			{
				std::vector<int16_t> expected;
				for (int replay = 0; replay < 4; ++replay)
				{
					run_pcm(nes, replay + 1);
					check(nes_load_state(nes, old.data(), old.size()) >= 0,
					      "legacy raw/v1 checkpoint remains loadable");
					const auto pcm = run_pcm(nes, 3);
					if (replay == 0) expected = pcm;
					check(same_pcm(pcm, expected), "legacy load resets cadence independently of previous history");
				}
			}
		}
		if (buf1[8] == 2)
		{
			const std::vector<uint8_t> valid(buf1.begin(), buf1.begin() + written1);
			const auto rejects_unchanged = [&](const std::vector<uint8_t>& bad, int error) {
				const auto before = save_bytes(nes);
				check(nes_load_state(nes, bad.data(), bad.size()) == error,
				      "malformed clock wrapper returns expected error");
				check(save_bytes(nes) == before, "invalid wrapper leaves machine and clock untouched");
			};
			for (const size_t offset : {size_t(81), size_t(89), size_t(93)})
			{
				auto bad = valid;
				bad[offset] ^= 1;
				rejects_unchanged(bad, NES_ERR_INVALID_CRC);
			}
			for (const uint64_t bits : {UINT64_C(0x7ff8000000000000), // NaN
			                            UINT64_C(0x7ff0000000000000), // infinity
			                            UINT64_C(0xbfe0000000000000), // -0.5
			                            UINT64_C(0x3ff0000000000000)}) // 1.0
			{
				auto bad = valid;
				put_le(bad, 81, bits, 8);
				refresh_state_crc(bad);
				rejects_unchanged(bad, NES_ERR_CORRUPT_FILE);
			}
			auto bad = valid;
			put_le(bad, 89, 3, 4);
			refresh_state_crc(bad);
			rejects_unchanged(bad, NES_ERR_CORRUPT_FILE);
			bad = valid;
			put_le(bad, 89, 0, 4);
			put_le(bad, 81, UINT64_C(0x3fe0000000000000), 8);
			refresh_state_crc(bad);
			rejects_unchanged(bad, NES_ERR_CORRUPT_FILE);
			bad = valid;
			bad.resize(92);
			refresh_state_crc(bad);
			rejects_unchanged(bad, NES_ERR_CORRUPT_FILE);
			bad = valid;
			bad.pop_back();
			rejects_unchanged(bad, NES_ERR_CORRUPT_FILE);
			bad = valid;
			bad[8] = 0;
			rejects_unchanged(bad, NES_ERR_UNSUPPORTED_VER);

			// CRC-valid malformed NST queue metadata reaches the NST parser and
			// must fail; reload a valid state between negatives because upstream
			// Machine::LoadState resets the machine on a raw NST parse failure.
			const size_t apu = find_chunk(valid, 101, valid.size(), "APU\0");
			check(apu < valid.size(), "locate APU for malformed queue cases");
			if (apu < valid.size())
			{
				const size_t buffer = find_chunk(valid, apu + 8,
				    apu + 8 + get_le32(valid, apu + 4), "BFR\0");
				check(buffer < valid.size(), "save includes optional BFR audio queue chunk");
				if (buffer < valid.size())
				{
					for (int failure = 0; failure < 4; ++failure)
					{
						bad = valid;
						if (failure == 0) put_le(bad, buffer + 8, 2, 4);
						if (failure == 1) put_le(bad, buffer + 12, 0x4000, 4);
						if (failure == 2) put_le(bad, buffer + 4, 4, 4);
						if (failure == 3)
						{
							const size_t length = 8 + get_le32(valid, buffer + 4);
							bad.insert(bad.begin() + buffer, valid.begin() + buffer,
							           valid.begin() + buffer + length);
							put_le(bad, apu + 4, get_le32(valid, apu + 4) + length, 4);
							put_le(bad, 97, get_le32(valid, 97) + length, 4);
						}
						refresh_state_crc(bad);
						check(nes_load_state(nes, bad.data(), bad.size()) ==
						      (failure == 0 ? NES_ERR_UNSUPPORTED_VER : NES_ERR_CORRUPT_FILE),
						      "invalid BFR version/count/length/duplicate is rejected");
						check(nes_load_state(nes, valid.data(), valid.size()) == NES_OK,
						      "valid state loads after malformed NST queue");
					}
				}
			}
		}
	}
	else
	{
		check(false, "wrapper tests skipped: save #1 not larger than the 81-byte header");
	}

	// ---- 9. capabilities ---------------------------------------------------
	// S1-3 spike: the ABI advertises fixed capability flags; verify the ones
	// that are contractually stable (num_pads, no debugger, no BPS patch).
	{
		nes_caps caps;
		std::memset(&caps, 0, sizeof(caps));
		const int rc = nes_get_capabilities(nes, &caps);
		check(rc >= 0, "nes_get_capabilities returns NES_OK or positive warning");
		{
			char buf[160];
			std::snprintf(buf, sizeof(buf), "caps.struct_size set (got %u)", caps.struct_size);
			check(caps.struct_size != 0, buf);
		}
		{
			char buf[160];
			std::snprintf(buf, sizeof(buf), "caps.version set (got %u)", caps.version);
			check(caps.version != 0, buf);
		}
		check(caps.num_pads == 4, "caps.num_pads == 4");
		check(caps.has_debugger == 0, "caps.has_debugger == 0 (core has no debugger)");
		check(caps.has_bps_patch == 0, "caps.has_bps_patch == 0 (IPS/UPS only)");
		check(caps.has_fds >= 0 && caps.has_nsf >= 0 && caps.has_rewinder >= 0,
		      "caps.has_fds/has_nsf/has_rewinder are non-negative");
		{
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "caps: fds=%u nsf=%u rewinder=%u max_cheats=%u pads=%u",
			              caps.has_fds, caps.has_nsf, caps.has_rewinder,
			              caps.max_cheat_codes, caps.num_pads);
			check(true, buf);
		}
	}

	// ---- 10. CPU RAM -------------------------------------------------------
	{
		const uint8_t* ram = NULL;
		size_t ram_size = 0;
		const int rc = nes_get_cpu_ram(nes, &ram, &ram_size);
		check(rc >= 0, "nes_get_cpu_ram returns NES_OK or positive warning");
		check(ram != NULL, "nes_get_cpu_ram returns a non-NULL RAM pointer");
		{
			char buf[128];
			std::snprintf(buf, sizeof(buf), "CPU RAM size == 0x800 (got 0x%zX)", ram_size);
			check(ram_size == 0x800, buf);
		}
		if (ram && ram_size >= 0x800)
		{
			// Just touch first + last byte; content varies per ROM/frame.
			const uint8_t lo = ram[0];
			const uint8_t hi = ram[0x7FF];
			char buf[160];
			std::snprintf(buf, sizeof(buf),
			              "touch ram[0]=0x%02X ram[0x7FF]=0x%02X (no crash)", lo, hi);
			check(true, buf);
		}
		else
		{
			check(false, "cannot touch CPU RAM: pointer or size unexpected");
		}
	}

	// ---- 11. GG cheat encode/decode round-trip ------------------------------
	// NOTE: the vendored NestopiaUE codec (NstApiCheats.cpp GameGenieEncode)
	// rejects addresses below 0x8000, so the spike uses hi-ROM addresses
	// (0x9234) instead of the plan's illustrative 0x1234/0x2345 — the
	// round-trip assertions themselves are unchanged.
	{
		// Plain cheat (no compare): 6-char GG code.
		char code[9] = {0};
		const int enc_rc = nes_cheat_encode(nes, NES_CHEAT_GAME_GENIE,
		                                    0x9234, 0xAB, 0, 0, code, sizeof(code));
		check(enc_rc >= 0, "GG encode (0x9234,0xAB,no compare) returns >= 0");
		{
			char buf[128];
			std::snprintf(buf, sizeof(buf), "GG encoded code = \"%s\"", code);
			check(enc_rc >= 0, buf);
		}
		uint16_t addr = 0;
		uint8_t  value = 0, compare = 0;
		int      use_compare = -1;
		const int dec_rc = nes_cheat_decode(nes, NES_CHEAT_GAME_GENIE, code,
		                                    &addr, &value, &compare, &use_compare);
		check(dec_rc >= 0, "GG decode returns >= 0");
		check(addr == 0x9234 && value == 0xAB && compare == 0 && use_compare == 0,
		      "GG round-trip preserves addr/value/compare/use_compare");

		// With compare: 8-char GG code, compare + use_compare preserved.
		char code2[9] = {0};
		const int enc2_rc = nes_cheat_encode(nes, NES_CHEAT_GAME_GENIE,
		                                     0x9234, 0x56, 0x78, 1, code2, sizeof(code2));
		check(enc2_rc >= 0, "GG encode with compare (0x9234,0x56,cmp=0x78) returns >= 0");
		uint16_t addr2 = 0;
		uint8_t  value2 = 0, compare2 = 0;
		int      use2 = -1;
		const int dec2_rc = nes_cheat_decode(nes, NES_CHEAT_GAME_GENIE, code2,
		                                     &addr2, &value2, &compare2, &use2);
		check(dec2_rc >= 0, "GG decode (compare) returns >= 0");
		check(addr2 == 0x9234 && value2 == 0x56 && compare2 == 0x78 && use2 == 1,
		      "GG round-trip preserves compare and use_compare");
	}

	// ---- 12. PAR cheat encode/decode round-trip -----------------------------
	// NOTE: ProActionRockyEncode requires address >= 0x8000 AND use_compare
	// (PAR codes always carry a compare byte), so the spike uses
	// (0x9234, 0xAB, 0, 1) — the plan's illustrative (0x1234, 0xAB, 0, 0)
	// cannot be encoded by the vendored codec at all.
	{
		char code[9] = {0};
		const int enc_rc = nes_cheat_encode(nes, NES_CHEAT_PRO_ACTION_ROCKY,
		                                    0x9234, 0xAB, 0, 1, code, sizeof(code));
		check(enc_rc >= 0, "PAR encode (0x9234,0xAB,cmp=0) returns >= 0");
		{
			char buf[128];
			std::snprintf(buf, sizeof(buf), "PAR encoded code = \"%s\"", code);
			check(enc_rc >= 0, buf);
		}
		uint16_t addr = 0;
		uint8_t  value = 0, compare = 0;
		int      use_compare = -1;
		const int dec_rc = nes_cheat_decode(nes, NES_CHEAT_PRO_ACTION_ROCKY, code,
		                                    &addr, &value, &compare, &use_compare);
		check(dec_rc >= 0, "PAR decode returns >= 0");
		check(addr == 0x9234 && value == 0xAB && compare == 0 && use_compare == 1,
		      "PAR round-trip preserves addr/value/compare/use_compare");

		// Second PAR code with a non-zero compare byte.
		char code2[9] = {0};
		const int enc2_rc = nes_cheat_encode(nes, NES_CHEAT_PRO_ACTION_ROCKY,
		                                     0x9234, 0x56, 0x78, 1, code2, sizeof(code2));
		check(enc2_rc >= 0, "PAR encode with compare (0x9234,0x56,cmp=0x78) returns >= 0");
		uint16_t addr2 = 0;
		uint8_t  value2 = 0, compare2 = 0;
		int      use2 = -1;
		const int dec2_rc = nes_cheat_decode(nes, NES_CHEAT_PRO_ACTION_ROCKY, code2,
		                                     &addr2, &value2, &compare2, &use2);
		check(dec2_rc >= 0, "PAR decode (compare) returns >= 0");
		check(addr2 == 0x9234 && value2 == 0x56 && compare2 == 0x78 && use2 == 1,
		      "PAR round-trip preserves compare and use_compare");
	}

	// ---- 13. cheat add / count / clear -------------------------------------
	{
		const int add1_rc = nes_cheat_add(nes, 0x1234, 0xAB, 0, 0);
		check(add1_rc >= 0, "nes_cheat_add #1 (0x1234,0xAB) returns >= 0");
		uint32_t count = 0;
		const int cnt1_rc = nes_cheat_count(nes, &count);
		check(cnt1_rc >= 0 && count >= 1, "nes_cheat_count >= 1 after first add");
		{
			char buf[128];
			std::snprintf(buf, sizeof(buf), "cheat count after add #1 = %u", count);
			check(true, buf);
		}
		const int add2_rc = nes_cheat_add(nes, 0x2345, 0x56, 0, 0);
		check(add2_rc >= 0, "nes_cheat_add #2 (0x2345,0x56) returns >= 0");
		uint32_t count2 = 0;
		const int cnt2_rc = nes_cheat_count(nes, &count2);
		check(cnt2_rc >= 0 && count2 >= 2, "nes_cheat_count >= 2 after second add");
		{
			char buf[128];
			std::snprintf(buf, sizeof(buf), "cheat count after add #2 = %u", count2);
			check(true, buf);
		}
		const int clr_rc = nes_cheat_clear(nes);
		check(clr_rc >= 0, "nes_cheat_clear returns >= 0");
		uint32_t count3 = 99;
		const int cnt3_rc = nes_cheat_count(nes, &count3);
		check(cnt3_rc >= 0 && count3 == 0, "nes_cheat_count == 0 after clear");
	}

	// ---- 14. callback registration (process-global; unset at the end) ------
	// Registered AFTER all other tests that could fire callbacks; the no-op
	// handlers are then unset with NULL so the test process is left clean.
	{
		auto noop_log      = [](void*, const char*, uint32_t) {};
		auto noop_file_io  = [](void*, int, int, uint8_t*, size_t, size_t*,
		                        const uint8_t*, size_t) -> int { return NES_OK; };
		auto noop_event    = [](void*, int, const void*) {};
		auto noop_question = [](void*, int) -> int { return 1; };

		check(nes_set_log_callback(nes, noop_log, NULL) >= 0,
		      "nes_set_log_callback(noop) returns >= 0");
		check(nes_set_file_io_callback(nes, noop_file_io, NULL) >= 0,
		      "nes_set_file_io_callback(noop) returns >= 0");
		check(nes_set_event_callback(nes, noop_event, NULL) >= 0,
		      "nes_set_event_callback(noop) returns >= 0");
		check(nes_set_question_callback(nes, noop_question, NULL) >= 0,
		      "nes_set_question_callback(noop) returns >= 0");

		// Unset (NULL) — idempotent, restores the pristine process state.
		check(nes_set_log_callback(nes, NULL, NULL) >= 0,
		      "nes_set_log_callback(NULL) returns >= 0");
		check(nes_set_file_io_callback(nes, NULL, NULL) >= 0,
		      "nes_set_file_io_callback(NULL) returns >= 0");
		check(nes_set_event_callback(nes, NULL, NULL) >= 0,
		      "nes_set_event_callback(NULL) returns >= 0");
		check(nes_set_question_callback(nes, NULL, NULL) >= 0,
		      "nes_set_question_callback(NULL) returns >= 0");
	}

	// ---- 15. input bridge (START held, run; clear, run) --------------------
	{
		std::vector<int16_t> in_audio(5 * 1024);
		uint32_t fr = 0, sw = 0;
		nes_set_input(nes, 0, NES_BTN_START);
		const int rc1 = nes_run_frames(nes, 5, in_audio.data(),
		                               static_cast<uint32_t>(in_audio.size()), &fr, &sw);
		check(rc1 >= 0 && fr == 5, "nes_set_input(0, START) + 5 frames runs normally");
		nes_clear_input(nes);
		uint32_t fr2 = 0, sw2 = 0;
		const int rc2 = nes_run_frames(nes, 5, in_audio.data(),
		                               static_cast<uint32_t>(in_audio.size()), &fr2, &sw2);
		check(rc2 >= 0 && fr2 == 5, "nes_clear_input + 5 frames runs normally");
	}

	nes_destroy(nes);

	// Separate cores keep this replay test independent of the smoke test's
	// input timing and process-global callback lifetime.
	nes = nes_create(nullptr);
	nes_t* peer = nes_create(nullptr);
	check(nes && peer, "create two replay cores");
	if (nes && peer)
	{
		check(nes_load_rom(nes, rom.data(), rom.size(), nullptr) >= 0 &&
		      nes_load_rom(peer, rom.data(), rom.size(), nullptr) >= 0,
		      "load two replay cores");
		check(save_bytes(nes) == save_bytes(peer), "fresh cores serialize identically");
		run_pcm(nes, 1);
		const auto checkpoint = save_bytes(nes);
		check(checkpoint.size() > 93 && checkpoint[8] == 2,
		      "new saves use explicit clock-aware wrapper version 2");
		run_pcm(peer, 7);
		std::vector<int16_t> expected;
		for (int replay = 0; replay < 8; ++replay)
		{
			nes_t* target = replay % 2 ? peer : nes;
			check(nes_load_state(target, checkpoint.data(), checkpoint.size()) == NES_OK,
			      "reload clock-aware checkpoint");
			const auto pcm = run_pcm(target, 3);
			if (replay == 0) expected = pcm;
			check(same_pcm(pcm, expected), "checkpoint replays preserve every PCM sample and count");
		}

		// Cover several fractional cadence phases and audible gameplay. A
		// replay must also match uninterrupted execution, not just another load.
		bool heard_audio = false;
		for (const uint32_t advance : {1u, 2u, 3u, 60u, 61u, 119u})
		{
			run_pcm(nes, advance);
			const auto saved = save_bytes(nes);
			const auto continuous_pcm = run_pcm(nes, 8);
			for (const int16_t sample : continuous_pcm) heard_audio |= sample != 0;
			const auto continuous_frame = frame_bytes(nes);
			const auto continuous_state = save_bytes(nes);
			for (nes_t* target : {nes, peer})
			{
				check(nes_load_state(target, saved.data(), saved.size()) == NES_OK,
				      "load uninterrupted comparison checkpoint");
				check(same_pcm(run_pcm(target, 8), continuous_pcm),
				      "replay matches uninterrupted PCM sample-for-sample");
				check(frame_bytes(target) == continuous_frame,
				      "replay matches uninterrupted video pixels");
				check(save_bytes(target) == continuous_state,
				      "replay matches uninterrupted serialized state");
			}
		}
		check(heard_audio, "uninterrupted replay coverage includes non-silent PCM");
	}
	nes_destroy(peer);
	nes_destroy(nes);

	// ---- summary ---------------------------------------------------------
	test_canonical_state(rom);
	test_canonical_allocation_history(rom);

	std::printf("\n=== RESULT: %s (%d failure%s) ===\n",
	            g_failures == 0 ? "PASS" : "FAIL",
	            g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
