/*
 * core/src/nes_core.cpp — NestopiaUE C ABI 实现: 生命周期 + ROM 加载 + 流适配器 (Task 3)
 *                                            + 帧循环/视频/音频格式 (Task 4)
 *                                            + 即时存档/金手指/内存 (Task 5)
 *
 * 权威签名: core/include/nes/nes.h (Task 2 最终版)
 * 映射依据: docs/nes-arch-review/t2b-c-abi-draft.md §2(逐函数映射) §3(phase0 清单) §4(适配器)
 *
 * Task 3 覆盖 (t2b §3):
 *   - nes_create / nes_destroy / nes_api_version / nes_core_version / nes_get_capabilities
 *   - nes_load_rom / nes_unload / nes_power / nes_reset
 *   - nes_set_log_callback / nes_set_file_io_callback / nes_set_event_callback / nes_set_question_callback
 *   - nes_load_rom_patched → NES_ERR_NOT_IMPLEMENTED (phase 1)
 *   - nes_get_rom_info 真实实现 (nes_load_rom(info_out) 需要)。
 *
 * Task 4 覆盖 (t2b §2 运行节 + 视频/音频格式节 + 输入桥):
 *   - nes_run_frames (音频主时钟帧循环) / nes_set_video_format / nes_get_video_frame
 *   - nes_set_audio_format (phase0 mono) / nes_set_input / nes_clear_input (Task 3 已实现)
 *
 * Task 5 覆盖 (t2b §2 即时存档/金手指/内存节):
 *   - nes_save_state / nes_load_state (MemOStream/MemIStream + Machine::Save/LoadState)
 *   - nes_cheat_add/remove/clear/count/encode/decode (GG + Pro-Action Rocky)
 *   - nes_get_cpu_ram (Cheats::GetRam, 2KB 只读)
 *   - 保持 NOT_IMPLEMENTED: nes_battery_flush (无干净内核 API), nes_mem_read/write
 *     (内核无总线 peek/poke), FDS 系列, nes_load_rom_patched (阶段1/2)。
 */
#include "nes/nes.h"
#include "nes_state.hpp"
#include "nes_stream.hpp"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>
#include <string>
#include <vector>

#include "NstApiEmulator.hpp"
#include "NstApiMachine.hpp"
#include "NstApiVideo.hpp"
#include "NstApiSound.hpp"
#include "NstApiInput.hpp"
#include "NstApiCartridge.hpp"
#include "NstApiCheats.hpp"
#include "NstApiUser.hpp"
#include "NstApiFds.hpp"

namespace
{
	constexpr uint32_t kScreenWidth  = 256;
	constexpr uint32_t kScreenHeight = 240;

	// Nestopia 基础类型 (NstBase.hpp, 定义在 namespace Nes 内)
	using Nes::uint;
	using Nes::ulong;
	using Nes::ushort;

	// ------------------------------------------------------------------
	// 重入保护 (t2b §4.6): Nestopia 的 log/fileIo/event/question/pad 回调都跑在
	// 模拟器线程 (nes_load_rom / nes_run_frames / nes_unload / nes_power 的调用栈内)。
	// 回调内禁止再调 nes_* (头文件注释约定返回 NES_ERR_REENTRANT)。
	// ------------------------------------------------------------------
	thread_local bool g_in_callback = false;

	struct CallbackScope
	{
		const bool prev;
		CallbackScope() : prev(g_in_callback) { g_in_callback = true; }
		~CallbackScope() { g_in_callback = prev; }
	};

	inline bool in_callback() { return g_in_callback; }

	// ------------------------------------------------------------------
	// wchar_t (Android/Linux 4 字节 UTF-32) → UTF-8 (t2b §4.7)
	// 不向 C ABI 暴露 wchar_t; dst 保证 NUL 结尾。
	// ------------------------------------------------------------------
	void wstring_to_utf8(const std::wstring& ws, char* dst, size_t cap)
	{
		if (cap == 0)
			return;

		size_t out = 0;
		for (const wchar_t wc : ws)
		{
			const uint32_t cp = static_cast<uint32_t>(wc);
			if (cp >= 0xD800 && cp <= 0xDFFF)
				continue; // 孤立代理项, 跳过

			if (cp < 0x80)
			{
				if (out + 1 >= cap) break;
				dst[out++] = static_cast<char>(cp);
			}
			else if (cp < 0x800)
			{
				if (out + 2 >= cap) break;
				dst[out++] = static_cast<char>(0xC0 | (cp >> 6));
				dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
			}
			else if (cp < 0x10000)
			{
				if (out + 3 >= cap) break;
				dst[out++] = static_cast<char>(0xE0 | (cp >> 12));
				dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
			}
			else
			{
				if (out + 4 >= cap) break;
				dst[out++] = static_cast<char>(0xF0 | (cp >> 18));
				dst[out++] = static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
				dst[out++] = static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
				dst[out++] = static_cast<char>(0x80 | (cp & 0x3F));
			}
		}
		dst[out] = '\0';
	}

	// ------------------------------------------------------------------
	// pixfmt → 每像素字节数 (帧缓冲分配用)
	// ------------------------------------------------------------------
	size_t pixfmt_bpp(nes_pixfmt fmt)
	{
		switch (fmt)
		{
			case NES_PIXFMT_RGB888:   return 3;
			case NES_PIXFMT_RGBA8888: return 4;
			case NES_PIXFMT_RGB565:
			default:                  return 2;
		}
	}

