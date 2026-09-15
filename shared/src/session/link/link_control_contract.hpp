#ifndef FLYNES_SESSION_LINK_LINK_CONTROL_CONTRACT_HPP
#define FLYNES_SESSION_LINK_LINK_CONTROL_CONTRACT_HPP

/*
 * W0 frozen contract: LINK_HELLO / LINK_READY control plane.
 *
 * Provenance. The three approved 2026-09-13 design documents freeze the
 * *semantics* of the link handshake (persist-before-send, mirrored roles,
 * fail-closed on unknown critical capability, "no CONNECTED_LOBBY until both
 * sides have verified READY and ACK") but they deliberately do NOT freeze any
 * exact byte layout for LINK_HELLO or LINK_READY. Per the parallel-worktree
 * plan (Task 2, steps 2 and 3) W0 freezes that layout here so that W1, W2 and
 * W3 never invent competing approximations.
 *
 * Everything in this header is a *contract*: constants, enums, POD layouts and
 * pure gate helpers. The exact codec lives in
 * shared/src/session/wire/link_hello.* and link_ready.* (W1, Task 3). This
 * header intentionally contains no provider access, no engine access and no
 * allocation.
 *
 * Object kind allocation (W0, 2026-09-15). Already taken by the registered
 * schema: 0x0210 SUSPEND_INTENT_V1, 0x0211 INPUT_RANGE_AUTHORIZATION_V1,
 * 0x0212 SESSION_SIGNING_KEY_BINDING_V1, 0x0213 PAIR_TRANSCRIPT_V1;
 * 0x0214/0x0215 are reserved by the invite-code protocol amendment. This
 * contract therefore allocates 0x0216 and 0x0217. Registration in
 * shared/schema/ and the platform schema mirrors is W0's Task 9 integration
 * step; until then these kinds exist only as frozen constants.
 *
 * Message tags. Control-channel message tags are derived from the schema
 * "messages" array order (tag = 0xFF00 + index). 0xFF00..0xFF05 are already
 * assigned, so LINK_HELLO and LINK_READY are allocated 0xFF06 and 0xFF07. The
 * Task 9 registration MUST keep that order or update these two constants and
 * the mirror; W1 must not renumber them locally.
 *
 * Mode and capability scope for this release. DUAL only. STREAM capability
 * bits are defined so that the wire never has to change, but this release must
 * never advertise them and must reject a STREAM-only proposal with an explicit
 * unsupported result instead of silently degrading or half-connecting.
 */

#include "wire/pair_handshake.hpp"
#include "wire/session_codec.hpp"
#include "wire/session_signing_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace flynes::session::link {

/* ------------------------------------------------------------------ kinds */

inline constexpr std::uint16_t kLinkHelloObjectKindV1 = 0x0216;
inline constexpr std::uint16_t kLinkReadyObjectKindV1 = 0x0217;

inline constexpr std::uint16_t kLinkHelloMessageTagV1 = 0xFF06;
inline constexpr std::uint16_t kLinkReadyMessageTagV1 = 0xFF07;

/* ---------------------------------------------------------------- domains */

/* Signature digest domains. Each carries the exact 4-byte big-endian length of
 * the preimage it covers, matching wire::domain_hash. */
inline constexpr const char* kLinkHelloDigestDomainV1 = "flynes-link-hello-v1";
inline constexpr const char* kLinkReadyDigestDomainV1 = "flynes-link-ready-v1";

/* Object hash domains. Each covers the complete object including its trailing
 * signature, matching the 0x0213 PAIR_TRANSCRIPT_V1 convention. */
inline constexpr const char* kLinkHelloObjectHashDomainV1 =
    "flynes-link-hello-object-v1";
inline constexpr const char* kLinkReadyObjectHashDomainV1 =
    "flynes-link-ready-object-v1";

/* Hash of the locally persisted negotiated capability/runtime result that
 * READY binds. It lives under its own domain so a negotiated result can never
 * be confused with either control message. */
inline constexpr const char* kLinkNegotiatedResultHashDomainV1 =
    "flynes-link-negotiated-result-v1";

/* ------------------------------------------------------------------ sizes */

