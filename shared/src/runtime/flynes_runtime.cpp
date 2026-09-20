#include <flynes/flynes_runtime.h>

#include <nes/nes.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <memory>
#include <mutex>
#include <new>
#include <vector>

namespace {

constexpr char kCheckpointMagic[8] = {'F', 'L', 'Y', 'R', 'T', '0', '1', '\0'};
constexpr std::uint32_t kPcmCapacity = 16384u;
constexpr std::uint32_t kAudioScratchSamples = 4096u;
constexpr std::size_t kSha256Size = 32u;

std::uint64_t monotonic_now_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

void put_u32_le(std::vector<std::uint8_t>& out, std::uint32_t value)
{
    out.push_back(static_cast<std::uint8_t>(value));
    out.push_back(static_cast<std::uint8_t>(value >> 8));
    out.push_back(static_cast<std::uint8_t>(value >> 16));
    out.push_back(static_cast<std::uint8_t>(value >> 24));
}

void put_u64_le(std::vector<std::uint8_t>& out, std::uint64_t value)
{
    for (int shift = 0; shift < 64; shift += 8)
    {
        out.push_back(static_cast<std::uint8_t>(value >> shift));
    }
}

bool read_bytes(const std::uint8_t*& cursor, const std::uint8_t* end, void* dst, std::size_t size)
{
    if (cursor == nullptr || end == nullptr || static_cast<std::size_t>(end - cursor) < size)
    {
        return false;
    }
    if (size != 0u && dst != nullptr)
    {
        std::memcpy(dst, cursor, size);
    }
    cursor += size;
    return true;
}

bool read_u32_le(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint32_t& value)
{
    std::uint8_t bytes[4];
    if (!read_bytes(cursor, end, bytes, sizeof(bytes)))
    {
        return false;
    }
    value = static_cast<std::uint32_t>(bytes[0]) |
            (static_cast<std::uint32_t>(bytes[1]) << 8) |
            (static_cast<std::uint32_t>(bytes[2]) << 16) |
            (static_cast<std::uint32_t>(bytes[3]) << 24);
    return true;
}

bool read_u64_le(const std::uint8_t*& cursor, const std::uint8_t* end, std::uint64_t& value)
{
    std::uint8_t bytes[8];
    if (!read_bytes(cursor, end, bytes, sizeof(bytes)))
    {
        return false;
    }
    value = 0;
    for (int index = 7; index >= 0; --index)
    {
        value = (value << 8) | static_cast<std::uint64_t>(bytes[index]);
    }
    return true;
}

std::uint32_t rotate_right(std::uint32_t value, unsigned int bits)
{
    return (value >> bits) | (value << (32u - bits));
}

void sha256_process(std::uint32_t state[8], const std::uint8_t block[64])
{
    static constexpr std::uint32_t k[64] = {
        0x428A2F98u, 0x71374491u, 0xB5C0FBCFu, 0xE9B5DBA5u,
        0x3956C25Bu, 0x59F111F1u, 0x923F82A4u, 0xAB1C5ED5u,
        0xD807AA98u, 0x12835B01u, 0x243185BEu, 0x550C7DC3u,
        0x72BE5D74u, 0x80DEB1FEu, 0x9BDC06A7u, 0xC19BF174u,
        0xE49B69C1u, 0xEFBE4786u, 0x0FC19DC6u, 0x240CA1CCu,
        0x2DE92C6Fu, 0x4A7484AAu, 0x5CB0A9DCu, 0x76F988DAu,
        0x983E5152u, 0xA831C66Du, 0xB00327C8u, 0xBF597FC7u,
        0xC6E00BF3u, 0xD5A79147u, 0x06CA6351u, 0x14292967u,
        0x27B70A85u, 0x2E1B2138u, 0x4D2C6DFCu, 0x53380D13u,
        0x650A7354u, 0x766A0ABBu, 0x81C2C92Eu, 0x92722C85u,
        0xA2BFE8A1u, 0xA81A664Bu, 0xC24B8B70u, 0xC76C51A3u,
        0xD192E819u, 0xD6990624u, 0xF40E3585u, 0x106AA070u,
        0x19A4C116u, 0x1E376C08u, 0x2748774Cu, 0x34B0BCB5u,
        0x391C0CB3u, 0x4ED8AA4Au, 0x5B9CCA4Fu, 0x682E6FF3u,
        0x748F82EEu, 0x78A5636Fu, 0x84C87814u, 0x8CC70208u,
        0x90BEFFFAu, 0xA4506CEBu, 0xBEF9A3F7u, 0xC67178F2u};
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<std::uint32_t>(block[i * 4]) << 24) |
               (static_cast<std::uint32_t>(block[i * 4 + 1]) << 16) |
               (static_cast<std::uint32_t>(block[i * 4 + 2]) << 8) |
               static_cast<std::uint32_t>(block[i * 4 + 3]);
    }
    for (int i = 16; i < 64; ++i)
    {
        const std::uint32_t s0 =
            rotate_right(w[i - 15], 7u) ^ rotate_right(w[i - 15], 18u) ^ (w[i - 15] >> 3u);
        const std::uint32_t s1 =
            rotate_right(w[i - 2], 17u) ^ rotate_right(w[i - 2], 19u) ^ (w[i - 2] >> 10u);
        w[i] = w[i - 16] + s0 + w[i - 7] + s1;
    }
    std::uint32_t a = state[0];
    std::uint32_t b = state[1];
    std::uint32_t c = state[2];
    std::uint32_t d = state[3];
    std::uint32_t e = state[4];
    std::uint32_t f = state[5];
    std::uint32_t g = state[6];
    std::uint32_t h = state[7];
    for (int i = 0; i < 64; ++i)
    {
        const std::uint32_t S1 =
            rotate_right(e, 6u) ^ rotate_right(e, 11u) ^ rotate_right(e, 25u);
        const std::uint32_t ch = (e & f) ^ ((~e) & g);
        const std::uint32_t temp1 = h + S1 + ch + k[i] + w[i];
        const std::uint32_t S0 =
            rotate_right(a, 2u) ^ rotate_right(a, 13u) ^ rotate_right(a, 22u);
        const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        const std::uint32_t temp2 = S0 + maj;
        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}

