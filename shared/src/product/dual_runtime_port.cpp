#include <flynes/product/dual_runtime_port.hpp>

#include "dual/dual_runtime_contract.hpp"
#include "wire/sha256.hpp"

#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <utility>

namespace flynes::product {
namespace {
constexpr std::size_t kCheckpointHeaderSize = 96;
constexpr char kCheckpointMagic[] = "FLYDUAL1";

void write_u64_le(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (unsigned i = 0; i < 8; ++i)
        out[i] = static_cast<std::uint8_t>(value >> (i * 8));
}

std::uint64_t read_u64_le(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for (unsigned i = 0; i < 8; ++i)
        value |= static_cast<std::uint64_t>(bytes[i]) << (i * 8);
    return value;
}

fly_session_result_v2 result_of(fly_result result) noexcept
{
    switch (result) {
    case FLY_RESULT_OK: return FLY_SESSION_V2_OK;
    case FLY_RESULT_INVALID_ARGUMENT: return FLY_SESSION_V2_INVALID_ARGUMENT;
    case FLY_RESULT_OUT_OF_MEMORY: return FLY_SESSION_V2_OUT_OF_MEMORY;
    case FLY_RESULT_BUFFER_TOO_SMALL: return FLY_SESSION_V2_BUFFER_TOO_SMALL;
    default: return FLY_SESSION_V2_INVALID_STATE;
    }
}

bool any_byte(const std::uint8_t* value, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
        if (value[i] != 0) return true;
    return false;
}
} // namespace

struct ProductDualRuntimePort::Context final
{
    std::atomic<unsigned> references{1};
    fly_runtime_t* runtime;
    AuthorizedContentResolver resolver;
    fly_session_dual_content_ref_v2 binding{};
    bool bound = false;
    bool stepped = false;

    Context(fly_runtime_t* borrowed, AuthorizedContentResolver resolve)
        : runtime(borrowed), resolver(std::move(resolve)) {}