	// ------------------------------------------------------------------
	// File::Action (NstApiUser.hpp) → ABI nes_io_action。
	// 两套枚举值不同 (EEPROM/FDS 顺序互换), 必须显式翻译, 不能直接强转。
	// 返回 0 表示 phase0 不支持的 action (TAPE/TURBOFILE/SAMPLE...)。
	// ------------------------------------------------------------------
	int file_action_to_abi(Nes::Api::User::File::Action action)
	{
		using Action = Nes::Api::User::File::Action;
		switch (action)
		{
			case Action::LOAD_BATTERY: return NES_IO_LOAD_BATTERY;
			case Action::SAVE_BATTERY: return NES_IO_SAVE_BATTERY;
			case Action::LOAD_EEPROM:  return NES_IO_LOAD_EEPROM;
			case Action::SAVE_EEPROM:  return NES_IO_SAVE_EEPROM;
			case Action::LOAD_FDS:     return NES_IO_LOAD_FDS;
			case Action::SAVE_FDS:     return NES_IO_SAVE_FDS;
			case Action::LOAD_ROM:     return NES_IO_LOAD_ROM;
			default:                   return 0;
		}
	}

	// ------------------------------------------------------------------
	// 内部上下文。进程内单实例设计 (t2b §0 #7): Nestopia 回调全部是静态单例,
	// userdata=ctx; 若宿主创建第二个实例, 静态回调被后者覆盖。
	// ------------------------------------------------------------------
	struct nes_ctx
	{
		Nes::Api::Emulator emulator;
		Nes::Api::Machine machine;
		Nes::Api::Video video;
		Nes::Api::Sound sound;
		Nes::Api::Input input;
		Nes::Api::Cartridge cartridge;
		Nes::Api::Cheats cheats;
		Nes::Api::Fds fds;

		uint8_t* framebuffer;                 // 256*240*bpp
		size_t framebuffer_size;              // 当前帧缓冲分配字节数 (Task 4: set_video_format 同步重分配)
		nes_video_frame video_frame;          // nes_get_video_frame 返回的描述符 (Task 4)
		nes_config cfg;                       // favored_system / sample_rate / pixfmt

		// 推式输入 → 拉式回调 (Pad::callback) 的桥 (t2b §0 #0)
		std::atomic<uint32_t> input_buttons[NES_PORT_MAX];

		// 回调字段 (静态单例分发器读这些字段)
		nes_log_fn log_cb;
		void* log_userdata;
		nes_file_io_fn file_io_cb;
		void* file_io_userdata;
		nes_event_fn event_cb;
		void* event_userdata;
		nes_question_fn question_cb;
		void* question_userdata;

		// 电池存档进程内缓存 (LOAD_BATTERY 优先取此, SAVE_BATTERY 存入并转发宿主)
		std::vector<uint8_t> battery_sram;

		nes_ctx()
			: emulator(),
			  machine(emulator),
			  video(emulator),
			  sound(emulator),
			  input(emulator),
			  cartridge(emulator),
			  cheats(emulator),
			  fds(emulator),
			  framebuffer(nullptr),
			  framebuffer_size(0),
			  video_frame{},
			  cfg{},
			  log_cb(nullptr),
			  log_userdata(nullptr),
			  file_io_cb(nullptr),
			  file_io_userdata(nullptr),
			  event_cb(nullptr),
			  event_userdata(nullptr),
			  question_cb(nullptr),
			  question_userdata(nullptr)
		{
			for (auto& b : input_buttons)
				b.store(0, std::memory_order_relaxed);

			video_frame.struct_size = sizeof(nes_video_frame);
			video_frame.version     = NES_STRUCT_VERSION;
		}

		~nes_ctx()
		{
			std::free(framebuffer);
		}
	};

	// ------------------------------------------------------------------
	// 静态回调 (Nestopia 单例, userdata = ctx)
	// ------------------------------------------------------------------

	void on_log(void* userdata, const char* text, ulong length)
	{
		nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
		if (!ctx || !ctx->log_cb)
			return;
		CallbackScope scope;
		ctx->log_cb(ctx->log_userdata, text, static_cast<uint32_t>(length));
	}

	void on_event(void* userdata, Nes::Api::User::Event event, const void* context)
	{
		nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
		if (!ctx || !ctx->event_cb)
			return;

		int abi_event = 0;
		switch (event)
		{
			case Nes::Api::User::EVENT_CPU_JAM:               abi_event = NES_EVENT_CPU_JAM; break;
			case Nes::Api::User::EVENT_DISPLAY_TIMER:         abi_event = NES_EVENT_DISPLAY_TIMER; break;
			case Nes::Api::User::EVENT_CPU_UNOFFICIAL_OPCODE: abi_event = NES_EVENT_CPU_UNOFFICIAL_OPCODE; break;
			default: return;
		}

		CallbackScope scope;
		ctx->event_cb(ctx->event_userdata, abi_event, context);
	}

	Nes::Api::User::Answer on_question(void* userdata, Nes::Api::User::Question question)
	{
		nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
		if (!ctx || !ctx->question_cb)
			return Nes::Api::User::ANSWER_DEFAULT;

		int abi_question = 0;
		switch (question)
		{
			case Nes::Api::User::QUESTION_NST_PRG_CRC_FAIL_CONTINUE:
				abi_question = NES_QUESTION_NST_CRC_FAIL_CONTINUE;
				break;
			default:
				return Nes::Api::User::ANSWER_DEFAULT;
		}

		CallbackScope scope;
		// ABI 约定: 返回 1=继续(ANSWER_YES) 0=中止(ANSWER_NO)
		return ctx->question_cb(ctx->question_userdata, abi_question)
			? Nes::Api::User::ANSWER_YES
			: Nes::Api::User::ANSWER_NO;
	}

