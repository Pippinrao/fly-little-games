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
	const char* rom_path = (argc > 1) ? argv[1] : "core/tests/fixtures/from_below.nes";
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

		// f2. version: setting version (u32 LE at offset 8) to 2 must be
		//     rejected as an unknown wrapper format.
		{
			std::vector<uint8_t> v2(buf1.begin(), buf1.begin() + written1);
			v2[8] = 2; v2[9] = 0; v2[10] = 0; v2[11] = 0;
			const int rc = nes_load_state(nes, v2.data(), v2.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "version=2 -> load fails (rc=%d %s, expected NES_ERR_UNSUPPORTED_VER)",
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
			std::vector<uint8_t> legacy(buf1.begin() + 81, buf1.begin() + written1);
			const int rc = nes_load_state(nes, legacy.data(), legacy.size());
			char buf[192];
			std::snprintf(buf, sizeof(buf),
			              "legacy raw NST (header stripped) loads (rc=%d %s, expected >= 0)",
			              rc, err_str(rc));
			check(rc >= 0, buf);
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

	// ---- summary ---------------------------------------------------------
	std::printf("\n=== RESULT: %s (%d failure%s) ===\n",
	            g_failures == 0 ? "PASS" : "FAIL",
	            g_failures, g_failures == 1 ? "" : "s");
	return g_failures == 0 ? 0 : 1;
}
