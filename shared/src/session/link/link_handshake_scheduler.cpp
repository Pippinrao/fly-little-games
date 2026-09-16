#include "link_handshake_scheduler.hpp"

#include "../wire/p256_point.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace flynes::session {
namespace {

namespace wire = flynes::session::wire;

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes != nullptr &&
           std::any_of(bytes, bytes + size,
                       [](std::uint8_t value) { return value != 0; });
}

wire::PairRoleV1 mirror_role(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator ? wire::PairRoleV1::Responder
                                               : wire::PairRoleV1::Initiator;
}


/* Maps a classified decode failure onto the public result. A generation
 * mismatch is a stale link that merely arrived late: it is reported as STALE
 * and never fails the live attempt. */
fly_session_result_v2 decode_failure(
    const wire::LinkControlDecodeReportV1& report) noexcept
{
    switch (report.issue) {
    case wire::LinkControlIssueV1::Generation:
        return FLY_SESSION_V2_STALE;
    case wire::LinkControlIssueV1::Capability:
        return report.proposal ==
                       link::LinkProposalSupportV1::UnknownCriticalCapability
                   ? FLY_SESSION_V2_PROTOCOL_VIOLATION
                   : FLY_SESSION_V2_UNSUPPORTED;
    case wire::LinkControlIssueV1::Signature:
    case wire::LinkControlIssueV1::IdentityRef:
        return FLY_SESSION_V2_AUTH_FAILED;
    default:
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;
    }
}

/* The digest domain is fixed per control message: the sender's signature covers
 * domain_hash(domain, exact pretag). accept_peer_message() names it explicitly
 * when it builds the verification request, so the provider verifies under the
 * same context the sender signed. */

} // namespace

void LinkHandshakeScheduler::record(LinkHandshakeStageV1 stage) noexcept
{
    if (trace_size_ < kLinkHandshakeTraceCapacityV1)
        trace_[trace_size_++] = stage;
}

LinkHandshakeStageV1 LinkHandshakeScheduler::stage_trace_at(
    std::size_t index) const noexcept
{
    return index < trace_size_ ? trace_[index] : LinkHandshakeStageV1::Empty;
}

fly_session_op_token_v2 LinkHandshakeScheduler::token(
    std::uint64_t operation_id) const noexcept
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(start_.engine_instance_id.begin(), start_.engine_instance_id.end(),
              value.engine_instance_id);
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    std::copy(start_.link_id.begin(), start_.link_id.end(), value.scope.link_id);
    value.connection_generation = start_.generation;
    value.operation_id = operation_id;
    return value;
}

void LinkHandshakeScheduler::reset_attempt(
    const LinkHandshakeStartV1& start) noexcept
{
    start_ = start;
    progress_ = link::LinkControlProgressV1{};
    local_hello_ = link::LinkHelloV1{};
    local_ready_ = link::LinkReadyV1{};
    local_ack_ = link::LinkReadyV1{};
    peer_hello_ = link::LinkHelloV1{};
    peer_ready_ = link::LinkReadyV1{};
    peer_ack_ = link::LinkReadyV1{};
    local_hello_bytes_.fill(0);
    local_ready_bytes_.fill(0);
    local_ack_bytes_.fill(0);
    local_hello_object_hash_.fill(0);
    peer_hello_object_hash_.fill(0);
    negotiated_result_hash_.fill(0);
    hello_pretag_.fill(0);
    ready_pretag_.fill(0);
    ack_pretag_.fill(0);
    peer_hello_bytes_.fill(0);
    peer_ready_bytes_.fill(0);
    peer_ack_bytes_.fill(0);
    persist_stage_ = LinkHandshakeStageV1::Empty;
    pending_verification_.reset();
    pending_persist_stage_ = LinkHandshakeStageV1::Empty;
    pending_peer_kind_ = 0;
    pending_peer_value_ = 0;
    pending_peer_preimage_.clear();
    pending_peer_hash_.fill(0);
    missing_inputs_ = false;
}

