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
    /* The 16-byte channel identity, exactly as wire::derive_channel_id_v1
     * produces it and as the bind codec consumes it. */
    std::array<std::uint8_t, 16> channel_id{};
    std::uint64_t connection_generation = 0;
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t link_generation = 0;
    /* The role of *this* receiver; the sender must be the mirror. */
    PairRoleV1 local_role = PairRoleV1::Initiator;
    /* Ready for the peer's READY, Ack for the peer's ACK. */
    link::LinkReadyPhaseV1 expected_phase = link::LinkReadyPhaseV1::Ready;
    /* wire::channel_bind_proof_hash_v1 of the authenticated bind proof. This is
     * the only bind identity READY carries; the former u64 channel_bind_id was
     * invented and has been deleted. */
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

/* Encodes the complete signature-free LINK_READY bytes and the digest it
 * covers. */
Status build_link_ready_pretag_v1(
    const link::LinkReadyV1& value, P256PointValidatorV1 validate_point,
    void* validator_context,
    std::array<std::uint8_t, link::kLinkReadyPretagSizeV1>* out_pretag,
    std::array<std::uint8_t, 32>* out_digest) noexcept;

/* Appends the canonical low-S signature, producing the exact 432 bytes and the
 * persisted object hash domain_hash(kLinkReadyObjectHashDomainV1, bytes, 432). */
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
 * Hash of the negotiated capability/runtime result under the dedicated contract
 * domain, so a negotiated result can never be confused with a control message.
 * READY binds exactly the value produced by negotiated_result_hash_v1() below.
 */
Status hash_link_negotiated_result_v1(const std::uint8_t* bytes,
                                      std::size_t size,
                                      std::array<std::uint8_t, 32>* out_hash) noexcept;

/*
 * The frozen preimage of the negotiated result that READY binds (owner decision
 * 2026-09-16). There is deliberately NO negotiated-result object format: the
 * "result" of negotiation is the pair of signed control objects both sides
 * already hold and have verified, in a fixed order.
 *
 *   preimage = initiator_hello_object_hash[32] || responder_hello_object_hash[32]
 *              (64 bytes, initiator first, both non-zero)
 *   hash     = domain_hash(kLinkNegotiatedResultHashDomainV1, preimage, 64)
 *
 * Both sides already carry local_hello_object_hash and peer_hello_object_hash in
 * READY, so the value can be recomputed and checked from READY alone.
 *
 * Fails closed on a null output or an all-zero input, so an unnegotiated attempt
 * can never install a zero result.
 */
Status negotiated_result_preimage_v1(
    const std::array<std::uint8_t, 32>& initiator_hello_object_hash,
    const std::array<std::uint8_t, 32>& responder_hello_object_hash,
    std::array<std::uint8_t, 64>* out_preimage) noexcept;

Status negotiated_result_hash_v1(
    const std::array<std::uint8_t, 32>& initiator_hello_object_hash,
    const std::array<std::uint8_t, 32>& responder_hello_object_hash,
    std::array<std::uint8_t, 32>* out_hash) noexcept;

} // namespace flynes::session::wire

#endif
