#include "flynes/flynes_session.h"

#include "link/link_control_contract.hpp"
#include "wire/app_frame.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

namespace link = flynes::session::link;
namespace wire = flynes::session::wire;

int failures = 0;

void check(bool value, const char* message)
{
    if (!value)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

/* ------------------------------------------------------- static contract */

/* Object kind allocation must not collide with the registered schema. */
static_assert(link::kLinkHelloObjectKindV1 == 0x0216, "LINK_HELLO kind");
static_assert(link::kLinkReadyObjectKindV1 == 0x0217, "LINK_READY kind");
static_assert(link::kLinkHelloObjectKindV1 != 0x0212, "must not reuse 0x0212");
static_assert(link::kLinkReadyObjectKindV1 != 0x0213, "must not reuse 0x0213");
static_assert(link::kLinkHelloObjectKindV1 != link::kLinkReadyObjectKindV1,
              "kinds must be distinct");

/* Control message tags stay inside the 0xFF00 message range. */
static_assert(link::kLinkHelloMessageTagV1 == 0xFF06, "LINK_HELLO tag");
static_assert(link::kLinkReadyMessageTagV1 == 0xFF07, "LINK_READY tag");
static_assert(link::kLinkHelloMessageTagV1 >= 0xFF00, "tags are 0xFF00+");
static_assert(link::kLinkReadyMessageTagV1 >= 0xFF00, "tags are 0xFF00+");

/* Exact frozen sizes, after the owner-authorized 2026-09-16 channel-identity
 * correction: the u64 channel_id became 16 bytes and the invented u64
 * channel_bind_id was deleted, so LINK_HELLO grew by 8 and LINK_READY shrank by
 * 16 relative to the first revision. The offsets themselves are pinned by
 * static_assert in the contract header. */
static_assert(link::kLinkHelloSizeV1 == 488, "LINK_HELLO exact size");
static_assert(link::kLinkReadySizeV1 == 432, "LINK_READY exact size");
static_assert(link::kLinkHelloPretagSizeV1 == 424, "LINK_HELLO pretag");
static_assert(link::kLinkReadyPretagSizeV1 == 368, "LINK_READY pretag");
static_assert(link::kLinkHelloChannelIdOffsetV1 == 48, "HELLO channel_id at 48");
static_assert(link::kLinkHelloSignatureOffsetV1 == 424,
              "HELLO signature at 424");
static_assert(link::kLinkReadyChannelIdOffsetV1 == 48, "READY channel_id at 48");
static_assert(link::kLinkReadyChannelBindHashOffsetV1 == 88,
              "READY channel_bind_hash at 88");
static_assert(link::kLinkReadySignatureOffsetV1 == 368,
              "READY signature at 368");
static_assert(sizeof(link::LinkIdentityVerifierRefV1) ==
                  wire::kIdentityVerifierRefSizeV1,
              "identity ref stays 112 bytes");
static_assert(sizeof(link::LinkIdentityVerifierRefV1) == 112,
              "identity ref is exactly 112 bytes");

/* Protocol phase and ready-phase discriminants are frozen values. */
static_assert(static_cast<std::uint8_t>(link::LinkPhaseV1::Initial) == 1,
              "phase INITIAL");
static_assert(static_cast<std::uint8_t>(link::LinkPhaseV1::Reconcile) == 2,
              "phase RECONCILE");
static_assert(static_cast<std::uint8_t>(link::LinkReadyPhaseV1::Ready) == 1,
              "ready phase READY");
static_assert(static_cast<std::uint8_t>(link::LinkReadyPhaseV1::Ack) == 2,
              "ready phase ACK");

/* Projected link states must equal the public ABI discriminants. */
static_assert(static_cast<std::uint8_t>(link::LinkControlStateV1::Connecting) ==
                  FLY_SESSION_LINK_CONNECTING_V2,
              "CONNECTING mirrors the public ABI");
static_assert(static_cast<std::uint8_t>(
                  link::LinkControlStateV1::ConnectedLobby) ==
                  FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
              "CONNECTED_LOBBY mirrors the public ABI");
static_assert(static_cast<std::uint8_t>(link::LinkControlStateV1::Failed) ==
                  FLY_SESSION_LINK_FAILED_V2,
              "FAILED mirrors the public ABI");

/* This release advertises DUAL only. */
static_assert(link::kLinkSupportedCapabilityMaskV1 ==
                  link::kLinkCapabilityDualV1,
              "only DUAL may be advertised");
static_assert((link::kLinkSupportedCapabilityMaskV1 &
               link::kLinkCapabilityStreamVideoV1) == 0,
              "STREAM video must not be advertised");
static_assert((link::kLinkSupportedCapabilityMaskV1 &
               link::kLinkCapabilityStreamAudioV1) == 0,
              "STREAM audio must not be advertised");

/* ---------------------------------------------------------- frozen domains */

/* Domain strings are part of the wire contract: changing one silently breaks
 * every golden vector, so they are pinned here at run time. */
void domain_contract()
{
    check(std::strcmp(link::kLinkHelloDigestDomainV1,
                      "flynes-link-hello-v1") == 0,
          "hello digest domain is frozen");
    check(std::strcmp(link::kLinkHelloObjectHashDomainV1,
                      "flynes-link-hello-object-v1") == 0,
          "hello object hash domain is frozen");
    check(std::strcmp(link::kLinkReadyDigestDomainV1,
                      "flynes-link-ready-v1") == 0,
          "ready digest domain is frozen");
    check(std::strcmp(link::kLinkReadyObjectHashDomainV1,
                      "flynes-link-ready-object-v1") == 0,
          "ready object hash domain is frozen");
    check(std::strcmp(link::kLinkNegotiatedResultHashDomainV1,
                      "flynes-link-negotiated-result-v1") == 0,
          "negotiated result domain is frozen");

    /* Every domain must be distinct, or a control object could be replayed as
     * another kind. */
    check(std::strcmp(link::kLinkHelloDigestDomainV1,
                      link::kLinkReadyDigestDomainV1) != 0,
          "hello and ready digest domains differ");
    check(std::strcmp(link::kLinkHelloObjectHashDomainV1,
                      link::kLinkReadyObjectHashDomainV1) != 0,
          "hello and ready object domains differ");
    check(std::strcmp(link::kLinkReadyDigestDomainV1,
                      link::kLinkNegotiatedResultHashDomainV1) != 0,
          "ready and negotiated result domains differ");
}

/* -------------------------------------------------------- capability gate */

void capability_contract()
{
    check(link::evaluate_link_proposal_v1(link::kLinkCapabilityDualV1) ==
              link::LinkProposalSupportV1::Supported,
          "DUAL proposal is supported");

    /* A STREAM-only proposal must be explicitly unsupported: it may not be
     * silently degraded to DUAL and may not leave a half-open connection. */
    const auto stream_only = static_cast<std::uint16_t>(
        link::kLinkCapabilityStreamVideoV1 | link::kLinkCapabilityStreamAudioV1);
    check(link::evaluate_link_proposal_v1(stream_only) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "STREAM-only proposal is unsupported, not degraded");
    check(link::evaluate_link_proposal_v1(0) ==
              link::LinkProposalSupportV1::UnsupportedByThisRelease,
          "empty capability proposal is unsupported");

    /* Unknown critical bits fail closed even alongside a valid DUAL bit. */
    check(link::evaluate_link_proposal_v1(0x8000) ==
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "unknown high capability bit is critical");
    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
              link::kLinkCapabilityDualV1 | 0x0100)) ==
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "DUAL plus unknown critical bit is rejected");
    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
              link::kLinkCapabilityDualV1 | 0xFFFF)) ==
              link::LinkProposalSupportV1::UnknownCriticalCapability,
          "unknown critical bits never pass");

    /* DUAL plus a known STREAM bit is still acceptable: the peer merely
     * advertises more than this release will use. */
    check(link::evaluate_link_proposal_v1(static_cast<std::uint16_t>(
              link::kLinkCapabilityDualV1 |
              link::kLinkCapabilityStreamAudioV1)) ==
              link::LinkProposalSupportV1::Supported,
          "DUAL plus a known STREAM bit is still usable");

    check(link::check_critical_extension_mask_v1(0) == wire::Status::Ok,
          "zero critical extension mask is legal");
    check(link::check_critical_extension_mask_v1(1) ==
              wire::Status::UnknownCriticalTag,
          "nonzero critical extension mask is rejected");
    check(link::check_critical_extension_mask_v1(0xFFFF) ==
              wire::Status::UnknownCriticalTag,
          "all critical extension bits are rejected");
}