	/*
	 * fileIoCallback 包装 (t2b §2 回调节 + §4.5):
	 *   SAVE_* → file.GetContent(const void*&, ulong&) 直接拿指针,
	 *             以 HOST_READS 方向转发给 nes_file_io_fn;
	 *   LOAD_* → file.GetMaxSize() 得知期望容量, 以 HOST_WRITES 方向让宿主
	 *             填 buf 与 *buf_len, 宿主成功后再 file.SetContent(buf, buf_len);
	 *   LOAD_BATTERY 优先从 ctx->battery_sram 缓存提供 (重启后由宿主经回调补齐);
	 *   SAVE_BATTERY 存入 ctx->battery_sram 并转发宿主持久化。
	 */
	void on_file_io(void* userdata, Nes::Api::User::File& file)
	{
		nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
		if (!ctx)
			return;

		const Nes::Api::User::File::Action action = file.GetAction();
		const int abi_action = file_action_to_abi(action);
		if (abi_action == 0)
			return; // TAPE/TURBOFILE/SAMPLE 等: phase0 不支持, 忽略

		CallbackScope scope;

		switch (action)
		{
			case Nes::Api::User::File::LOAD_BATTERY:
				// 进程内缓存优先
				if (!ctx->battery_sram.empty())
				{
					file.SetContent(ctx->battery_sram.data(),
					                static_cast<ulong>(ctx->battery_sram.size()));
					return;
				}
				break; // 缓存为空 → 回落通用 LOAD 路径 (宿主持久化数据入口)

			case Nes::Api::User::File::LOAD_EEPROM:
			case Nes::Api::User::File::LOAD_FDS:
			case Nes::Api::User::File::LOAD_ROM:
				break;

			case Nes::Api::User::File::SAVE_BATTERY:
			case Nes::Api::User::File::SAVE_EEPROM:
			case Nes::Api::User::File::SAVE_FDS:
			{
				const void* mem = nullptr;
				ulong size = 0;
				if (NES_FAILED(file.GetContent(mem, size)))
					return;

				if (action == Nes::Api::User::File::SAVE_BATTERY && mem && size)
				{
					ctx->battery_sram.assign(
						static_cast<const uint8_t*>(mem),
						static_cast<const uint8_t*>(mem) + size);
				}

				if (ctx->file_io_cb)
				{
					ctx->file_io_cb(ctx->file_io_userdata, abi_action,
					                NES_IO_DIR_HOST_READS,
					                nullptr, 0, nullptr,
					                static_cast<const uint8_t*>(mem), size);
				}
				return;
			}

			default:
				return;
		}

		// ---- 通用 LOAD 路径: 宿主填缓冲 (direction = HOST_WRITES) ----
		if (!ctx->file_io_cb)
			return;

		const ulong cap = file.GetMaxSize();
		if (cap == 0)
			return;

		std::vector<uint8_t> buf(static_cast<size_t>(cap));
		size_t buf_len = 0;

		const int rc = ctx->file_io_cb(ctx->file_io_userdata, abi_action,
		                               NES_IO_DIR_HOST_WRITES,
		                               buf.data(), buf.size(), &buf_len,
		                               nullptr, 0);
		if (rc >= 0 && buf_len > 0)
		{
			file.SetContent(buf.data(), static_cast<ulong>(buf_len));
			if (action == Nes::Api::User::File::LOAD_BATTERY)
				ctx->battery_sram.assign(buf.begin(), buf.begin() + buf_len);
		}
	}

	/*
	 * Pad::callback (拉取式输入, NstApiInput.hpp / NstInpPad.cpp:102):
	 * 每帧由核心回调索取当前按键; index = type - PAD1 ∈ [0,3]。
	 */
	bool on_pad_poll(void* userdata, Nes::Core::Input::Controllers::Pad& pad, uint index)
	{
		nes_ctx* ctx = static_cast<nes_ctx*>(userdata);
		if (ctx && index < NES_PORT_MAX)
			pad.buttons = ctx->input_buttons[index].load(std::memory_order_relaxed);
		return true;
	}

	// ------------------------------------------------------------------
	// 视频 / 音频 配置
	// ------------------------------------------------------------------

	void apply_render_state(nes_ctx* ctx)
	{
		Nes::Api::Video::RenderState rs;
		switch (ctx->cfg.pixfmt)
		{
			case NES_PIXFMT_RGB888:
				rs.bits.count = 24;
				rs.bits.mask.r = 0xFF0000;
				rs.bits.mask.g = 0x00FF00;
				rs.bits.mask.b = 0x0000FF;
				break;
			case NES_PIXFMT_RGBA8888:
				rs.bits.count = 32;
				rs.bits.mask.r = 0x00FF0000;
				rs.bits.mask.g = 0x0000FF00;
				rs.bits.mask.b = 0x000000FF;
				break;
			case NES_PIXFMT_RGB565:
			default:
				rs.bits.count = 16;
				rs.bits.mask.r = 0xF800;
				rs.bits.mask.g = 0x07E0;
				rs.bits.mask.b = 0x001F;
				break;
		}
		rs.width  = static_cast<ushort>(kScreenWidth);
		rs.height = static_cast<ushort>(kScreenHeight);
		rs.filter = Nes::Api::Video::RenderState::FILTER_NONE;
		ctx->video.SetRenderState(rs);
	}

	// 帧描述符同步 (pixels/pitch 随帧缓冲与 pixfmt 变化; struct_size/version 在 ctor 已定)
	void update_video_frame(nes_ctx* ctx)
	{
		ctx->video_frame.width  = kScreenWidth;
		ctx->video_frame.height = kScreenHeight;
		ctx->video_frame.format = ctx->cfg.pixfmt;
		ctx->video_frame.pitch  = static_cast<int32_t>(kScreenWidth * pixfmt_bpp(ctx->cfg.pixfmt));
		ctx->video_frame.pixels = ctx->framebuffer;
	}

	void apply_audio_config(nes_ctx* ctx)
	{
		ctx->sound.SetSampleRate(ctx->cfg.sample_rate);
		ctx->sound.SetSpeaker(Nes::Api::Sound::SPEAKER_MONO); // phase0 固定 mono (t2b §0 #4)
	}

	// ------------------------------------------------------------------
	// ROM 信息填充 (nes_get_rom_info / nes_load_rom(info_out) 共用)
	// ------------------------------------------------------------------

