#include "dual_runtime_adapter.hpp"

#include <cstring>

namespace flynes::session::dual {
namespace {

fly_session_dual_content_ref_v2 to_abi(const DualContentRefV1& content) noexcept
{
    fly_session_dual_content_ref_v2 value{};
    std::memcpy(value.session_id, content.session_id.data(), 16u);
    std::memcpy(value.branch_id, content.branch_id.data(), 16u);
    std::memcpy(value.content_hash, content.content_hash.data(), 32u);
    value.timeline_epoch = content.timeline_epoch;
    return value;
}

fly_session_dual_input_bundle_v2 to_abi(
    const DualInputBundleV1& bundle) noexcept
{
    fly_session_dual_input_bundle_v2 value{};
    value.struct_size = FLY_SESSION_DUAL_INPUT_BUNDLE_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::memcpy(value.session_id, bundle.key.session_id.data(), 16u);
    std::memcpy(value.branch_id, bundle.key.branch_id.data(), 16u);
    value.timeline_epoch = bundle.key.timeline_epoch;
    value.frame_index = bundle.key.frame_index;
    value.seat_revision = bundle.key.seat_revision;
    value.logical_seat = bundle.logical_seat;
    value.predicted_port_mask = bundle.predicted_port_mask;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
    {
        value.ports[port].mask = bundle.ports[port].mask;
        value.ports[port].input_sequence = bundle.ports[port].input_sequence;
    }
    return value;
}

void from_abi(const fly_session_dual_frame_outcome_v2& value,
              DualFrameOutcomeV1* out) noexcept
{
    out->frame_index = value.frame_index;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
        out->applied_input_sequence[port] = value.applied_input_sequence[port];
    out->honoured_port_mask = value.honoured_port_mask;
}

} // namespace

CAbiDualRuntimePortV1::CAbiDualRuntimePortV1(
    const fly_session_dual_runtime_port_v2* port) noexcept
    : port_(port)
{
}

fly_session_result_v2 CAbiDualRuntimePortV1::load(
    const DualContentRefV1& content) noexcept
{
    if (port_ == nullptr || port_->load == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    const auto value = to_abi(content);
    ++forwarded_;
    return port_->load(port_->context, &value);
}

fly_session_result_v2 CAbiDualRuntimePortV1::step(
    const DualInputBundleV1& input, DualFrameOutcomeV1* out) noexcept
{
    if (port_ == nullptr || port_->step == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    const auto value = to_abi(input);
    fly_session_dual_frame_outcome_v2 outcome{};
    ++forwarded_;
    const auto result = port_->step(port_->context, &value, &outcome);
    /*
     * The outcome is copied back only on OK. A provider that fails must not be
     * able to leave a half-written frame index or a stale sequence array behind
     * for the scheduler to read as this frame's result, so a failing call leaves
     * `out` exactly as the caller initialised it.
     */
    if (result == FLY_SESSION_V2_OK)
        from_abi(outcome, out);
    return result;
}

fly_session_result_v2 CAbiDualRuntimePortV1::export_state(
    std::uint8_t* out, std::size_t capacity, std::size_t* out_written,
    std::array<std::uint8_t, 32>* out_hash) noexcept
{
    if (port_ == nullptr || port_->export_state == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    if (out == nullptr || out_written == nullptr || out_hash == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    std::uint8_t hash[32] = {};
    std::size_t written = 0;
    ++forwarded_;
    const auto result =
        port_->export_state(port_->context, out, capacity, &written, hash);
    if (result != FLY_SESSION_V2_OK)
        return result;
    if (written > capacity)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    *out_written = written;
    std::memcpy(out_hash->data(), hash, 32u);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 CAbiDualRuntimePortV1::import_state(
    const std::uint8_t* bytes, std::size_t size) noexcept
{
    if (port_ == nullptr || port_->import_state == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    if (bytes == nullptr && size != 0u)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    ++forwarded_;
    return port_->import_state(port_->context, bytes, size);
}

fly_session_result_v2 CAbiDualRuntimePortV1::state_digest(
    std::uint64_t frame_index, DualStateDigestV1* out) noexcept
{
    if (port_ == nullptr || port_->state_digest == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    if (out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    fly_session_dual_state_digest_v2 value{};
    ++forwarded_;
    const auto result =
        port_->state_digest(port_->context, frame_index, &value);
    if (result != FLY_SESSION_V2_OK)
        return result;
    static_assert(sizeof(DualStateDigestV1) ==
                      sizeof(fly_session_dual_state_digest_v2),
                  "the frozen DUAL digest and its ABI mirror stay the same size");
    for (std::size_t index = 0; index < 32u; ++index)
    {
        out->state[index] = value.state[index];
        out->frame[index] = value.frame[index];
        out->pcm[index] = value.pcm[index];
    }
    return FLY_SESSION_V2_OK;
}

} // namespace flynes::session::dual
