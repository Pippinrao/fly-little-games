#include <flynes/flynes_runtime.h>
#include <nes/nes.h>
#include "../src/session/wire/sha256.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <new>
#include <string>
#include <vector>

#ifdef FLYNES_TEST_WRAP_SOURCE_INFO
// Mapping-only test seam. It changes a copied query result, never a real core's
// mode; this is deliberately not evidence of PAL execution in production.
namespace { thread_local bool report_pal_for_mapping_test = false; }
extern "C" int __real_nes_get_rom_info(const nes_t*, nes_rom_info*);
extern "C" int __wrap_nes_get_rom_info(const nes_t* core, nes_rom_info* out)
{
    const auto result = __real_nes_get_rom_info(core, out);
    if (result >= 0 && report_pal_for_mapping_test) out->region_ntsc = 0;
    return result;
}
#endif

// Test-only allocation observation, enabled solely around a checkpoint loader
// call. Malformed fixtures retain normal save sizes; no oversized allocations.
namespace {
thread_local bool observe_allocations = false;
thread_local std::size_t observed_allocations = 0;
struct AllocationObservation
{
    explicit AllocationObservation(bool enabled)
    {
        observed_allocations = 0;
        observe_allocations = enabled;
    }
    ~AllocationObservation() { observe_allocations = false; }
    AllocationObservation(const AllocationObservation&) = delete;
    AllocationObservation& operator=(const AllocationObservation&) = delete;
};
}
void* operator new(std::size_t size)
{
    void* memory = std::malloc(size ? size : 1);
    if (!memory) throw std::bad_alloc();
    if (observe_allocations) ++observed_allocations;
    return memory;
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* memory) noexcept { std::free(memory); }
void operator delete[](void* memory) noexcept { std::free(memory); }
void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::vector<std::uint8_t> read_file(const char* path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input)
    {
        return {};
    }
    input.seekg(0, std::ios::end);
    const std::streamoff size = input.tellg();
    if (size <= 0)
    {
        return {};
    }
    input.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    input.read(reinterpret_cast<char*>(bytes.data()), size);
    if (!input)
    {
        return {};
    }
    return bytes;
}

int hex_nibble(char value)
{
    if (value >= '0' && value <= '9')
    {
        return value - '0';
    }
    if (value >= 'A' && value <= 'F')
    {
        return value - 'A' + 10;
    }
    if (value >= 'a' && value <= 'f')
    {
        return value - 'a' + 10;
    }
    return -1;
}

bool decode_sha256_hex(const char* hex, std::uint8_t out[32])
{
    for (int index = 0; index < 32; ++index)
    {
        const int high = hex_nibble(hex[index * 2]);
        const int low = hex_nibble(hex[index * 2 + 1]);
        if (high < 0 || low < 0)
        {
            return false;
        }
        out[index] = static_cast<std::uint8_t>((high << 4) | low);
    }
    return hex[64] == '\0';
}

fly_runtime_config make_config()
{
    fly_runtime_config config{};
    config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    config.sample_rate = 0;
    config.reserved = 0;
    return config;
}

fly_runtime_t* create_runtime(std::uint32_t sample_rate = 0)
{
    fly_runtime_config config = make_config();
    config.sample_rate = sample_rate;
    fly_runtime_t* runtime = nullptr;
    const fly_result result = fly_runtime_create(&config, &runtime);
    check(result == FLY_RESULT_OK, "fly_runtime_create succeeds");
    check(runtime != nullptr, "fly_runtime_create writes a handle");
    return runtime;
}

bool load_fixture_rom(fly_runtime_t* runtime, const std::uint8_t* expected_sha256)
{
    const std::vector<std::uint8_t> rom = read_file(FLYNES_RUNTIME_ROM_FIXTURE);
    check(!rom.empty(), "thwaite.nes fixture is readable");
    if (rom.empty())
    {
        return false;
    }
    const fly_result result =
        fly_runtime_load_rom(runtime, rom.data(), rom.size(), expected_sha256);
    if (result != FLY_RESULT_OK)
    {
        // The core returns the Nestopia result code verbatim, so print it: a
        // failing fixture must say why, not just that it failed.
        std::fprintf(stderr, "fixture load rejected: %s (rc=%d, %zu bytes, sha256 pin=%s)\n",
                     FLYNES_RUNTIME_ROM_FIXTURE, static_cast<int>(result), rom.size(),
                     expected_sha256 == nullptr ? "none" : "set");
    }
    check(result == FLY_RESULT_OK, "fly_runtime_load_rom accepts the fixture");
    return result == FLY_RESULT_OK;
}

fly_frame_input_v1 make_input(std::uint64_t epoch, std::uint64_t frame)
{
    fly_frame_input_v1 input{};
    input.struct_size = FLY_FRAME_INPUT_V1_SIZE;
    input.version = FLY_FRAME_INPUT_VERSION_1;
    input.timeline_epoch = epoch;
    input.frame_index = frame;
    input.buttons[0] = 0x01u;
    input.buttons[1] = 0x02u;
    input.buttons[2] = 0x04u;
    input.buttons[3] = 0x08u;
    input.input_sequence[0] = 10u + frame;
    input.input_sequence[1] = 20u + frame;
    input.input_sequence[2] = 30u + frame;
    input.input_sequence[3] = 40u + frame;
    input.predicted_port_mask = 0;
    input.batch_sequence = 100u + frame;
    input.capture_time_ns = 1000u + frame;
    input.receive_time_ns = 2000u + frame;
    return input;
}

fly_frame_result_v1 make_result()
{
    fly_frame_result_v1 result{};
    result.struct_size = FLY_FRAME_RESULT_V1_SIZE;
    result.version = FLY_FRAME_RESULT_VERSION_1;
    return result;
}

std::vector<std::uint8_t> save_checkpoint(fly_runtime_t* runtime)
{
    std::size_t written = 0;
    std::size_t needed = 0;
    const fly_result query =
        fly_runtime_save_checkpoint(runtime, nullptr, 0, &written, &needed);
    check(query == FLY_RESULT_BUFFER_TOO_SMALL, "checkpoint size query reports too small");
    check(needed > 0, "checkpoint size query reports a payload");
    std::vector<std::uint8_t> bytes(needed);
    written = 0;
    const fly_result saved =
        fly_runtime_save_checkpoint(runtime, bytes.data(), bytes.size(), &written, &needed);
    check(saved == FLY_RESULT_OK, "checkpoint save succeeds");
    check(written == bytes.size(), "checkpoint save fills the queried size");
    return bytes;
}

void test_create_load_and_symbols()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr)
    {
        return;
    }
    check(load_fixture_rom(runtime, nullptr), "fixture ROM loads without a SHA-256 pin");

    std::uint8_t expected[32];
    check(decode_sha256_hex(
              "EE51CD9562F28195BA015D9857C6C4FC9BF67CDFB213E95F655E586B92195173",
              expected),
          "fixture SHA-256 hex decodes");
    fly_runtime_destroy(runtime);
    runtime = create_runtime();
    if (runtime == nullptr)
    {
        return;
    }
    check(load_fixture_rom(runtime, expected), "fixture ROM loads with a matching SHA-256");

    expected[0] ^= 0xFFu;
    const std::vector<std::uint8_t> rom = read_file(FLYNES_RUNTIME_ROM_FIXTURE);
    const fly_result mismatch =
        fly_runtime_load_rom(runtime, rom.data(), rom.size(), expected);
    check(mismatch == FLY_RESULT_INVALID_ARGUMENT,
          "mismatched expected SHA-256 is rejected");
    fly_runtime_destroy(runtime);

    const std::vector<std::uint8_t> header_bytes = read_file(FLYNES_RUNTIME_HEADER);
    const std::string header_text(header_bytes.begin(), header_bytes.end());
    check(header_text.find("fly_runtime_step_frame") != std::string::npos,
          "header exports fly_runtime_step_frame");
    check(header_text.find("fly_runtime_set_input") == std::string::npos,
          "header does not export fly_runtime_set_input");
    check(header_text.find("fly_runtime_run_frame") == std::string::npos,
          "header does not export fly_runtime_run_frame");
    check(header_text.find("flynes_session") == std::string::npos,
          "runtime header does not create a session ABI");
}

