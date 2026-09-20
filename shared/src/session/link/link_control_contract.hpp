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
 *
 * ★ Channel identity correction (owner-authorized, 2026-09-16). The first
 * revision of this header invented a u64 channel_id and a u64 channel_bind_id.
 * That was wrong: every layer of the channel identity in this repository is
 * std::array<std::uint8_t, 16> (wire::derive_channel_id_v1,
 * wire::encode_initial_channel_bind_v1, wire::encode_channel_bind_ack_v1,
 * InitialQuicBindScheduler::channel_id()). No u64 channel identity exists
 * anywhere in the implementation or in the approved 2026-09-13 design
 * documents, so the control messages could never be encoded. This header now
 * carries the 16-byte channel id and binds the channel with the 32-byte
 * channel_bind_hash alone; the redundant u64 channel_bind_id is deleted
 * outright rather than kept as a second, parallel identity. The authenticating
 * value is wire::channel_bind_proof_hash_v1(listener/connector proof) — the
 * exact value both sides already verify inside the ACK — so READY does not
 * need a second bind identifier to be unambiguous.
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

/* Domain for the negotiated-result hash that LINK_READY binds.
 *
 * PREIMAGE FROZEN BY THE OWNER, 2026-09-16. There is deliberately NO
 * negotiated-result object format, and it is deliberately not 512 bytes: the
 * "result" of negotiation is the pair of signed control objects both sides
 * already hold and have verified, in a fixed order.
 *
 *   preimage = initiator_hello_object_hash[32] || responder_hello_object_hash[32]
 *              (exactly 64 bytes, initiator first, both non-zero)
 *   hash     = domain_hash(kLinkNegotiatedResultHashDomainV1, preimage, 64)
 *
 * A side that is the initiator puts its own HELLO object hash first and the
 * peer's second; the responder does the reverse. Because READY already carries
 * local_hello_object_hash and peer_hello_object_hash, any receiver can recompute
 * this value from READY alone and reject a mismatch, so the claim is checkable
 * rather than trusted. The single implementation is
 * wire::negotiated_result_hash_v1 / wire::negotiated_result_preimage_v1 in
 * shared/src/session/wire/link_ready.cpp. */
inline constexpr const char* kLinkNegotiatedResultHashDomainV1 =
    "flynes-link-negotiated-result-v1";

/* The exact 64-byte size of that preimage. */
inline constexpr std::size_t kLinkNegotiatedResultPreimageSizeV1 = 64;

/* Domain for the canonical channel-bind binding hash that LINK_READY carries.
 *
 * Provenance. The first contract revision invented both a u64 channel_id and a
 * u64 channel_bind_id. Neither exists in the implementation. The approved
 * 2026-09-04 design identifies a bind by exactly three authenticated values and
 * nothing else — `channel_id[16]`, `connector_proof_hash[32]` and
 * `listener_proof_hash[32]`, both proof hashes being carried inside the 96-byte
 * CHANNEL_BIND_ACK body (spec:437) — and initial_quic_bind_scheduler.hpp already
 * documents that same triple. READY therefore binds those three values through
 * one canonical hash instead of a second, parallel identifier, and both sides
 * hold the value only after each has verified the ACK.
 *
 *   preimage = channel_id[16] || connector_proof_hash[32] ||
 *              listener_proof_hash[32]                      (exactly 80 bytes)
 *   hash     = domain_hash(kLinkChannelBindBindingHashDomainV1, preimage, 80)
 *
 * Both proof hashes MUST be wire::channel_bind_proof_hash_v1(<the exact 248-byte
 * proof>) outputs. The single implementation is
 * wire::channel_bind_binding_hash_v1 in shared/src/session/wire/channel_bind.cpp;
 * no other definition of the bind identity may exist. */
inline constexpr const char* kLinkChannelBindBindingHashDomainV1 =
    "flynes-channel-bind-binding-v1";

/* The exact 80-byte size of that preimage. */
inline constexpr std::size_t kLinkChannelBindBindingPreimageSizeV1 = 80;

/* ------------------------------------------------------------------ sizes */

