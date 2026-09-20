#ifndef FLYNES_SESSION_WIRE_PAIR_REVEAL_HPP
#define FLYNES_SESSION_WIRE_PAIR_REVEAL_HPP

#include "pair_handshake.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

inline constexpr std::size_t kPairRevealAadSizeV1 = 124;
inline constexpr std::size_t kPairRevealPlaintextSizeV1 = 320;
inline constexpr std::size_t kPairRevealCiphertextAndTagSizeV1 = 336;
inline constexpr std::size_t kPairRevealBodySizeV1 = 352;

struct PairRevealEnvelopeV1 final
{
    std::array<std::uint8_t, 12> public_nonce{};
    std::array<std::uint8_t, kPairRevealCiphertextAndTagSizeV1>
        ciphertext_and_tag{};
};

Status build_pair_reveal_aad_v1(
    const PairContextV1& context, const PairCommitV1& initiator_commit,
    const PairCommitV1& responder_commit, PairRoleV1 sender,
    std::array<std::uint8_t, kPairRevealAadSizeV1>* out) noexcept;

Status encode_pair_reveal_envelope_v1(
    const std::array<std::uint8_t, 12>& public_nonce,
    const std::array<std::uint8_t, kPairRevealCiphertextAndTagSizeV1>&
        ciphertext_and_tag,
    std::array<std::uint8_t, kPairRevealBodySizeV1>* out) noexcept;

Status decode_pair_reveal_envelope_v1(
    const std::uint8_t* bytes, std::size_t size,
    PairRevealEnvelopeV1* out) noexcept;

} // namespace flynes::session::wire

#endif
