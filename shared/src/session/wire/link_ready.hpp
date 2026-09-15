#ifndef FLYNES_SESSION_WIRE_LINK_READY_HPP
#define FLYNES_SESSION_WIRE_LINK_READY_HPP

/*
 * W1 Task 3: exact LINK_READY_V1 wire codec, used for both READY (ready_phase
 * Ready) and the required ACK (ready_phase Ack). Layout, sizes and domain
 * strings come from the frozen W0 contract (../link/link_control_contract.hpp).
 *
 * READY is legal only in phase RECONCILE. It binds the sender's own persisted
 * HELLO, the peer HELLO it verified, the locally persisted negotiated result,
 * both verified resume summaries and the persisted merge result. The ACK uses
 * the same bytes with ready_phase = Ack and therefore binds the same hashes.
 *
 * Shares LinkControlSignatureVerifierV1, LinkControlIssueV1 and
 * LinkControlDecodeReportV1 with link_hello.hpp so that both control messages
 * are decoded and classified identically.
 */

#include "link_hello.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

/* Everything the receiver must already know before it will accept a READY. */
struct LinkReadyExpectationsV1 final
{
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t channel_id = 0;
    std::uint64_t channel_bind_id = 0;
    std::uint64_t connection_generation = 0;
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t link_generation = 0;
    /* The role of *this* receiver; the sender must be the mirror. */
    PairRoleV1 local_role = PairRoleV1::Initiator;
    /* Ready for the peer's READY, Ack for the peer's ACK. */
    link::LinkReadyPhaseV1 expected_phase = link::LinkReadyPhaseV1::Ready;
    std::array<std::uint8_t, 32> channel_bind_hash{};
    /* Our own persisted HELLO object hash. */
    std::array<std::uint8_t, 32> local_hello_object_hash{};
    /* The peer's own persisted HELLO object hash, as we verified it. */
    std::array<std::uint8_t, 32> peer_hello_object_hash{};
    std::array<std::uint8_t, 32> negotiated_result_hash{};
    std::array<std::uint8_t, 32> local_summary_hash{};
    std::array<std::uint8_t, 32> peer_summary_hash{};
    std::array<std::uint8_t, 32> merge_result_hash{};
    /* from the accepted binding */
    std::array<std::uint8_t, 65> peer_session_signing_public_key{};
};

/* Encodes bytes[0..320) and the digest it covers. */
Status build_link_ready_pretag_v1(
    const link::LinkReadyV1& value, P256PointValidatorV1 validate_point,
    void* validator_context,
    std::array<std::uint8_t, link::kLinkReadyPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept;

/* Appends the canonical low-S signature, producing the exact 384 bytes and the
 * persisted object hash domain_hash(kLinkReadyObjectHashDomainV1, bytes, 384). */
Status finish_link_ready_v1(
    const std::array<std::uint8_t, link::kLinkReadyPretagSizeV1>& pretag,
    const std::array<std::uint8_t, 64>& signature,
    std::array<std::uint8_t, link::kLinkReadySizeV1>* out_bytes,
    link::LinkReadyV1* out_value) noexcept;

/*
 * Stage 1: parse and validate every non-cryptographic READY/ACK field. Never
 * verifies the signature and never calls a provider.
 */
Status parse_link_ready_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkReadyExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlDecodeReportV1* out_report,
    LinkControlParsedV1* out_parsed, link::LinkReadyV1* out) noexcept;

/*
 * Stage 2: accept or fail closed once the asynchronous verification result for
 * exactly this parsed value has arrived.
 */
Status accept_link_ready_v1(const LinkControlParsedV1& parsed,
                            const link::LinkReadyV1& parsed_value,
                            LinkControlVerificationOutcomeV1 outcome,
                            LinkControlDecodeReportV1* out_report,
                            link::LinkReadyV1* out) noexcept;

/*
 * Two-stage convenience wrapper: parse, then synchronously verify through the
 * caller's own verifier. The scheduler does not use the verifier callback; it
 * drives the two stages separately through the asynchronous crypto port.
 */
Status decode_link_ready_v1(
    const std::uint8_t* bytes, std::size_t size,
    const LinkReadyExpectationsV1& expected, P256PointValidatorV1 validate_point,
    void* validator_context, LinkControlSignatureVerifierV1 verify_signature,
    void* verifier_context, LinkControlDecodeReportV1* out_report,
    link::LinkReadyV1* out) noexcept;

/*
 * Hash of the locally persisted negotiated capability/runtime result, under the
 * dedicated contract domain so a negotiated result can never be confused with a
 * control message. READY binds exactly this hash. The result's own byte layout
 * belongs to the layer that negotiates it (see the W1 report's integration
 * request); this codec only fixes the domain.
 */
Status hash_link_negotiated_result_v1(const std::uint8_t* bytes,
                                      std::size_t size,
                                      std::array<std::uint8_t, 32>* out_hash) noexcept;

} // namespace flynes::session::wire

#endif