	int fill_rom_info(const nes_ctx* ctx, nes_rom_info* info)
	{
		std::memset(info, 0, sizeof(*info));
		info->struct_size = sizeof(nes_rom_info);
		info->version = NES_STRUCT_VERSION;

		const Nes::Api::Cartridge::Profile* profile = ctx->cartridge.GetProfile();
		if (!profile)
			return NES_ERR_NOT_READY; // 无卡带

		wstring_to_utf8(profile->game.title,     info->title,     sizeof(info->title));
		wstring_to_utf8(profile->game.publisher, info->publisher, sizeof(info->publisher));
		wstring_to_utf8(profile->game.developer, info->developer, sizeof(info->developer));

		if (profile->game.region.empty())
		{
			const char* r = (ctx->machine.GetMode() == Nes::Api::Machine::NTSC) ? "NTSC" : "PAL";
			std::strncpy(info->region, r, sizeof(info->region) - 1);
			info->region[sizeof(info->region) - 1] = '\0';
		}
		else
		{
			wstring_to_utf8(profile->game.region, info->region, sizeof(info->region));
		}

		info->mapper       = profile->board.mapper;
		info->submapper    = profile->board.subMapper;
		info->prg_size     = profile->board.GetPrg();
		info->chr_size     = profile->board.GetChr();
		info->wram_size    = profile->board.GetWram();
		info->vram_size    = profile->board.GetVram();
		info->has_battery  = profile->board.HasBattery() ? 1u : 0u;
		info->system       = static_cast<uint32_t>(profile->system.type);
		info->cpu          = static_cast<uint32_t>(profile->system.cpu);
		info->ppu          = static_cast<uint32_t>(profile->system.ppu);
		info->region_ntsc  = (ctx->machine.GetMode() == Nes::Api::Machine::NTSC) ? 1u : 0u;
		info->patched      = profile->patched ? 1u : 0u;
		info->players      = profile->game.players;

		// Hash::Get 只写 hex 字符、不写 NUL (NstApiCartridge.cpp:145), 需手动补齐
		profile->hash.Get(info->sha1, info->crc32);
		info->sha1[40] = '\0';
		info->crc32[8] = '\0';

		return NES_OK;
	}
}

extern "C" {

// ======================================================================
// 生命周期 / 版本能力
// ======================================================================

NES_API nes_t* nes_create(const nes_config* cfg)
{
	if (in_callback())
		return nullptr;

	nes_ctx* ctx = nullptr;
	try
	{
		ctx = new nes_ctx();
	}
	catch (...)
	{
		return nullptr;
	}

	// 缺省配置: NTSC / 48000 / RGB565 (头文件注释)
	ctx->cfg.favored_system = NES_FAVORED_NES_NTSC;
	ctx->cfg.sample_rate    = 48000;
	ctx->cfg.pixfmt         = NES_PIXFMT_RGB565;
	ctx->cfg.struct_size    = sizeof(nes_config);
	ctx->cfg.version        = NES_STRUCT_VERSION;

	// 按 struct_size 逐个字段信任, 兼容未来扩展/更小结构
	if (cfg)
	{
		const size_t have = cfg->struct_size ? cfg->struct_size : sizeof(nes_config);
		if (have >= 8 && cfg->favored_system >= NES_FAVORED_NES_NTSC &&
		                cfg->favored_system <= NES_FAVORED_DENDY)
			ctx->cfg.favored_system = cfg->favored_system;
		if (have >= 12 && cfg->sample_rate)
			ctx->cfg.sample_rate = cfg->sample_rate;
		if (have >= 16 && cfg->pixfmt >= NES_PIXFMT_RGB565 &&
		                  cfg->pixfmt <= NES_PIXFMT_RGBA8888)
			ctx->cfg.pixfmt = cfg->pixfmt;
	}

	ctx->framebuffer = static_cast<uint8_t*>(
		std::malloc(kScreenWidth * kScreenHeight * pixfmt_bpp(ctx->cfg.pixfmt)));
	if (!ctx->framebuffer)
	{
		delete ctx;
		return nullptr;
	}
	ctx->framebuffer_size = kScreenWidth * kScreenHeight * pixfmt_bpp(ctx->cfg.pixfmt);
	update_video_frame(ctx);

	apply_render_state(ctx);
	apply_audio_config(ctx);

	// 注册静态回调 (进程级单例, userdata = ctx)
	Nes::Api::User::logCallback.Set(&on_log, ctx);
	Nes::Api::User::fileIoCallback.Set(&on_file_io, ctx);
	Nes::Api::User::eventCallback.Set(&on_event, ctx);
	Nes::Api::User::questionCallback.Set(&on_question, ctx);
	Nes::Core::Input::Controllers::Pad::callback.Set(&on_pad_poll, ctx);

	return reinterpret_cast<nes_t*>(ctx);
}

NES_API void nes_destroy(nes_t* nes)
{
	if (!nes)
		return;
	if (in_callback())
		return; // 回调内销毁会 use-after-free, 拒绝

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	ctx->machine.Unload(); // 内部 PowerOff → 触发 SAVE_BATTERY → fileIoCallback

	// 只注销仍指向本实例的静态回调 (单实例设计下正常只有一个 ctx)
	{
		Nes::Api::User::LogCallback fn = nullptr;
		void* ud = nullptr;
		Nes::Api::User::logCallback.Get(fn, ud);
		if (ud == ctx) Nes::Api::User::logCallback.Unset();
	}
	{
		Nes::Api::User::FileIoCallback fn = nullptr;
		void* ud = nullptr;
		Nes::Api::User::fileIoCallback.Get(fn, ud);
		if (ud == ctx) Nes::Api::User::fileIoCallback.Unset();
	}
	{
		Nes::Api::User::EventCallback fn = nullptr;
		void* ud = nullptr;
		Nes::Api::User::eventCallback.Get(fn, ud);
		if (ud == ctx) Nes::Api::User::eventCallback.Unset();
	}
	{
		Nes::Api::User::QuestionCallback fn = nullptr;
		void* ud = nullptr;
		Nes::Api::User::questionCallback.Get(fn, ud);
		if (ud == ctx) Nes::Api::User::questionCallback.Unset();
	}
	{
		Nes::Core::Input::Controllers::Pad::PollCallback fn = nullptr;
		void* ud = nullptr;
		Nes::Core::Input::Controllers::Pad::callback.Get(fn, ud);
		if (ud == ctx) Nes::Core::Input::Controllers::Pad::callback.Unset();
	}

	delete ctx;
}

NES_API uint32_t nes_api_version(void)
{
	return (NES_API_VERSION_MAJOR << 16) | (NES_API_VERSION_MINOR << 8) | NES_API_VERSION_PATCH;
}

NES_API const char* nes_core_version(void)
{
	// vendor 锁定 tag 1.53.2 @4470a2e; 内核源码无 NST_VERSION 宏, 用编译期常量
	return "1.53.2";
}

NES_API int nes_get_capabilities(const nes_t* nes, nes_caps* caps)
{
	(void)nes;
	if (!caps)
		return NES_ERR_INVALID_PARAM;

	std::memset(caps, 0, sizeof(*caps));
	caps->struct_size     = sizeof(nes_caps);
	caps->version         = NES_STRUCT_VERSION;
	caps->has_fds         = 1;  // FDS 盘面模拟可用 (仍缺 BIOS, 见 nes_set_fds_bios)
	caps->has_nsf         = 1;  // NSF 音频播放
	caps->has_rewinder    = 1;  // NstApiRewinder 存在
	caps->has_debugger    = 0;  // 内核无调试器
	caps->has_bps_patch   = 0;  // 内核只支持 IPS/UPS, 无 BPS
	caps->max_cheat_codes = 64;
	caps->num_pads        = NES_PORT_MAX;
	return NES_OK;
}

// ======================================================================
// ROM 加载 / 电源 / 复位
// ======================================================================

NES_API int nes_load_database(nes_t* nes, const uint8_t* xml, size_t size)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;
	if (!xml || size == 0)
		return NES_ERR_INVALID_PARAM;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// 内存字节 → std::istream → ImageDatabase::Load (供 load_rom 查 profile)
	nes_stream::MemIStream stream(xml, size);
	Nes::Api::Cartridge::Database db = ctx->cartridge.GetDatabase();
	return static_cast<int>(db.Load(stream.stream()));
}