void sha256(const std::uint8_t* data, std::size_t size, std::uint8_t out[32])
{
    std::uint32_t state[8] = {0x6A09E667u, 0xBB67AE85u, 0x3C6EF372u, 0xA54FF53Au,
                              0x510E527Fu, 0x9B05688Cu, 0x1F83D9ABu, 0x5BE0CD19u};
    const std::size_t full_blocks = size / 64u;
    for (std::size_t index = 0; index < full_blocks; ++index)
    {
        sha256_process(state, data + index * 64u);
    }
    const std::size_t remaining = size - full_blocks * 64u;
    std::uint8_t tail[128];
    std::memset(tail, 0, sizeof(tail));
    if (remaining != 0u)
    {
        std::memcpy(tail, data + full_blocks * 64u, remaining);
    }
    tail[remaining] = 0x80u;
    const std::size_t padded_size = remaining < 56u ? 64u : 128u;
    const std::uint64_t bit_length = static_cast<std::uint64_t>(size) * 8u;
    for (std::size_t index = 0; index < 8u; ++index)
    {
        tail[padded_size - 1u - index] =
            static_cast<std::uint8_t>(bit_length >> (index * 8u));
    }
    sha256_process(state, tail);
    if (padded_size == 128u)
    {
        sha256_process(state, tail + 64);
    }
    for (int i = 0; i < 8; ++i)
    {
        out[i * 4] = static_cast<std::uint8_t>(state[i] >> 24);
        out[i * 4 + 1] = static_cast<std::uint8_t>(state[i] >> 16);
        out[i * 4 + 2] = static_cast<std::uint8_t>(state[i] >> 8);
        out[i * 4 + 3] = static_cast<std::uint8_t>(state[i]);
    }
}

bool is_clear_reason(std::uint32_t reason)
{
    return reason == FLY_RUNTIME_CLEAR_STOP || reason == FLY_RUNTIME_CLEAR_PAUSE ||
           reason == FLY_RUNTIME_CLEAR_PEER_DISCONNECT ||
           reason == FLY_RUNTIME_CLEAR_SEAT_REASSIGN;
}