bool LinkHandshakeScheduler::begin(const LinkHandshakeStartV1& start)
{
    /*
     * begin() validates exactly the inputs the FIRST step consumes, and every
     * later step validates its own inputs through require_inputs() immediately
     * before it acts. Two reasons, both load-bearing:
     *
     *   1. Peer-side inputs (the peer's accepted 0x0212 binding hash, its
     *      identity_key_id and the session signing key that binding
     *      authenticates) cannot be known before the peer's own authenticated
     *      HELLO arrives. Demanding them here would make the handshake
     *      unstartable; demanding them at accept_peer_hello() puts the check
     *      exactly where the codec already rejects a zero or mismatched value.
     *   2. Inputs that this build has no producer for yet (the contract's u64
     *      channel_id / channel_bind_hash, the negotiated
     *      result) are refused at their point of use instead of silently
     *      forwarded as zeros.
     *
     * No check is weakened: every field still fails closed, only closer to the
     * operation that needs it, and a missing producer is reported as such.
     */
    const bool valid =
        start.generation != 0 && start.link_generation != 0 &&
        start.first_operation_id != 0 &&
        link::link_role_is_valid_v1(start.local_role) &&
        nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) &&
        nonzero(start.link_id.data(), start.link_id.size()) &&
        nonzero(start.session_id.data(), start.session_id.size()) &&
        nonzero(start.pair_transcript_hash.data(), start.pair_transcript_hash.size()) &&
        nonzero(start.local_binding_hash.data(), start.local_binding_hash.size()) &&
        wire::validate_p256_uncompressed_point(
            start.local_identity_public_key.data()) &&
        wire::validate_p256_uncompressed_point(
            start.local_session_signing_public_key.data());
    if (!valid) {
        if (!begun_) {
            progress_.failed = true;
            stage_ = LinkHandshakeStageV1::Failed;
        }
        return false;
    }
    if (begun_ && start.generation <= start_.generation)
        return false;
    if (pending_) {
        /* An older attempt must release its outstanding operation without
         * advancing the new link. */
        operations_.cancel(pending_->token);
        pending_.reset();
    }
    reset_attempt(start);
    /* Operation ids stay monotonic across attempts: the journal rejects any id
     * it has already seen, so a restart continues above the previous high water
     * mark instead of rewinding. */
    if (next_operation_id_ < start_.first_operation_id)
        next_operation_id_ = start_.first_operation_id;
    begun_ = true;
    return request_local_binding() == FLY_SESSION_V2_OK;
}

std::optional<LinkHandshakeEffect> LinkHandshakeScheduler::poll_effect() const
{
    return pending_;
}

fly_session_result_v2 LinkHandshakeScheduler::fail(
    fly_session_result_v2 result) noexcept
{
    if (pending_) {
        operations_.cancel(pending_->token);
        pending_.reset();
    }
    pending_verification_.reset();
    pending_persist_stage_ = LinkHandshakeStageV1::Empty;
    pending_peer_kind_ = 0;
    pending_peer_value_ = 0;
    pending_peer_preimage_.clear();
    pending_peer_hash_.fill(0);
    progress_.failed = true;
    stage_ = LinkHandshakeStageV1::Failed;
    record(LinkHandshakeStageV1::Failed);
    return result == FLY_SESSION_V2_OK ? FLY_SESSION_V2_CONTRACT_VIOLATION
                                       : result;
}

fly_session_result_v2 LinkHandshakeScheduler::require_inputs(bool present) noexcept
{
    if (present) return FLY_SESSION_V2_OK;
    missing_inputs_ = true;
    return fail(FLY_SESSION_V2_UNSUPPORTED);
}

