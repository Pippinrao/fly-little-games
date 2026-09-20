#ifndef FLYNES_TESTS_NEARBY_HARNESS_NES_DUAL_RUNTIME_PORT_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_NES_DUAL_RUNTIME_PORT_HPP

#include "dual/dual_runtime_contract.hpp"
#include "flynes/flynes_runtime.h"
#include "flynes/flynes_session.h"
#include "wire/sha256.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <memory>
#include <new>
#include <vector>

namespace flynes::session::nes_port {

namespace dual = flynes::session::dual;

inline std::vector<std::uint8_t> read_rom()
{
    std::ifstream in(FLYNES_RUNTIME_ROM_FIXTURE, std::ios::binary);
    if (!in)
        return {};
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end <= 0)
        return {};
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(end));
    in.seekg(0, std::ios::beg);
    if (!in.read(reinterpret_cast<char*>(bytes.data()),
                 static_cast<std::streamsize>(end)))
        return {};
    return bytes;
}

class NesDualRuntimePortV1 final : public dual::DualRuntimePort
{
public:
    NesDualRuntimePortV1()
    {
        fly_runtime_config config{};
        config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
        config.version = FLY_RUNTIME_CONFIG_VERSION_1;
        config.sample_rate = FLY_RUNTIME_DEFAULT_SAMPLE_RATE;
        created_sample_rate = config.sample_rate;
        fly_runtime_create(&config, &runtime_);
    }

    ~NesDualRuntimePortV1() override { fly_runtime_destroy(runtime_); }

