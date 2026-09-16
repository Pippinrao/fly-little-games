#include "canonical_input_wire.hpp"

#include "wire/session_codec.hpp"

#include <cstring>

namespace flynes::session::dual {
namespace {

constexpr std::size_t kVersionOffset = 0;
constexpr std::size_t kSessionIdOffset = 2;
constexpr std::size_t kBranchIdOffset = 18;
constexpr std::size_t kAuthorityTermOffset = 34;
constexpr std::size_t kTimelineEpochOffset = 42;
constexpr std::size_t kSeatRevisionOffset = 50;
constexpr std::size_t kModeGenerationOffset = 58;
constexpr std::size_t kFrameIndexOffset = 66;
constexpr std::size_t kBatchSequenceOffset = 74;
constexpr std::size_t kPredictedMaskOffset = 82;
constexpr std::size_t kReservedOffset = 83;
constexpr std::size_t kPortsOffset = 90;
constexpr std::size_t kPortSlotBytes = 16;

void store_u16be(std::uint8_t* out, std::uint16_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

void store_u32be(std::uint8_t* out, std::uint32_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

std::uint32_t load_u32be(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           static_cast<std::uint32_t>(bytes[3]);
}

void store_u64be(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (int index = 0; index < 8; ++index)
        out[index] = static_cast<std::uint8_t>(value >> ((7 - index) * 8));
}

std::uint64_t load_u64be(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for (int index = 0; index < 8; ++index)
        value = (value << 8u) | static_cast<std::uint64_t>(bytes[index]);
    return value;
}

bool source_kind_is_known(std::uint8_t value) noexcept
{
    return value >= 1u && value <= 7u;
}

} // namespace

DualInputWireStatusV1 encode_dual_input_bundle_v1(
    const DualInputBundleV1& bundle, const DualInputWireContextV1& context,
    std::uint8_t out[kDualInputWireBytesV1]) noexcept
{
    if (out == nullptr)
        return DualInputWireStatusV1::InvalidArgument;
    if (!canonical_input_is_canonical_v1(bundle))
        return DualInputWireStatusV1::InvalidBundle;
    if (context.authority_term == 0 || context.mode_generation == 0 ||
        context.batch_sequence == 0)
        return DualInputWireStatusV1::InvalidContext;

    std::uint8_t bytes[kDualInputWireBytesV1] = {};
    store_u16be(bytes + kVersionOffset, 1u);
    std::memcpy(bytes + kSessionIdOffset, bundle.key.session_id.data(), 16u);
    std::memcpy(bytes + kBranchIdOffset, bundle.key.branch_id.data(), 16u);
    store_u64be(bytes + kAuthorityTermOffset, context.authority_term);
    store_u64be(bytes + kTimelineEpochOffset, bundle.key.timeline_epoch);
    store_u64be(bytes + kSeatRevisionOffset, bundle.key.seat_revision);
    store_u64be(bytes + kModeGenerationOffset, context.mode_generation);
    store_u64be(bytes + kFrameIndexOffset, bundle.key.frame_index);
    store_u64be(bytes + kBatchSequenceOffset, context.batch_sequence);
    /* reserved_zero[7] at 83 stays zero. */

    std::uint32_t predicted_mask = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
    {
        const bool predicted =
            (bundle.predicted_port_mask & (1u << port)) != 0;
        DualPortSourceKindV1 kind = predicted ? DualPortSourceKindV1::Predicted
                                              : DualPortSourceKindV1::RealSample;
        DualPortClearReasonV1 reason = DualPortClearReasonV1::None;
        if (context.stated[port])
        {
            kind = context.source_kind[port];
            reason = context.clear_reason[port];
            /* A prediction marker may not disagree with the stated kind: a port
             * marked predicted must be PREDICTED, and a port marked real may not
             * be. The decoder derives the marker from the kind, so a mismatch
             * would silently change the receiver's view of reality. */
            if (predicted != (kind == DualPortSourceKindV1::Predicted))
                return DualInputWireStatusV1::InvalidContext;
        }
        if (!source_kind_is_known(static_cast<std::uint8_t>(kind)))
            return DualInputWireStatusV1::InvalidContext;
        if (static_cast<std::uint8_t>(reason) > 4u)
            return DualInputWireStatusV1::InvalidContext;
        if (kind == DualPortSourceKindV1::Predicted)
            predicted_mask |= 1u << port;

        const std::uint32_t raw = bundle.ports[port].mask;
        if ((raw & ~kDualFullPortMaskV1) != 0u)
            return DualInputWireStatusV1::InvalidBundle;
        /*
         * The impossible d-pad state never leaves the send path. The canonical
         * builder already clears both bits of an opposing pair
         * (normalize_dual_port_mask_v1) and canonical_input_is_canonical_v1
         * already refuses a bundle that skipped it, so a mask that still differs
         * from its normalized form here is a hand-built bundle: the encoder
         * refuses it rather than emitting a contradiction the peer would have to
         * resolve.
         */
        const std::uint32_t mask = normalize_dual_port_mask_v1(raw);
        if (mask != raw)
            return DualInputWireStatusV1::InvalidBundle;

        std::uint8_t* slot = bytes + kPortsOffset + port * kPortSlotBytes;
        slot[0] = static_cast<std::uint8_t>(kind);
        slot[1] = static_cast<std::uint8_t>(reason);
        /* slot[2..4) reserved, already zero. */
        store_u32be(slot + 4, mask);
        store_u64be(slot + 8, bundle.ports[port].input_sequence);
    }
    bytes[kPredictedMaskOffset] = static_cast<std::uint8_t>(predicted_mask);
    std::memset(bytes + kReservedOffset, 0, 7u);

    /* The release's own validator is the authority for these bytes: if it
     * refuses them, the encoder is wrong and must say so instead of handing a
     * record to the transport that the peer would reject. */
    std::uint8_t hash[32] = {};
    if (wire::check("CanonicalInputBundleV1", bytes, kDualInputWireBytesV1,
                    hash) != wire::Status::Ok)
        return DualInputWireStatusV1::InvalidBundle;

    std::memcpy(out, bytes, kDualInputWireBytesV1);
    return DualInputWireStatusV1::Ok;
}

DualInputWireStatusV1 decode_dual_input_bundle_v1(
    const std::uint8_t* bytes, std::size_t size, std::uint8_t logical_seat,
    const std::array<std::uint8_t, 32>& owner_signing_key_id,
    DualInputBundleV1* out) noexcept
{
    if (bytes == nullptr || out == nullptr ||
        size != kDualInputWireBytesV1)
        return DualInputWireStatusV1::InvalidArgument;
    if (logical_seat >= kDualPortCountV1)
        return DualInputWireStatusV1::InvalidArgument;

    /* The frozen validator runs first: length, version, reserved bytes, every
     * enum, the prediction/source_kind agreement and the d-pad rule are all
     * enforced there, not re-implemented here. */
    std::uint8_t hash[32] = {};
    if (wire::check("CanonicalInputBundleV1", bytes, size, hash) !=
        wire::Status::Ok)
        return DualInputWireStatusV1::InvalidBytes;

    DualInputBundleV1 bundle{};
    std::memcpy(bundle.key.session_id.data(), bytes + kSessionIdOffset, 16u);
    std::memcpy(bundle.key.branch_id.data(), bytes + kBranchIdOffset, 16u);
    bundle.key.timeline_epoch = load_u64be(bytes + kTimelineEpochOffset);
    bundle.key.frame_index = load_u64be(bytes + kFrameIndexOffset);
    bundle.key.seat_revision = load_u64be(bytes + kSeatRevisionOffset);
    bundle.logical_seat = logical_seat;
    bundle.owner_signing_key_id = owner_signing_key_id;

    std::uint32_t predicted_mask = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
    {
        const std::uint8_t* slot = bytes + kPortsOffset + port * kPortSlotBytes;
        const std::uint8_t kind = slot[0];
        if (kind == static_cast<std::uint8_t>(DualPortSourceKindV1::Predicted))
            predicted_mask |= 1u << port;
        bundle.ports[port].mask = load_u32be(slot + 4);
        bundle.ports[port].input_sequence = load_u64be(slot + 8);
    }
    bundle.predicted_port_mask = predicted_mask;
    bundle.reserved_zero0[0] = 0;
    bundle.reserved_zero0[1] = 0;
    bundle.reserved_zero0[2] = 0;

    /* A decoded record is only a legal input unit when the canonical-form
     * predicate accepts it, which includes the non-zero, strictly increasing
     * per-port sequence rule. */
    if (!canonical_input_is_canonical_v1(bundle))
        return DualInputWireStatusV1::InvalidBytes;

    *out = bundle;
    return DualInputWireStatusV1::Ok;
}

} // namespace flynes::session::dual