NES_API int nes_load_rom(nes_t* nes, const uint8_t* data, size_t size, nes_rom_info* info_out)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;
	if (!data || size == 0)
		return NES_ERR_INVALID_PARAM;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// 内存字节 → std::istream → Machine::Load (自动识别 iNES/UNIF/FDS/NSF/XML)
	nes_stream::MemIStream stream(data, size);
	Nes::Result result = ctx->machine.Load(
		stream.stream(),
		static_cast<Nes::Api::Machine::FavoredSystem>(ctx->cfg.favored_system));

	// 负值 = 失败, 直接透传 Nestopia Result (与 nes_err 同值)
	if (NES_FAILED(result))
		return static_cast<int>(result);

	ctx->input.AutoSelectControllers();
	ctx->input.AutoSelectAdapter();

	// 上电 (内部触发 fileIoCallback LOAD_BATTERY → on_file_io)
	result = ctx->machine.Power(true);

	// Power 可能重建 APU/渲染状态, 重新施加视频/音频配置
	apply_render_state(ctx);
	apply_audio_config(ctx);

	if (info_out)
	{
		const int rc = fill_rom_info(ctx, info_out);
		if (NES_FAILED(rc))
			return rc;
	}

	return static_cast<int>(result);
}

NES_API int nes_load_rom_patched(nes_t* nes,
                                 const uint8_t* rom, size_t rom_size,
                                 const uint8_t* patch, size_t patch_size, /* IPS/UPS */
                                 nes_rom_info* info_out)
{
	(void)nes; (void)rom; (void)rom_size; (void)patch; (void)patch_size; (void)info_out;
	// 阶段1: IPS/UPS 软补丁打通后再开 (t2b §3)
	return NES_ERR_NOT_IMPLEMENTED;
}

NES_API int nes_unload(nes_t* nes)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	const Nes::Result r = ctx->machine.Power(false); // 触发 SAVE_BATTERY
	ctx->machine.Unload();
	return static_cast<int>(r);
}

NES_API int nes_power(nes_t* nes, int on)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	// on=0 (PowerOff) 内部触发 SAVE_BATTERY
	return static_cast<int>(ctx->machine.Power(on != 0));
}

NES_API int nes_reset(nes_t* nes, int hard)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	return static_cast<int>(ctx->machine.Reset(hard != 0));
}

// ======================================================================
// 回调注册 (进程级单例; 静态分发器已注册, 这里只更新 ctx 字段)
// ======================================================================

NES_API int nes_set_log_callback(nes_t* nes, nes_log_fn fn, void* userdata)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	ctx->log_cb = fn;
	ctx->log_userdata = userdata;
	return NES_OK;
}

NES_API int nes_set_file_io_callback(nes_t* nes, nes_file_io_fn fn, void* userdata)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	ctx->file_io_cb = fn;
	ctx->file_io_userdata = userdata;
	return NES_OK;
}

NES_API int nes_set_event_callback(nes_t* nes, nes_event_fn fn, void* userdata)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	ctx->event_cb = fn;
	ctx->event_userdata = userdata;
	return NES_OK;
}

NES_API int nes_set_question_callback(nes_t* nes, nes_question_fn fn, void* userdata)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	ctx->question_cb = fn;
	ctx->question_userdata = userdata;
	return NES_OK;
}

// ======================================================================
// 即时存档 / 电池 (Task 5, t2b §2)
// ======================================================================

// ======================================================================
// 运行 (帧循环) / 视频 / 音频 格式 (Task 4, t2b §2)
// ======================================================================

/*
 * nes_run_frames — 核心时序点: 音频是主时钟 (spec §7.2)。
 * 宿主按 AudioTrack 消费量反推 max_frames, 本函数只「跑至多 N 帧 + 交还样本」,
 * 不 sleep / busy-wait (阻塞发生在宿主 AudioTrack.write)。
 *
 * 每帧样本数 per_frame = sample_rate / 帧率 (NTSC 60.0988 / PAL 50.0070, 取整)。
 * 内核保证写满请求的 length[0] (NstApu.cpp streamed = length[0]+length[1]),
 * 不足/超额样本由内核内部缓冲累积 (NstApiSound.hpp:73-79)。
 */