/* ------------------------------------------------------------ role rules */

void role_contract()
{
    check(link::link_role_is_valid_v1(wire::PairRoleV1::Initiator),
          "initiator role is valid");
    check(link::link_role_is_valid_v1(wire::PairRoleV1::Responder),
          "responder role is valid");
    check(!link::link_role_is_valid_v1(static_cast<wire::PairRoleV1>(0)),
          "role 0 is invalid");
    check(!link::link_role_is_valid_v1(static_cast<wire::PairRoleV1>(3)),
          "role 3 is invalid");

    check(link::link_roles_are_mirrored_v1(wire::PairRoleV1::Initiator,
                                           wire::PairRoleV1::Responder),
          "initiator to responder is mirrored");
    check(link::link_roles_are_mirrored_v1(wire::PairRoleV1::Responder,
                                           wire::PairRoleV1::Initiator),
          "responder to initiator is mirrored");
    check(!link::link_roles_are_mirrored_v1(wire::PairRoleV1::Initiator,
                                            wire::PairRoleV1::Initiator),
          "reflected identity roles are rejected");
    check(!link::link_roles_are_mirrored_v1(wire::PairRoleV1::Responder,
                                            wire::PairRoleV1::Responder),
          "reflected responder roles are rejected");
}

/* ------------------------------------------------------- lobby projection */