void test_step_frame_and_rejection()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr || !load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }

    const std::vector<std::uint8_t> before = save_checkpoint(runtime);
    fly_frame_result_v1 missing = make_result();
    check(fly_runtime_step_frame(runtime, nullptr, &missing) == FLY_RESULT_INVALID_ARGUMENT,
          "missing input bundle is rejected");
    check(save_checkpoint(runtime) == before, "rejected missing bundle does not advance core");

    fly_frame_input_v1 jump = make_input(1, 2);
    fly_frame_result_v1 jump_result = make_result();
    check(fly_runtime_step_frame(runtime, &jump, &jump_result) == FLY_RESULT_INVALID_ARGUMENT,
          "first frame_index must be zero");
    check(save_checkpoint(runtime) == before, "rejected jump does not advance core");

    fly_frame_input_v1 input = make_input(7, 0);
    fly_frame_result_v1 result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "step_frame advances the first frame");
    check(result.timeline_epoch == 7, "result keeps the accepted timeline");
    check(result.frame_index == 0, "result reports the requested frame_index");
    check(result.post_state_frame == 0, "post-state frame matches the completed index");
    check(result.frame_sequence == 1, "core frame_sequence advances once");
    check(result.applied_input_sequence[0] == 10, "port 0 input sequence is applied");
    check(result.applied_input_sequence[1] == 20, "port 1 input sequence is applied");
    check(result.applied_input_sequence[2] == 30, "port 2 input sequence is applied");
    check(result.applied_input_sequence[3] == 40, "port 3 input sequence is applied");
    check(result.video_published == 1, "a complete video frame is published");
    check(result.pcm_published == 1, "PCM is published");
    check(result.used_predicted_ports == 0, "no predicted ports were used");
    check(result.audio_last_sample_sequence >= result.audio_first_sample_sequence,
          "audio sample sequences are ordered");
    check(result.source_time_ns == input.capture_time_ns,
          "source_time_ns is diagnostic capture time");
    check(result.produced_time_ns != 0, "produced_time_ns is a local clock sample");

    const std::vector<std::uint8_t> after_first = save_checkpoint(runtime);
    fly_frame_input_v1 repeat = make_input(7, 0);
    fly_frame_result_v1 repeat_result = make_result();
    check(fly_runtime_step_frame(runtime, &repeat, &repeat_result) == FLY_RESULT_INVALID_ARGUMENT,
          "repeated frame_index is rejected");
    check(save_checkpoint(runtime) == after_first, "repeat does not advance core");

    fly_frame_input_v1 skipped = make_input(7, 2);
    fly_frame_result_v1 skipped_result = make_result();
    check(fly_runtime_step_frame(runtime, &skipped, &skipped_result) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "jumped frame_index is rejected");
    check(save_checkpoint(runtime) == after_first, "jump does not advance core");

    fly_frame_input_v1 wrong_epoch = make_input(8, 1);
    fly_frame_result_v1 epoch_result = make_result();
    check(fly_runtime_step_frame(runtime, &wrong_epoch, &epoch_result) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "mismatched timeline_epoch is rejected");
    check(save_checkpoint(runtime) == after_first, "timeline mismatch does not advance core");

    input.predicted_port_mask = 0x2u;
    input.frame_index = 1;
    input.batch_sequence = 101;
    input.input_sequence[0] = 11;
    input.input_sequence[1] = 21;
    input.input_sequence[2] = 31;
    input.input_sequence[3] = 41;
    result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "step_frame advances the next consecutive frame");
    check(result.frame_index == 1, "second frame uses frame_index 1");
    check(result.post_state_frame == 1, "post-state follows the second frame");
    check(result.used_predicted_ports == 1, "predicted port mask is reported as used");
    check(result.applied_input_sequence[1] == 21, "predicted port still records its sequence");

    fly_runtime_destroy(runtime);
}

void test_pcm_pull()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr || !load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }

    std::array<std::int16_t, 64> empty_samples{};
    empty_samples.fill(static_cast<std::int16_t>(0x7FFF));
    fly_pcm_block_v1 empty_block{};
    empty_block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    empty_block.version = FLY_PCM_BLOCK_VERSION_1;
    check(fly_runtime_pull_pcm(runtime, empty_samples.data(),
                               static_cast<std::uint32_t>(empty_samples.size()),
                               &empty_block) == FLY_RESULT_OK,
          "empty PCM pull succeeds without blocking");
    check(empty_block.sample_count == empty_samples.size(),
          "underrun reports the requested silence count");
    bool silent = true;
    for (std::int16_t sample : empty_samples)
    {
        silent = silent && sample == 0;
    }
    check(silent, "underrun outputs silence");

    fly_frame_input_v1 input = make_input(1, 0);
    fly_frame_result_v1 result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "step_frame produces PCM");
    check(result.pcm_published == 1, "step publishes PCM");

    std::vector<std::int16_t> samples(2048, static_cast<std::int16_t>(0x7FFF));
    fly_pcm_block_v1 block{};
    block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    block.version = FLY_PCM_BLOCK_VERSION_1;
    check(fly_runtime_pull_pcm(runtime, samples.data(),
                               static_cast<std::uint32_t>(samples.size()),
                               &block) == FLY_RESULT_OK,
          "PCM pull after a frame succeeds");
    check(block.sample_count > 0, "PCM pull returns produced samples");
    check(block.first_sample_sequence == result.audio_first_sample_sequence,
          "PCM first_sample_sequence matches the frame result");
    check(block.sample_count ==
              (result.audio_last_sample_sequence - result.audio_first_sample_sequence + 1u),
          "PCM sample_count matches the published range");
    check(block.media_time_ns != 0 || block.first_sample_sequence == 0,
          "PCM reports media_time for the pulled block");

    fly_runtime_destroy(runtime);
}

void test_copy_latest_frame()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr || !load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }

    std::vector<std::uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES, 0xAAu);
    fly_latest_frame_v1 meta{};
    meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    meta.version = FLY_LATEST_FRAME_VERSION_1;
    check(fly_runtime_copy_latest_frame(runtime, pixels.data(), pixels.size(), &meta) ==
              FLY_RESULT_INVALID_STATE,
          "copy_latest_frame before a step has no complete frame");

    fly_frame_input_v1 input = make_input(3, 0);
    fly_frame_result_v1 result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "step_frame publishes a complete frame");

    check(fly_runtime_copy_latest_frame(runtime, pixels.data(), 16, &meta) ==
              FLY_RESULT_BUFFER_TOO_SMALL,
          "undersized copy does not publish a half frame");

    std::vector<std::uint8_t> first(FLY_RUNTIME_RGB565_BYTES, 0xAAu);
    std::vector<std::uint8_t> second(FLY_RUNTIME_RGB565_BYTES, 0x55u);
    fly_latest_frame_v1 first_meta = meta;
    fly_latest_frame_v1 second_meta = meta;
    check(fly_runtime_copy_latest_frame(runtime, first.data(), first.size(), &first_meta) ==
              FLY_RESULT_OK,
          "copy_latest_frame copies a complete RGB565 frame");
    check(first_meta.width == FLY_RUNTIME_FRAME_WIDTH, "frame width is 256");
    check(first_meta.height == FLY_RUNTIME_FRAME_HEIGHT, "frame height is 240");
    check(first_meta.format == FLY_RUNTIME_PIXEL_FORMAT_RGB565, "frame format is RGB565");
    check(first_meta.bytes_written == FLY_RUNTIME_RGB565_BYTES, "copy writes the full frame");
    check(first_meta.timeline_epoch == result.timeline_epoch, "copy keeps timeline_epoch");
    check(first_meta.frame_index == result.frame_index, "copy keeps authority frame_index");
    check(first_meta.frame_sequence == result.frame_sequence, "copy keeps frame_sequence");
    check(first_meta.applied_input_sequence[0] == result.applied_input_sequence[0] &&
              first_meta.applied_input_sequence[1] == result.applied_input_sequence[1] &&
              first_meta.applied_input_sequence[2] == result.applied_input_sequence[2] &&
              first_meta.applied_input_sequence[3] == result.applied_input_sequence[3],
          "copy keeps applied input sequences");
    check(first_meta.source_time_ns == result.source_time_ns, "copy keeps source_time_ns");
    check(first_meta.produced_time_ns == result.produced_time_ns,
          "copy keeps produced_time_ns");

    check(fly_runtime_copy_latest_frame(runtime, second.data(), second.size(), &second_meta) ==
              FLY_RESULT_OK,
          "a second copy also succeeds");
    check(first == second, "consumers only observe a complete frame, never a half frame");
    check(std::memcmp(&first_meta, &second_meta, sizeof(first_meta)) == 0,
          "complete-frame metadata is stable across copies");

    fly_runtime_destroy(runtime);
}

