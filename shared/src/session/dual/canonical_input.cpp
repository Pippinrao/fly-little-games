#include "canonical_input.hpp"

#include "wire/sha256.hpp"

#include <cstddef>

namespace flynes::session::dual {
namespace {

constexpr char kCanonicalInputDigestDomainV1[] =
    "flynes-dual-canonical-input-v1";

void store_u64be(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (std::uint32_t index = 0; index < 8; ++index)
        out[index] = static_cast<std::uint8_t>(
            value >> (56u - (8u * index)));
}

bool all_zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t index = 0; index < size; ++index)
        if (bytes[index] != 0)
            return false;
    return true;
}

} // namespace

DualInputStatusV1 canonical_input_build_v1(
    const DualInputKeyV1& key, std::uint8_t logical_seat,
    const std::array<std::uint8_t, 32>& owner_signing_key_id,
    const DualPortInputArrayV1& samples,
    DualInputBundleV1* out) noexcept
{
    if (out == nullptr)
        return DualInputStatusV1::InvalidArgument;
    if (logical_seat >= static_cast<std::uint8_t>(kDualPortCountV1))
        return DualInputStatusV1::InvalidArgument;
    if (!dual_input_key_is_valid_v1(key))
        return DualInputStatusV1::InvalidKey;
    if (all_zero(owner_signing_key_id.data(), owner_signing_key_id.size()))
        return DualInputStatusV1::InvalidKey;

    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        if (samples[port].sequence == 0)
            return DualInputStatusV1::InvalidSequence;
        if (port != 0 && samples[port].sequence <= samples[port - 1].sequence)
            return DualInputStatusV1::InvalidSequence;
    }

    DualInputBundleV1 bundle{};
    bundle.key = key;
    bundle.logical_seat = logical_seat;
    bundle.predicted_port_mask = 0;
    bundle.owner_signing_key_id = owner_signing_key_id;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        const auto normalized = normalize_dual_port_mask_v1(samples[port].mask);
        if ((samples[port].mask & ~kDualFullPortMaskV1) != 0)
            return DualInputStatusV1::InvalidMask;
        bundle.ports[port].mask = normalized;
        bundle.ports[port].input_sequence = samples[port].sequence;
    }

    *out = bundle;
    return DualInputStatusV1::Ok;
}

bool canonical_input_is_canonical_v1(const DualInputBundleV1& bundle) noexcept
{
    if (!dual_input_key_is_valid_v1(bundle.key))
        return false;
    if (bundle.logical_seat >= static_cast<std::uint8_t>(kDualPortCountV1))
        return false;
    if (all_zero(bundle.owner_signing_key_id.data(),
                 bundle.owner_signing_key_id.size()))
        return false;
    if ((bundle.predicted_port_mask & ~kDualFullPortMaskV1) != 0)
        return false;
    for (const auto byte : bundle.reserved_zero0)
        if (byte != 0)
            return false;

    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        const auto& sample = bundle.ports[port];
        if (sample.input_sequence == 0)
            return false;
        if (port != 0 &&
            sample.input_sequence <= bundle.ports[port - 1].input_sequence)
            return false;
        if ((sample.mask & ~kDualFullPortMaskV1) != 0)
            return false;
        if (sample.mask != normalize_dual_port_mask_v1(sample.mask))
            return false;
    }
    return true;
}

bool canonical_input_is_duplicate_v1(const DualInputBundleV1& stored,
                                     const DualInputBundleV1& incoming) noexcept
{
    /* The prediction marker is derived metadata, not transmitted bytes, so it
     * is deliberately excluded: retransmitting the same real bytes is
     * idempotent whether or not either copy was framed as a prediction. */
    return stored == incoming;
}

std::array<std::uint8_t, 32> canonical_input_digest_v1(
    const DualInputBundleV1& bundle) noexcept
{
    /* Fixed 128 byte pre-image: key, seat, prediction marker, the complete
     * four-port sample array in canonical port order and the owner key. */
    std::array<std::uint8_t, 128> preimage{};
    std::size_t offset = 0;
    for (const auto byte : bundle.key.session_id)
        preimage[offset++] = byte;
    for (const auto byte : bundle.key.branch_id)
        preimage[offset++] = byte;
    store_u64be(preimage.data() + offset, bundle.key.timeline_epoch);
    offset += 8;
    store_u64be(preimage.data() + offset, bundle.key.frame_index);
    offset += 8;
    store_u64be(preimage.data() + offset, bundle.key.seat_revision);
    offset += 8;
    preimage[offset++] = bundle.logical_seat;
    preimage[offset++] = canonical_input_is_canonical_v1(bundle) ? 1u : 0u;
    preimage[offset++] =
        static_cast<std::uint8_t>(bundle.predicted_port_mask & kDualFullPortMaskV1);
    preimage[offset++] = 0;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port) {
        preimage[offset++] = static_cast<std::uint8_t>(bundle.ports[port].mask);
        store_u64be(preimage.data() + offset, bundle.ports[port].input_sequence);
        offset += 8;
    }
    for (const auto byte : bundle.owner_signing_key_id)
        preimage[offset++] = byte;

    return wire::domain_hash(kCanonicalInputDigestDomainV1, preimage.data(),
                             offset);
}

} // namespace flynes::session::dual