NES_API int nes_run_frames(nes_t* nes, uint32_t max_frames,
                           int16_t* audio_out, uint32_t audio_cap_samples,
                           uint32_t* frames_run, uint32_t* samples_written)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;
	if (!audio_out || audio_cap_samples == 0)
		return NES_ERR_INVALID_PARAM;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	if (frames_run)      *frames_run = 0;
	if (samples_written) *samples_written = 0;

	// 模式正确帧率 (PAL 用 50.0070, 否则按 NTSC 60.0988; 用错会漂移)
	const double fps = (ctx->machine.GetMode() == Nes::Api::Machine::PAL) ? 50.0070 : 60.0988;
	const uint32_t per_frame = static_cast<uint32_t>(ctx->cfg.sample_rate / fps);
	if (per_frame == 0)
		return NES_ERR_INVALID_PARAM;

	// 视频输出: 正 pitch、自顶向下; 不设 lock/unlock 回调
	// (Output::Locker 无回调时仅要求 pixels && pitch, NstApiVideo.hpp:122-128)
	Nes::Core::Video::Output vo(
		ctx->framebuffer,
		static_cast<long>(kScreenWidth * pixfmt_bpp(ctx->cfg.pixfmt)));

	// 输入: Pad::callback 已在 nes_create 注册 (推→拉桥), 此处只传对象;
	// Controllers 默认构造为空, 设备状态 (Pad::state 等) 持在核心 Device 内, 每帧重建无副作用
	Nes::Core::Input::Controllers pads;

	// 音频输出: 无环形缓冲 (samples[1]/length[1] = 0)
	Nes::Core::Sound::Output so(audio_out, per_frame);
	so.samples[1] = nullptr;
	so.length[1]  = 0;

	uint32_t written = 0;
	uint32_t i = 0;
	Nes::Result last = Nes::RESULT_OK;

	for (i = 0; i < max_frames; ++i)
	{
		// 音频缓冲满 → 提前停止 (写成 - written 避免 uint32 溢出)
		if (per_frame > audio_cap_samples - written)
			break;

		// 每帧把输出位置重新指到当前写入偏移 (内核写你给的位置)
		so.samples[0] = audio_out + written;
		so.length[0]  = per_frame;

		const Nes::Result r = ctx->emulator.Execute(&vo, &so, &pads);
		if (r != Nes::RESULT_OK)
			last = r;
		written += so.length[0];
	}

	if (frames_run)      *frames_run = i;
	if (samples_written) *samples_written = written;

	return NES_FAILED(last) ? static_cast<int>(last) : NES_OK;
}

NES_API int nes_set_video_format(nes_t* nes, nes_pixfmt format, nes_video_filter filter)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// phase0: 仅 RGB565 + FILTER_NONE (t2b §3); NTSC 滤波/其他 pixfmt 阶段后再开
	if (format != NES_PIXFMT_RGB565 || filter != NES_FILTER_NONE)
		return NES_ERR_NOT_IMPLEMENTED;

	// 先同步重分配后缓冲 (尺寸不变则复用; realloc 失败旧缓冲仍有效, 状态不变)
	const size_t fb_size = kScreenWidth * kScreenHeight * pixfmt_bpp(format);
	if (fb_size != ctx->framebuffer_size)
	{
		uint8_t* fb = static_cast<uint8_t*>(std::realloc(ctx->framebuffer, fb_size));
		if (!fb)
			return NES_ERR_OUT_OF_MEMORY;
		ctx->framebuffer = fb;
		ctx->framebuffer_size = fb_size;
	}

	ctx->cfg.pixfmt = format;
	apply_render_state(ctx); // bits.count=16, r=0xF800 g=0x07E0 b=0x001F, 256x240, FILTER_NONE
	update_video_frame(ctx);

	return NES_OK;
}

NES_API const nes_video_frame* nes_get_video_frame(const nes_t* nes)
{
	if (!nes)
		return nullptr;
	const nes_ctx* ctx = reinterpret_cast<const nes_ctx*>(nes);
	return &ctx->video_frame;
}

NES_API int nes_set_audio_format(nes_t* nes, uint32_t sample_rate, int stereo)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// phase0 仅 mono (t2b §3); stereo 返回 NOT_IMPLEMENTED
	if (stereo != 0)
		return NES_ERR_NOT_IMPLEMENTED;

	// 内核合法范围 44100–96000 (NstApiSound.hpp SetSampleRate)
	if (sample_rate < 44100 || sample_rate > 96000)
		return NES_ERR_INVALID_PARAM;

	const Nes::Result r = ctx->sound.SetSampleRate(sample_rate);
	if (NES_FAILED(r))
		return static_cast<int>(r);

	ctx->cfg.sample_rate = sample_rate;
	ctx->sound.SetSpeaker(Nes::Api::Sound::SPEAKER_MONO); // 保持 mono

	return NES_OK;
}

NES_API void nes_set_input(nes_t* nes, uint32_t port, uint32_t buttons)
{
	if (!nes || port >= NES_PORT_MAX)
		return;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	// 推→拉桥: on_pad_poll 每帧从这里取 (t2b §0 #0); 非回调路径, 无需重入检查
	ctx->input_buttons[port].store(buttons, std::memory_order_relaxed);
}

NES_API void nes_clear_input(nes_t* nes)
{
	if (!nes)
		return;
	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	for (auto& b : ctx->input_buttons)
		b.store(0, std::memory_order_relaxed);
}