void test_rollback_and_checkpoint()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr || !load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }

    check(fly_runtime_capture_rollback(runtime, FLY_RUNTIME_ROLLBACK_SLOTS) ==
              FLY_RESULT_OUT_OF_RANGE,
          "rollback slot 12 is rejected");

    std::array<std::vector<std::uint8_t>, FLY_RUNTIME_ROLLBACK_SLOTS> captured{};
    for (std::uint32_t frame = 0; frame < FLY_RUNTIME_ROLLBACK_SLOTS; ++frame)
    {
        fly_frame_input_v1 input = make_input(1, frame);
        fly_frame_result_v1 result = make_result();
        check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
              "rollback ring can step twelve frames");
        check(fly_runtime_capture_rollback(runtime, frame) == FLY_RESULT_OK,
              "rollback capture by index succeeds");
        captured[frame] = save_checkpoint(runtime);
    }

    fly_frame_input_v1 extra = make_input(1, FLY_RUNTIME_ROLLBACK_SLOTS);
    fly_frame_result_v1 extra_result = make_result();
    check(fly_runtime_step_frame(runtime, &extra, &extra_result) == FLY_RESULT_OK,
          "simulation can continue past the rollback ring");

    check(fly_runtime_restore_rollback(runtime, 1) == FLY_RESULT_OK,
          "rollback restore by index succeeds");
    check(save_checkpoint(runtime) == captured[1],
          "restored slot 1 matches the checkpoint captured at that frame");

    fly_frame_input_v1 replay = make_input(1, 2);
    fly_frame_result_v1 replay_result = make_result();
    check(fly_runtime_step_frame(runtime, &replay, &replay_result) == FLY_RESULT_OK,
          "the next frame after restore is the captured successor");
    check(replay_result.frame_index == 2, "restore resumes at the following frame_index");

    const std::vector<std::uint8_t> restored = save_checkpoint(runtime);
    fly_runtime_t* other = create_runtime();
    if (other != nullptr && load_fixture_rom(other, nullptr))
    {
        check(fly_runtime_load_checkpoint(other, restored.data(), restored.size()) ==
                  FLY_RESULT_OK,
              "runtime checkpoint imports into another handle");
        check(save_checkpoint(other) == restored,
              "imported checkpoint is session-agnostic wrapper plus core state");
    }
    fly_runtime_destroy(other);
    fly_runtime_destroy(runtime);
}

void test_clear_input_ports()
{
    fly_runtime_t* runtime = create_runtime();
    if (runtime == nullptr || !load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }

    fly_frame_input_v1 input = make_input(1, 0);
    fly_frame_result_v1 result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "clear test starts from a stepped frame");
    const std::vector<std::uint8_t> before = save_checkpoint(runtime);

    check(fly_runtime_clear_input_ports(runtime, 0x5u, FLY_RUNTIME_CLEAR_STOP) == FLY_RESULT_OK,
          "clear_input_ports accepts STOP");
    check(fly_runtime_clear_input_ports(runtime, 0x2u, FLY_RUNTIME_CLEAR_PAUSE) == FLY_RESULT_OK,
          "clear_input_ports accepts PAUSE");
    check(fly_runtime_clear_input_ports(runtime, 0, FLY_RUNTIME_CLEAR_PEER_DISCONNECT) ==
              FLY_RESULT_OK,
          "a zero mask is a no-op");
    check(fly_runtime_clear_input_ports(runtime, 0x8u, FLY_RUNTIME_CLEAR_SEAT_REASSIGN) ==
              FLY_RESULT_OK,
          "clear_input_ports accepts SEAT_REASSIGN");

    const std::vector<std::uint8_t> after = save_checkpoint(runtime);
    check(after != before, "clearing ports changes wrapper input state");

    fly_runtime_t* replay = create_runtime();
    if (replay != nullptr && load_fixture_rom(replay, nullptr))
    {
        check(fly_runtime_load_checkpoint(replay, before.data(), before.size()) == FLY_RESULT_OK,
              "reload the pre-clear checkpoint");
        check(fly_runtime_clear_input_ports(replay, 0x5u, FLY_RUNTIME_CLEAR_STOP) == FLY_RESULT_OK,
              "clear only ports 0 and 2");
        const std::vector<std::uint8_t> masked = save_checkpoint(replay);
        check(fly_runtime_load_checkpoint(replay, before.data(), before.size()) == FLY_RESULT_OK,
              "reload again to clear a different mask");
        check(fly_runtime_clear_input_ports(replay, 0xFu, FLY_RUNTIME_CLEAR_STOP) == FLY_RESULT_OK,
              "clear every port");
        const std::vector<std::uint8_t> all = save_checkpoint(replay);
        check(masked != all, "a partial mask does not zero unmasked ports");
        check(masked != before, "the partial mask still zeros selected ports");
    }
    fly_runtime_destroy(replay);
    fly_runtime_destroy(runtime);
}

fly_runtime_frame_digest_v1 make_digest()
{
    fly_runtime_frame_digest_v1 digest{};
    digest.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE;
    digest.version = FLY_RUNTIME_FRAME_DIGEST_VERSION_1;
    return digest;
}

// Hash public observations with the session codec's separate implementation,
// never the runtime's private hashing or frame/PCM storage.
std::array<std::uint8_t, 32> observation_sha256(const std::vector<std::uint8_t>& bytes)
{
    return flynes::session::wire::sha256(bytes.data(), bytes.size());
}

void check_observation_sha256_vectors()
{
    const char* messages[] = {
        "", "abc", "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"};
    const char* expected_hex[] = {
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855",
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad",
        "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"};
    for (std::size_t index = 0; index < 3; ++index)
    {
        const std::string message = messages[index];
        std::uint8_t expected[32]{};
        check(decode_sha256_hex(expected_hex[index], expected), "SHA-256 vector decodes");
        const auto actual = observation_sha256({message.begin(), message.end()});
        check(std::memcmp(actual.data(), expected, sizeof(expected)) == 0,
              "independent SHA-256 oracle matches standard padding and multi-block vectors");
    }
}

void check_same_hashes(const fly_runtime_frame_digest_v1& a,
                       const fly_runtime_frame_digest_v1& b,
                       const char* context)
{
    if (std::memcmp(a.state_sha256, b.state_sha256, 32) != 0 ||
        std::memcmp(a.frame_sha256, b.frame_sha256, 32) != 0 ||
        std::memcmp(a.pcm_sha256, b.pcm_sha256, 32) != 0)
    {
        std::fprintf(stderr, "digest mismatch: %s (frame %llu)\n", context,
                     static_cast<unsigned long long>(a.frame_index));
        const std::uint8_t* hashes[] = {a.state_sha256, b.state_sha256,
                                       a.frame_sha256, b.frame_sha256,
                                       a.pcm_sha256, b.pcm_sha256};
        const char* names[] = {"state A", "state B", "frame A", "frame B", "PCM A", "PCM B"};
        for (std::size_t index = 0; index < 6; ++index)
        {
            std::fprintf(stderr, "%s: ", names[index]);
            for (std::size_t byte = 0; byte < 32; ++byte)
                std::fprintf(stderr, "%02x", static_cast<unsigned int>(hashes[index][byte]));
            std::fputc('\n', stderr);
        }
    }
    check(std::memcmp(a.state_sha256, b.state_sha256, 32) == 0,
          "identical simulation has identical core state SHA-256");
    check(std::memcmp(a.frame_sha256, b.frame_sha256, 32) == 0,
          "identical simulation has identical frame SHA-256");
    check(std::memcmp(a.pcm_sha256, b.pcm_sha256, 32) == 0,
          "identical simulation has identical per-frame PCM SHA-256");
}

void check_digest_rejected(fly_runtime_t* runtime, std::uint64_t epoch,
                           std::uint64_t frame, fly_result expected,
                           fly_runtime_frame_digest_v1 output = make_digest())
{
    std::memset(output.state_sha256, 0xA5, sizeof(output.state_sha256));
    std::memset(output.frame_sha256, 0xA5, sizeof(output.frame_sha256));
    std::memset(output.pcm_sha256, 0xA5, sizeof(output.pcm_sha256));
    const auto before = output;
    check(fly_runtime_copy_frame_digest(runtime, epoch, frame, &output) == expected,
          "digest rejects unavailable frame or invalid output contract");
    check(std::memcmp(&output, &before, sizeof(output)) == 0,
          "rejected digest leaves all output bytes unchanged");
}

struct PulledPcm
{
    std::array<std::int16_t, 4096> samples{};
    fly_pcm_block_v1 block{};
};

PulledPcm pull_digest_test_pcm(fly_runtime_t* runtime)
{
    PulledPcm pcm;
    pcm.block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
    pcm.block.version = FLY_PCM_BLOCK_VERSION_1;
    check(fly_runtime_pull_pcm(runtime, pcm.samples.data(),
                               static_cast<std::uint32_t>(pcm.samples.size()),
                               &pcm.block) == FLY_RESULT_OK,
          "digest test can pull real PCM");
    return pcm;
}

std::vector<std::uint8_t> make_timing_rom(bool pal)
{
    // Synthetic NES 2.0 NROM: an infinite JMP with an explicit timing field.
    std::vector<std::uint8_t> rom(16u + 16384u + 8192u, 0);
    rom[0] = 'N'; rom[1] = 'E'; rom[2] = 'S'; rom[3] = 0x1A;
    rom[4] = 1; rom[5] = 1; rom[7] = 0x08;
    rom[12] = pal ? 1 : 0;
    rom[16] = 0x4C; rom[17] = 0; rom[18] = 0x80;
    for (std::size_t vector = 0x3FFAu; vector <= 0x3FFEu; vector += 2)
        rom[16u + vector + 1u] = 0x80;
    return rom;
}

fly_runtime_source_timing_v1 make_timing()
{
    fly_runtime_source_timing_v1 out{};
    out.struct_size = FLY_RUNTIME_SOURCE_TIMING_V1_SIZE;
    out.version = FLY_RUNTIME_SOURCE_TIMING_VERSION_1;
    return out;
}