fly_session_result_v2 LinkHandshakeScheduler::issue(LinkHandshakeEffect effect,
                                                    LinkHandshakeStageV1 stage)
{
    const auto registered = operations_.expect(effect.token,
                                               effect.expected_payload_kind);
    if (registered != FLY_SESSION_V2_OK) return fail(registered);
    stage_ = stage;
    record(stage);
    pending_ = std::move(effect);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 LinkHandshakeScheduler::read_buffer(
    const ParsedProviderEvent& event, std::vector<std::uint8_t>& out) const
{
    std::uint64_t size = 0;
    if (event.buffer == nullptr ||
        fly_session_buffer_size_v2(event.buffer, &size) != FLY_SESSION_V2_OK ||
        size == 0 || size > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try {
        out.assign(static_cast<std::size_t>(size), 0);
    } catch (const std::bad_alloc&) {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), size};
    return fly_session_buffer_read_v2(event.buffer, 0, destination, &written) ==
                       FLY_SESSION_V2_OK &&
                   written == size
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 LinkHandshakeScheduler::persist_object(
    std::uint32_t object_kind, const std::array<std::uint8_t, 32>& hash,
    const std::uint8_t* bytes, std::size_t size, LinkHandshakeStageV1 stage)
{
    try {
        LinkHandshakeEffect effect{};
        effect.kind = stage == LinkHandshakeStageV1::PersistHello
                          ? LinkHandshakeEffectKind::PersistHelloObject
                      : stage == LinkHandshakeStageV1::PersistReady
                          ? LinkHandshakeEffectKind::PersistReadyObject
                      : stage == LinkHandshakeStageV1::PersistAck
                          ? LinkHandshakeEffectKind::PersistAckObject
                      : stage == LinkHandshakeStageV1::PersistPeerHello
                          ? LinkHandshakeEffectKind::PersistPeerHelloObject
                      : stage == LinkHandshakeStageV1::PersistPeerReady
                          ? LinkHandshakeEffectKind::PersistPeerReadyObject
                          : LinkHandshakeEffectKind::PersistPeerAckObject;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
        effect.object_kind = object_kind;
        effect.expected_hash = hash;
        effect.value.assign(bytes, bytes + size);
        persist_stage_ = stage;
        return issue(std::move(effect), stage);
    } catch (const std::bad_alloc&) {
        return fail(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 LinkHandshakeScheduler::send_bytes(
    const std::uint8_t* bytes, std::size_t size, LinkHandshakeStageV1 stage)
{
    /* The control stream is a real owned resource; without it nothing may be
     * sent, and an absent handle is a not-wired seam rather than a protocol
     * violation. */
    const auto inputs = require_inputs(start_.control_stream != 0 && size != 0);
    if (inputs != FLY_SESSION_V2_OK) return inputs;
    try {
        LinkHandshakeEffect effect{};
        effect.kind = stage == LinkHandshakeStageV1::SendHello
                          ? LinkHandshakeEffectKind::SendHello
                      : stage == LinkHandshakeStageV1::SendReady
                          ? LinkHandshakeEffectKind::SendReady
                          : LinkHandshakeEffectKind::SendAck;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_END_V2;
        effect.resource = start_.control_stream;
        effect.value.assign(bytes, bytes + size);
        return issue(std::move(effect), stage);
    } catch (const std::bad_alloc&) {
        return fail(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 LinkHandshakeScheduler::sign(
    std::uint32_t purpose, const std::array<std::uint8_t, 32>& digest,
    const char* domain, LinkHandshakeStageV1 stage)
{
    try {
        LinkHandshakeEffect effect{};
        effect.kind = stage == LinkHandshakeStageV1::SignHello
                          ? LinkHandshakeEffectKind::SignHello
                      : stage == LinkHandshakeStageV1::SignReady
                          ? LinkHandshakeEffectKind::SignReady
                          : LinkHandshakeEffectKind::SignAck;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
        effect.key_purpose = purpose;
        effect.resource = start_.session_signing_key;
        effect.digest = digest;
        effect.domain.assign(reinterpret_cast<const std::uint8_t*>(domain),
                             reinterpret_cast<const std::uint8_t*>(domain) +
                                 std::strlen(domain));
        return issue(std::move(effect), stage);
    } catch (const std::bad_alloc&) {
        return fail(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 LinkHandshakeScheduler::request_local_binding()
{
    LinkHandshakeEffect effect{};
    effect.kind = LinkHandshakeEffectKind::ReadLocalBindingObject;
    effect.token = token(next_operation_id_++);
    effect.expected_payload_kind = FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
    effect.object_kind = wire::kSessionSigningBindingObjectKindV1;
    effect.expected_hash = start_.local_binding_hash;
    return issue(std::move(effect), LinkHandshakeStageV1::ReadLocalBinding);
}

fly_session_result_v2 LinkHandshakeScheduler::request_hello_signature()
{
    /* HELLO binds the locked plan, the locked bearer path, the pair transcript
     * object and the 16-byte channel id, plus the session signing key. The
     * channel id is owned by InitialQuicBindScheduler (channel_bound() is
     * already true at this point), so an all-zero value here can only mean the
     * caller forgot to hand it over: that is a not-wired seam, not a protocol
     * failure, and no zero is ever emitted. */
    const auto inputs = require_inputs(
        nonzero(start_.channel_id.data(), start_.channel_id.size()) &&
        nonzero(start_.selected_plan_hash.data(),
                start_.selected_plan_hash.size()) &&
        nonzero(start_.endpoint_offer_hash.data(),
                start_.endpoint_offer_hash.size()) &&
        nonzero(start_.pair_transcript_object_hash.data(),
                start_.pair_transcript_object_hash.size()) &&
        start_.session_signing_key != 0);
    if (inputs != FLY_SESSION_V2_OK) return inputs;
    link::LinkHelloV1 value{};
    value.version = 1;
    value.sender_role = start_.local_role;
    value.receiver_role = mirror_role(start_.local_role);
    value.phase = link::LinkPhaseV1::Initial;
    value.session_id = start_.session_id;
    value.link_id = start_.link_id;
    value.channel_id = start_.channel_id;
    value.connection_generation = start_.generation;
    value.link_generation = start_.link_generation;
    value.wire_major = 2;
    value.wire_minor = 0;
    value.capability_bits = link::kLinkSupportedCapabilityMaskV1;
    value.critical_extension_mask = 0;
    value.determinism_profile = start_.determinism_profile;
    value.core_state_format = start_.core_state_format;
    value.selected_plan_hash = start_.selected_plan_hash;
    value.endpoint_offer_hash = start_.endpoint_offer_hash;
    value.pair_transcript_object_hash = start_.pair_transcript_object_hash;
    value.session_signing_binding_hash = start_.local_binding_hash;
    value.identity_verifier_ref.version = {{0, 1}};
    value.identity_verifier_ref.identity_key_id =
        wire::link_identity_key_id_v1(start_.local_identity_public_key.data());
    value.identity_verifier_ref.identity_public_key =
        start_.local_identity_public_key;
    value.session_signing_public_key = start_.local_session_signing_public_key;
    std::array<std::uint8_t, 32> digest{};
    if (wire::build_link_hello_pretag_v1(
            value, wire::validate_p256_uncompressed_point_callback, nullptr,
            &hello_pretag_, &digest) != wire::Status::Ok)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    return sign(FLY_SESSION_KEY_SESSION_SIGNING_V2, digest,
                link::kLinkHelloDigestDomainV1,
                LinkHandshakeStageV1::SignHello);
}

fly_session_result_v2 LinkHandshakeScheduler::request_negotiated_result()
{
    /* Persist-before-send: an empty negotiated result must never be persisted as
     * if it were a negotiation. This build has no canonical result producer. */
    const auto inputs = require_inputs(!start_.negotiated_result.empty());
    if (inputs != FLY_SESSION_V2_OK) return inputs;
    try {
        LinkHandshakeEffect effect{};
        effect.kind = LinkHandshakeEffectKind::PersistNegotiatedResult;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind =
            FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
        static constexpr char name_space[] = "flynes/link-negotiated/v1";
        effect.name_space.assign(
            reinterpret_cast<const std::uint8_t*>(name_space),
            reinterpret_cast<const std::uint8_t*>(name_space) +
                sizeof(name_space) - 1);
        effect.record_key.assign(start_.session_id.begin(),
                                 start_.session_id.end());
        effect.record_key.insert(effect.record_key.end(),
                                 start_.link_id.begin(), start_.link_id.end());
        effect.record_key.push_back(
            static_cast<std::uint8_t>(start_.local_role));
        effect.value = start_.negotiated_result;
        effect.expected_revision = 0;
        return issue(std::move(effect),
                     LinkHandshakeStageV1::PersistNegotiatedResult);
    } catch (const std::bad_alloc&) {
        return fail(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 LinkHandshakeScheduler::request_ready_signature()
{
    /* READY binds the channel bind (16-byte id + authenticated proof hash), both
     * verified resume summaries, the merge result, the negotiated result and
     * both persisted HELLOs. None of those may be emitted as zeros. */
    const auto inputs = require_inputs(
        nonzero(start_.channel_id.data(), start_.channel_id.size()) &&
        nonzero(start_.channel_bind_hash.data(), start_.channel_bind_hash.size()) &&
        nonzero(start_.local_summary_hash.data(),
                start_.local_summary_hash.size()) &&
        nonzero(start_.peer_summary_hash.data(),
                start_.peer_summary_hash.size()) &&
        nonzero(start_.merge_result_hash.data(),
                start_.merge_result_hash.size()) &&
        nonzero(local_hello_object_hash_.data(),
                local_hello_object_hash_.size()) &&
        nonzero(peer_hello_object_hash_.data(),
                peer_hello_object_hash_.size()) &&
        nonzero(negotiated_result_hash_.data(),
                negotiated_result_hash_.size()));
    if (inputs != FLY_SESSION_V2_OK) return inputs;
    link::LinkReadyV1 value{};
    value.version = 1;
    value.sender_role = start_.local_role;
    value.receiver_role = mirror_role(start_.local_role);
    value.phase = link::LinkPhaseV1::Reconcile;
    value.ready_phase = link::LinkReadyPhaseV1::Ready;
    value.session_id = start_.session_id;
    value.link_id = start_.link_id;
    value.channel_id = start_.channel_id;
    value.connection_generation = start_.generation;
    value.reconnect_attempt = start_.reconnect_attempt;
    value.link_generation = start_.link_generation;
    value.channel_bind_hash = start_.channel_bind_hash;
    value.local_hello_object_hash = local_hello_object_hash_;
    value.peer_hello_object_hash = peer_hello_object_hash_;
    value.negotiated_result_hash = negotiated_result_hash_;
    value.local_summary_hash = start_.local_summary_hash;
    value.peer_summary_hash = start_.peer_summary_hash;
    value.merge_result_hash = start_.merge_result_hash;
    std::array<std::uint8_t, 32> digest{};
    if (wire::build_link_ready_pretag_v1(
            value, wire::validate_p256_uncompressed_point_callback, nullptr,
            &ready_pretag_, &digest) != wire::Status::Ok)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    return sign(FLY_SESSION_KEY_SESSION_SIGNING_V2, digest,
                link::kLinkReadyDigestDomainV1,
                LinkHandshakeStageV1::SignReady);
}

fly_session_result_v2 LinkHandshakeScheduler::request_ack_signature()
{
    link::LinkReadyV1 value = local_ready_;
    value.ready_phase = link::LinkReadyPhaseV1::Ack;
    /* The ACK binds the same verified summary/merge hashes as READY, so only
     * the ready phase and the signature change. */
    std::array<std::uint8_t, 32> digest{};
    if (wire::build_link_ready_pretag_v1(
            value, wire::validate_p256_uncompressed_point_callback, nullptr,
            &ack_pretag_, &digest) != wire::Status::Ok)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    return sign(FLY_SESSION_KEY_SESSION_SIGNING_V2, digest,
                link::kLinkReadyDigestDomainV1,
                LinkHandshakeStageV1::SignAck);
}

fly_session_result_v2 LinkHandshakeScheduler::store_pending_peer_message(
    const std::uint8_t* bytes, std::size_t size,
    const wire::LinkControlParsedV1& parsed,
    const wire::LinkControlVerifyRequestV1& request,
    LinkHandshakeStageV1 verify_stage, std::uint32_t peer_value,
    std::uint32_t peer_kind,
    const std::array<std::uint8_t, 32>& peer_hash,
    LinkHandshakeStageV1 persist_stage)
{
    try {
        LinkHandshakeEffect effect{};
        effect.kind = LinkHandshakeEffectKind::VerifyPeerSignature;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2;
        effect.value.assign(bytes, bytes + size);
        effect.domain = request.domain;
        effect.digest = parsed.digest;
        effect.signer_public_key = request.public_key_x963;
        effect.signature = request.signature;
        pending_verification_ = request;
        pending_peer_value_ = peer_value;
        pending_peer_kind_ = peer_kind;
        pending_peer_hash_ = peer_hash;
        pending_peer_preimage_ = effect.value;
        pending_persist_stage_ = persist_stage;
        return issue(std::move(effect), verify_stage);
    } catch (const std::bad_alloc&) {
        return fail(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 LinkHandshakeScheduler::complete_verify_peer_signature()
{
    if (!pending_verification_)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    const auto& request = *pending_verification_;
    const wire::LinkControlParsedV1 parsed{
        request.public_key_x963, request.digest, request.signature};
    wire::LinkControlDecodeReportV1 report{};
    const bool is_hello =
        stage_ == LinkHandshakeStageV1::AwaitPeerHelloSignature;
    const bool is_ready =
        stage_ == LinkHandshakeStageV1::AwaitPeerReadySignature;
    const bool is_ack =
        stage_ == LinkHandshakeStageV1::AwaitPeerAckSignature;
    if (!is_hello && !is_ready && !is_ack)
        return fail(FLY_SESSION_V2_INVALID_STATE);

    /*
     * The asynchronous crypto verification already succeeded (a rejected
     * signature arrives as a non-OK provider result and fail()s in complete()).
     * What stage 2 still has to prove is that the key the provider verified is
     * exactly the key the accepted peer binding authenticated: a HELLO that
     * carries some other session signing key must not be accepted even if that
     * other key produced a perfectly valid signature. That is the whole point of
     * separating parse from accept.
     */
    const auto outcome =
        std::memcmp(request.public_key_x963.data(),
                    start_.peer_session_signing_public_key.data(), 65) == 0
            ? wire::LinkControlVerificationOutcomeV1::Accepted
            : wire::LinkControlVerificationOutcomeV1::Rejected;

    if (is_hello) {
        if (wire::accept_link_hello_v1(parsed, peer_hello_, outcome, &report,
                                       &peer_hello_) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        progress_.peer_hello_verified = true;
    } else if (is_ready) {
        if (wire::accept_link_ready_v1(parsed, peer_ready_, outcome, &report,
                                       &peer_ready_) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        progress_.peer_ready_verified = true;
    } else if (wire::accept_link_ready_v1(parsed, peer_ack_, outcome, &report,
                                          &peer_ack_) != wire::Status::Ok) {
        return fail(FLY_SESSION_V2_AUTH_FAILED);
    } else {
        progress_.peer_ack_received = true;
    }
    pending_verification_.reset();
    pending_.reset();
    persist_stage_ = pending_persist_stage_;
    return persist_peer_message();
}

fly_session_result_v2 LinkHandshakeScheduler::persist_peer_message()
{
    const bool is_hello = pending_peer_kind_ == link::kLinkHelloObjectKindV1;
    if (pending_peer_kind_ == 0 ||
        (!is_hello && pending_peer_kind_ != link::kLinkReadyObjectKindV1))
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    const auto object_size = is_hello ? peer_hello_bytes_.size()
                                      : peer_ready_bytes_.size();
    if (pending_peer_preimage_.size() != object_size)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    auto* destination = is_hello ? peer_hello_bytes_.data()
                         : pending_peer_value_ == 1 ? peer_ready_bytes_.data()
                                                    : peer_ack_bytes_.data();
    std::copy(pending_peer_preimage_.begin(), pending_peer_preimage_.end(),
              destination);
    return persist_object(pending_peer_kind_, pending_peer_hash_,
                          pending_peer_preimage_.data(), object_size,
                          pending_persist_stage_);
}

fly_session_result_v2 LinkHandshakeScheduler::accept_peer_message(
    const std::uint8_t* bytes, std::size_t size, LinkHandshakeStageV1 awaiting,
    std::uint8_t ready_phase, LinkHandshakeStageV1 persist_stage)
{
    if (!begun_ || progress_.failed) return FLY_SESSION_V2_INVALID_STATE;
    const bool is_hello = ready_phase == 0;
    const bool already_verified =
        ready_phase == 0   ? progress_.peer_hello_verified
        : ready_phase == 1 ? progress_.peer_ready_verified
                           : progress_.peer_ack_received;
    if (already_verified) {
        const auto* stored = ready_phase == 0   ? peer_hello_bytes_.data()
                             : ready_phase == 1 ? peer_ready_bytes_.data()
                                                : peer_ack_bytes_.data();
        const auto stored_size = ready_phase == 0 ? peer_hello_bytes_.size()
                                                  : peer_ready_bytes_.size();
        if (bytes != nullptr && size == stored_size &&
            std::memcmp(bytes, stored, size) == 0)
            return FLY_SESSION_V2_DUPLICATE;
        return fail(FLY_SESSION_V2_PROTOCOL_VIOLATION);
    }
    if (stage_ != awaiting) return FLY_SESSION_V2_INVALID_STATE;
    if (bytes == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;

    wire::LinkControlDecodeReportV1 report{};
    if (is_hello) {
        wire::LinkHelloExpectationsV1 expected{};
        expected.session_id = start_.session_id;
        expected.link_id = start_.link_id;
        expected.channel_id = start_.channel_id;
        expected.connection_generation = start_.generation;
        expected.link_generation = start_.link_generation;
        expected.local_role = start_.local_role;
        expected.selected_plan_hash = start_.selected_plan_hash;
        expected.endpoint_offer_hash = start_.endpoint_offer_hash;
        expected.pair_transcript_object_hash = start_.pair_transcript_object_hash;
        expected.peer_binding_hash = start_.peer_binding_hash;
        expected.peer_identity_key_id = start_.peer_identity_key_id;
        expected.peer_identity_public_key = start_.peer_identity_public_key;
        /* Unknown before the peer's own binding is accepted; the HELLO codec
         * rejects a zero expectation, so this fails closed rather than
         * substituting anything. */
        expected.peer_session_signing_public_key =
            start_.peer_session_signing_public_key;
        wire::LinkControlParsedV1 parsed{};
        link::LinkHelloV1 value{};
        const auto status = wire::parse_link_hello_v1(
            bytes, size, expected,
            wire::validate_p256_uncompressed_point_callback, nullptr, &report,
            &parsed, &value);
        if (status != wire::Status::Ok) {
            if (report.issue == wire::LinkControlIssueV1::Generation)
                return FLY_SESSION_V2_STALE;
            return fail(decode_failure(report));
        }
        if (size != peer_hello_bytes_.size())
            return fail(FLY_SESSION_V2_PROTOCOL_VIOLATION);
        peer_hello_ = value;
        /* READY binds the peer's own persisted HELLO, which is this exact
         * object, so the hash is recorded at parse time and the durable write
         * happens only after stage 2 accepts the signature. */
        peer_hello_object_hash_ = value.object_hash;
        wire::LinkControlVerifyRequestV1 request{};
        if (wire::link_control_verify_request_v1(
                parsed, link::kLinkHelloDigestDomainV1, &request) !=
            wire::Status::Ok)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        return store_pending_peer_message(
            bytes, size, parsed, request,
            LinkHandshakeStageV1::AwaitPeerHelloSignature, 0,
            link::kLinkHelloObjectKindV1, value.object_hash, persist_stage);
    }

    wire::LinkReadyExpectationsV1 expected{};
    expected.session_id = start_.session_id;
    expected.link_id = start_.link_id;
    expected.channel_id = start_.channel_id;
    expected.connection_generation = start_.generation;
    expected.reconnect_attempt = start_.reconnect_attempt;
    expected.link_generation = start_.link_generation;
    expected.local_role = start_.local_role;
    expected.expected_phase = static_cast<link::LinkReadyPhaseV1>(ready_phase);
    expected.channel_bind_hash = start_.channel_bind_hash;
    expected.local_hello_object_hash = local_hello_object_hash_;
    expected.peer_hello_object_hash = peer_hello_object_hash_;
    expected.negotiated_result_hash = negotiated_result_hash_;
    expected.local_summary_hash = start_.local_summary_hash;
    expected.peer_summary_hash = start_.peer_summary_hash;
    expected.merge_result_hash = start_.merge_result_hash;
    expected.peer_session_signing_public_key =
        start_.peer_session_signing_public_key;
    wire::LinkControlParsedV1 parsed{};
    link::LinkReadyV1 value{};
    const auto status = wire::parse_link_ready_v1(
        bytes, size, expected, wire::validate_p256_uncompressed_point_callback,
        nullptr, &report, &parsed, &value);
    if (status != wire::Status::Ok) {
        if (report.issue == wire::LinkControlIssueV1::Generation)
            return FLY_SESSION_V2_STALE;
        return fail(decode_failure(report));
    }
    if (size != peer_ready_bytes_.size())
        return fail(FLY_SESSION_V2_PROTOCOL_VIOLATION);
    /* Recorded now, published only after stage 2 accepts the signature. */
    if (ready_phase == 1) {
        peer_ready_ = value;
    } else {
        peer_ack_ = value;
    }
    wire::LinkControlVerifyRequestV1 request{};
    if (wire::link_control_verify_request_v1(
            parsed, link::kLinkReadyDigestDomainV1, &request) != wire::Status::Ok)
        return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    return store_pending_peer_message(
        bytes, size, parsed, request,
        ready_phase == 1 ? LinkHandshakeStageV1::AwaitPeerReadySignature
                         : LinkHandshakeStageV1::AwaitPeerAckSignature,
        ready_phase, link::kLinkReadyObjectKindV1, value.object_hash,
        persist_stage);
}

fly_session_result_v2 LinkHandshakeScheduler::accept_peer_hello(
    const std::uint8_t* bytes, std::size_t size)
{
    return accept_peer_message(bytes, size, LinkHandshakeStageV1::AwaitPeerHello,
                               0, LinkHandshakeStageV1::PersistPeerHello);
}

fly_session_result_v2 LinkHandshakeScheduler::accept_peer_ready(
    const std::uint8_t* bytes, std::size_t size)
{
    return accept_peer_message(bytes, size, LinkHandshakeStageV1::AwaitPeerReady,
                               1, LinkHandshakeStageV1::PersistPeerReady);
}

fly_session_result_v2 LinkHandshakeScheduler::accept_peer_ack(
    const std::uint8_t* bytes, std::size_t size)
{
    return accept_peer_message(bytes, size, LinkHandshakeStageV1::AwaitPeerAck,
                               2, LinkHandshakeStageV1::PersistPeerAck);
}

fly_session_result_v2 LinkHandshakeScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || !begun_ || progress_.failed)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto kind = pending_->kind;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK) return fail(completion.result);

    std::vector<std::uint8_t> bytes;
    if (kind == LinkHandshakeEffectKind::ReadLocalBindingObject) {
        if (completion.payload.resource == 0 ||
            read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != wire::kSessionSigningBindingSizeV1)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        wire::SessionSigningBindingV1 binding{};
        if (wire::decode_session_signing_binding_v1(
                bytes.data(), bytes.size(), start_.pair_transcript_hash,
                start_.session_id, start_.local_role,
                start_.local_identity_public_key,
                wire::validate_p256_uncompressed_point_callback, nullptr,
                &binding) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        if (binding.hash != start_.local_binding_hash ||
            binding.session_signing_public_key !=
                start_.local_session_signing_public_key)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        progress_.local_binding_durable = true;
        pending_.reset();
        return request_hello_signature();
    }
    if (kind == LinkHandshakeEffectKind::SignHello) {
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != 64)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        std::array<std::uint8_t, 64> signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        if (wire::finish_link_hello_v1(hello_pretag_, signature,
                                       &local_hello_bytes_,
                                       &local_hello_) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        local_hello_object_hash_ = local_hello_.object_hash;
        pending_.reset();
        return persist_object(link::kLinkHelloObjectKindV1,
                              local_hello_object_hash_, local_hello_bytes_.data(),
                              local_hello_bytes_.size(),
                              LinkHandshakeStageV1::PersistHello);
    }
    if (kind == LinkHandshakeEffectKind::PersistHelloObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != local_hello_object_hash_)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        progress_.local_hello_durable = true;
        pending_.reset();
        return send_bytes(local_hello_bytes_.data(), local_hello_bytes_.size(),
                          LinkHandshakeStageV1::SendHello);
    }
    if (kind == LinkHandshakeEffectKind::VerifyPeerSignature) {
        /*
         * Stage 2. The provider already told us whether the signature is valid
         * (completion.result); now the parsed value is either accepted - and
         * only now persisted and answered - or the link fails closed here.
         */
        return complete_verify_peer_signature();
    }
    if (kind == LinkHandshakeEffectKind::SendHello) {
        pending_.reset();
        stage_ = LinkHandshakeStageV1::AwaitPeerHello;
        record(stage_);
        return FLY_SESSION_V2_OK;
    }
    if (kind == LinkHandshakeEffectKind::PersistPeerHelloObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != peer_hello_object_hash_)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_.reset();
        return request_negotiated_result();
    }
    if (kind == LinkHandshakeEffectKind::PersistNegotiatedResult) {
        if (completion.payload.resource == 0)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, 32> hash{};
        if (wire::hash_link_negotiated_result_v1(
                start_.negotiated_result.data(), start_.negotiated_result.size(),
                &hash) != wire::Status::Ok ||
            hash != start_.negotiated_result_hash)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        negotiated_result_hash_ = hash;
        progress_.negotiated_result_durable = true;
        pending_.reset();
        return request_ready_signature();
    }
    if (kind == LinkHandshakeEffectKind::SignReady) {
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != 64)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        std::array<std::uint8_t, 64> signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        if (wire::finish_link_ready_v1(ready_pretag_, signature,
                                       &local_ready_bytes_,
                                       &local_ready_) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        pending_.reset();
        return persist_object(link::kLinkReadyObjectKindV1,
                              local_ready_.object_hash, local_ready_bytes_.data(),
                              local_ready_bytes_.size(),
                              LinkHandshakeStageV1::PersistReady);
    }
    if (kind == LinkHandshakeEffectKind::PersistReadyObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != local_ready_.object_hash)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        progress_.local_ready_durable = true;
        pending_.reset();
        return send_bytes(local_ready_bytes_.data(), local_ready_bytes_.size(),
                          LinkHandshakeStageV1::SendReady);
    }
    if (kind == LinkHandshakeEffectKind::SendReady) {
        pending_.reset();
        stage_ = LinkHandshakeStageV1::AwaitPeerReady;
        record(stage_);
        return FLY_SESSION_V2_OK;
    }
    if (kind == LinkHandshakeEffectKind::PersistPeerReadyObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != peer_ready_.object_hash)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_.reset();
        return request_ack_signature();
    }
    if (kind == LinkHandshakeEffectKind::SignAck) {
        if (read_buffer(completion.payload, bytes) != FLY_SESSION_V2_OK ||
            bytes.size() != 64)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        std::array<std::uint8_t, 64> signature{};
        std::copy(bytes.begin(), bytes.end(), signature.begin());
        if (wire::finish_link_ready_v1(ack_pretag_, signature,
                                       &local_ack_bytes_,
                                       &local_ack_) != wire::Status::Ok)
            return fail(FLY_SESSION_V2_AUTH_FAILED);
        pending_.reset();
        return persist_object(link::kLinkReadyObjectKindV1,
                              local_ack_.object_hash, local_ack_bytes_.data(),
                              local_ack_bytes_.size(),
                              LinkHandshakeStageV1::PersistAck);
    }
    if (kind == LinkHandshakeEffectKind::PersistAckObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != local_ack_.object_hash)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_.reset();
        return send_bytes(local_ack_bytes_.data(), local_ack_bytes_.size(),
                          LinkHandshakeStageV1::SendAck);
    }
    if (kind == LinkHandshakeEffectKind::SendAck) {
        pending_.reset();
        stage_ = LinkHandshakeStageV1::AwaitPeerAck;
        record(stage_);
        return FLY_SESSION_V2_OK;
    }
    if (kind == LinkHandshakeEffectKind::PersistPeerAckObject) {
        if (completion.payload.resource == 0 ||
            completion.payload.hash != peer_ack_.object_hash)
            return fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_.reset();
        stage_ = LinkHandshakeStageV1::Connected;
        record(stage_);
        return connected() ? FLY_SESSION_V2_OK
                           : fail(FLY_SESSION_V2_CONTRACT_VIOLATION);
    }
    return fail(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 LinkHandshakeScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK) fail(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 LinkHandshakeScheduler::on_link_terminal(
    fly_session_result_v2 result) noexcept
{
    if (!begun_) return FLY_SESSION_V2_INVALID_STATE;
    if (result == FLY_SESSION_V2_OK) result = FLY_SESSION_V2_IO_FAILED;
    if (progress_.failed) return result;
    return fail(result);
}

fly_session_result_v2 LinkHandshakeScheduler::shutdown() noexcept
{
    if (!begun_) return FLY_SESSION_V2_INVALID_STATE;
    return fail(FLY_SESSION_V2_CLOSED);
}

} // namespace flynes::session