    static void retain(void* pointer) noexcept
    {
        if (pointer != nullptr)
            static_cast<Context*>(pointer)->references.fetch_add(1, std::memory_order_relaxed);
    }
    static void release(void* pointer) noexcept
    {
        if (pointer != nullptr &&
            static_cast<Context*>(pointer)->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
            delete static_cast<Context*>(pointer);
    }
    static fly_session_result_v2 load(void* pointer, const fly_session_dual_content_ref_v2* content) noexcept
    {
        if (pointer == nullptr || content == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto& self = *static_cast<Context*>(pointer);
        if (self.runtime == nullptr || content->timeline_epoch == 0 ||
            !any_byte(content->session_id, 16)) return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (!self.resolver) return FLY_SESSION_V2_PERMISSION_DENIED;
        try {
            const auto rom = self.resolver(*content);
            if (rom.result != FLY_SESSION_V2_OK)
                return rom.result < 0 ? rom.result : FLY_SESSION_V2_CONTRACT_VIOLATION;
            if (!rom.bytes || rom.bytes->empty()) return FLY_SESSION_V2_UNAVAILABLE;
            const auto hash = session::wire::sha256(rom.bytes->data(), rom.bytes->size());
            if (std::memcmp(hash.data(), content->content_hash, hash.size()) != 0)
                return FLY_SESSION_V2_INVALID_ARGUMENT;
            // Real ROM load may reset the borrowed core before reporting failure.
            // Publish no binding until that operation actually succeeds.
            self.bound = false;
            self.stepped = false;
            const auto result = fly_runtime_load_rom_fresh(self.runtime, rom.bytes->data(),
                                                     rom.bytes->size(), content->content_hash);
            if (result != FLY_RESULT_OK) return result_of(result);
            self.binding = *content;
            self.bound = true;
            return FLY_SESSION_V2_OK;
        } catch (const std::bad_alloc&) {
            return FLY_SESSION_V2_OUT_OF_MEMORY;
        } catch (...) {
            return FLY_SESSION_V2_UNAVAILABLE;
        }
    }

    static fly_session_result_v2 step(void* pointer, const fly_session_dual_input_bundle_v2* input,
                                      fly_session_dual_frame_outcome_v2* out) noexcept
    {
        if (pointer == nullptr || input == nullptr || out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (input->struct_size < FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE ||
            input->abi_version != FLY_SESSION_ABI_VERSION_2) return FLY_SESSION_V2_ABI_MISMATCH;
        if (any_byte(input->reserved_zero, 3) || input->seat_revision == 0 ||
            input->logical_seat >= FLY_SESSION_DUAL_PORT_COUNT_V2 ||
            (input->predicted_port_mask & ~0xFu) != 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
        for (unsigned p = 0; p < FLY_SESSION_DUAL_PORT_COUNT_V2; ++p) {
            const auto& sample = input->ports[p];
            if (sample.reserved_zero != 0 || sample.input_sequence == 0 ||
                (p != 0 && sample.input_sequence <= input->ports[p - 1].input_sequence) ||
                sample.mask != session::dual::normalize_dual_port_mask_v1(sample.mask))
                return FLY_SESSION_V2_INVALID_ARGUMENT;
        }
        auto& self = *static_cast<Context*>(pointer);
        if (!self.bound) return FLY_SESSION_V2_INVALID_STATE;
        if (std::memcmp(input->session_id, self.binding.session_id, 16) != 0 ||
            std::memcmp(input->branch_id, self.binding.branch_id, 16) != 0 ||
            input->timeline_epoch != self.binding.timeline_epoch) return FLY_SESSION_V2_STALE;
        fly_frame_input_v1 frame{};
        frame.struct_size = FLY_FRAME_INPUT_V1_SIZE;
        frame.version = FLY_FRAME_INPUT_VERSION_1;
        frame.timeline_epoch = input->timeline_epoch;
        frame.frame_index = input->frame_index;
        frame.predicted_port_mask = input->predicted_port_mask;
        for (unsigned p = 0; p < FLY_RUNTIME_PORT_COUNT; ++p) {
            frame.buttons[p] = input->ports[p].mask;
            frame.input_sequence[p] = input->ports[p].input_sequence;
        }
        fly_frame_result_v1 actual{};
        actual.struct_size = FLY_FRAME_RESULT_V1_SIZE;
        actual.version = FLY_FRAME_RESULT_VERSION_1;
        const auto result = fly_runtime_step_frame(self.runtime, &frame, &actual);
        if (result != FLY_RESULT_OK) return result_of(result);
        self.stepped = true;
        fly_session_dual_frame_outcome_v2 outcome{};
        outcome.frame_index = actual.frame_index;
        outcome.honoured_port_mask = 0x3u;
        for (unsigned p = 0; p < FLY_RUNTIME_PORT_COUNT; ++p)
            outcome.applied_input_sequence[p] = actual.applied_input_sequence[p];
        *out = outcome;
        return FLY_SESSION_V2_OK;
    }

    static fly_session_result_v2 state_digest(void* pointer, std::uint64_t frame,
                                              fly_session_dual_state_digest_v2* out) noexcept
    {
        if (pointer == nullptr || out == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto& self = *static_cast<Context*>(pointer);
        if (!self.bound || !self.stepped) return FLY_SESSION_V2_INVALID_STATE;
        fly_runtime_frame_digest_v1 digest{};
        digest.struct_size = FLY_RUNTIME_FRAME_DIGEST_V1_SIZE;
        digest.version = FLY_RUNTIME_FRAME_DIGEST_VERSION_1;
        const auto result = fly_runtime_copy_frame_digest(self.runtime, self.binding.timeline_epoch,
                                                         frame, &digest);
        if (result != FLY_RESULT_OK) return result_of(result);
        if (digest.predicted_port_mask != 0) return FLY_SESSION_V2_INVALID_STATE;
        fly_session_dual_state_digest_v2 value{};
        std::memcpy(value.state, digest.state_sha256, 32);
        std::memcpy(value.frame, digest.frame_sha256, 32);
        std::memcpy(value.pcm, digest.pcm_sha256, 32);
        *out = value;
        return FLY_SESSION_V2_OK;
    }

    static fly_session_result_v2 export_state(void* pointer, std::uint8_t* out, std::size_t capacity,
                                              std::size_t* out_written, std::uint8_t* hash_out) noexcept
    {
        if (pointer == nullptr || out == nullptr || out_written == nullptr || hash_out == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto& self = *static_cast<Context*>(pointer);
        if (!self.bound || !self.stepped) return FLY_SESSION_V2_INVALID_STATE;
        try {
            std::size_t written = 0, needed = 0;
            const auto query = fly_runtime_save_checkpoint(self.runtime, nullptr, 0, &written, &needed);
            if (query != FLY_RESULT_BUFFER_TOO_SMALL) return result_of(query);
            if (needed > std::numeric_limits<std::size_t>::max() - kCheckpointHeaderSize)
                return FLY_SESSION_V2_OUT_OF_MEMORY;
            const auto total = needed + kCheckpointHeaderSize;
            if (capacity < total) return FLY_SESSION_V2_BUFFER_TOO_SMALL;
            std::vector<std::uint8_t> envelope(total);
            auto result = fly_runtime_save_checkpoint(self.runtime, envelope.data() + kCheckpointHeaderSize,
                                                      needed, &written, &needed);
            if (result != FLY_RESULT_OK) return result_of(result);
            if (written != total - kCheckpointHeaderSize) return FLY_SESSION_V2_CONTRACT_VIOLATION;
            std::memcpy(envelope.data(), kCheckpointMagic, 8);
            // The initialized u64 encodes version u32=1 and reserved u32=0.
            write_u64_le(envelope.data() + 8, 1);
            std::memcpy(envelope.data() + 16, self.binding.session_id, 16);
            std::memcpy(envelope.data() + 32, self.binding.branch_id, 16);
            std::memcpy(envelope.data() + 48, self.binding.content_hash, 32);
            write_u64_le(envelope.data() + 80, self.binding.timeline_epoch);
            write_u64_le(envelope.data() + 88, written);
            const auto hash = session::wire::sha256(envelope.data(), envelope.size());
            std::memcpy(out, envelope.data(), total);
            std::memcpy(hash_out, hash.data(), hash.size());
            *out_written = total;
            return FLY_SESSION_V2_OK;
        } catch (const std::bad_alloc&) {
            return FLY_SESSION_V2_OUT_OF_MEMORY;
        } catch (...) {
            return FLY_SESSION_V2_UNAVAILABLE;
        }
    }

    static fly_session_result_v2 import_state(void* pointer, const std::uint8_t* bytes,
                                              std::size_t size) noexcept
    {
        if (pointer == nullptr || bytes == nullptr || size <= kCheckpointHeaderSize)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto& self = *static_cast<Context*>(pointer);
        if (!self.bound) return FLY_SESSION_V2_INVALID_STATE;
        if (std::memcmp(bytes, kCheckpointMagic, 8) != 0 || read_u64_le(bytes + 8) != 1 ||
            read_u64_le(bytes + 88) != size - kCheckpointHeaderSize)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (std::memcmp(bytes + 16, self.binding.session_id, 16) != 0 ||
            std::memcmp(bytes + 32, self.binding.branch_id, 16) != 0 ||
            std::memcmp(bytes + 48, self.binding.content_hash, 32) != 0 ||
            read_u64_le(bytes + 80) != self.binding.timeline_epoch)
            return FLY_SESSION_V2_STALE;
        const auto result = fly_runtime_load_checkpoint_for_epoch(
            self.runtime, bytes + kCheckpointHeaderSize, size - kCheckpointHeaderSize,
            self.binding.timeline_epoch);
        if (result != FLY_RESULT_OK) return result_of(result);
        self.stepped = false;
        return FLY_SESSION_V2_OK;
    }
};

ProductDualRuntimePort::ProductDualRuntimePort(fly_runtime_t* runtime, AuthorizedContentResolver resolver)
    : context_(new Context(runtime, std::move(resolver))) {}
ProductDualRuntimePort::~ProductDualRuntimePort() { Context::release(context_); }
fly_session_dual_runtime_port_v2 ProductDualRuntimePort::port() const noexcept
{
    fly_session_dual_runtime_port_v2 value{};
    value.struct_size = FLY_SESSION_DUAL_RUNTIME_PORT_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.context = context_;
    value.retain = Context::retain;
    value.release = Context::release;
    value.load = Context::load;
    value.step = Context::step;
    value.export_state = Context::export_state;
    value.import_state = Context::import_state;
    value.state_digest = Context::state_digest;
    return value;
}
} // namespace flynes::product
