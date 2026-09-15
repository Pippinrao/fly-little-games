#ifndef FLYNES_SESSION_WIRE_LINK_HELLO_HPP
#define FLYNES_SESSION_WIRE_LINK_HELLO_HPP

/*
 * W1 Task 3: exact LINK_HELLO_V1 wire codec.
 *
 * Every constant, size and domain string is taken from the frozen W0 contract
 * (../link/link_control_contract.hpp). This header adds no competing layout.
 *
 * Scope: exact bytes, digest, object hash, field legality and the immutable
 * decoded value. No provider access, no UI, no SessionEngine.
 *
 * Signature model. LINK_HELLO carries the sender's *session signing* public key
 * and the hash of the sender's own persisted 0x0212 SessionSigningKeyBindingV1.
 * The binding is what authenticates that session key: it is signed by the
 * long-term identity key, so the caller MUST have accepted it through
 * wire::decode_session_signing_binding_v1(...) with the expected identity public
 * key before deriving LinkHelloExpectationsV1 from it. This codec then enforces
 * the remaining linkage itself:
 *   - sender_session_signing_public_key == expected.peer_session_signing_public_key
 *   - session_signing_binding_hash      == expected.peer_binding_hash
 *   - identity_verifier_ref.identity_public_key  == expected.peer_identity_public_key
 *   - identity_verifier_ref.identity_key_id      == expected.peer_identity_key_id
 *   - identity_verifier_ref.identity_key_id      == domain_hash("flynes-identity-key-id-v1",
 *                                                             identity_public_key)
 * so a sender cannot substitute a session key its identity never bound.
 *
 * Signature verification is mandatory and fail-closed: verify_signature must be
 * non-null, the digest passed to it is exactly
 * domain_hash(kLinkHelloDigestDomainV1, bytes[0..416), 416) and it is never
 * hashed a second time, and the signature must already be canonical low-S.
 */

#include "../link/link_control_contract.hpp"
#include "pair_handshake.hpp"
#include "session_codec.hpp"
#include "session_signing_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

/* Verifies a P-256 ECDSA signature over an already domain-separated digest.
 * Implementations must not re-hash the digest. */
using LinkControlSignatureVerifierV1 = bool (*)(
    void* context, const std::uint8_t public_key_x963[65],
    const std::uint8_t digest[32], const std::uint8_t signature[64]);

/*
 * First-failure classification. The Status alone cannot tell "a stale link is
 * still talking to us" from "the peer lied", and the handshake scheduler must
 * make exactly that distinction: a generation mismatch is dropped as STALE with
 * no side effect, while a bad signature fails the link closed.
 */
enum class LinkControlIssueV1 : std::uint8_t
{
    None = 0,
    Size,
    Reserved,
    Discriminant,
    Version,
    IdentityRef,
    BindingRef,
    ExpectedField,
    Generation,
    Capability,
    Signature
};

struct LinkControlDecodeReportV1 final
{
    Status status = Status::Ok;
    LinkControlIssueV1 issue = LinkControlIssueV1::None;
    link::LinkProposalSupportV1 proposal = link::LinkProposalSupportV1::Supported;
};

/*
 * Everything the receiver must already know about the sender before it will
 * accept a HELLO. Fields marked "from the accepted binding" come from
 * wire::SessionSigningBindingV1 as returned by
 * decode_session_signing_binding_v1(); the identity public key comes from the
 * accepted pair transcript.
 */
struct LinkHelloExpectationsV1 final
{
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t channel_id = 0;
    std::uint64_t connection_generation = 0;
    std::uint64_t link_generation = 0;
    /* The role of *this* receiver; the sender must be the mirror. */
    PairRoleV1 local_role = PairRoleV1::Initiator;
    std::array<std::uint8_t, 32> pair_transcript_object_hash{};
    /* Echoes of the locked plan and the locked bearer path; both sides lock the
     * same pair, so a HELLO that echoes anything else is rejected. */
    std::array<std::uint8_t, 32> selected_plan_hash{};
    std::array<std::uint8_t, 32> endpoint_offer_hash{};
    /* from the accepted binding */
    std::array<std::uint8_t, 32> peer_binding_hash{};
    std::array<std::uint8_t, 32> peer_identity_key_id{};
    std::array<std::uint8_t, 65> peer_session_signing_public_key{};
    /* from the accepted pair transcript */
    std::array<std::uint8_t, 65> peer_identity_public_key{};
};

/*
 * Encodes bytes[0..416) and the digest it covers. Reserved regions are written
 * zero; value.signature / value.digest / value.object_hash are ignored here.
 * Fails closed when the value would advertise anything outside
 * kLinkSupportedCapabilityMaskV1 (this release never advertises STREAM) or when
 * critical_extension_mask is nonzero.
 */
Status build_link_hello_pretag_v1(
    const link::LinkHelloV1& value, P256PointValidatorV1 validate_point,
    void* validator_context,
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept;

/*
 * Appends the canonical low-S signature, producing the exact 480 bytes and the
 * persisted object hash domain_hash(kLinkHelloObjectHashDomainV1, bytes, 480).
 */
Status finish_link_hello_v1(
    const std::array<std::uint8_t, link::kLinkHelloPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, link::kLinkHelloSizeV1>* out_bytes,
    link::LinkHelloV1* out_value) noexcept;

/*
 * Decodes and fully verifies a peer HELLO. On any non-Ok return *out is left
 * untouched apart from the report, so a caller can never observe a partially
 * populated value.
 */
Status decode_link_hello_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkHelloExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlSignatureVerifierV1 verify_signature,
    void* verifier_context, LinkControlDecodeReportV1* out_report,
    link::LinkHelloV1* out) noexcept;

/* True only for a canonical (nonzero, low-S) 64-byte signature encoding. */
bool link_control_signature_is_canonical_v1(
    const std::uint8_t signature[64]) noexcept;

/*
 * The long-term identity key id: domain_hash("flynes-identity-key-id-v1", key).
 * This is the exact derivation the 0x0212 binding uses, so a caller building
 * identity_verifier_ref outside this codec cannot drift from it.
 */
std::array<std::uint8_t, 32> link_identity_key_id_v1(
    const std::uint8_t public_key_x963[65]) noexcept;

} // namespace flynes::session::wire

#endif