void check_timing_rejected(fly_runtime_t* runtime, fly_result expected,
                           fly_runtime_source_timing_v1 out = make_timing())
{
    out.source_region = 0xA5A5A5A5u;
    out.frame_rate_numerator = 0xA5A5A5A5u;
    out.frame_rate_denominator = 0xA5A5A5A5u;
    out.sample_rate = 0xA5A5A5A5u;
    const auto before = out;
    check(fly_runtime_get_source_timing(runtime, &out) == expected,
          "source timing rejects invalid output or unavailable machine");
    check(std::memcmp(&out, &before, sizeof(out)) == 0,
          "source timing failure leaves every output byte unchanged");
}

void test_source_timing_actual_mode_and_output_contract()
{
    auto* runtime = create_runtime();
    check_timing_rejected(runtime, FLY_RESULT_INVALID_STATE);
    check_timing_rejected(nullptr, FLY_RESULT_INVALID_ARGUMENT);
    check(fly_runtime_get_source_timing(runtime, nullptr) == FLY_RESULT_INVALID_ARGUMENT,
          "source timing rejects null output");
    auto invalid = make_timing();
    --invalid.struct_size;
    check_timing_rejected(runtime, FLY_RESULT_INVALID_ARGUMENT, invalid);
    invalid = make_timing();
    ++invalid.version;
    check_timing_rejected(runtime, FLY_RESULT_INVALID_ARGUMENT, invalid);

    for (const bool pal : {false, true})
    {
        const auto rom = make_timing_rom(pal);
        nes_config config{};
        config.struct_size = sizeof(config);
        config.version = NES_STRUCT_VERSION;
        config.favored_system = NES_FAVORED_NES_NTSC;
        auto* reference = nes_create(&config);
        check(reference != nullptr, "source-timing reference core creates");
        check(nes_load_rom(reference, rom.data(), rom.size(), nullptr) >= 0,
              "source-timing reference core loads the synthetic ROM");
        nes_rom_info info{};
        check(nes_get_rom_info(reference, &info) == NES_OK && info.region_ntsc == 1u,
              "existing wrapper keeps actual NTSC mode even for a PAL-tagged ROM");
        nes_destroy(reference);
        check(fly_runtime_load_rom_fresh(runtime, rom.data(), rom.size(), nullptr) ==
                  FLY_RESULT_OK, "same runtime fresh-loads NTSC then PAL");
        struct ExtendedTiming { fly_runtime_source_timing_v1 value; std::uint32_t tail; } out{};
        out.value = make_timing();
        out.value.struct_size = sizeof(out);
        out.tail = 0xACED1234u;
        check(fly_runtime_get_source_timing(runtime, &out.value) == FLY_RESULT_OK,
              "source timing succeeds immediately after load without stepping");
        check(out.value.source_region == FLY_RUNTIME_SOURCE_REGION_NTSC &&
                  out.value.frame_rate_numerator == 150247u &&
                  out.value.frame_rate_denominator == 2500u,
              "source timing reports actual NTSC mode rather than the ROM's PAL tag");
        check(out.value.sample_rate == 48000u, "default source sample rate resolves to 48000");
        check(out.value.struct_size == sizeof(out) && out.tail == 0xACED1234u,
              "source timing preserves caller size and oversized output tail");
    }

#ifdef FLYNES_TEST_WRAP_SOURCE_INFO
    report_pal_for_mapping_test = true;
    auto mapped_pal = make_timing();
    const auto mapped_result = fly_runtime_get_source_timing(runtime, &mapped_pal);
    report_pal_for_mapping_test = false;
    check(mapped_result == FLY_RESULT_OK &&
              mapped_pal.source_region == FLY_RUNTIME_SOURCE_REGION_PAL &&
              mapped_pal.frame_rate_numerator == 50007u &&
              mapped_pal.frame_rate_denominator == 1000u,
          "mapping-only: a reported actual PAL mode maps to the wrapper PAL rational");
    std::puts("source timing: PAL mapping seam covered; no actual PAL execution claim");
#endif

    const auto before_rejection = [&] {
        auto out = make_timing();
        (void)fly_runtime_get_source_timing(runtime, &out);
        return out;
    }();
    const auto ntsc = make_timing_rom(false);
    const std::uint8_t bad_hash[32]{};
    check(fly_runtime_load_rom_fresh(runtime, ntsc.data(), ntsc.size(), bad_hash) ==
              FLY_RESULT_INVALID_ARGUMENT, "fresh hash rejection occurs before round mutation");
    auto after_rejection = make_timing();
    check(fly_runtime_get_source_timing(runtime, &after_rejection) == FLY_RESULT_OK &&
              std::memcmp(&after_rejection, &before_rejection, sizeof(after_rejection)) == 0,
          "prevalidation failure preserves the current loaded source timing");
    const std::uint8_t invalid_rom[] = {0, 1, 2, 3};
    check(fly_runtime_load_rom_fresh(runtime, invalid_rom, sizeof(invalid_rom), nullptr) ==
              FLY_RESULT_INVALID_ARGUMENT, "accepted fresh round fails to load invalid ROM");
    check_timing_rejected(runtime, FLY_RESULT_INVALID_STATE);
    check(fly_runtime_load_rom_fresh(runtime, ntsc.data(), ntsc.size(), nullptr) == FLY_RESULT_OK,
          "source timing can recover with another fresh round");
    check(fly_runtime_get_source_timing(runtime, &after_rejection) == FLY_RESULT_OK &&
              after_rejection.source_region == FLY_RUNTIME_SOURCE_REGION_NTSC,
          "recovered source timing belongs to the newly loaded round");
    fly_runtime_destroy(runtime);
}

void test_source_timing_is_non_consuming()
{
    for (const std::uint32_t rate : {0u, 44100u, 48000u})
    for (const int fixture : {0, 1, 2})
    {
        auto* observed = create_runtime(rate);
        auto* control = create_runtime(rate);
        const auto rom = fixture == 2 ? read_file(FLYNES_RUNTIME_ROM_FIXTURE)
                                     : make_timing_rom(fixture == 1);
        check(fly_runtime_load_rom_fresh(observed, rom.data(), rom.size(), nullptr) == FLY_RESULT_OK &&
                  fly_runtime_load_rom_fresh(control, rom.data(), rom.size(), nullptr) == FLY_RESULT_OK,
              "timing observation pair loads identical fresh contexts");
        for (std::uint64_t frame = 0; frame < 3; ++frame)
        {
            auto input = make_input(47, frame);
            auto result = make_result();
            check(fly_runtime_step_frame(observed, &input, &result) == FLY_RESULT_OK &&
                      fly_runtime_step_frame(control, &input, &result) == FLY_RESULT_OK,
                  "timing observation pair steps identical complete frames");
            const auto checkpoint = save_checkpoint(observed);
            auto digest = make_digest();
            check(fly_runtime_copy_frame_digest(observed, 47, frame, &digest) == FLY_RESULT_OK,
                  "timing observation has an exact frame digest before query");
            std::vector<std::uint8_t> pixels(FLY_RUNTIME_RGB565_BYTES), after_pixels(pixels.size());
            fly_latest_frame_v1 meta{};
            meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
            meta.version = FLY_LATEST_FRAME_VERSION_1;
            check(fly_runtime_copy_latest_frame(observed, pixels.data(), pixels.size(), &meta) ==
                      FLY_RESULT_OK, "timing observation captures video before query");
            for (int query = 0; query < 4; ++query)
            {
                auto timing = make_timing();
                check(fly_runtime_get_source_timing(observed, &timing) == FLY_RESULT_OK &&
                          timing.sample_rate == (rate == 0u ? 48000u : rate),
                      "source timing returns actual configured rate without stepping");
            }
            check(save_checkpoint(observed) == checkpoint,
                  "timing queries leave the complete checkpoint byte-identical");
            auto after_digest = make_digest();
            check(fly_runtime_copy_frame_digest(observed, 47, frame, &after_digest) == FLY_RESULT_OK &&
                      std::memcmp(&digest, &after_digest, sizeof(digest)) == 0,
                  "timing queries preserve frame identity and all digest bytes");
            auto after_meta = meta;
            check(fly_runtime_copy_latest_frame(observed, after_pixels.data(), after_pixels.size(),
                                                &after_meta) == FLY_RESULT_OK &&
                      after_pixels == pixels && std::memcmp(&meta, &after_meta, sizeof(meta)) == 0,
                  "timing queries preserve video pixels, frame counters and diagnostics");
            const auto pcm = pull_digest_test_pcm(observed);
            const auto expected_pcm = pull_digest_test_pcm(control);
            check(pcm.samples == expected_pcm.samples &&
                      std::memcmp(&pcm.block, &expected_pcm.block, sizeof(pcm.block)) == 0 &&
                      pcm.block.sample_count > 0u && pcm.block.sample_count < 4096u,
                  "timing queries preserve full queued PCM and all consumer/producer observations");
        }
        fly_runtime_destroy(observed);
        fly_runtime_destroy(control);
    }
}

