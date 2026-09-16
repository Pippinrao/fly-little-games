#include "dual_state_digest.hpp"

#include "canonical_input.hpp"
#include "wire/sha256.hpp"

#include <cstddef>
#include <vector>

namespace flynes::session::dual {
namespace {

constexpr char kStateDigestDomainV1[] = "flynes-dual-state-digest-v1";
constexpr char kFrameDigestDomainV1[] = "flynes-dual-frame-digest-v1";

void store_u64be(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (std::uint32_t index = 0; index < 8; ++index)
        out[index] =
            static_cast<std::uint8_t>(value >> (56u - (8u * index)));
}

} // namespace

DualStateDigestV1 dual_state_digest_v1(const std::uint8_t* state_bytes,
                                       std::size_t state_size,
                                       std::uint64_t frame_index) noexcept
{
    DualStateDigestV1 digest{};
    std::array<std::uint8_t, 8> frame_bytes{};
    store_u64be(frame_bytes.data(), frame_index);

    if (state_bytes == nullptr || state_size == 0) {
        digest.state = wire::domain_hash(kStateDigestDomainV1,
                                         frame_bytes.data(),
                                         frame_bytes.size());
    } else {
        std::array<std::uint8_t, 8> prefix{};
        store_u64be(prefix.data(), frame_index);
        /* SHA256(domain || u32be(size) || data) is the wire convention; feed
         * the frame index as the data prefix so the frame is bound. */
        std::vector<std::uint8_t> combined;
        combined.reserve(frame_bytes.size() + state_size);
        combined.insert(combined.end(), prefix.begin(), prefix.end());
        combined.insert(combined.end(), state_bytes, state_bytes + state_size);
        digest.state = wire::domain_hash(kStateDigestDomainV1,
                                         combined.data(), combined.size());
    }

    /* The frame component binds the frame index to the same state bytes. */
    {
        std::vector<std::uint8_t> combined;
        combined.reserve(8 + state_size);
        combined.insert(combined.end(), frame_bytes.begin(), frame_bytes.end());
        if (state_bytes != nullptr && state_size != 0)
            combined.insert(combined.end(), state_bytes,
                            state_bytes + state_size);
        digest.frame = wire::domain_hash(kFrameDigestDomainV1,
                                         combined.data(), combined.size());
    }

    /* PCM is a function of the same committed state; an empty state still
     * yields a stable, frame bound component. */
    {
        std::vector<std::uint8_t> combined;
        combined.reserve(16 + state_size);
        combined.insert(combined.end(), frame_bytes.begin(), frame_bytes.end());
        for (std::uint32_t index = 0; index < 8; ++index)
            combined.push_back(0xA5u);
        if (state_bytes != nullptr && state_size != 0)
            combined.insert(combined.end(), state_bytes,
                            state_bytes + state_size);
        digest.pcm = wire::domain_hash(kStateDigestDomainV1, combined.data(),
                                       combined.size());
    }

    return digest;
}

std::array<std::uint8_t, 32> dual_frame_digest_v1(
    std::uint64_t frame_index, const DualInputBundleV1& input) noexcept
{
    std::array<std::uint8_t, 8> frame_bytes{};
    store_u64be(frame_bytes.data(), frame_index);
    const auto input_digest = canonical_input_digest_v1(input);
    std::array<std::uint8_t, 40> preimage{};
    for (std::size_t index = 0; index < frame_bytes.size(); ++index)
        preimage[index] = frame_bytes[index];
    for (std::size_t index = 0; index < input_digest.size(); ++index)
        preimage[frame_bytes.size() + index] = input_digest[index];
    return wire::domain_hash(kFrameDigestDomainV1, preimage.data(),
                             preimage.size());
}

DualFreezeReasonV1 dual_digest_gate_v1(const DualStateDigestV1& local,
                                       const DualStateDigestV1& peer) noexcept
{
    return evaluate_dual_digest_v1(local, peer);
}

bool dual_digest_equal_v1(const DualStateDigestV1& lhs,
                          const DualStateDigestV1& rhs) noexcept
{
    return lhs == rhs;
}

} // namespace flynes::session::dual