/*
 * LINK_HELLO_V1, exactly 480 bytes, big-endian, zero-filled reserved fields.
 *
 *   off  size  field
 *     0     2  version u16be (= 1)
 *     2     6  reserved_zero[6]
 *     8     1  sender_role      (wire::PairRoleV1: 1 initiator, 2 responder)
 *     9     1  receiver_role    (must be the mirrored role)
 *    10     1  phase            (LinkPhaseV1::Initial = 1)
 *    11     5  reserved_zero[5]
 *    16    16  session_id[16]
 *    32    16  link_id[16]
 *    48     8  channel_id u64be
 *    56     8  connection_generation u64be
 *    64     8  link_generation u64be
 *    72     2  wire_major u16be (= 2)
 *    74     2  wire_minor u16be (= 0)
 *    76     2  capability_bits u16be       (link_capability_v1_mask)
 *    78     2  critical_extension_mask u16be (MUST be 0)
 *    80     1  determinism_profile u8
 *    81     1  core_state_format u8
 *    82     2  reserved_zero[2]
 *    84    32  selected_plan_hash[32]         echo of the locked plan
 *   116    32  endpoint_offer_hash[32]        echo of the locked bearer path
 *   148    32  pair_transcript_object_hash[32] 0x0213 object hash
 *   180    32  session_signing_binding_hash[32] sender's own 0x0212 hash
 *   212   112  identity_verifier_ref[112]     sender long-term identity ref
 *   324    65  session_signing_public_key_x963[65]
 *   389    27  reserved_zero[27]
 *   416    64  sender_signature[64]           canonical low-S
 *
 * sender_signature covers digest = domain_hash(kLinkHelloDigestDomainV1,
 * bytes[0..416), 416). The persisted object hash is
 * domain_hash(kLinkHelloObjectHashDomainV1, bytes[0..480), 480).
 *
 * "Persist before send": the exact 312-byte 0x0212 binding and its object MUST
 * be durable before this message is emitted, and the exact 480 bytes MUST be
 * durable before READY may reference their object hash.
 */
inline constexpr std::size_t kLinkHelloPretagSizeV1 = 416;
inline constexpr std::size_t kLinkHelloSizeV1 = 480;

/*
 * LINK_READY_V1, exactly 384 bytes, big-endian, zero-filled reserved fields.
 *
 *   off  size  field
 *     0     2  version u16be (= 1)
 *     2     6  reserved_zero[6]
 *     8     1  sender_role
 *     9     1  receiver_role
 *    10     1  phase            (MUST be LinkPhaseV1::Reconcile = 2)
 *    11     1  ready_phase      (LinkReadyPhaseV1::Ready = 1 / Ack = 2)
 *    12     4  reserved_zero[4]
 *    16    16  session_id[16]
 *    32    16  link_id[16]
 *    48     8  channel_id u64be
 *    56     8  channel_bind_id u64be
 *    64     8  connection_generation u64be
 *    72     8  reconnect_attempt u64be
 *    80     8  link_generation u64be
 *    88    32  channel_bind_hash[32]
 *   120    32  local_hello_object_hash[32]   sender's own persisted HELLO
 *   152    32  peer_hello_object_hash[32]    verified peer HELLO
 *   184    32  negotiated_result_hash[32]    locally persisted result
 *   216    32  local_summary_hash[32]        locally persisted summary
 *   248    32  peer_summary_hash[32]         verified peer summary
 *   280    32  merge_result_hash[32]         persisted reconciliation result
 *   312     8  reserved_zero[8]
 *   320    64  sender_signature[64]
 *
 * sender_signature covers digest = domain_hash(kLinkReadyDigestDomainV1,
 * bytes[0..320), 320). The persisted object hash is
 * domain_hash(kLinkReadyObjectHashDomainV1, bytes[0..384), 384).
 *
 * READY is only legal in phase RECONCILE. A ROUTE_ONLY exchange, or anything
 * before the tail-status prelude and root read, MUST NOT produce READY.
 * ACK binds both verified summary hashes plus the merge result hash.
 */
inline constexpr std::size_t kLinkReadyPretagSizeV1 = 320;
inline constexpr std::size_t kLinkReadySizeV1 = 384;

static_assert(kLinkHelloPretagSizeV1 + 64u == kLinkHelloSizeV1,
              "LINK_HELLO pretag plus signature must equal the exact size");
static_assert(kLinkReadyPretagSizeV1 + 64u == kLinkReadySizeV1,
              "LINK_READY pretag plus signature must equal the exact size");

/* ------------------------------------------------------------------ enums */

enum class LinkPhaseV1 : std::uint8_t
{
    Initial = 1,
    Reconcile = 2
};

enum class LinkReadyPhaseV1 : std::uint8_t
{
    Ready = 1,
    Ack = 2
};