/*
 * LINK_HELLO_V1, exactly 488 bytes, big-endian, zero-filled reserved fields.
 * (Was 480 while channel_id was a u64; +16 then -8 for the corrected layout.)
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
 *    48    16  channel_id[16]   wire::derive_channel_id_v1 result
 *    64     8  connection_generation u64be
 *    72     8  link_generation u64be
 *    80     2  wire_major u16be (= 2)
 *    82     2  wire_minor u16be (= 0)
 *    84     2  capability_bits u16be       (link_capability_v1_mask)
 *    86     2  critical_extension_mask u16be (MUST be 0)
 *    88     1  determinism_profile u8
 *    89     1  core_state_format u8
 *    90     2  reserved_zero[2]
 *    92    32  selected_plan_hash[32]         echo of the locked plan
 *   124    32  endpoint_offer_hash[32]        echo of the locked bearer path
 *   156    32  pair_transcript_object_hash[32] 0x0213 object hash
 *   188    32  session_signing_binding_hash[32] sender's own 0x0212 hash
 *   220   112  identity_verifier_ref[112]     sender long-term identity ref
 *   332    65  session_signing_public_key_x963[65]
 *   397    27  reserved_zero[27]
 *   424    64  sender_signature[64]
 *
 * sender_signature covers digest = domain_hash(kLinkHelloDigestDomainV1,
 * bytes[0..424), 424). The persisted object hash is
 * domain_hash(kLinkHelloObjectHashDomainV1, bytes[0..488), 488).
 *
 * "Persist before send": the exact 312-byte 0x0212 binding and its object MUST
 * be durable before this message is emitted, and the exact 488 bytes MUST be
 * durable before READY may reference their object hash. The signature covers
 * bytes[0..424) and is appended at 424..488.
 */
inline constexpr std::size_t kLinkHelloPretagSizeV1 = 424;
inline constexpr std::size_t kLinkHelloSizeV1 = 488;

/*
 * LINK_READY_V1, exactly 432 bytes (a 368-byte pretag plus the 64-byte signature
 * at 368..432), big-endian, zero-filled reserved fields. The 368 figure that used
 * to stand here was the pretag alone, which contradicted this table and
 * kLinkReadySizeV1.
 * (Was 384: +8 for the 16-byte channel_id, -24 for the deleted u64
 * channel_bind_id and the u64 that followed it.)
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
 *    48    16  channel_id[16]   wire::derive_channel_id_v1 result
 *    64     8  connection_generation u64be
 *    72     8  reconnect_attempt u64be
 *    80     8  link_generation u64be
 *    88    32  channel_bind_hash[32]  wire::channel_bind_proof_hash_v1 of the
 *                                     authenticated bind proof (both roles agree
 *                                     on it; the u64 channel_bind_id is gone)
 *   120    32  local_hello_object_hash[32]   sender's own persisted HELLO
 *   152    32  peer_hello_object_hash[32]    verified peer HELLO
 *   184    32  negotiated_result_hash[32]    locally persisted result
 *   216    32  local_summary_hash[32]        locally persisted summary
 *   248    32  peer_summary_hash[32]         verified peer summary
 *   280    32  merge_result_hash[32]         persisted reconciliation result
 *   312    56  reserved_zero[56]
 *   368    64  sender_signature[64]
 *
 * sender_signature covers digest = domain_hash(kLinkReadyDigestDomainV1,
 * bytes[0..368), 368). The persisted object hash is
 * domain_hash(kLinkReadyObjectHashDomainV1, bytes[0..432), 432).
 *
 * READY is only legal in phase RECONCILE. A ROUTE_ONLY exchange, or anything
 * before the tail-status prelude and root read, MUST NOT produce READY.
 * ACK binds both verified summary hashes plus the merge result hash.
 */
inline constexpr std::size_t kLinkReadyPretagSizeV1 = 368;
inline constexpr std::size_t kLinkReadySizeV1 = 432;

static_assert(kLinkHelloPretagSizeV1 + 64u == kLinkHelloSizeV1,
              "LINK_HELLO pretag plus signature must equal the exact size");
static_assert(kLinkReadyPretagSizeV1 + 64u == kLinkReadySizeV1,
              "LINK_READY pretag plus signature must equal the exact size");

/*
 * Pinned offsets. These exist so that a later edit to either field list cannot
 * silently move a byte on the wire; the codecs, the golden vectors and the
 * schema registration all read the wire at exactly these offsets.
 */