void check_checkpoint_epoch_rejection(const std::vector<std::uint8_t>& checkpoint,
                                      std::uint64_t expected_epoch,
                                      fly_result expected_result, const char* context,
                                      bool legacy_loader = false,
                                      bool require_allocation_free = false)
{
    auto* runtime = create_runtime();
    auto* control = create_runtime();
    if (!load_fixture_rom(runtime, nullptr) || !load_fixture_rom(control, nullptr))
    {
        fly_runtime_destroy(runtime);
        fly_runtime_destroy(control);
        return;
    }
    for (std::uint64_t frame = 0; frame < 3; ++frame)
    {
        auto input = make_input(31, frame);
        auto result = make_result();
        check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
              "epoch rejection target steps real core");
        check(fly_runtime_step_frame(control, &input, &result) == FLY_RESULT_OK,
              "epoch rejection control steps real core");
        if (frame < 2)
        {
            pull_digest_test_pcm(runtime);
            pull_digest_test_pcm(control);
        }
    }
    const auto before = save_checkpoint(runtime);
    auto before_digest = make_digest();
    check(fly_runtime_copy_frame_digest(runtime, 31, 2, &before_digest) == FLY_RESULT_OK,
          "epoch rejection begins with a valid digest");
    std::vector<std::uint8_t> before_pixels(FLY_RUNTIME_RGB565_BYTES);
    fly_latest_frame_v1 before_meta{};
    before_meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
    before_meta.version = FLY_LATEST_FRAME_VERSION_1;
    check(fly_runtime_copy_latest_frame(runtime, before_pixels.data(), before_pixels.size(),
                                        &before_meta) == FLY_RESULT_OK,
          "epoch rejection begins with a complete frame");

    fly_result loaded;
    {
        AllocationObservation observation(require_allocation_free);
        loaded = legacy_loader
            ? fly_runtime_load_checkpoint(runtime, checkpoint.data(), checkpoint.size())
            : fly_runtime_load_checkpoint_for_epoch(runtime, checkpoint.data(), checkpoint.size(),
                                                    expected_epoch);
    }
    if (require_allocation_free)
        check(observed_allocations == 0,
              "checkpoint payload length is rejected before any allocation");
    check(loaded == expected_result, context);
    if (loaded != expected_result)
    {
        // A rejected-config regression must stop at the return-value assertion:
        // never observe PCM or step a runtime that accepted invalid parameters.
        fly_runtime_destroy(runtime);
        fly_runtime_destroy(control);
        return;
    }
    check(save_checkpoint(runtime) == before,
          "rejected epoch checkpoint preserves complete serialized state");
    auto after_digest = make_digest();
    check(fly_runtime_copy_frame_digest(runtime, 31, 2, &after_digest) == FLY_RESULT_OK,
          "rejected epoch checkpoint preserves digest availability");
    check(std::memcmp(&before_digest, &after_digest, sizeof(before_digest)) == 0,
          "rejected epoch checkpoint preserves exact digest bytes");
    std::vector<std::uint8_t> after_pixels(FLY_RUNTIME_RGB565_BYTES);
    auto after_meta = before_meta;
    check(fly_runtime_copy_latest_frame(runtime, after_pixels.data(), after_pixels.size(),
                                        &after_meta) == FLY_RESULT_OK,
          "rejected epoch checkpoint preserves frame availability");
    check(before_pixels == after_pixels &&
              std::memcmp(&before_meta, &after_meta, sizeof(before_meta)) == 0,
          "rejected epoch checkpoint preserves pixels and all frame metadata");
    const auto actual_pcm = pull_digest_test_pcm(runtime);
    const auto control_pcm = pull_digest_test_pcm(control);
    check(control_pcm.block.sample_count > 0 &&
              control_pcm.block.sample_count < control_pcm.samples.size(),
          "epoch rejection compares actual produced PCM instead of empty-queue fallback");
    check(actual_pcm.samples == control_pcm.samples &&
              std::memcmp(&actual_pcm.block, &control_pcm.block, sizeof(actual_pcm.block)) == 0,
          "rejected epoch checkpoint preserves real queued PCM, sequence and media time");
    fly_runtime_destroy(runtime);
    fly_runtime_destroy(control);
}

void test_checkpoint_epoch_guard()
{
    auto* donor = create_runtime();
    const auto unloaded = save_checkpoint(donor);
    check_checkpoint_epoch_rejection(unloaded, 31, FLY_RESULT_INVALID_STATE,
                                    "epoch load rejects a checkpoint without a loaded ROM");
    if (!load_fixture_rom(donor, nullptr))
    {
        fly_runtime_destroy(donor);
        return;
    }
    const auto unset_timeline = save_checkpoint(donor);
    check_checkpoint_epoch_rejection(unset_timeline, 31, FLY_RESULT_INVALID_STATE,
                                    "epoch load rejects an unset checkpoint timeline");
    auto input = make_input(31, 0);
    auto result = make_result();
    check(fly_runtime_step_frame(donor, &input, &result) == FLY_RESULT_OK,
          "epoch checkpoint donor establishes a timeline");
    const auto same_epoch = save_checkpoint(donor);
    check_checkpoint_epoch_rejection(same_epoch, 32, FLY_RESULT_INVALID_STATE,
                                    "epoch load rejects the wrong expected epoch");
    check_checkpoint_epoch_rejection(same_epoch, 0, FLY_RESULT_INVALID_ARGUMENT,
                                    "epoch load rejects zero expected epoch");
    auto malformed = same_epoch;
    malformed[0] ^= 0xffu;
    check_checkpoint_epoch_rejection(malformed, 32, FLY_RESULT_INVALID_ARGUMENT,
                                    "malformed checkpoint keeps argument-error precedence");
    malformed = same_epoch;
    malformed.pop_back();
    check_checkpoint_epoch_rejection(malformed, 31, FLY_RESULT_INVALID_ARGUMENT,
                                    "epoch load rejects truncated checkpoint structure");
    check_checkpoint_epoch_rejection({}, 31, FLY_RESULT_INVALID_ARGUMENT,
                                    "epoch load rejects an empty payload");
    check(load_fixture_rom(donor, nullptr), "epoch donor resets for a different timeline");
    input = make_input(32, 0);
    check(fly_runtime_step_frame(donor, &input, &result) == FLY_RESULT_OK,
          "epoch donor establishes a different loaded timeline");
    const auto different_epoch = save_checkpoint(donor);
    check_checkpoint_epoch_rejection(different_epoch, 31, FLY_RESULT_INVALID_STATE,
                                    "epoch load rejects a different loaded checkpoint epoch");

    check(fly_runtime_load_checkpoint_for_epoch(nullptr, same_epoch.data(), same_epoch.size(),
                                               31) == FLY_RESULT_INVALID_ARGUMENT,
          "epoch load rejects a null runtime");
    check(fly_runtime_load_checkpoint_for_epoch(donor, nullptr, same_epoch.size(), 31) ==
              FLY_RESULT_INVALID_ARGUMENT, "epoch load rejects null checkpoint bytes");
    check(fly_runtime_load_checkpoint_for_epoch(donor, same_epoch.data(), 0, 31) ==
              FLY_RESULT_INVALID_ARGUMENT, "epoch load rejects zero checkpoint size");

    auto* receiver = create_runtime();
    if (load_fixture_rom(receiver, nullptr))
    {
        input = make_input(31, 0);
        input.buttons[0] ^= 0xffu;
        check(fly_runtime_step_frame(receiver, &input, &result) == FLY_RESULT_OK,
              "receiving instance establishes its own different state in the same epoch");
        check(save_checkpoint(receiver) != same_epoch,
              "imported same-epoch checkpoint was not produced by the receiving instance");
        check(fly_runtime_load_checkpoint_for_epoch(receiver, same_epoch.data(),
                                                   same_epoch.size(), 31) == FLY_RESULT_OK,
              "epoch load accepts a matching checkpoint from another runtime instance");
        check(save_checkpoint(receiver) == same_epoch,
              "accepted epoch checkpoint follows the complete normal snapshot load path");
        check_digest_rejected(receiver, 31, 0, FLY_RESULT_INVALID_STATE);
        input = make_input(31, 1);
        check(fly_runtime_step_frame(receiver, &input, &result) == FLY_RESULT_OK,
              "accepted epoch checkpoint continues at its next frame");
        auto digest = make_digest();
        check(fly_runtime_copy_frame_digest(receiver, 31, 1, &digest) == FLY_RESULT_OK,
              "first step after accepted epoch checkpoint reestablishes the digest");
        check(fly_runtime_load_checkpoint(receiver, unset_timeline.data(), unset_timeline.size()) ==
                  FLY_RESULT_OK, "legacy checkpoint load still accepts an unset timeline");
        check(fly_runtime_load_checkpoint(receiver, unloaded.data(), unloaded.size()) ==
                  FLY_RESULT_OK, "legacy checkpoint load still accepts an unloaded snapshot");
    }
    fly_runtime_destroy(receiver);
    fly_runtime_destroy(donor);
}