/*
 * Capability bits. This release supports DUAL only. The two STREAM bits exist
 * so the wire format never has to change, but kLinkSupportedCapabilityMaskV1
 * excludes them: advertising them is a contract violation, and a peer that
 * offers only STREAM bits must be rejected as unsupported rather than being
 * silently degraded or half-connected.
 */
enum LinkCapabilityV1 : std::uint16_t
{
    kLinkCapabilityDualV1 = 0x0001,
    kLinkCapabilityStreamVideoV1 = 0x0002,
    kLinkCapabilityStreamAudioV1 = 0x0004
};

inline constexpr std::uint16_t kLinkKnownCapabilityMaskV1 =
    static_cast<std::uint16_t>(kLinkCapabilityDualV1) |
    static_cast<std::uint16_t>(kLinkCapabilityStreamVideoV1) |
    static_cast<std::uint16_t>(kLinkCapabilityStreamAudioV1);

/* This release advertises exactly this and nothing more. */
inline constexpr std::uint16_t kLinkSupportedCapabilityMaskV1 =
    static_cast<std::uint16_t>(kLinkCapabilityDualV1);

enum class LinkProposalSupportV1 : std::uint8_t
{
    Supported = 0,
    /* Peer offered only bits this release does not implement (STREAM-only). */
    UnsupportedByThisRelease = 1,
    /* Peer offered a bit that is not in the known mask: fail closed. */
    UnknownCriticalCapability = 2
};

/*
 * Projected link states. These values are bound to the public ABI by
 * static_assert in the contract test: FLY_SESSION_LINK_CONNECTING_V2 = 7,
 * FLY_SESSION_LINK_CONNECTED_LOBBY_V2 = 8, FLY_SESSION_LINK_FAILED_V2 = 9.
 */
enum class LinkControlStateV1 : std::uint8_t
{
    Connecting = 7,
    ConnectedLobby = 8,
    Failed = 9
};

/*
 * Durable/send/ack progress of one link attempt. Every field is a decision the
 * engine has already made; the projection below is a pure function of them so
 * that no caller can reach CONNECTED_LOBBY by another route.
 */
struct LinkControlProgressV1 final
{
    bool local_binding_durable = false;
    bool local_hello_durable = false;
    bool peer_hello_verified = false;
    bool negotiated_result_durable = false;
    bool local_ready_durable = false;
    bool peer_ready_verified = false;
    bool peer_ack_received = false;
    bool failed = false;
};

/* ---------------------------------------------------------------- layout */

/*
 * Long-term identity verifier reference, exactly 112 bytes. This mirrors the
 * region embedded in the registered 312-byte SessionSigningKeyBindingV1 at
 * bytes 64..176 (wire/session_signing_binding.cpp), which is why the size
 * constant is reused rather than re-declared.
 */
struct LinkIdentityVerifierRefV1 final
{
    std::array<std::uint8_t, 2> version{};
    std::array<std::uint8_t, 6> reserved_zero0{};
    std::array<std::uint8_t, 32> identity_key_id{};
    std::array<std::uint8_t, 65> identity_public_key{};
    std::array<std::uint8_t, 7> reserved_zero1{};
};

struct LinkHelloV1 final
{
    std::uint16_t version = 1;
    wire::PairRoleV1 sender_role = wire::PairRoleV1::Initiator;
    wire::PairRoleV1 receiver_role = wire::PairRoleV1::Responder;
    LinkPhaseV1 phase = LinkPhaseV1::Initial;
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t channel_id = 0;
    std::uint64_t connection_generation = 0;
    std::uint64_t link_generation = 0;
    std::uint16_t wire_major = 2;
    std::uint16_t wire_minor = 0;
    std::uint16_t capability_bits = kLinkSupportedCapabilityMaskV1;
    std::uint16_t critical_extension_mask = 0;
    std::uint8_t determinism_profile = 0;
    std::uint8_t core_state_format = 0;
    std::array<std::uint8_t, 32> selected_plan_hash{};
    std::array<std::uint8_t, 32> endpoint_offer_hash{};
    std::array<std::uint8_t, 32> pair_transcript_object_hash{};
    std::array<std::uint8_t, 32> session_signing_binding_hash{};
    LinkIdentityVerifierRefV1 identity_verifier_ref{};
    std::array<std::uint8_t, 65> session_signing_public_key{};
    std::array<std::uint8_t, 64> signature{};
    std::array<std::uint8_t, 32> digest{};
    std::array<std::uint8_t, 32> object_hash{};
};

