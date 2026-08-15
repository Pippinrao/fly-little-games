/*
 * core/tests/test_core.cpp — headless smoke test (Task 6)
 *
 * First runtime validation of the whole NestopiaUE core through the C ABI
 * (core/include/nes/nes.h). Loads a real NES ROM ("From Below", MIT homebrew),
 * runs 60 frames, and verifies video / audio / save-state behavior:
 *
 *   load ROM -> run 60 frames -> video non-blank -> audio non-silent
 *             -> save state -> mutate (30 frames) -> load state
 *             -> save again (byte-equality / NES_OK round-trip)
 *
 * Self-contained: stdio only, no external deps. Exit code 0 = PASS.
 */
#include "nes/nes.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

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
		case NES_ERR_UNSUPPORTED_MAPPER:  return "NES_ERR_UNSUPPORTED_MAPPER";
		case NES_ERR_MISSING_BIOS:        return "NES_ERR_MISSING_BIOS";
		case NES_ERR_WRONG_MODE:          return "NES_ERR_WRONG_MODE";
		case NES_ERR_BUFFER_TOO_SMALL:    return "NES_ERR_BUFFER_TOO_SMALL";
		case NES_ERR_NOT_IMPLEMENTED:     return "NES_ERR_NOT_IMPLEMENTED";
		case NES_ERR_REENTRANT:           return "NES_ERR_REENTRANT";
		default:                          return "unknown";
		}
	}
} // namespace

int main(int argc, char** argv)
{
	const char* rom_path = (argc > 1) ? argv[1] : "core/tests/fixtures/from_below.nes";
	std::printf("=== FlyNES headless smoke test (Task 6) ===\n");
	std::printf("ROM path: %s\n", rom_path);
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

	// ---- 2. read ROM + validate + load ----------------------------------
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

	// ---- 3. audio format -------------------------------------------------
	const int af_rc = nes_set_audio_format(nes, 48000, 0);
	check(af_rc == NES_OK, "nes_set_audio_format(48000, mono) returns NES_OK");

	// ---- 4. run 60 frames ------------------------------------------------
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

	// ---- 5. video check --------------------------------------------------
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

	// ---- 6. audio check --------------------------------------------------
	// From Below's intro is silent for several seconds (verified empirically:
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

	// ---- 7. save-state round-trip ----------------------------------------
	// 7a. save state #1 (validates MemOStream seek back-patching; a broken
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

	// 7b. mutate the game state (30 more frames).
	{
		std::vector<int16_t> audio2(30 * 1024);
		uint32_t fr2 = 0, sw2 = 0;
		const int rc = nes_run_frames(nes, 30, audio2.data(),
		                              static_cast<uint32_t>(audio2.size()), &fr2, &sw2);
		char buf[128];
		std::snprintf(buf, sizeof(buf), "mutate: 30 frames ran (rc=%d frames=%u)", rc, fr2);
		check(rc == NES_OK && fr2 == 30, buf);
	}

	// 7c. reload the saved state; must be accepted (CRC validates the blob,
	//     so a NES_OK here also proves the back-patched chunk lengths were
	//     written correctly).
	const int load_rc2 = nes_load_state(nes, buf1.data(), written1);
	check(load_rc2 == NES_OK, "nes_load_state #1 returns NES_OK");

	// 7d. determinism: save again right after reload without running frames —
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

	// 7e. as specified: run 1 frame after reload, save to buf2, compare with
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

	nes_destroy(nes);

	// ---- summary ---------------------------------------------------------
	std::printf("\n=== RESULT: %s (%d failure%s) ===\n",
	            g_failures == 0 ? "PASS" : "FAIL",
	            g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