/*
 * nes_save_state — Machine::SaveState(ostream, USE_COMPRESSION) (t2b §2) + S1-2 包装。
 * 先经 nes_stream::GrowableOStream 全量产出裸 NST 字节, 再套 FLYNST1 安全包装头
 * (magic+version+core_version+rom_sha1+payload_len+crc32) 拷进调用方 out[0..cap)。
 * GrowableOStream 与 MemOStream 一样实现 seekp (状态存档器在 NstState.cpp
 * Saver::End 里回填 chunk 长度, 没有 seek 的话首个 End() 就抛 CORRUPT_FILE)。
 *
 * 返回语义:
 *   - 成功: *written = *needed = 81 + 裸 NST 长度; 返回 NES_OK。
 *   - 裸 NST 产出失败 (未上电等): 透传 SaveState 的 Result (与 nes_err 同值,
 *     负值或警告照原样返回), *written = *needed = 0。
 *   - cap 不足: NES_ERR_BUFFER_TOO_SMALL; *written = 0, *needed = 81 + 裸长度。
 *     由于包装前已全量产出, *needed 是精确大小 (旧版 MemOStream 直写调用方
 *     缓冲时只是下界), 宿主按 *needed 精确分配一次即可重试成功。
 *
 * 大小探测 (out==NULL && cap==0): 合法, 照常全量产出后返回 BUFFER_TOO_SMALL,
 * *needed 即精确大小 —— 比旧版「下界 + 加倍重试」更适合一次性分配。
 */
NES_API int nes_save_state(nes_t* nes, uint8_t* out, size_t cap, size_t* written, size_t* needed)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;
	if (!written || !needed)
		return NES_ERR_INVALID_PARAM;
	if (cap > 0 && !out)
		return NES_ERR_INVALID_PARAM;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// 1. 全量产出裸 NST 到可增长缓冲
	nes_stream::GrowableOStream raw;
	const Nes::Result result = ctx->machine.SaveState(raw.stream(), Nes::Api::Machine::USE_COMPRESSION);
	if (result != Nes::RESULT_OK)
	{
		*written = 0;
		*needed  = 0;
		return static_cast<int>(result);
	}

	// 2. 源 ROM SHA1 (profile 缺失 → 全 0, 加载时跳过 SHA1 校验)
	char sha1_hex[41];
	const Nes::Api::Cartridge::Profile* profile = ctx->cartridge.GetProfile();
	if (profile)
	{
		char crc_buf[9];
		profile->hash.Get(sha1_hex, crc_buf);
		sha1_hex[40] = '\0'; // Hash::Get 只写 40 hex、不写 NUL (同 fill_rom_info)
	}
	else
	{
		std::memset(sha1_hex, 0, sizeof(sha1_hex));
	}

	// 3. 包装进调用方缓冲 (cap 不足 → BUFFER_TOO_SMALL + 精确 *needed)
	const std::vector<uint8_t>& raw_data = raw.data();
	return flynes_state::wrap(raw_data.data(), raw_data.size(), sha1_hex,
	                          out, cap, written, needed);
}

/*
 * nes_load_state — 兼容 FLYNST1 包装档与旧版裸 NST 档 (S1-2)。
 *   - 包装档: unwrap 校验 magic/version/len/crc32/sha1; 失败返回具体错误码
 *     (UNSUPPORTED_VER / INVALID_CRC / STATE_ROM_MISMATCH / CORRUPT_FILE),
 *     此时不触碰机器状态;
 *   - 魔数不符 → 视为旧版裸 NST, 原样喂 Machine::LoadState (旧档兼容路径);
 *   - 通过校验后: Machine::LoadState(MemIStream(payload)), Result 与 nes_err
 *     同值直接透传 (RESULT_ERR_INVALID_CRC(-7) 即 NES_ERR_INVALID_CRC)。
 */
NES_API int nes_load_state(nes_t* nes, const uint8_t* in, size_t size)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;
	if (!in || size == 0)
		return NES_ERR_INVALID_PARAM;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);

	// 当前 ROM SHA1 (profile 缺失 → 全 0 → unwrap 跳过 SHA1 校验)
	char current_sha1[41];
	const Nes::Api::Cartridge::Profile* profile = ctx->cartridge.GetProfile();
	if (profile)
	{
		char crc_buf[9];
		profile->hash.Get(current_sha1, crc_buf);
		current_sha1[40] = '\0';
	}
	else
	{
		std::memset(current_sha1, 0, sizeof(current_sha1));
	}

	const uint8_t* payload = nullptr;
	size_t payload_len = 0;
	bool is_wrapped = false;
	const int rc = flynes_state::unwrap(in, size, current_sha1, &payload, &payload_len, &is_wrapped);
	if (rc != NES_OK)
		return rc;

	// 包装档 → 校验后的 payload; 旧版裸 NST → 原输入
	const uint8_t* data = is_wrapped ? payload : in;
	const size_t len    = is_wrapped ? payload_len : size;

	nes_stream::MemIStream stream(data, len);
	const Nes::Result result = ctx->machine.LoadState(stream.stream());
	return static_cast<int>(result);
}

NES_API int nes_battery_flush(nes_t* nes)
{
	(void)nes;
	// 1.53.2 无公开「仅存电池」API (t2b §2): 安全点请用 nes_save_state
	return NES_ERR_NOT_IMPLEMENTED;
}

// ======================================================================
// 金手指 (Task 5, t2b §2)
// ======================================================================

/*
 * nes_cheat_add — Cheats::SetCode(Code(addr,value,compare,useCompare))。
 * 同地址已有码会被替换 (NstApiCheats.hpp:101)。
 */
NES_API int nes_cheat_add(nes_t* nes, uint16_t addr, uint8_t value, uint8_t compare, int use_compare)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	const Nes::Api::Cheats::Code code(addr, value, compare, use_compare != 0);
	return static_cast<int>(ctx->cheats.SetCode(code));
}

NES_API int nes_cheat_remove(nes_t* nes, uint32_t index)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	return static_cast<int>(ctx->cheats.DeleteCode(index));
}

NES_API int nes_cheat_clear(nes_t* nes)
{
	if (!nes)
		return NES_ERR_INVALID_PARAM;
	if (in_callback())
		return NES_ERR_REENTRANT;

	nes_ctx* ctx = reinterpret_cast<nes_ctx*>(nes);
	return static_cast<int>(ctx->cheats.ClearCodes());
}