void test_checkpoint_payload_length_before_allocation()
{
    auto* donor = create_runtime();
    if (!load_fixture_rom(donor, nullptr))
    {
        fly_runtime_destroy(donor);
        return;
    }
    auto input = make_input(31, 0);
    auto result = make_result();
    check(fly_runtime_step_frame(donor, &input, &result) == FLY_RESULT_OK,
          "payload-length donor establishes its timeline through the public API");
    const auto checkpoint = save_checkpoint(donor);
    fly_runtime_destroy(donor);
    // Keep the original, bounded core/pixel sizes from a public save. Only
    // remove payload bytes or add one trailing byte; never inflate a size.
    constexpr std::size_t checkpoint_header_size = 152;
    check(checkpoint.size() > checkpoint_header_size,
          "payload-length fixture has a complete v1 header and payload");
    if (checkpoint.size() <= checkpoint_header_size) return;
    auto header_only = checkpoint;
    header_only.resize(checkpoint_header_size);
    auto missing_pixel_byte = checkpoint;
    missing_pixel_byte.pop_back();
    auto trailing_byte = checkpoint;
    trailing_byte.push_back(0);
    for (const bool legacy_loader : {false, true})
    {
        for (const auto* malformed : {&header_only, &missing_pixel_byte, &trailing_byte})
            check_checkpoint_epoch_rejection(*malformed, 31, FLY_RESULT_INVALID_ARGUMENT,
                "loader rejects a mismatched checkpoint payload length",
                legacy_loader, true);
    }
}

void test_checkpoint_sample_rate_validation()
{
    for (const std::uint32_t rate : {44100u, FLY_RUNTIME_DEFAULT_SAMPLE_RATE, 96000u})
    {
        auto* donor = create_runtime(rate);
        if (!load_fixture_rom(donor, nullptr))
        {
            fly_runtime_destroy(donor);
            continue;
        }
        auto input = make_input(31, 0);
        auto result = make_result();
        check(fly_runtime_step_frame(donor, &input, &result) == FLY_RESULT_OK,
              "sample-rate donor establishes its timeline through the public API");
        const auto checkpoint = save_checkpoint(donor);
        fly_runtime_destroy(donor);

        for (const bool legacy_loader : {false, true})
        {
            if (rate != FLY_RUNTIME_DEFAULT_SAMPLE_RATE)
            {
                check_checkpoint_epoch_rejection(checkpoint, 31, FLY_RESULT_INVALID_STATE,
                    legacy_loader ? "legacy loader rejects a different valid sample rate"
                                  : "epoch loader rejects a different valid sample rate",
                    legacy_loader);
            }
            else
            {
                for (const std::uint32_t invalid_rate : {0u, 44099u, 96001u})
                {
                    auto malformed = checkpoint;
                    // Codec-only fixture edit: v1 stores LE sample_rate after
                    // magic, version and reserved. Start with a public save.
                    constexpr std::size_t sample_rate_offset = 8u + 4u + 4u;
                    check(malformed.size() >= sample_rate_offset + 4u,
                          "saved sample-rate fixture contains the v1 header");
                    if (malformed.size() < sample_rate_offset + 4u) continue;
                    for (std::size_t byte = 0; byte < 4u; ++byte)
                        malformed[sample_rate_offset + byte] =
                            static_cast<std::uint8_t>(invalid_rate >> (byte * 8u));
                    check_checkpoint_epoch_rejection(malformed, 31, FLY_RESULT_INVALID_ARGUMENT,
                        legacy_loader ? "legacy loader rejects an invalid checkpoint sample rate"
                                      : "epoch loader rejects an invalid checkpoint sample rate",
                        legacy_loader);
                }
            }

            // The zero creation option resolves to the default rate; checkpoints
            // saved with that explicit rate remain compatible with such handles.
            auto* receiver = create_runtime(rate == FLY_RUNTIME_DEFAULT_SAMPLE_RATE ? 0u : rate);
            if (load_fixture_rom(receiver, nullptr))
            {
                const auto loaded = legacy_loader
                    ? fly_runtime_load_checkpoint(receiver, checkpoint.data(), checkpoint.size())
                    : fly_runtime_load_checkpoint_for_epoch(receiver, checkpoint.data(),
                                                            checkpoint.size(), 31);
                check(loaded == FLY_RESULT_OK, "matching sample-rate checkpoint remains loadable");
                check(save_checkpoint(receiver) == checkpoint,
                      "matching sample-rate load preserves the complete v1 checkpoint");
            }
            fly_runtime_destroy(receiver);
        }
    }
}

void test_checkpoint_wrong_rom_preserves_observation()
{
    auto rom = read_file(FLYNES_RUNTIME_ROM_FIXTURE);
    // Make another valid iNES ROM by changing only CHR data. Checkpoint bytes
    // come entirely from the public API and remain opaque to this test.
    const bool has_chr = rom.size() > 16 && rom[0] == 'N' && rom[1] == 'E' &&
        rom[2] == 'S' && rom[3] == 0x1a && rom[5] > 0;
    check(has_chr, "wrong-ROM donor fixture contains CHR data");
    if (!has_chr) return;
    const std::size_t chr_offset = 16u + ((rom[6] & 4u) ? 512u : 0u) +
        static_cast<std::size_t>(rom[4]) * 16384u;
    check(chr_offset < rom.size(), "wrong-ROM donor CHR data is in bounds");
    if (chr_offset >= rom.size()) return;
    rom[chr_offset] ^= 1u;
    auto* donor = create_runtime();
    check(fly_runtime_load_rom(donor, rom.data(), rom.size(), nullptr) == FLY_RESULT_OK,
          "wrong-ROM donor loads a valid different ROM");
    auto input = make_input(31, 0);
    auto result = make_result();
    check(fly_runtime_step_frame(donor, &input, &result) == FLY_RESULT_OK,
          "wrong-ROM donor establishes matching epoch through a real step");
    const auto checkpoint = save_checkpoint(donor);
    fly_runtime_destroy(donor);
    check_checkpoint_epoch_rejection(checkpoint, 31, FLY_RESULT_INTERNAL_ERROR,
                                    "epoch loader rejects another ROM's valid checkpoint");
    check_checkpoint_epoch_rejection(checkpoint, 31, FLY_RESULT_INTERNAL_ERROR,
                                    "legacy loader rejects another ROM's valid checkpoint", true);
}