    fly_session_result_v2 load(const dual::DualContentRefV1& content) noexcept override
    {
        if (runtime_ == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        const auto rom = read_rom();
        if (rom.empty())
            return FLY_SESSION_V2_UNAVAILABLE;
        const auto digest = flynes::session::wire::sha256(rom.data(), rom.size());
        if (digest != content.content_hash)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (fly_runtime_load_rom(runtime_, rom.data(), rom.size(),
                                 content.content_hash.data()) != FLY_RESULT_OK)
            return FLY_SESSION_V2_INVALID_STATE;
        ++loads;
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 step(const dual::DualInputBundleV1& input,
                               dual::DualFrameOutcomeV1* out) noexcept override
    {
        if (runtime_ == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        fly_frame_input_v1 frame{};
        frame.struct_size = FLY_FRAME_INPUT_V1_SIZE;
        frame.version = FLY_FRAME_INPUT_VERSION_1;
        frame.timeline_epoch = input.key.timeline_epoch;
        frame.frame_index = input.key.frame_index;
        frame.predicted_port_mask = input.predicted_port_mask;
        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
        {
            frame.buttons[port] = input.ports[port].mask;
            frame.input_sequence[port] = input.ports[port].input_sequence;
        }
        fly_frame_result_v1 result{};
        result.struct_size = FLY_FRAME_RESULT_V1_SIZE;
        result.version = FLY_FRAME_RESULT_VERSION_1;
        if (fly_runtime_step_frame(runtime_, &frame, &result) != FLY_RESULT_OK)
            return FLY_SESSION_V2_INVALID_STATE;
        ++steps;
        mix_pulled_pcm();
        out->frame_index = result.frame_index;
        out->honoured_port_mask = 0x3u;
        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
            out->applied_input_sequence[port] = result.applied_input_sequence[port];
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 export_state(std::uint8_t* out, std::size_t capacity,
                                       std::size_t* out_written,
                                       std::array<std::uint8_t, 32>* out_hash) noexcept override
    {
        if (runtime_ == nullptr || out == nullptr || out_written == nullptr ||
            out_hash == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::size_t needed = 0;
        if (fly_runtime_save_checkpoint(runtime_, out, capacity, out_written,
                                        &needed) != FLY_RESULT_OK)
            return capacity < needed ? FLY_SESSION_V2_BUFFER_TOO_SMALL
                                     : FLY_SESSION_V2_INVALID_STATE;
        *out_hash = flynes::session::wire::sha256(out, *out_written);
        return FLY_SESSION_V2_OK;
    }

    fly_session_result_v2 import_state(const std::uint8_t* bytes,
                                       std::size_t size) noexcept override
    {
        if (runtime_ == nullptr || bytes == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        return fly_runtime_load_checkpoint(runtime_, bytes, size) == FLY_RESULT_OK
                   ? FLY_SESSION_V2_OK
                   : FLY_SESSION_V2_INVALID_STATE;
    }

    fly_session_result_v2 state_digest(std::uint64_t,
                                       dual::DualStateDigestV1* out) noexcept override
    {
        if (runtime_ == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::size_t written = 0;
        std::size_t needed = 0;
        const auto query = fly_runtime_save_checkpoint(runtime_, nullptr, 0,
                                                       &written, &needed);
        if (query != FLY_RESULT_BUFFER_TOO_SMALL || needed < 152u)
            return FLY_SESSION_V2_INVALID_STATE;
        std::unique_ptr<std::uint8_t[]> storage(
            new (std::nothrow) std::uint8_t[needed]);
        if (!storage)
            return FLY_SESSION_V2_UNAVAILABLE;
        if (fly_runtime_save_checkpoint(runtime_, storage.get(), needed, &written,
                                        &needed) != FLY_RESULT_OK ||
            written < 152u)
            return FLY_SESSION_V2_INVALID_STATE;
        std::uint32_t core_size = 0;
        std::uint32_t pixels_size = 0;
        std::memcpy(&core_size, storage.get() + 144, 4);
        std::memcpy(&pixels_size, storage.get() + 148, 4);
        if (static_cast<std::size_t>(152u) + core_size + pixels_size > written)
            return FLY_SESSION_V2_INVALID_STATE;
        out->state = flynes::session::wire::sha256(storage.get() + 152, core_size);
        out->frame = flynes::session::wire::sha256(storage.get() + 152 + core_size,
                                                   pixels_size);
        if (!pcm_have_)
            return FLY_SESSION_V2_INVALID_STATE;
        out->pcm = pcm_digest_;
        return FLY_SESSION_V2_OK;
    }

    std::array<std::uint8_t, 32> checkpoint_count_hash(std::size_t offset) noexcept
    {
        std::array<std::uint8_t, 32> hash{};
        std::size_t written = 0;
        std::size_t needed = 0;
        if (fly_runtime_save_checkpoint(runtime_, nullptr, 0, &written, &needed) !=
                FLY_RESULT_BUFFER_TOO_SMALL ||
            needed < offset + 8u)
            return hash;
        std::unique_ptr<std::uint8_t[]> storage(
            new (std::nothrow) std::uint8_t[needed]);
        if (!storage)
            return hash;
        if (fly_runtime_save_checkpoint(runtime_, storage.get(), needed, &written,
                                        &needed) != FLY_RESULT_OK ||
            written < offset + 8u)
            return hash;
        return flynes::session::wire::sha256(storage.get() + offset, 8);
    }

    std::uint64_t steps = 0;
    std::uint64_t loads = 0;
    std::uint32_t created_sample_rate = 0;
    std::uint64_t pcm_sample_count = 0;
    std::uint64_t pcm_first_sequence = 0;
    std::uint64_t pcm_last_media_time_ns = 0;

private:
    void mix_pulled_pcm() noexcept
    {
        constexpr std::uint32_t kCap = 4096u;
        std::int16_t samples[kCap];
        for (;;)
        {
            fly_pcm_block_v1 block{};
            block.struct_size = FLY_PCM_BLOCK_V1_SIZE;
            block.version = FLY_PCM_BLOCK_VERSION_1;
            if (fly_runtime_pull_pcm(runtime_, samples, kCap, &block) != FLY_RESULT_OK)
                return;
            if (block.sample_count == 0u ||
                (block.sample_count == kCap && block.first_sample_sequence == 0 &&
                 block.media_time_ns == 0))
                return;
            if (!pcm_have_)
                pcm_first_sequence = block.first_sample_sequence;
            pcm_sample_count += block.sample_count;
            pcm_last_media_time_ns = block.media_time_ns;
            std::uint8_t combined[32 + static_cast<std::size_t>(kCap) * 2];
            std::memcpy(combined, pcm_digest_.data(), pcm_digest_.size());
            std::memcpy(combined + pcm_digest_.size(), samples,
                        static_cast<std::size_t>(block.sample_count) *
                            sizeof(std::int16_t));
            pcm_digest_ = flynes::session::wire::sha256(
                combined, pcm_digest_.size() +
                              static_cast<std::size_t>(block.sample_count) *
                                  sizeof(std::int16_t));
            pcm_have_ = true;
            if (block.sample_count < kCap)
                return;
        }
    }

    fly_runtime_t* runtime_ = nullptr;
    std::array<std::uint8_t, 32> pcm_digest_{};
    bool pcm_have_ = false;
};

struct NesDualRuntimeCAbiV1 final
{
    NesDualRuntimePortV1 port;

    static void retain(void*) {}
    static void release(void*) {}

    static fly_session_result_v2 load(
        void* context, const fly_session_dual_content_ref_v2* content)
    {
        if (context == nullptr || content == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        dual::DualContentRefV1 value{};
        std::memcpy(value.session_id.data(), content->session_id, 16u);
        std::memcpy(value.branch_id.data(), content->branch_id, 16u);
        std::memcpy(value.content_hash.data(), content->content_hash, 32u);
        value.timeline_epoch = content->timeline_epoch;
        return static_cast<NesDualRuntimeCAbiV1*>(context)->port.load(value);
    }

    static fly_session_result_v2 step(
        void* context, const fly_session_dual_input_bundle_v2* input,
        fly_session_dual_frame_outcome_v2* out)
    {
        if (context == nullptr || input == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        dual::DualInputBundleV1 bundle{};
        std::memcpy(bundle.key.session_id.data(), input->session_id, 16u);
        std::memcpy(bundle.key.branch_id.data(), input->branch_id, 16u);
        bundle.key.timeline_epoch = input->timeline_epoch;
        bundle.key.frame_index = input->frame_index;
        bundle.key.seat_revision = input->seat_revision;
        bundle.logical_seat = input->logical_seat;
        bundle.predicted_port_mask = input->predicted_port_mask;
        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
        {
            bundle.ports[port].mask = input->ports[port].mask;
            bundle.ports[port].input_sequence = input->ports[port].input_sequence;
        }
        dual::DualFrameOutcomeV1 outcome{};
        const auto result =
            static_cast<NesDualRuntimeCAbiV1*>(context)->port.step(bundle,
                                                                   &outcome);
        if (result != FLY_SESSION_V2_OK)
            return result;
        *out = {};
        out->frame_index = outcome.frame_index;
        out->honoured_port_mask = outcome.honoured_port_mask;
        for (std::uint32_t port = 0; port < dual::kDualPortCountV1; ++port)
            out->applied_input_sequence[port] = outcome.applied_input_sequence[port];
        return FLY_SESSION_V2_OK;
    }

    static fly_session_result_v2 export_state(
        void* context, std::uint8_t* out, std::size_t capacity,
        std::size_t* out_written, std::uint8_t hash_out[32])
    {
        if (context == nullptr || out == nullptr || out_written == nullptr ||
            hash_out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        std::array<std::uint8_t, 32> hash{};
        const auto result = static_cast<NesDualRuntimeCAbiV1*>(context)->port.export_state(
            out, capacity, out_written, &hash);
        if (result != FLY_SESSION_V2_OK)
            return result;
        std::memcpy(hash_out, hash.data(), 32u);
        return FLY_SESSION_V2_OK;
    }

    static fly_session_result_v2 import_state(void* context, const std::uint8_t* bytes,
                                              std::size_t size)
    {
        if (context == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        return static_cast<NesDualRuntimeCAbiV1*>(context)->port.import_state(bytes,
                                                                              size);
    }

    static fly_session_result_v2 state_digest(void* context, std::uint64_t frame_index,
                                              fly_session_dual_state_digest_v2* out)
    {
        if (context == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        dual::DualStateDigestV1 digest{};
        const auto result =
            static_cast<NesDualRuntimeCAbiV1*>(context)->port.state_digest(
                frame_index, &digest);
        if (result != FLY_SESSION_V2_OK)
            return result;
        *out = {};
        std::memcpy(out->state, digest.state.data(), 32u);
        std::memcpy(out->frame, digest.frame.data(), 32u);
        std::memcpy(out->pcm, digest.pcm.data(), 32u);
        return FLY_SESSION_V2_OK;
    }
};

inline void bind_nes_dual_runtime(fly_session_dual_runtime_port_v2& port,
                                  NesDualRuntimeCAbiV1& runtime) noexcept
{
    port.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
    port.abi_version = FLY_SESSION_ABI_VERSION_2;
    port.context = &runtime;
    port.retain = NesDualRuntimeCAbiV1::retain;
    port.release = NesDualRuntimeCAbiV1::release;
    port.load = NesDualRuntimeCAbiV1::load;
    port.step = NesDualRuntimeCAbiV1::step;
    port.export_state = NesDualRuntimeCAbiV1::export_state;
    port.import_state = NesDualRuntimeCAbiV1::import_state;
    port.state_digest = NesDualRuntimeCAbiV1::state_digest;
}

} // namespace flynes::session::nes_port

#endif