void projection_contract()
{
    link::LinkControlProgressV1 progress{};
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "no progress stays CONNECTING");

    /* Binding durable alone must not advance the projection. */
    progress.local_binding_durable = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "durable binding alone stays CONNECTING");

    /* Local hello durable, but nothing verified from the peer yet. */
    progress.local_hello_durable = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "one-sided HELLO stays CONNECTING");

    /* One-sided READY is the classic false positive: the peer READY is missing
     * even though everything local is durable. */
    progress.peer_hello_verified = true;
    progress.negotiated_result_durable = true;
    progress.local_ready_durable = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "one-sided READY must not reach CONNECTED_LOBBY");

    /* Peer READY without the ACK is still not a complete handshake. */
    progress.peer_ready_verified = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "verified peer READY without ACK stays CONNECTING");

    /* Only the full closure may publish the lobby. */
    progress.peer_ack_received = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::ConnectedLobby,
          "full HELLO/READY/ACK closure reaches CONNECTED_LOBBY");

    /* Any failure wins over a would-be complete closure. */
    progress.failed = true;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Failed,
          "failure overrides closure");
    progress.failed = false;

    /* Missing the negotiated durable result is a hard stop, since the peer
     * READY would then reference a result this side never persisted. */
    progress.negotiated_result_durable = false;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "missing negotiated result stays CONNECTING");
    progress.negotiated_result_durable = true;

    /* Missing the local HELLO durable record likewise blocks the lobby. */
    progress.local_hello_durable = false;
    check(link::project_link_control_state_v1(progress) ==
              link::LinkControlStateV1::Connecting,
          "missing durable local HELLO stays CONNECTING");
}

/* --------------------------------------------------- checked monotonicity */

void checked_arithmetic_contract()
{
    std::uint64_t next = 0;

    check(!link::checked_next_link_generation_v1(0, &next),
          "generation 0 is never a legal base");
    check(!link::checked_next_link_generation_v1(UINT64_MAX, &next),
          "generation overflow is rejected, not wrapped");
    check(!link::checked_next_link_generation_v1(7, nullptr),
          "null out pointer is rejected");
    next = 0;
    check(link::checked_next_link_generation_v1(7, &next) && next == 8,
          "generation increments monotonically");

    next = 0;
    check(!link::checked_next_link_sequence_v1(0, &next),
          "sequence 0 is never a legal base");
    check(!link::checked_next_link_sequence_v1(UINT64_MAX, &next),
          "sequence overflow is rejected, not wrapped");
    next = 0;
    check(link::checked_next_link_sequence_v1(41, &next) && next == 42,
          "sequence increments monotonically");
}

/*
 * gap 7, part one: the contract's message tags and the schema-registered
 * application-frame tags must be the same thing. The 0x0216/0x0217 objects and
 * the 0xFF06/0xFF07 messages are now registered in shared/schema and in
 * wire/app_frame.cpp; this keeps the frozen contract from drifting away from
 * the registry that actually frames them on the Control channel.
 *
 * What this does NOT do: it does not put the 6-byte frame header on the wire.
 * The link handshake still has no transport at all (see the report's gap 4), so
 * nothing sends these bytes yet.
 */