static_assert(kLinkHelloSizeV1 == 488, "LINK_HELLO_V1 is 488 bytes");
static_assert(kLinkReadySizeV1 == 432, "LINK_READY_V1 is 432 bytes");
inline constexpr std::size_t kLinkHelloChannelIdOffsetV1 = 48;
inline constexpr std::size_t kLinkHelloConnectionGenerationOffsetV1 = 64;
inline constexpr std::size_t kLinkHelloLinkGenerationOffsetV1 = 72;
inline constexpr std::size_t kLinkHelloWireMajorOffsetV1 = 80;
inline constexpr std::size_t kLinkHelloCapabilityOffsetV1 = 84;
inline constexpr std::size_t kLinkHelloCriticalExtensionOffsetV1 = 86;
inline constexpr std::size_t kLinkHelloDeterminismOffsetV1 = 88;
inline constexpr std::size_t kLinkHelloCoreStateFormatOffsetV1 = 89;
inline constexpr std::size_t kLinkHelloReservedZero2OffsetV1 = 90;
inline constexpr std::size_t kLinkHelloSelectedPlanHashOffsetV1 = 92;
inline constexpr std::size_t kLinkHelloEndpointOfferHashOffsetV1 = 124;
inline constexpr std::size_t kLinkHelloPairTranscriptOffsetV1 = 156;
inline constexpr std::size_t kLinkHelloBindingHashOffsetV1 = 188;
inline constexpr std::size_t kLinkHelloIdentityRefOffsetV1 = 220;
inline constexpr std::size_t kLinkHelloSessionSigningKeyOffsetV1 = 332;
inline constexpr std::size_t kLinkHelloReservedTailOffsetV1 = 397;
inline constexpr std::size_t kLinkHelloSignatureOffsetV1 = 424;

static_assert(kLinkHelloIdentityRefOffsetV1 + 112u ==
                  kLinkHelloSessionSigningKeyOffsetV1,
              "identity ref is exactly 112 bytes");
static_assert(kLinkHelloSessionSigningKeyOffsetV1 + 65u + 27u ==
                  kLinkHelloSignatureOffsetV1,
              "27 reserved bytes separate the session key from the signature");
static_assert(kLinkHelloSignatureOffsetV1 == kLinkHelloPretagSizeV1,
              "the signature starts exactly at the end of the pretag");

inline constexpr std::size_t kLinkReadyChannelIdOffsetV1 = 48;
inline constexpr std::size_t kLinkReadyConnectionGenerationOffsetV1 = 64;
inline constexpr std::size_t kLinkReadyReconnectAttemptOffsetV1 = 72;
inline constexpr std::size_t kLinkReadyLinkGenerationOffsetV1 = 80;
inline constexpr std::size_t kLinkReadyChannelBindHashOffsetV1 = 88;
inline constexpr std::size_t kLinkReadyReservedTailOffsetV1 = 312;
inline constexpr std::size_t kLinkReadyReservedTailSizeV1 = 56;
inline constexpr std::size_t kLinkReadySignatureOffsetV1 = 368;

static_assert(kLinkReadyChannelBindHashOffsetV1 + 7u * 32u ==
                  kLinkReadyReservedTailOffsetV1,
              "seven bound hashes run from 88 to 312");
static_assert(kLinkReadyReservedTailOffsetV1 + kLinkReadyReservedTailSizeV1 ==
                  kLinkReadySignatureOffsetV1,
              "the reserved tail ends exactly where the signature begins");
static_assert(kLinkReadySignatureOffsetV1 == kLinkReadyPretagSizeV1,
              "the signature starts exactly at the end of the pretag");

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
    /* The 16-byte channel identity from wire::derive_channel_id_v1, exactly the
     * value wire::encode_initial_channel_bind_v1 / decode_channel_bind_ack_v1
     * take. There is no u64 channel identity anywhere in this protocol. */
    std::array<std::uint8_t, 16> channel_id{};
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
    std::array<std::uint8_t, 16> channel_id{};
    std::uint64_t connection_generation = 0;
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t link_generation = 0;
    /* wire::channel_bind_proof_hash_v1 of the authenticated bind proof. This is
     * the single channel-bind binding, which is why the former u64
     * channel_bind_id is gone. */
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
