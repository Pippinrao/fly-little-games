#include "portability_smoke.hpp"

#include "fixture_sha256.hpp"
#include <flynes/flynes_app.h>
#include <nes/nes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

extern "C" int flynes_ios_abi_layout_is_current(void);

namespace flynes::ios {
namespace {

constexpr std::string_view kExpectedFullFileSha256 =
    "EE51CD9562F28195BA015D9857C6C4FC9BF67CDFB213E95F655E586B92195173";
constexpr std::string_view kExpectedCoreCartridgeSha1 =
    "81A5AC43BE5BD26615FC89E7EFE0337E309C16E7";
constexpr std::uint32_t kSmokeFrameCount = 60u;
constexpr std::uint32_t kAudioCapacityPerFrame = 1024u;
constexpr std::size_t kMaxSmokeStateBytes = 8u * 1024u * 1024u;
constexpr std::int16_t kAudioSentinel = static_cast<std::int16_t>(0x5A5A);
constexpr std::array<std::uint32_t, NES_PORT_MAX> kInputMasks = {
    NES_BTN_A,
    NES_BTN_B,
    NES_BTN_SELECT,
    NES_BTN_START,
};

using AppPtr = std::unique_ptr<fly_app_t, decltype(&fly_app_destroy)>;
using SnapshotPtr =
    std::unique_ptr<fly_catalog_snapshot_t, decltype(&fly_catalog_snapshot_release)>;
using NesPtr = std::unique_ptr<nes_t, decltype(&nes_destroy)>;

PortabilitySmokeResult fail(std::string stage, int code = 0)
{
    PortabilitySmokeResult result;
    std::ostringstream report;
    report << "FLYNES_IOS_SMOKE_FAIL stage=" << stage << " code=" << code;
    result.report = report.str();
    return result;
}

bool root_length_is_valid(std::string_view root) noexcept
{
    return !root.empty() && root.size() <= FLY_APP_ROOT_MAX_UTF8_BYTES &&
           root.size() <= std::numeric_limits<std::uint32_t>::max();
}

std::string bounded_c_string(const char* begin, std::size_t capacity)
{
    const char* const end = begin + capacity;
    const char* const terminator = std::find(begin, end, '\0');
    return std::string(begin, terminator);
}

} // namespace

PortabilitySmokeResult run_portability_smoke(const PortabilitySmokeInput& input)
{
    try
    {
        if (flynes_ios_abi_layout_is_current() != 1)
            return fail("abi_layout");
        if (!root_length_is_valid(input.data_root) || !root_length_is_valid(input.cache_root))
            return fail("app_roots");
        if (input.rom_bytes == nullptr || input.rom_size < 16u ||
            std::memcmp(input.rom_bytes, "NES\x1A", 4u) != 0)
            return fail("rom_header");

        const std::string full_hash = fixture_sha256_hex(input.rom_bytes, input.rom_size);
        if (full_hash != kExpectedFullFileSha256)
            return fail("rom_full_file_sha256");
        const std::uint32_t expected_api_version =
            (NES_API_VERSION_MAJOR << 16u) |
            (NES_API_VERSION_MINOR << 8u) |
            NES_API_VERSION_PATCH;
        const char* const core_version = nes_core_version();
        if (nes_api_version() != expected_api_version || core_version == nullptr ||
            std::string(core_version) != "1.53.2")
            return fail("nes_version");

        fly_platform_capabilities capabilities{};
        capabilities.struct_size = FLY_PLATFORM_CAPABILITIES_V1_SIZE;
        capabilities.version = FLY_PLATFORM_CAPABILITIES_VERSION_1;

        fly_app_config app_config{};
        app_config.struct_size = FLY_APP_CONFIG_V1_SIZE;
        app_config.version = FLY_APP_CONFIG_VERSION_1;
        app_config.data_root_utf8 = input.data_root.data();
        app_config.cache_root_utf8 = input.cache_root.data();
        app_config.platform_capabilities = &capabilities;
        app_config.data_root_utf8_length =
            static_cast<std::uint32_t>(input.data_root.size());
        app_config.cache_root_utf8_length =
            static_cast<std::uint32_t>(input.cache_root.size());

        fly_app_t* raw_app = nullptr;
        const fly_result app_result = fly_app_create(&app_config, &raw_app);
        if (app_result != FLY_RESULT_OK || raw_app == nullptr)
            return fail("fly_app_create", app_result);
        AppPtr app(raw_app, &fly_app_destroy);

        fly_catalog_snapshot_t* raw_snapshot = nullptr;
        const fly_result snapshot_result = fly_catalog_snapshot(app.get(), &raw_snapshot);
        if (snapshot_result != FLY_RESULT_OK || raw_snapshot == nullptr)
            return fail("fly_catalog_snapshot", snapshot_result);
        SnapshotPtr snapshot(raw_snapshot, &fly_catalog_snapshot_release);

        // nes_create(NULL) intentionally exercises the documented default and
        // avoids treating the current draft nes_config layout as a new contract.
        NesPtr nes(nes_create(nullptr), &nes_destroy);
        if (!nes)
            return fail("nes_create");

        nes_rom_info rom_info{};
        const int load_result =
            nes_load_rom(nes.get(), input.rom_bytes, input.rom_size, &rom_info);
        if (load_result < NES_OK)
            return fail("nes_load_rom", load_result);

        const std::string core_hash = bounded_c_string(rom_info.sha1, sizeof(rom_info.sha1));
        if (core_hash != kExpectedCoreCartridgeSha1 || rom_info.mapper != 0u ||
            rom_info.prg_size != 32768u || rom_info.chr_size != 8192u)
            return fail("core_cartridge_sha1");

        const int audio_format_result = nes_set_audio_format(nes.get(), 48000u, 0);
        if (audio_format_result != NES_OK)
            return fail("nes_set_audio_format", audio_format_result);

        std::vector<std::int16_t> audio(
            static_cast<std::size_t>(kSmokeFrameCount) * kAudioCapacityPerFrame,
            kAudioSentinel);
        std::uint32_t frames_run = 0u;
        std::uint32_t samples_written = 0u;
        for (std::uint32_t port = 0u; port < kInputMasks.size(); ++port)
            nes_set_input(nes.get(), port, kInputMasks[port]);
        const int run_result = nes_run_frames(
            nes.get(),
            kSmokeFrameCount,
            audio.data(),
            static_cast<std::uint32_t>(audio.size()),
            &frames_run,
            &samples_written);
        if (run_result != NES_OK || frames_run != kSmokeFrameCount ||
            samples_written == 0u || samples_written > audio.size())
        {
            nes_clear_input(nes.get());
            return fail("nes_run_frames", run_result);
        }
        const auto audio_end =
            audio.begin() + static_cast<std::ptrdiff_t>(samples_written);
        const std::uint32_t audio_samples_changed = static_cast<std::uint32_t>(
            std::count_if(audio.begin(), audio_end, [](std::int16_t sample) {
                return sample != kAudioSentinel;
            }));
        if (audio_samples_changed == 0u ||
            !std::all_of(audio_end, audio.end(), [](std::int16_t sample) {
                return sample == kAudioSentinel;
            }))
        {
            nes_clear_input(nes.get());
            return fail("nes_audio_write_bounds");
        }

        nes_input_sample input_sample{};
        input_sample.struct_size = sizeof(input_sample);
        const int input_sample_result = nes_get_last_input_sample(nes.get(), &input_sample);
        nes_clear_input(nes.get());
        if (input_sample_result != NES_OK || input_sample.version != NES_STRUCT_VERSION ||
            input_sample.generation != kInputMasks.size() ||
            !std::equal(kInputMasks.begin(), kInputMasks.end(), input_sample.pad_bits) ||
            input_sample.native_monotonic_ns == 0u)
            return fail("nes_input_sample", input_sample_result);

        std::vector<std::uint16_t> pixels(256u * 240u);
        nes_video_snapshot video_snapshot{};
        video_snapshot.struct_size = sizeof(video_snapshot);
        const int video_result = nes_copy_video_frame(
            nes.get(), pixels.data(), pixels.size() * sizeof(pixels[0]), &video_snapshot);
        const std::uint64_t non_black_pixels = static_cast<std::uint64_t>(
            std::count_if(pixels.begin(), pixels.end(), [](std::uint16_t pixel) {
                return pixel != 0u;
            }));
        if (video_result != NES_OK || video_snapshot.version != NES_STRUCT_VERSION ||
            video_snapshot.sequence == 0u || video_snapshot.width != 256u ||
            video_snapshot.height != 240u || video_snapshot.format != NES_PIXFMT_RGB565 ||
            video_snapshot.pitch != 512 ||
            video_snapshot.bytes_written != pixels.size() * sizeof(pixels[0]) ||
            video_snapshot.native_monotonic_ns == 0u || non_black_pixels == 0u)
            return fail("nes_copy_video_frame", video_result);

        std::size_t probe_written = std::numeric_limits<std::size_t>::max();
        std::size_t state_size = 0u;
        const int probe_result =
            nes_save_state(nes.get(), nullptr, 0u, &probe_written, &state_size);
        if (probe_result != NES_ERR_BUFFER_TOO_SMALL || probe_written != 0u ||
            state_size == 0u || state_size > kMaxSmokeStateBytes)
            return fail("nes_save_state_probe", probe_result);

        std::vector<std::uint8_t> state(state_size);
        std::size_t state_written = 0u;
        std::size_t state_needed = 0u;
        const int save_result = nes_save_state(
            nes.get(), state.data(), state.size(), &state_written, &state_needed);
        if (save_result != NES_OK || state_written != state.size() ||
            state_needed != state.size())
            return fail("nes_save_state", save_result);

        std::uint32_t mutation_frames = 0u;
        std::uint32_t mutation_samples = 0u;
        const int mutation_result = nes_run_frames(
            nes.get(),
            3u,
            audio.data(),
            static_cast<std::uint32_t>(audio.size()),
            &mutation_frames,
            &mutation_samples);
        if (mutation_result != NES_OK || mutation_frames != 3u)
            return fail("nes_run_after_save", mutation_result);

        const int load_state_result = nes_load_state(nes.get(), state.data(), state.size());
        if (load_state_result != NES_OK)
            return fail("nes_load_state", load_state_result);

        std::vector<std::uint8_t> restored_state(state.size());
        std::size_t restored_written = 0u;
        std::size_t restored_needed = 0u;
        const int restored_save_result = nes_save_state(
            nes.get(),
            restored_state.data(),
            restored_state.size(),
            &restored_written,
            &restored_needed);
        if (restored_save_result != NES_OK || restored_written != state_written ||
            restored_needed != state_needed || restored_state != state)
            return fail("nes_state_round_trip", restored_save_result);

        // Prove the documented snapshot ownership: it remains queryable after
        // its originating app has been destroyed, then is explicitly released.
        nes.reset();
        app.reset();
        std::uint64_t catalog_generation = std::numeric_limits<std::uint64_t>::max();
        std::uint64_t catalog_count = std::numeric_limits<std::uint64_t>::max();
        const fly_result generation_result =
            fly_catalog_snapshot_generation(snapshot.get(), &catalog_generation);
        const fly_result count_result =
            fly_catalog_snapshot_count(snapshot.get(), &catalog_count);
        if (generation_result != FLY_RESULT_OK || count_result != FLY_RESULT_OK ||
            catalog_generation != 0u || catalog_count != 0u)
            return fail("fly_snapshot_after_destroy");
        snapshot.reset();

        PortabilitySmokeResult result;
        result.exit_code = 0;
        result.frames_run = frames_run;
        result.audio_samples = samples_written;
        result.audio_samples_changed = audio_samples_changed;
        result.state_bytes = state_written;
        result.input_generation = input_sample.generation;
        result.input_monotonic_ns = input_sample.native_monotonic_ns;
        std::copy(kInputMasks.begin(), kInputMasks.end(), result.input_pad_bits.begin());
        result.video_sequence = video_snapshot.sequence;
        result.video_monotonic_ns = video_snapshot.native_monotonic_ns;
        result.non_black_pixels = non_black_pixels;
        result.catalog_generation = catalog_generation;
        result.catalog_count = catalog_count;
        result.full_file_sha256 = full_hash;
        result.core_cartridge_sha1 = core_hash;

        std::ostringstream report;
        report << "FLYNES_IOS_SMOKE_PASS"
               << " frames=" << result.frames_run
               << " audio_samples=" << result.audio_samples
               << " audio_samples_changed=" << result.audio_samples_changed
               << " state_bytes=" << result.state_bytes
               << " input_generation=" << result.input_generation
               << " input_monotonic_ns=" << result.input_monotonic_ns
               << " input_pad0=" << result.input_pad_bits[0]
               << " input_pad1=" << result.input_pad_bits[1]
               << " input_pad2=" << result.input_pad_bits[2]
               << " input_pad3=" << result.input_pad_bits[3]
               << " video_sequence=" << result.video_sequence
               << " video_monotonic_ns=" << result.video_monotonic_ns
               << " non_black_pixels=" << result.non_black_pixels
               << " catalog_generation=" << result.catalog_generation
               << " catalog_count=" << result.catalog_count
               << " full_file_sha256=" << result.full_file_sha256
               << " core_cartridge_sha1=" << result.core_cartridge_sha1;
        result.report = report.str();
        return result;
    }
    catch (const std::bad_alloc&)
    {
        return fail("out_of_memory");
    }
    catch (...)
    {
        return fail("unexpected_exception");
    }
}

} // namespace flynes::ios