void test_frame_digest_is_exact_and_non_consuming()
{
    check_observation_sha256_vectors();
    fly_runtime_t* observed = create_runtime();
    fly_runtime_t* drained = create_runtime();
    fly_runtime_t* control = create_runtime();
    check_digest_rejected(observed, 19, 0, FLY_RESULT_INVALID_STATE);
    if (!load_fixture_rom(observed, nullptr) || !load_fixture_rom(drained, nullptr) ||
        !load_fixture_rom(control, nullptr))
    {
        fly_runtime_destroy(observed);
        fly_runtime_destroy(drained);
        fly_runtime_destroy(control);
        return;
    }
    check_digest_rejected(observed, 19, 0, FLY_RESULT_INVALID_STATE);
    check_digest_rejected(nullptr, 19, 0, FLY_RESULT_INVALID_ARGUMENT);
    check(fly_runtime_copy_frame_digest(observed, 19, 0, nullptr) ==
              FLY_RESULT_INVALID_ARGUMENT, "NULL digest output is rejected");
    auto bad = make_digest();
    bad.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE - 1;
    check_digest_rejected(observed, 19, 0, FLY_RESULT_INVALID_ARGUMENT, bad);
    bad = make_digest();
    ++bad.version;
    check_digest_rejected(observed, 19, 0, FLY_RESULT_INVALID_ARGUMENT, bad);

    for (std::uint64_t frame = 0; frame < 40; ++frame)
    {
        auto input = make_input(19, frame);
        input.predicted_port_mask = frame % 2 == 0 ? 0xAu : 0u;
        auto result = make_result();
        check(fly_runtime_step_frame(observed, &input, &result) == FLY_RESULT_OK,
              "observed runtime steps a frame");
        input.capture_time_ns += 123456;
        input.receive_time_ns += 987654;
        check(fly_runtime_step_frame(drained, &input, &result) == FLY_RESULT_OK,
              "drained runtime steps with different diagnostic times");
        check(fly_runtime_step_frame(control, &input, &result) == FLY_RESULT_OK,
              "unobserved control steps a frame");
        const auto early_pcm = pull_digest_test_pcm(drained);
        check(early_pcm.block.sample_count > 0 && early_pcm.block.sample_count < 4096,
              "early drain consumes the complete real frame PCM");

        std::vector<std::uint8_t> before_pixels(FLY_RUNTIME_RGB565_BYTES);
        fly_latest_frame_v1 before_meta{};
        before_meta.struct_size = FLY_LATEST_FRAME_V1_SIZE;
        before_meta.version = FLY_LATEST_FRAME_VERSION_1;
        check(fly_runtime_copy_latest_frame(observed, before_pixels.data(),
                                            before_pixels.size(), &before_meta) == FLY_RESULT_OK,
              "public frame copy supplies bytes and diagnostics before digest query");
        auto a = make_digest();
        auto b = make_digest();
        check(fly_runtime_copy_frame_digest(observed, 19, frame, &a) == FLY_RESULT_OK,
              "exact current completed frame has a digest");
        check(fly_runtime_copy_frame_digest(drained, 19, frame, &b) == FLY_RESULT_OK,
              "digest remains available after all PCM was consumed");
        check(a.timeline_epoch == 19 && a.frame_index == frame,
              "digest identifies the exact requested frame");
        check(a.predicted_port_mask == input.predicted_port_mask && a.reserved == 0,
              "digest reports the actual prediction mask without claiming commitment");
        check_same_hashes(a, b, "separate runtime / different diagnostic times");
        const std::array<std::uint8_t, 32> zero{};
        check(std::memcmp(a.state_sha256, zero.data(), 32) != 0 &&
                  std::memcmp(a.frame_sha256, zero.data(), 32) != 0 &&
                  std::memcmp(a.pcm_sha256, zero.data(), 32) != 0,
              "digest hashes contain actual observations");
        check_digest_rejected(observed, 20, frame, FLY_RESULT_INVALID_STATE);
        check_digest_rejected(observed, 19, frame + 1, FLY_RESULT_INVALID_STATE);
        if (frame > 0)
            check_digest_rejected(observed, 19, frame - 1, FLY_RESULT_INVALID_STATE);

        std::vector<std::uint8_t> after_pixels(FLY_RUNTIME_RGB565_BYTES);
        auto after_meta = before_meta;
        check(fly_runtime_copy_latest_frame(observed, after_pixels.data(),
                                            after_pixels.size(), &after_meta) == FLY_RESULT_OK,
              "public frame copy remains available after digest queries");
        check(before_pixels == after_pixels &&
                  std::memcmp(&before_meta, &after_meta, sizeof(before_meta)) == 0,
              "digest queries preserve frame bytes and all metadata including diagnostic times");
        std::vector<std::uint8_t> frame_bytes;
        frame_bytes.reserve(before_pixels.size());
        for (std::size_t offset = 0; offset < before_pixels.size(); offset += 2)
        {
            std::uint16_t pixel = 0;
            std::memcpy(&pixel, before_pixels.data() + offset, sizeof(pixel));
            frame_bytes.push_back(static_cast<std::uint8_t>(pixel & 0xFFu));
            frame_bytes.push_back(static_cast<std::uint8_t>(pixel >> 8u));
        }
        const auto frame_hash = observation_sha256(frame_bytes);
        check(std::memcmp(a.frame_sha256, frame_hash.data(), frame_hash.size()) == 0,
              "frame digest hashes every public RGB565 pixel encoded little-endian");
        const auto after_query = pull_digest_test_pcm(observed);
        const auto no_query = pull_digest_test_pcm(control);
        check(after_query.samples == no_query.samples &&
                  after_query.samples == early_pcm.samples,
              "query preserves PCM bytes compared to unobserved control");
        check(after_query.block.sample_count == no_query.block.sample_count &&
                  after_query.block.first_sample_sequence == no_query.block.first_sample_sequence &&
                  after_query.block.media_time_ns == no_query.block.media_time_ns,
              "query preserves PCM count, sequence, and media time");
        check(after_query.block.sample_count <= after_query.samples.size(),
              "PCM count stays within public pull capacity");
        if (after_query.block.sample_count <= after_query.samples.size())
        {
            std::vector<std::uint8_t> pcm_bytes;
            for (std::size_t index = 0; index < after_query.block.sample_count; ++index)
            {
                const auto sample = static_cast<std::uint16_t>(after_query.samples[index]);
                pcm_bytes.push_back(static_cast<std::uint8_t>(sample & 0xFFu));
                pcm_bytes.push_back(static_cast<std::uint8_t>(sample >> 8u));
            }
            const auto pcm_hash = observation_sha256(pcm_bytes);
            check(std::memcmp(a.pcm_sha256, pcm_hash.data(), pcm_hash.size()) == 0,
                  "PCM digest hashes exactly the pulled signed samples encoded little-endian");
        }
        check(fly_runtime_copy_frame_digest(observed, 19, frame, &b) == FLY_RESULT_OK,
              "repeated digest after PCM drain succeeds");
        check_same_hashes(a, b, "same runtime after PCM consumption");
    }
    fly_runtime_destroy(observed);
    fly_runtime_destroy(drained);
    fly_runtime_destroy(control);
}

void test_frame_digest_invalidation_and_replay()
{
    fly_runtime_t* runtime = create_runtime();
    if (!load_fixture_rom(runtime, nullptr))
    {
        fly_runtime_destroy(runtime);
        return;
    }
    auto input = make_input(23, 0);
    auto result = make_result();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "digest replay starts from a complete frame");
    const auto checkpoint = save_checkpoint(runtime);
    check(fly_runtime_capture_rollback(runtime, 0) == FLY_RESULT_OK,
          "digest replay captures rollback");
    input = make_input(23, 1);
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "digest replay advances past checkpoint");
    auto future = make_digest();
    check(fly_runtime_copy_frame_digest(runtime, 23, 1, &future) == FLY_RESULT_OK,
          "future digest is initially available");
    check(fly_runtime_restore_rollback(runtime, 0) == FLY_RESULT_OK,
          "digest rollback restore succeeds");
    check_digest_rejected(runtime, 23, 1, FLY_RESULT_INVALID_STATE);
    check_digest_rejected(runtime, 23, 0, FLY_RESULT_INVALID_STATE);

    auto replay = make_digest();
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "rollback replays the original successor input");
    check(fly_runtime_copy_frame_digest(runtime, 23, 1, &replay) == FLY_RESULT_OK,
          "rollback successor has a new digest");
    check_same_hashes(future, replay, "original future versus rollback replay");
    for (unsigned int attempt = 0; attempt < 2; ++attempt)
    {
        check(fly_runtime_load_checkpoint(runtime, checkpoint.data(), checkpoint.size()) ==
                  FLY_RESULT_OK, "digest replay loads the same checkpoint");
        check_digest_rejected(runtime, 23, 0, FLY_RESULT_INVALID_STATE);
        check_digest_rejected(runtime, 23, 1, FLY_RESULT_INVALID_STATE);
        check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
              "successful replay step reestablishes digest");
        auto current = make_digest();
        check(fly_runtime_copy_frame_digest(runtime, 23, 1, &current) == FLY_RESULT_OK,
              "replayed next frame has a digest");
        check_same_hashes(future, current, "original future versus checkpoint replay");
        const auto pcm = pull_digest_test_pcm(runtime);
        std::fprintf(stderr, "checkpoint replay %u: PCM samples=%u first_sequence=%llu\n",
                     attempt, pcm.block.sample_count,
                     static_cast<unsigned long long>(pcm.block.first_sample_sequence));
        if (attempt != 0)
            check_same_hashes(replay, current, "same checkpoint replay");
        replay = current;
    }
    check(fly_runtime_clear_input_ports(runtime, 1, FLY_RUNTIME_CLEAR_PAUSE) ==
              FLY_RESULT_OK, "digest test clears input");
    check_digest_rejected(runtime, 23, 1, FLY_RESULT_INVALID_STATE);
    input = make_input(23, 2);
    check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
          "step after input clear succeeds");
    check(fly_runtime_copy_frame_digest(runtime, 23, 2, &replay) == FLY_RESULT_OK,
          "step after input clear reestablishes digest");
    check(load_fixture_rom(runtime, nullptr), "digest test reloads ROM");
    check_digest_rejected(runtime, 23, 2, FLY_RESULT_INVALID_STATE);
    fly_runtime_destroy(runtime);
}

