#include <flynes/flynes_runtime.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

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

fly_runtime_t* create_runtime()
{
    const fly_runtime_config config = make_config();
    fly_runtime_t* runtime = nullptr;
    const fly_result result = fly_runtime_create(&config, &runtime);
    check(result == FLY_RESULT_OK, "fly_runtime_create succeeds");
    check(runtime != nullptr, "fly_runtime_create writes a handle");
    return runtime;
}

bool load_fixture_rom(fly_runtime_t* runtime, const std::uint8_t* expected_sha256)
{
    const std::vector<std::uint8_t> rom = read_file(FLYNES_RUNTIME_ROM_FIXTURE);
    check(!rom.empty(), "from_below.nes fixture is readable");
    if (rom.empty())
    {
        return false;
    }
    const fly_result result =
        fly_runtime_load_rom(runtime, rom.data(), rom.size(), expected_sha256);
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
              "1A3AC4FAF4B35640505344059AE5D91DAE07CD47E1FB4D9D2A33C76391F1C555",
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

} // namespace

static_assert(sizeof(fly_result) == sizeof(std::int32_t), "fly_result must stay 32-bit");
static_assert(FLY_RUNTIME_CONFIG_VERSION_1 == 1, "runtime config version changed");
static_assert(FLY_FRAME_INPUT_VERSION_1 == 1, "frame input version changed");
static_assert(FLY_FRAME_RESULT_VERSION_1 == 1, "frame result version changed");
static_assert(FLY_LATEST_FRAME_VERSION_1 == 1, "latest frame version changed");
static_assert(FLY_PCM_BLOCK_VERSION_1 == 1, "PCM block version changed");
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
    test_create_load_and_symbols();
    test_step_frame_and_rejection();
    test_pcm_pull();
    test_copy_latest_frame();
    test_rollback_and_checkpoint();
    test_clear_input_ports();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_runtime_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_runtime_test: PASS");
    return 0;
}
