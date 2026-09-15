#include "flynes/flynes_session.h"

#include "link/link_control_contract.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

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

/* Exact frozen sizes. */
static_assert(link::kLinkHelloSizeV1 == 480, "LINK_HELLO exact size");
static_assert(link::kLinkReadySizeV1 == 384, "LINK_READY exact size");
static_assert(link::kLinkHelloPretagSizeV1 == 416, "LINK_HELLO pretag");
static_assert(link::kLinkReadyPretagSizeV1 == 320, "LINK_READY pretag");
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

} // namespace

int main()
{
    domain_contract();
    capability_contract();
    role_contract();
    projection_contract();
    checked_arithmetic_contract();

    if (failures != 0)
    {
        std::fprintf(stderr, "%d link control contract checks failed\n", failures);
        return 1;
    }
    std::puts("link control contract tests passed");
    return 0;
}