void test_frame_digest_uses_canonical_core_state_and_preserves_checkpoint()
{
    fly_runtime_t* runtime = create_runtime();
    nes_config config{};
    config.struct_size = sizeof(config);
    config.version = NES_STRUCT_VERSION;
    config.favored_system = NES_FAVORED_NES_NTSC;
    config.sample_rate = FLY_RUNTIME_DEFAULT_SAMPLE_RATE;
    config.pixfmt = NES_PIXFMT_RGB565;
    nes_t* reference = nes_create(&config);
    const auto rom = read_file(FLYNES_RUNTIME_ROM_FIXTURE);
    check(reference != nullptr, "canonical state oracle creates a real core");
    if (reference == nullptr || !load_fixture_rom(runtime, nullptr) || rom.empty())
    {
        nes_destroy(reference);
        fly_runtime_destroy(runtime);
        return;
    }
    check(nes_load_rom(reference, rom.data(), rom.size(), nullptr) >= 0,
          "canonical state oracle loads the same ROM");
    for (std::uint64_t frame = 0; frame < 8; ++frame)
    {
        auto input = make_input(29, frame);
        auto result = make_result();
        check(fly_runtime_step_frame(runtime, &input, &result) == FLY_RESULT_OK,
              "canonical state test steps runtime");
        for (std::uint32_t port = 0; port < NES_PORT_MAX; ++port)
            nes_set_input(reference, port, input.buttons[port]);
        std::array<std::int16_t, 4096> samples{};
        std::uint32_t frames_run = 0;
        std::uint32_t samples_written = 0;
        check(nes_run_frames(reference, 1, samples.data(),
                             static_cast<std::uint32_t>(samples.size()),
                             &frames_run, &samples_written) >= 0 && frames_run == 1,
              "canonical state oracle steps the identical real-core input");

        std::size_t written = 0;
        std::size_t needed = 0;
        check(nes_copy_canonical_state(reference, nullptr, 0, &written, &needed) ==
                  NES_ERR_BUFFER_TOO_SMALL && needed > 0,
              "real core exposes its canonical state size");
        std::vector<std::uint8_t> canonical(needed);
        check(nes_copy_canonical_state(reference, canonical.data(), canonical.size(),
                                        &written, &needed) >= 0 && written == canonical.size(),
              "real core exports complete canonical bytes");
        const auto expected_hash = observation_sha256(canonical);
        const auto checkpoint_before = save_checkpoint(runtime);
        auto digest = make_digest();
        check(fly_runtime_copy_frame_digest(runtime, 29, frame, &digest) == FLY_RESULT_OK,
              "canonical state digest query succeeds");
        check(std::memcmp(digest.state_sha256, expected_hash.data(), expected_hash.size()) == 0,
              "runtime state SHA-256 matches independent hash of uncompressed real-core state");
        check(save_checkpoint(runtime) == checkpoint_before,
              "canonical digest leaves the ordinary checkpoint byte-identical");

        check(nes_save_state(reference, nullptr, 0, &written, &needed) ==
                  NES_ERR_BUFFER_TOO_SMALL && needed > 0,
              "ordinary core save retains its size query");
        std::vector<std::uint8_t> compressed(needed);
        check(nes_save_state(reference, compressed.data(), compressed.size(),
                              &written, &needed) >= 0 && written == compressed.size(),
              "ordinary core save still produces a complete checkpoint");
        check(compressed.size() < canonical.size(),
              "ordinary save remains compressed while digest canonical state is uncompressed");
        const auto compressed_hash = observation_sha256(compressed);
        check(std::memcmp(digest.state_sha256, compressed_hash.data(), compressed_hash.size()) != 0,
              "digest does not hash zlib-dependent ordinary save bytes");
        // Checkpoint v1 has a 152-byte header, then core state and RGB565 bytes.
        constexpr std::size_t checkpoint_header_size = 152;
        check(checkpoint_before.size() == checkpoint_header_size + compressed.size() +
                                             FLY_RUNTIME_RGB565_BYTES,
              "checkpoint v1 keeps its original compressed payload layout");
        if (checkpoint_before.size() >= checkpoint_header_size + compressed.size())
            check(std::memcmp(checkpoint_before.data() + checkpoint_header_size,
                              compressed.data(), compressed.size()) == 0,
                  "checkpoint still embeds the ordinary compressed core save exactly");
    }
    nes_destroy(reference);
    fly_runtime_destroy(runtime);
}

} // namespace

static_assert(sizeof(fly_result) == sizeof(std::int32_t), "fly_result must stay 32-bit");
static_assert(FLY_RUNTIME_CONFIG_VERSION_1 == 1, "runtime config version changed");
static_assert(FLY_FRAME_INPUT_VERSION_1 == 1, "frame input version changed");
static_assert(FLY_FRAME_RESULT_VERSION_1 == 1, "frame result version changed");
static_assert(FLY_LATEST_FRAME_VERSION_1 == 1, "latest frame version changed");
static_assert(FLY_PCM_BLOCK_VERSION_1 == 1, "PCM block version changed");
static_assert(FLY_RUNTIME_SOURCE_TIMING_VERSION_1 == 1, "source timing version changed");
static_assert(FLY_RUNTIME_SOURCE_TIMING_V1_SIZE == 24 &&
                  sizeof(fly_runtime_source_timing_v1) == 24,
              "source timing v1 ABI size changed");
static_assert(offsetof(fly_runtime_source_timing_v1, source_region) == 8 &&
                  offsetof(fly_runtime_source_timing_v1, sample_rate) == 20,
              "source timing v1 field offsets changed");
static_assert(FLY_RUNTIME_FRAME_DIGEST_VERSION_1 == 1, "frame digest version changed");
static_assert(FLY_RUNTIME_FRAME_DIGEST_V1_SIZE == 128, "frame digest ABI size changed");
static_assert(sizeof(fly_runtime_frame_digest_v1) == FLY_RUNTIME_FRAME_DIGEST_V1_SIZE,
              "frame digest ABI contains unexpected padding");
static_assert(offsetof(fly_runtime_frame_digest_v1, timeline_epoch) == 8,
              "frame digest timeline offset changed");
static_assert(offsetof(fly_runtime_frame_digest_v1, frame_index) == 16,
              "frame digest frame offset changed");
static_assert(offsetof(fly_runtime_frame_digest_v1, predicted_port_mask) == 24,
              "frame digest prediction offset changed");
static_assert(offsetof(fly_runtime_frame_digest_v1, state_sha256) == 32,
              "frame digest state hash offset changed");
static_assert(offsetof(fly_runtime_frame_digest_v1, frame_sha256) == 64,
              "frame digest pixel hash offset changed");
static_assert(offsetof(fly_runtime_frame_digest_v1, pcm_sha256) == 96,
              "frame digest PCM hash offset changed");
static_assert(FLY_RUNTIME_ROLLBACK_SLOTS == 12, "rollback ring size changed");
static_assert(FLY_RUNTIME_PORT_COUNT == 4, "port count changed");
static_assert(FLY_RUNTIME_FRAME_WIDTH == 256, "frame width changed");
static_assert(FLY_RUNTIME_FRAME_HEIGHT == 240, "frame height changed");
static_assert(FLY_RUNTIME_RGB565_BYTES == 256u * 240u * 2u, "RGB565 size changed");
static_assert(FLY_RUNTIME_CLEAR_STOP == 1, "STOP reason value changed");
static_assert(FLY_RUNTIME_CLEAR_PAUSE == 2, "PAUSE reason value changed");
static_assert(FLY_RUNTIME_CLEAR_PEER_DISCONNECT == 3, "PEER_DISCONNECT reason value changed");
static_assert(FLY_RUNTIME_CLEAR_SEAT_REASSIGN == 4, "SEAT_REASSIGN reason value changed");

static_assert(offsetof(fly_frame_input_v1, struct_size) == 0, "input prefix changed");
static_assert(offsetof(fly_frame_input_v1, version) == sizeof(std::uint32_t),
              "input version offset changed");
static_assert(offsetof(fly_frame_input_v1, timeline_epoch) == 8, "input timeline offset changed");
static_assert(offsetof(fly_frame_input_v1, frame_index) == 16, "input frame_index offset changed");
static_assert(offsetof(fly_frame_input_v1, buttons) == 24, "input buttons offset changed");
static_assert(offsetof(fly_frame_input_v1, input_sequence) == 40,
              "input sequence offset changed");
static_assert(offsetof(fly_frame_input_v1, predicted_port_mask) == 72,
              "predicted mask offset changed");
static_assert(offsetof(fly_frame_input_v1, batch_sequence) == 80,
              "batch_sequence offset changed");
static_assert(offsetof(fly_frame_input_v1, capture_time_ns) == 88,
              "capture_time_ns offset changed");
static_assert(offsetof(fly_frame_input_v1, receive_time_ns) == 96,
              "receive_time_ns offset changed");
static_assert(FLY_FRAME_INPUT_V1_SIZE == sizeof(fly_frame_input_v1),
              "input v1 prefix size changed");

static_assert(offsetof(fly_frame_result_v1, struct_size) == 0, "result prefix changed");
static_assert(offsetof(fly_frame_result_v1, post_state_frame) == 24,
              "post_state_frame offset changed");
static_assert(offsetof(fly_frame_result_v1, frame_sequence) == 32,
              "result frame_sequence offset changed");
static_assert(offsetof(fly_frame_result_v1, audio_first_sample_sequence) == 40,
              "audio first sample offset changed");
static_assert(offsetof(fly_frame_result_v1, applied_input_sequence) == 56,
              "applied input sequence offset changed");
static_assert(offsetof(fly_frame_result_v1, source_time_ns) == 88,
              "result source_time_ns offset changed");
static_assert(offsetof(fly_frame_result_v1, video_published) == 104,
              "video_published offset changed");
static_assert(FLY_FRAME_RESULT_V1_SIZE == sizeof(fly_frame_result_v1),
              "result v1 prefix size changed");

int main()
{
    test_source_timing_actual_mode_and_output_contract();
    test_source_timing_is_non_consuming();
    test_create_load_and_symbols();
    test_step_frame_and_rejection();
    test_pcm_pull();
    test_copy_latest_frame();
    test_rollback_and_checkpoint();
    test_clear_input_ports();
    test_checkpoint_epoch_guard();
    test_checkpoint_payload_length_before_allocation();
    test_checkpoint_sample_rate_validation();
    test_checkpoint_wrong_rom_preserves_observation();
    test_frame_digest_is_exact_and_non_consuming();
    test_frame_digest_invalidation_and_replay();
    test_frame_digest_uses_canonical_core_state_and_preserves_checkpoint();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_runtime_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_runtime_test: PASS");
    return 0;
}