void app_frame_registration_contract()
{
    wire::FrameTagInfo hello_tag{};
    check(wire::frame_tag_info(link::kLinkHelloMessageTagV1, &hello_tag) &&
              hello_tag.type_namespace == wire::FrameTypeNamespace::Message &&
              std::strcmp(hello_tag.type_name, "LinkHelloV1") == 0,
          "contract LINK_HELLO tag is the registered LinkHelloV1 message tag");
    wire::FrameTagInfo ready_tag{};
    check(wire::frame_tag_info(link::kLinkReadyMessageTagV1, &ready_tag) &&
              ready_tag.type_namespace == wire::FrameTypeNamespace::Message &&
              std::strcmp(ready_tag.type_name, "LinkReadyV1") == 0,
          "contract LINK_READY tag is the registered LinkReadyV1 message tag");
    check(wire::frame_tag_info(link::kLinkHelloObjectKindV1, &hello_tag) &&
              hello_tag.type_namespace == wire::FrameTypeNamespace::ObjectKind,
          "0x0216 is a registered ObjectKind frame tag");
    check(wire::frame_tag_info(link::kLinkReadyObjectKindV1, &ready_tag) &&
              ready_tag.type_namespace == wire::FrameTypeNamespace::ObjectKind,
          "0x0217 is a registered ObjectKind frame tag");

    /* Both are legal on the Control channel, and the Control-channel object
     * bound admits their real sizes (488 / 432 bytes plus the frame header). */
    check(wire::tag_is_legal_on(wire::QuicChannel::Control,
                                link::kLinkHelloObjectKindV1) &&
              wire::tag_is_legal_on(wire::QuicChannel::Control,
                                    link::kLinkReadyObjectKindV1),
          "both link control objects are legal on the Control channel");
    check(wire::max_object_bytes(wire::QuicChannel::Control) >=
              link::kLinkHelloSizeV1 &&
              wire::max_object_bytes(wire::QuicChannel::Control) >=
                  link::kLinkReadySizeV1,
          "the Control channel bound admits the exact control object sizes");

    /* The exact framing the engine will have to emit: a message-namespace
     * record parses back under the Control channel as the same tag and body. */
    const std::size_t body_size = link::kLinkHelloSizeV1;
    std::vector<std::uint8_t> body(body_size, 0x5a);
    std::vector<std::uint8_t> framed(6u + body_size, 0);
    std::size_t written = 0;
    check(wire::encode_app_frame(link::kLinkHelloMessageTagV1, body.data(),
                                 body.size(), framed.data(), framed.size(),
                                 &written) == wire::Status::Ok &&
              written == framed.size(),
          "the exact LINK_HELLO frame header encodes");
    const std::uint32_t declared =
        (static_cast<std::uint32_t>(framed[0]) << 24u) |
        (static_cast<std::uint32_t>(framed[1]) << 16u) |
        (static_cast<std::uint32_t>(framed[2]) << 8u) |
        static_cast<std::uint32_t>(framed[3]);
    check(declared == 2u + body_size,
          "the frame length counts the 2-byte tag plus the object bytes");
    check(framed[4] == static_cast<std::uint8_t>(
                           link::kLinkHelloMessageTagV1 >> 8u) &&
              framed[5] == static_cast<std::uint8_t>(
                               link::kLinkHelloMessageTagV1 & 0xFFu),
          "the frame tag field carries the contract's LINK_HELLO tag verbatim");
    /* The framed window the parser validates is exactly the object after the
     * header: check() still sees offset 0 of the exact 488 object bytes. A
     * synthetic body must therefore be refused by the codec, which is what
     * proves the header is outside the validated window. */
    wire::AppFrame parsed{};
    check(wire::parse_app_frame(wire::QuicChannel::Control, framed.data(),
                                framed.size(), &parsed) != wire::Status::Ok,
          "a body that is not a valid LINK_HELLO object is refused by the codec "
          "inside the frame");

    /* A tag that is not registered can never be framed, so a contract constant
     * that is not in the schema fails closed rather than reaching the wire. */
    check(wire::encode_app_frame(0xFFFFu, body.data(), body.size(),
                                 framed.data(), framed.size(),
                                 &written) == wire::Status::UnknownKind,
          "an unregistered control tag is refused by the framer");
}

} // namespace

int main()
{
    domain_contract();
    capability_contract();
    role_contract();
    projection_contract();
    checked_arithmetic_contract();
    app_frame_registration_contract();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d link control contract checks failed\n", failures);
        return 1;
    }
    std::puts("link control contract tests passed");
    return 0;
}