NES_API int nes_cheat_count(const nes_t* nes, uint32_t* count)
{
	if (!nes || !count)
		return NES_ERR_INVALID_PARAM;

	const nes_ctx* ctx = reinterpret_cast<const nes_ctx*>(nes);
	*count = static_cast<uint32_t>(ctx->cheats.NumCodes());
	return NES_OK;
}

/*
 * nes_cheat_encode — 静态编解码 (NstApiCheats.hpp:173/191), 不依赖实例状态。
 * GG/PAR 输出均 ≤ 8 字符 + NUL (char[9]); cap < 9 → NES_ERR_BUFFER_TOO_SMALL。
 */
NES_API int nes_cheat_encode(nes_t* nes, nes_cheat_format fmt,
                             uint16_t addr, uint8_t value, uint8_t compare, int use_compare,
                             char* out, size_t cap)
{
	(void)nes; // 静态编解码, 无需实例
	if (fmt != NES_CHEAT_GAME_GENIE && fmt != NES_CHEAT_PRO_ACTION_ROCKY)
		return NES_ERR_INVALID_PARAM;
	if (!out)
		return NES_ERR_INVALID_PARAM;
	if (cap < 9) // 8 字符 + NUL
		return NES_ERR_BUFFER_TOO_SMALL;

	const Nes::Api::Cheats::Code code(addr, value, compare, use_compare != 0);
	char buf[9];
	const Nes::Result result = (fmt == NES_CHEAT_GAME_GENIE)
		? Nes::Api::Cheats::GameGenieEncode(code, buf)
		: Nes::Api::Cheats::ProActionRockyEncode(code, buf);
	if (NES_FAILED(result))
		return static_cast<int>(result);

	std::memcpy(out, buf, sizeof(buf)); // 含 NUL
	return NES_OK;
}

/*
 * nes_cheat_decode — code 为 NUL 结尾字符串; 输出指针可传 NULL 跳过对应字段。
 */
NES_API int nes_cheat_decode(nes_t* nes, nes_cheat_format fmt, const char* code,
                             uint16_t* addr, uint8_t* value, uint8_t* compare, int* use_compare)
{
	(void)nes; // 静态编解码, 无需实例
	if (fmt != NES_CHEAT_GAME_GENIE && fmt != NES_CHEAT_PRO_ACTION_ROCKY)
		return NES_ERR_INVALID_PARAM;
	if (!code)
		return NES_ERR_INVALID_PARAM;

	Nes::Api::Cheats::Code decoded;
	const Nes::Result result = (fmt == NES_CHEAT_GAME_GENIE)
		? Nes::Api::Cheats::GameGenieDecode(code, decoded)
		: Nes::Api::Cheats::ProActionRockyDecode(code, decoded);
	if (NES_FAILED(result))
		return static_cast<int>(result);

	if (addr)        *addr        = decoded.address;
	if (value)       *value       = decoded.value;
	if (compare)     *compare     = decoded.compare;
	if (use_compare) *use_compare = decoded.useCompare ? 1 : 0;
	return NES_OK;
}

// ======================================================================
// 内存 (Task 5, t2b §2)
// ======================================================================

NES_API int nes_mem_read(nes_t* nes, uint16_t addr, uint8_t* out)
{
	(void)nes; (void)addr; (void)out;
	// 内核无总线 peek API (t2b §2), phase0 不实现
	return NES_ERR_NOT_IMPLEMENTED;
}

NES_API int nes_mem_write(nes_t* nes, uint16_t addr, uint8_t value)
{
	(void)nes; (void)addr; (void)value;
	return NES_ERR_NOT_IMPLEMENTED;
}

/*
 * nes_get_cpu_ram — Cheats::GetRam() (NstApiCheats.hpp:164) 只读 2KB CPU RAM
 * (0x0000-0x1FFF 内部映射)。CPU 对象在 Emulator 构造时即存在, 无卡带也可查。
 */
NES_API int nes_get_cpu_ram(const nes_t* nes, const uint8_t** ram, size_t* size)
{
	if (!nes || !ram || !size)
		return NES_ERR_INVALID_PARAM;

	const nes_ctx* ctx = reinterpret_cast<const nes_ctx*>(nes);
	Nes::Api::Cheats::Ram ram_ref = ctx->cheats.GetRam(); // const uchar (&)[0x800]
	*ram  = static_cast<const uint8_t*>(ram_ref);
	*size = Nes::Api::Cheats::RAM_SIZE; // 0x800 (2KB)
	return NES_OK;
}

NES_API int nes_get_rom_info(const nes_t* nes, nes_rom_info* info)
{
	if (!nes || !info)
		return NES_ERR_INVALID_PARAM;
	const nes_ctx* ctx = reinterpret_cast<const nes_ctx*>(nes);
	return fill_rom_info(ctx, info);
}

NES_API int nes_set_fds_bios(nes_t* nes, const uint8_t* bios, size_t size)
{
	(void)nes; (void)bios; (void)size;
	return NES_ERR_NOT_IMPLEMENTED; // 阶段2 (Fds::SetBIOS)
}

NES_API int nes_fds_insert_disk(nes_t* nes, uint32_t disk, uint32_t side)
{
	(void)nes; (void)disk; (void)side;
	return NES_ERR_NOT_IMPLEMENTED; // 阶段2
}

NES_API int nes_fds_change_side(nes_t* nes)
{
	(void)nes;
	return NES_ERR_NOT_IMPLEMENTED; // 阶段2
}

NES_API int nes_fds_eject_disk(nes_t* nes)
{
	(void)nes;
	return NES_ERR_NOT_IMPLEMENTED; // 阶段2
}

NES_API int nes_fds_disk_count(const nes_t* nes, uint32_t* disks, uint32_t* sides)
{
	(void)nes; (void)disks; (void)sides;
	return NES_ERR_NOT_IMPLEMENTED; // 阶段2
}

} // extern "C"