struct LinkReadyV1 final
{
    std::uint16_t version = 1;
    wire::PairRoleV1 sender_role = wire::PairRoleV1::Initiator;
    wire::PairRoleV1 receiver_role = wire::PairRoleV1::Responder;
    LinkPhaseV1 phase = LinkPhaseV1::Reconcile;
    LinkReadyPhaseV1 ready_phase = LinkReadyPhaseV1::Ready;
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t channel_id = 0;
    std::uint64_t channel_bind_id = 0;
    std::uint64_t connection_generation = 0;
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t link_generation = 0;
    std::array<std::uint8_t, 32> channel_bind_hash{};
    std::array<std::uint8_t, 32> local_hello_object_hash{};
    std::array<std::uint8_t, 32> peer_hello_object_hash{};
    std::array<std::uint8_t, 32> negotiated_result_hash{};
    std::array<std::uint8_t, 32> local_summary_hash{};
    std::array<std::uint8_t, 32> peer_summary_hash{};
    std::array<std::uint8_t, 32> merge_result_hash{};
    std::array<std::uint8_t, 64> signature{};
    std::array<std::uint8_t, 32> digest{};
    std::array<std::uint8_t, 32> object_hash{};
};

static_assert(sizeof(LinkIdentityVerifierRefV1) ==
                  wire::kIdentityVerifierRefSizeV1,
              "identity verifier reference must stay exactly 112 bytes");

/* ----------------------------------------------------------- pure helpers */

inline bool link_role_is_valid_v1(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator ||
           role == wire::PairRoleV1::Responder;
}

/* Roles are always mirrored and ordered by the pair transcript. */
inline bool link_roles_are_mirrored_v1(wire::PairRoleV1 sender,
                                      wire::PairRoleV1 receiver) noexcept
{
    if (!link_role_is_valid_v1(sender) || !link_role_is_valid_v1(receiver))
        return false;
    return sender != receiver;
}

/*
 * Evaluate a peer capability offer. Unknown bits outside the known mask are a
 * critical failure. A peer that offers only STREAM is explicitly unsupported
 * in this release; it must never be silently dropped to DUAL or half-open.
 */
inline LinkProposalSupportV1 evaluate_link_proposal_v1(
    std::uint16_t peer_capability_bits) noexcept
{
    if ((peer_capability_bits &
         static_cast<std::uint16_t>(~kLinkKnownCapabilityMaskV1)) != 0)
        return LinkProposalSupportV1::UnknownCriticalCapability;
    if ((peer_capability_bits & kLinkCapabilityDualV1) == 0)
        return LinkProposalSupportV1::UnsupportedByThisRelease;
    return LinkProposalSupportV1::Supported;
}

/* Unknown critical extension bits are always fatal. */
inline wire::Status check_critical_extension_mask_v1(
    std::uint16_t critical_extension_mask) noexcept
{
    return critical_extension_mask == 0 ? wire::Status::Ok
                                        : wire::Status::UnknownCriticalTag;
}

/*
 * Checked monotonic bumps. Zero is never a legal generation or sequence, and
 * saturation is rejected instead of wrapping.
 */
inline bool checked_next_link_generation_v1(std::uint64_t current,
                                            std::uint64_t* out_next) noexcept
{
    if (out_next == nullptr || current == 0 ||
        current == UINT64_MAX)
        return false;
    *out_next = current + 1u;
    return true;
}

inline bool checked_next_link_sequence_v1(std::uint64_t current,
                                          std::uint64_t* out_next) noexcept
{
    return checked_next_link_generation_v1(current, out_next);
}

/*
 * The single gate that may publish CONNECTED_LOBBY. Both sides must hold a
 * durable local READY and must have verified the peer READY and its ACK; a
 * one-sided READY, a lost ACK or any failure keeps the projection at
 * CONNECTING (or FAILED). The engine MUST NOT publish the lobby through any
 * other path.
 */
inline LinkControlStateV1 project_link_control_state_v1(
    const LinkControlProgressV1& progress) noexcept
{
    if (progress.failed)
        return LinkControlStateV1::Failed;
    if (progress.local_binding_durable && progress.local_hello_durable &&
        progress.peer_hello_verified && progress.negotiated_result_durable &&
        progress.local_ready_durable && progress.peer_ready_verified &&
        progress.peer_ack_received)
        return LinkControlStateV1::ConnectedLobby;
    return LinkControlStateV1::Connecting;
}

} // namespace flynes::session::link

#endif