fly_result validate_config(const fly_runtime_config* config)
{
    if (config == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->struct_size < FLY_RUNTIME_CONFIG_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (config->version != FLY_RUNTIME_CONFIG_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (config->reserved != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (config->sample_rate != 0u &&
        (config->sample_rate < 44100u || config->sample_rate > 96000u))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

fly_result validate_input(const fly_frame_input_v1* input)
{
    if (input == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (input->struct_size < FLY_FRAME_INPUT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (input->version != FLY_FRAME_INPUT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (input->reserved0 != 0u || (input->predicted_port_mask & ~0xFu) != 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return FLY_RESULT_OK;
}

} // namespace

namespace flynes_runtime_detail {

struct Snapshot
{
    bool valid = false;
    bool timeline_set = false;
    bool rom_loaded = false;
    bool has_complete_frame = false;
    std::uint32_t sample_rate = FLY_RUNTIME_DEFAULT_SAMPLE_RATE;
    std::uint32_t predicted_port_mask = 0;
    std::uint32_t buttons[FLY_RUNTIME_PORT_COUNT] = {};
    std::uint64_t timeline_epoch = 0;
    std::uint64_t next_frame_index = 0;
    std::uint64_t input_sequence[FLY_RUNTIME_PORT_COUNT] = {};
    std::uint64_t frame_sequence = 0;
    std::uint64_t pcm_producer_sequence = 0;
    std::uint64_t last_batch_sequence = 0;
    std::uint64_t last_source_time_ns = 0;
    std::uint64_t last_produced_time_ns = 0;
    std::vector<std::uint8_t> core;
    std::vector<std::uint8_t> pixels;
};

struct NesDeleter
{
    void operator()(nes_t* nes) const
    {
        if (nes != nullptr)
        {
            nes_destroy(nes);
        }
    }
};

} // namespace flynes_runtime_detail

using flynes_runtime_detail::NesDeleter;
using flynes_runtime_detail::Snapshot;

struct fly_runtime_handle
{
    std::mutex mutex;
    std::unique_ptr<nes_t, NesDeleter> nes;
    bool rom_loaded = false;
    bool timeline_set = false;
    bool has_complete_frame = false;
    std::uint32_t sample_rate = FLY_RUNTIME_DEFAULT_SAMPLE_RATE;
    std::uint32_t predicted_port_mask = 0;
    std::uint32_t buttons[FLY_RUNTIME_PORT_COUNT] = {};
    std::uint64_t timeline_epoch = 0;
    std::uint64_t next_frame_index = 0;
    std::uint64_t input_sequence[FLY_RUNTIME_PORT_COUNT] = {};
    std::uint64_t frame_sequence = 0;
    std::uint64_t pcm_producer_sequence = 0;
    std::uint64_t last_batch_sequence = 0;
    std::uint64_t last_source_time_ns = 0;
    std::uint64_t last_produced_time_ns = 0;
    std::vector<std::int16_t> audio_scratch;
    bool has_frame_digest = false;
    std::uint8_t frame_pcm_sha256[kSha256Size] = {};
    std::vector<std::int16_t> pcm;
    std::uint32_t pcm_read = 0;
    std::uint32_t pcm_count = 0;
    std::uint64_t pcm_read_sequence = 0;
    std::vector<std::uint8_t> complete_pixels;
    Snapshot rollback[FLY_RUNTIME_ROLLBACK_SLOTS];

    void reset_simulation()
    {
        rom_loaded = false;
        timeline_set = false;
        has_complete_frame = false;
        has_frame_digest = false;
        predicted_port_mask = 0;
        timeline_epoch = 0;
        next_frame_index = 0;
        frame_sequence = 0;
        pcm_producer_sequence = 0;
        last_batch_sequence = 0;
        last_source_time_ns = 0;
        last_produced_time_ns = 0;
        pcm_read = 0;
        pcm_count = 0;
        pcm_read_sequence = 0;
        std::fill(std::begin(buttons), std::end(buttons), 0u);
        std::fill(std::begin(input_sequence), std::end(input_sequence), 0u);
        std::fill(pcm.begin(), pcm.end(), static_cast<std::int16_t>(0));
        std::fill(complete_pixels.begin(), complete_pixels.end(), static_cast<std::uint8_t>(0));
        for (Snapshot& slot : rollback)
        {
            slot = Snapshot{};
        }
    }

    void clear_pcm()
    {
        pcm_read = 0;
        pcm_count = 0;
        pcm_read_sequence = pcm_producer_sequence;
        std::fill(pcm.begin(), pcm.end(), static_cast<std::int16_t>(0));
    }

    void push_pcm(const std::int16_t* samples, std::uint32_t count)
    {
        for (std::uint32_t index = 0; index < count; ++index)
        {
            if (pcm_count == static_cast<std::uint32_t>(pcm.size()))
            {
                pcm_read = (pcm_read + 1u) % static_cast<std::uint32_t>(pcm.size());
                ++pcm_read_sequence;
                --pcm_count;
            }
            const std::uint32_t write =
                (pcm_read + pcm_count) % static_cast<std::uint32_t>(pcm.size());
            pcm[write] = samples[index];
            ++pcm_count;
        }
        pcm_producer_sequence += count;
    }

    fly_result capture_snapshot(Snapshot& snapshot)
    {
        if (nes == nullptr)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        snapshot = Snapshot{};
        snapshot.timeline_set = timeline_set;
        snapshot.rom_loaded = rom_loaded;
        snapshot.has_complete_frame = has_complete_frame;
        snapshot.sample_rate = sample_rate;
        snapshot.predicted_port_mask = predicted_port_mask;
        snapshot.timeline_epoch = timeline_epoch;
        snapshot.next_frame_index = next_frame_index;
        snapshot.frame_sequence = frame_sequence;
        snapshot.pcm_producer_sequence = pcm_producer_sequence;
        snapshot.last_batch_sequence = last_batch_sequence;
        snapshot.last_source_time_ns = last_source_time_ns;
        snapshot.last_produced_time_ns = last_produced_time_ns;
        std::copy(std::begin(buttons), std::end(buttons), snapshot.buttons);
        std::copy(std::begin(input_sequence), std::end(input_sequence), snapshot.input_sequence);
        if (has_complete_frame)
        {
            snapshot.pixels = complete_pixels;
        }
        if (rom_loaded)
        {
            std::size_t written = 0;
            std::size_t needed = 0;
            const int query = nes_save_state(nes.get(), nullptr, 0, &written, &needed);
            if (query != NES_ERR_BUFFER_TOO_SMALL || needed == 0)
            {
                return FLY_RESULT_INTERNAL_ERROR;
            }
            snapshot.core.resize(needed);
            written = 0;
            needed = 0;
            const int saved =
                nes_save_state(nes.get(), snapshot.core.data(), snapshot.core.size(),
                               &written, &needed);
            if (saved < 0 || written != snapshot.core.size())
            {
                return FLY_RESULT_INTERNAL_ERROR;
            }
        }
        snapshot.valid = true;
        return FLY_RESULT_OK;
    }

    fly_result apply_snapshot(const Snapshot& snapshot, bool clear_rollback)
    {
        if (!snapshot.valid || nes == nullptr)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
        if (snapshot.rom_loaded && snapshot.core.empty())
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
        // Checkpoints intentionally contain no per-frame PCM observation.
        const bool had_frame_digest = has_frame_digest;
        has_frame_digest = false;
        if (snapshot.rom_loaded)
        {
            const int loaded =
                nes_load_state(nes.get(), snapshot.core.data(), snapshot.core.size());
            if (loaded < 0)
            {
                // This wrapper-only error precedes Machine::LoadState. Do not
                // generalize to CRC/corrupt errors: the machine can emit those
                // after modifying or resetting state, invalidating the digest.
                if (loaded == NES_ERR_STATE_ROM_MISMATCH)
                {
                    has_frame_digest = had_frame_digest;
                }
                return FLY_RESULT_INTERNAL_ERROR;
            }
            for (std::uint32_t port = 0; port < FLY_RUNTIME_PORT_COUNT; ++port)
            {
                nes_set_input(nes.get(), port, snapshot.buttons[port]);
            }
        }
        rom_loaded = snapshot.rom_loaded;
        timeline_set = snapshot.timeline_set;
        has_complete_frame = snapshot.has_complete_frame;
        sample_rate = snapshot.sample_rate;
        predicted_port_mask = snapshot.predicted_port_mask;
        timeline_epoch = snapshot.timeline_epoch;
        next_frame_index = snapshot.next_frame_index;
        frame_sequence = snapshot.frame_sequence;
        pcm_producer_sequence = snapshot.pcm_producer_sequence;
        last_batch_sequence = snapshot.last_batch_sequence;
        last_source_time_ns = snapshot.last_source_time_ns;
        last_produced_time_ns = snapshot.last_produced_time_ns;
        std::copy(std::begin(snapshot.buttons), std::end(snapshot.buttons), buttons);
        std::copy(std::begin(snapshot.input_sequence), std::end(snapshot.input_sequence),
                  input_sequence);
        if (snapshot.has_complete_frame)
        {
            complete_pixels = snapshot.pixels;
        }
        else
        {
            std::fill(complete_pixels.begin(), complete_pixels.end(), static_cast<std::uint8_t>(0));
        }
        clear_pcm();
        if (clear_rollback)
        {
            for (Snapshot& slot : rollback)
            {
                slot = Snapshot{};
            }
        }
        return FLY_RESULT_OK;
    }
};

namespace {

std::vector<std::uint8_t> serialize_snapshot(const Snapshot& snapshot)
{
    std::vector<std::uint8_t> out;
    out.reserve(128u + snapshot.core.size() + snapshot.pixels.size());
    out.insert(out.end(), kCheckpointMagic, kCheckpointMagic + 8);
    put_u32_le(out, FLY_RUNTIME_CHECKPOINT_VERSION_1);
    put_u32_le(out, 0);
    put_u32_le(out, snapshot.sample_rate);
    put_u32_le(out, snapshot.predicted_port_mask);
    put_u32_le(out, snapshot.timeline_set ? 1u : 0u);
    put_u32_le(out, snapshot.rom_loaded ? 1u : 0u);
    put_u32_le(out, snapshot.has_complete_frame ? 1u : 0u);
    put_u32_le(out, 0);
    put_u64_le(out, snapshot.timeline_epoch);
    put_u64_le(out, snapshot.next_frame_index);
    put_u64_le(out, snapshot.frame_sequence);
    put_u64_le(out, snapshot.pcm_producer_sequence);
    put_u64_le(out, snapshot.last_batch_sequence);
    put_u64_le(out, snapshot.last_source_time_ns);
    put_u64_le(out, snapshot.last_produced_time_ns);
    for (std::uint32_t button : snapshot.buttons)
    {
        put_u32_le(out, button);
    }
    for (std::uint64_t sequence : snapshot.input_sequence)
    {
        put_u64_le(out, sequence);
    }
    put_u32_le(out, static_cast<std::uint32_t>(snapshot.core.size()));
    put_u32_le(out, static_cast<std::uint32_t>(snapshot.pixels.size()));
    out.insert(out.end(), snapshot.core.begin(), snapshot.core.end());
    out.insert(out.end(), snapshot.pixels.begin(), snapshot.pixels.end());
    return out;
}

bool deserialize_snapshot(const std::uint8_t* bytes, std::size_t size, Snapshot& snapshot)
{
    snapshot = Snapshot{};
    if (bytes == nullptr || size < 8u)
    {
        return false;
    }
    const std::uint8_t* cursor = bytes;
    const std::uint8_t* const end = bytes + size;
    char magic[8];
    if (!read_bytes(cursor, end, magic, 8) || std::memcmp(magic, kCheckpointMagic, 8) != 0)
    {
        return false;
    }
    std::uint32_t version = 0;
    std::uint32_t reserved = 0;
    std::uint32_t timeline_set = 0;
    std::uint32_t rom_loaded = 0;
    std::uint32_t has_frame = 0;
    std::uint32_t reserved1 = 0;
    std::uint32_t core_size = 0;
    std::uint32_t pixel_size = 0;
    if (!read_u32_le(cursor, end, version) || version != FLY_RUNTIME_CHECKPOINT_VERSION_1 ||
        !read_u32_le(cursor, end, reserved) || reserved != 0u ||
        !read_u32_le(cursor, end, snapshot.sample_rate) ||
        snapshot.sample_rate < 44100u || snapshot.sample_rate > 96000u ||
        !read_u32_le(cursor, end, snapshot.predicted_port_mask) ||
        !read_u32_le(cursor, end, timeline_set) || !read_u32_le(cursor, end, rom_loaded) ||
        !read_u32_le(cursor, end, has_frame) || !read_u32_le(cursor, end, reserved1) ||
        reserved1 != 0u || !read_u64_le(cursor, end, snapshot.timeline_epoch) ||
        !read_u64_le(cursor, end, snapshot.next_frame_index) ||
        !read_u64_le(cursor, end, snapshot.frame_sequence) ||
        !read_u64_le(cursor, end, snapshot.pcm_producer_sequence) ||
        !read_u64_le(cursor, end, snapshot.last_batch_sequence) ||
        !read_u64_le(cursor, end, snapshot.last_source_time_ns) ||
        !read_u64_le(cursor, end, snapshot.last_produced_time_ns))
    {
        return false;
    }
    for (std::uint32_t& button : snapshot.buttons)
    {
        if (!read_u32_le(cursor, end, button))
        {
            return false;
        }
    }
    for (std::uint64_t& sequence : snapshot.input_sequence)
    {
        if (!read_u64_le(cursor, end, sequence))
        {
            return false;
        }
    }
    if (!read_u32_le(cursor, end, core_size) || !read_u32_le(cursor, end, pixel_size))
    {
        return false;
    }
    snapshot.timeline_set = timeline_set != 0u;
    snapshot.rom_loaded = rom_loaded != 0u;
    snapshot.has_complete_frame = has_frame != 0u;
    if ((snapshot.has_complete_frame && pixel_size != FLY_RUNTIME_RGB565_BYTES) ||
        (!snapshot.has_complete_frame && pixel_size != 0u) ||
        (snapshot.rom_loaded && core_size == 0u) ||
        (!snapshot.rom_loaded && core_size != 0u))
    {
        return false;
    }
    // Validate the complete payload before allocating from declared lengths.
    // Subtraction after the bound check avoids a core_size + pixel_size overflow.
    const std::size_t remaining = static_cast<std::size_t>(end - cursor);
    if (core_size > remaining || pixel_size != remaining - core_size)
    {
        return false;
    }
    snapshot.core.resize(core_size);
    snapshot.pixels.resize(pixel_size);
    if ((core_size != 0u && !read_bytes(cursor, end, snapshot.core.data(), core_size)) ||
        (pixel_size != 0u && !read_bytes(cursor, end, snapshot.pixels.data(), pixel_size)) ||
        cursor != end)
    {
        return false;
    }
    snapshot.valid = true;
    return true;
}

fly_result copy_published_frame(fly_runtime_handle& runtime)
{
    nes_video_snapshot snapshot{};
    snapshot.struct_size = sizeof(snapshot);
    snapshot.version = NES_STRUCT_VERSION;
    const int copied =
        nes_copy_video_frame(runtime.nes.get(), runtime.complete_pixels.data(),
                             runtime.complete_pixels.size(), &snapshot);
    if (copied < 0 || snapshot.width != FLY_RUNTIME_FRAME_WIDTH ||
        snapshot.height != FLY_RUNTIME_FRAME_HEIGHT ||
        snapshot.bytes_written != FLY_RUNTIME_RGB565_BYTES)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
    runtime.has_complete_frame = true;
    return FLY_RESULT_OK;
}

} // namespace

extern "C" fly_result fly_runtime_create(const fly_runtime_config* config,
                                         fly_runtime_t** runtime_out)
{
    if (runtime_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    *runtime_out = nullptr;
    const fly_result validation = validate_config(config);
    if (validation != FLY_RESULT_OK)
    {
        return validation;
    }
    try
    {
        auto runtime = std::make_unique<fly_runtime_handle>();
        nes_config nes_cfg{};
        nes_cfg.struct_size = sizeof(nes_cfg);
        nes_cfg.version = NES_STRUCT_VERSION;
        nes_cfg.favored_system = NES_FAVORED_NES_NTSC;
        nes_cfg.sample_rate =
            config->sample_rate == 0u ? FLY_RUNTIME_DEFAULT_SAMPLE_RATE : config->sample_rate;
        nes_cfg.pixfmt = NES_PIXFMT_RGB565;
        runtime->sample_rate = nes_cfg.sample_rate;
        runtime->nes.reset(nes_create(&nes_cfg));
        if (runtime->nes == nullptr)
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        runtime->audio_scratch.assign(kAudioScratchSamples, 0);
        runtime->pcm.assign(kPcmCapacity, 0);
        runtime->complete_pixels.assign(FLY_RUNTIME_RGB565_BYTES, 0);
        *runtime_out = runtime.release();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" void fly_runtime_destroy(fly_runtime_t* runtime)
{
    delete runtime;
}

extern "C" fly_result fly_runtime_load_rom(fly_runtime_t* runtime,
                                           const uint8_t* bytes,
                                           size_t size,
                                           const uint8_t* expected_sha256)
{
    if (runtime == nullptr || bytes == nullptr || size == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (expected_sha256 != nullptr)
    {
        std::uint8_t digest[kSha256Size];
        sha256(bytes, size, digest);
        if (std::memcmp(digest, expected_sha256, kSha256Size) != 0)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (runtime->nes == nullptr)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        runtime->reset_simulation();
        nes_rom_info info{};
        info.struct_size = sizeof(info);
        info.version = NES_STRUCT_VERSION;
        const int loaded = nes_load_rom(runtime->nes.get(), bytes, size, &info);
        if (loaded < 0)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
        runtime->rom_loaded = true;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_load_rom_fresh(fly_runtime_t* runtime,
                                                 const uint8_t* bytes,
                                                 size_t size,
                                                 const uint8_t* expected_sha256)
{
    if (runtime == nullptr || bytes == nullptr || size == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (expected_sha256 != nullptr)
    {
        std::uint8_t digest[kSha256Size];
        sha256(bytes, size, digest);
        if (std::memcmp(digest, expected_sha256, kSha256Size) != 0)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        runtime->reset_simulation();
        runtime->nes.reset();
        nes_config config{};
        config.struct_size = sizeof(config);
        config.version = NES_STRUCT_VERSION;
        config.favored_system = NES_FAVORED_NES_NTSC;
        config.sample_rate = runtime->sample_rate;
        config.pixfmt = NES_PIXFMT_RGB565;
        // The previous core is already retired. Keep the replacement local
        // until loaded so every failure (including an exception) destroys it
        // while holding the runtime mutex and leaves the runtime unloaded.
        std::unique_ptr<nes_t, NesDeleter> replacement(nes_create(&config));
        if (replacement == nullptr)
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        if (nes_load_rom(replacement.get(), bytes, size, nullptr) < 0)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
        runtime->nes = std::move(replacement);
        runtime->rom_loaded = true;
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_step_frame(fly_runtime_t* runtime,
                                             const fly_frame_input_v1* input,
                                             fly_frame_result_v1* result)
{
    const fly_result input_validation = validate_input(input);
    if (input_validation != FLY_RESULT_OK)
    {
        return input_validation;
    }
    if (runtime == nullptr || result == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (result->struct_size < FLY_FRAME_RESULT_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (result->version != FLY_FRAME_RESULT_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (!runtime->rom_loaded)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        if (!runtime->timeline_set)
        {
            if (input->frame_index != 0u)
            {
                return FLY_RESULT_INVALID_ARGUMENT;
            }
        }
        else if (input->timeline_epoch != runtime->timeline_epoch ||
                 input->frame_index != runtime->next_frame_index)
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }

        // A partially failed step must never expose the previous observation
        // paired with an already modified core.
        runtime->has_frame_digest = false;
        for (std::uint32_t port = 0; port < FLY_RUNTIME_PORT_COUNT; ++port)
        {
            nes_set_input(runtime->nes.get(), port, input->buttons[port]);
            runtime->buttons[port] = input->buttons[port];
            runtime->input_sequence[port] = input->input_sequence[port];
        }

        std::uint32_t frames_run = 0;
        std::uint32_t samples_written = 0;
        const int ran = nes_run_frames(runtime->nes.get(), 1u, runtime->audio_scratch.data(),
                                       static_cast<std::uint32_t>(runtime->audio_scratch.size()),
                                       &frames_run, &samples_written);
        if (ran < 0 || frames_run != 1u)
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }

        const std::uint64_t audio_first = runtime->pcm_producer_sequence;
        runtime->push_pcm(runtime->audio_scratch.data(), samples_written);
        const fly_result copied = copy_published_frame(*runtime);
        if (copied != FLY_RESULT_OK)
        {
            return copied;
        }

        runtime->timeline_set = true;
        runtime->timeline_epoch = input->timeline_epoch;
        runtime->next_frame_index = input->frame_index + 1u;
        runtime->predicted_port_mask = input->predicted_port_mask;
        runtime->last_batch_sequence = input->batch_sequence;
        runtime->last_source_time_ns = input->capture_time_ns;
        runtime->last_produced_time_ns = monotonic_now_ns();
        ++runtime->frame_sequence;

        // Hash the actual frame output before any audio callback can consume it.
        // Explicit byte order keeps this independent of host endianness.
        std::uint8_t pcm_bytes[kAudioScratchSamples * 2u];
        for (std::uint32_t index = 0; index < samples_written; ++index)
        {
            const auto sample = static_cast<std::uint16_t>(runtime->audio_scratch[index]);
            pcm_bytes[index * 2u] = static_cast<std::uint8_t>(sample);
            pcm_bytes[index * 2u + 1u] = static_cast<std::uint8_t>(sample >> 8u);
        }
        sha256(pcm_bytes, static_cast<std::size_t>(samples_written) * 2u,
               runtime->frame_pcm_sha256);
        runtime->has_frame_digest = true;

        fly_frame_result_v1 output{};
        output.struct_size = result->struct_size;
        output.version = result->version;
        output.timeline_epoch = runtime->timeline_epoch;
        output.frame_index = input->frame_index;
        output.post_state_frame = input->frame_index;
        output.frame_sequence = runtime->frame_sequence;
        output.audio_first_sample_sequence = audio_first;
        output.audio_last_sample_sequence =
            samples_written == 0u ? audio_first : (runtime->pcm_producer_sequence - 1u);
        std::copy(std::begin(runtime->input_sequence), std::end(runtime->input_sequence),
                  output.applied_input_sequence);
        output.source_time_ns = runtime->last_source_time_ns;
        output.produced_time_ns = runtime->last_produced_time_ns;
        output.video_published = 1;
        output.pcm_published = samples_written == 0u ? 0u : 1u;
        output.used_predicted_ports = input->predicted_port_mask == 0u ? 0u : 1u;
        output.reserved = 0;
        std::memcpy(result, &output, sizeof(output));
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_copy_frame_digest(fly_runtime_t* runtime,
                                                    uint64_t timeline_epoch,
                                                    uint64_t frame_index,
                                                    fly_runtime_frame_digest_v1* out)
{
    if (runtime == nullptr || out == nullptr ||
        out->struct_size < FLY_RUNTIME_FRAME_DIGEST_V1_SIZE ||
        out->version != FLY_RUNTIME_FRAME_DIGEST_VERSION_1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (!runtime->has_frame_digest || !runtime->has_complete_frame ||
            !runtime->timeline_set || runtime->timeline_epoch != timeline_epoch ||
            runtime->next_frame_index - 1u != frame_index)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        std::size_t written = 0;
        std::size_t needed = 0;
        const int query =
            nes_copy_canonical_state(runtime->nes.get(), nullptr, 0, &written, &needed);
        if (query != NES_ERR_BUFFER_TOO_SMALL || needed == 0)
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        std::vector<std::uint8_t> core(needed);
        const int copied = nes_copy_canonical_state(runtime->nes.get(), core.data(),
                                                     core.size(), &written, &needed);
        if (copied < 0 || written != core.size())
        {
            return FLY_RESULT_INTERNAL_ERROR;
        }
        // Presentation buffers retain their native RGB565 representation. Only
        // the digest encoding is little-endian, independently of the host.
        std::vector<std::uint8_t> pixels(runtime->complete_pixels.size());
        for (std::size_t offset = 0; offset < pixels.size(); offset += 2)
        {
            std::uint16_t pixel = 0;
            std::memcpy(&pixel, runtime->complete_pixels.data() + offset, sizeof(pixel));
            pixels[offset] = static_cast<std::uint8_t>(pixel);
            pixels[offset + 1] = static_cast<std::uint8_t>(pixel >> 8u);
        }
        fly_runtime_frame_digest_v1 digest{};
        digest.struct_size = out->struct_size;
        digest.version = out->version;
        digest.timeline_epoch = timeline_epoch;
        digest.frame_index = frame_index;
        digest.predicted_port_mask = runtime->predicted_port_mask;
        sha256(core.data(), core.size(), digest.state_sha256);
        sha256(pixels.data(), pixels.size(), digest.frame_sha256);
        std::memcpy(digest.pcm_sha256, runtime->frame_pcm_sha256, kSha256Size);
        std::memcpy(out, &digest, sizeof(digest));
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_get_source_timing(
    fly_runtime_t* runtime, fly_runtime_source_timing_v1* out)
{
    if (runtime == nullptr || out == nullptr ||
        out->struct_size < FLY_RUNTIME_SOURCE_TIMING_V1_SIZE ||
        out->version != FLY_RUNTIME_SOURCE_TIMING_VERSION_1)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (!runtime->rom_loaded || runtime->nes == nullptr)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        nes_rom_info info{};
        info.struct_size = sizeof(info);
        info.version = NES_STRUCT_VERSION;
        if (nes_get_rom_info(runtime->nes.get(), &info) < 0)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        const bool ntsc = info.region_ntsc != 0;
        fly_runtime_source_timing_v1 value{};
        value.struct_size = out->struct_size;
        value.version = FLY_RUNTIME_SOURCE_TIMING_VERSION_1;
        value.source_region = ntsc ? FLY_RUNTIME_SOURCE_REGION_NTSC
                                   : FLY_RUNTIME_SOURCE_REGION_PAL;
        value.frame_rate_numerator = ntsc ? NES_SOURCE_NTSC_RATE_NUMERATOR
                                          : NES_SOURCE_PAL_RATE_NUMERATOR;
        value.frame_rate_denominator = ntsc ? NES_SOURCE_NTSC_RATE_DENOMINATOR
                                            : NES_SOURCE_PAL_RATE_DENOMINATOR;
        value.sample_rate = runtime->sample_rate;
        std::memcpy(out, &value, sizeof(value));
        return FLY_RESULT_OK;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_clear_input_ports(fly_runtime_t* runtime,
                                                    uint32_t port_mask,
                                                    uint32_t reason)
{
    if (runtime == nullptr || !is_clear_reason(reason))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        runtime->has_frame_digest = false;
        for (std::uint32_t port = 0; port < FLY_RUNTIME_PORT_COUNT; ++port)
        {
            if ((port_mask & (1u << port)) == 0u)
            {
                continue;
            }
            runtime->buttons[port] = 0;
            if (runtime->nes != nullptr)
            {
                nes_set_input(runtime->nes.get(), port, 0);
            }
        }
        return FLY_RESULT_OK;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_copy_latest_frame(fly_runtime_t* runtime,
                                                    void* rgb565_out,
                                                    size_t cap,
                                                    fly_latest_frame_v1* meta_out)
{
    if (runtime == nullptr || meta_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (meta_out->struct_size < FLY_LATEST_FRAME_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (meta_out->version != FLY_LATEST_FRAME_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (cap > 0u && rgb565_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (!runtime->has_complete_frame)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        if (cap < FLY_RUNTIME_RGB565_BYTES)
        {
            return FLY_RESULT_BUFFER_TOO_SMALL;
        }
        std::memcpy(rgb565_out, runtime->complete_pixels.data(), FLY_RUNTIME_RGB565_BYTES);
        fly_latest_frame_v1 meta{};
        meta.struct_size = meta_out->struct_size;
        meta.version = meta_out->version;
        meta.width = FLY_RUNTIME_FRAME_WIDTH;
        meta.height = FLY_RUNTIME_FRAME_HEIGHT;
        meta.format = FLY_RUNTIME_PIXEL_FORMAT_RGB565;
        meta.reserved = 0;
        meta.timeline_epoch = runtime->timeline_epoch;
        meta.frame_index = runtime->next_frame_index == 0u ? 0u : runtime->next_frame_index - 1u;
        meta.frame_sequence = runtime->frame_sequence;
        std::copy(std::begin(runtime->input_sequence), std::end(runtime->input_sequence),
                  meta.applied_input_sequence);
        meta.source_time_ns = runtime->last_source_time_ns;
        meta.produced_time_ns = runtime->last_produced_time_ns;
        meta.bytes_written = FLY_RUNTIME_RGB565_BYTES;
        meta.reserved1 = 0;
        std::memcpy(meta_out, &meta, sizeof(meta));
        return FLY_RESULT_OK;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_pull_pcm(fly_runtime_t* runtime,
                                           int16_t* samples_out,
                                           uint32_t sample_capacity,
                                           fly_pcm_block_v1* block_out)
{
    if (runtime == nullptr || block_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (block_out->struct_size < FLY_PCM_BLOCK_V1_SIZE)
    {
        return FLY_RESULT_STRUCT_TOO_SMALL;
    }
    if (block_out->version != FLY_PCM_BLOCK_VERSION_1)
    {
        return FLY_RESULT_UNSUPPORTED_VERSION;
    }
    if (sample_capacity == 0u || samples_out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::unique_lock<std::mutex> lock(runtime->mutex, std::try_to_lock);
        fly_pcm_block_v1 block{};
        block.struct_size = block_out->struct_size;
        block.version = block_out->version;
        if (!lock.owns_lock() || runtime->pcm_count == 0u)
        {
            std::memset(samples_out, 0, static_cast<std::size_t>(sample_capacity) * sizeof(int16_t));
            block.sample_count = sample_capacity;
            block.first_sample_sequence = 0;
            block.media_time_ns = 0;
            std::memcpy(block_out, &block, sizeof(block));
            return FLY_RESULT_OK;
        }
        const std::uint32_t copied = std::min(sample_capacity, runtime->pcm_count);
        block.first_sample_sequence = runtime->pcm_read_sequence;
        block.sample_count = copied;
        block.media_time_ns =
            (block.first_sample_sequence * 1000000000ull) / runtime->sample_rate;
        for (std::uint32_t index = 0; index < copied; ++index)
        {
            const std::uint32_t slot =
                (runtime->pcm_read + index) % static_cast<std::uint32_t>(runtime->pcm.size());
            samples_out[index] = runtime->pcm[slot];
        }
        runtime->pcm_read =
            (runtime->pcm_read + copied) % static_cast<std::uint32_t>(runtime->pcm.size());
        runtime->pcm_count -= copied;
        runtime->pcm_read_sequence += copied;
        std::memcpy(block_out, &block, sizeof(block));
        return FLY_RESULT_OK;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_capture_rollback(fly_runtime_t* runtime, uint32_t slot)
{
    if (runtime == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (slot >= FLY_RUNTIME_ROLLBACK_SLOTS)
    {
        return FLY_RESULT_OUT_OF_RANGE;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        return runtime->capture_snapshot(runtime->rollback[slot]);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_restore_rollback(fly_runtime_t* runtime, uint32_t slot)
{
    if (runtime == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (slot >= FLY_RUNTIME_ROLLBACK_SLOTS)
    {
        return FLY_RESULT_OUT_OF_RANGE;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        if (!runtime->rollback[slot].valid)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        return runtime->apply_snapshot(runtime->rollback[slot], false);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

extern "C" fly_result fly_runtime_save_checkpoint(fly_runtime_t* runtime,
                                                  uint8_t* out,
                                                  size_t cap,
                                                  size_t* written,
                                                  size_t* needed)
{
    if (runtime == nullptr || written == nullptr || needed == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    if (cap > 0u && out == nullptr)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        std::lock_guard<std::mutex> lock(runtime->mutex);
        Snapshot snapshot;
        const fly_result captured = runtime->capture_snapshot(snapshot);
        if (captured != FLY_RESULT_OK)
        {
            *written = 0;
            *needed = 0;
            return captured;
        }
        const std::vector<std::uint8_t> bytes = serialize_snapshot(snapshot);
        *needed = bytes.size();
        if (cap < bytes.size())
        {
            *written = 0;
            return FLY_RESULT_BUFFER_TOO_SMALL;
        }
        std::memcpy(out, bytes.data(), bytes.size());
        *written = bytes.size();
        return FLY_RESULT_OK;
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

namespace {

// The fixed v1 header ends with the two payload lengths. Check them before
// constructing Snapshot: MSVC's checked std::vector allocates even when empty.
constexpr std::size_t kCheckpointFixedBytes =
    8u + 8u * sizeof(std::uint32_t) + 7u * sizeof(std::uint64_t) +
    FLY_RUNTIME_PORT_COUNT * (sizeof(std::uint32_t) + sizeof(std::uint64_t)) +
    2u * sizeof(std::uint32_t);

bool checkpoint_payload_length_valid(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr || size < kCheckpointFixedBytes) return false;
    const std::uint8_t* cursor = bytes + kCheckpointFixedBytes - 2u * sizeof(std::uint32_t);
    const std::uint8_t* const end = bytes + size;
    std::uint32_t core_size = 0;
    std::uint32_t pixel_size = 0;
    if (!read_u32_le(cursor, end, core_size) || !read_u32_le(cursor, end, pixel_size))
        return false;
    const std::size_t remaining = size - kCheckpointFixedBytes;
    return core_size <= remaining && pixel_size == remaining - core_size;
}

fly_result load_checkpoint(fly_runtime_t* runtime, const uint8_t* bytes, size_t size,
                           uint64_t expected_timeline_epoch)
{
    if (runtime == nullptr || !checkpoint_payload_length_valid(bytes, size))
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    try
    {
        Snapshot snapshot;
        if (!deserialize_snapshot(bytes, size, snapshot))
        {
            return FLY_RESULT_INVALID_ARGUMENT;
        }
        if (expected_timeline_epoch != 0u &&
            (!snapshot.rom_loaded || !snapshot.timeline_set ||
             snapshot.timeline_epoch != expected_timeline_epoch))
        {
            return FLY_RESULT_INVALID_STATE;
        }
        std::lock_guard<std::mutex> lock(runtime->mutex);
        // Loading a snapshot cannot reconfigure the core's audio output. Keep
        // the v1 format and same-configuration saves compatible, but reject a
        // different valid rate before touching core state or its observations.
        if (snapshot.sample_rate != runtime->sample_rate)
        {
            return FLY_RESULT_INVALID_STATE;
        }
        return runtime->apply_snapshot(snapshot, true);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_RESULT_OUT_OF_MEMORY;
    }
    catch (...)
    {
        return FLY_RESULT_INTERNAL_ERROR;
    }
}

} // namespace

extern "C" fly_result fly_runtime_load_checkpoint(fly_runtime_t* runtime,
                                                  const uint8_t* bytes,
                                                  size_t size)
{
    return load_checkpoint(runtime, bytes, size, 0);
}

extern "C" fly_result fly_runtime_load_checkpoint_for_epoch(
    fly_runtime_t* runtime, const uint8_t* bytes, size_t size,
    uint64_t expected_timeline_epoch)
{
    if (expected_timeline_epoch == 0u)
    {
        return FLY_RESULT_INVALID_ARGUMENT;
    }
    return load_checkpoint(runtime, bytes, size, expected_timeline_epoch);
}
