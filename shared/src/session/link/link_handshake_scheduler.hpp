#ifndef FLYNES_SESSION_LINK_LINK_HANDSHAKE_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_LINK_HANDSHAKE_SCHEDULER_HPP

/*
 * W1 Task 4: pure LINK_HELLO / LINK_READY handshake scheduler.
 *
 * The scheduler owns no platform object. It is driven by effects and values
 * only: the engine polls one effect at a time, dispatches it to a provider port
 * and feeds the terminal back through complete(). Inbound peer control messages
 * are handed over as exact bytes through accept_peer_*(), which decode and
 * verify them with the exact codecs from Task 3.
 *
 * The single legal sequence, with persist-before-send enforced everywhere:
 *
 *   durable local 0x0212 binding (read + decoded, never assumed)
 *     -> persist HELLO object -> send HELLO
 *     -> receive/verify peer HELLO -> persist peer HELLO object
 *     -> persist negotiated result
 *     -> persist READY object -> send READY
 *     -> receive/verify peer READY -> persist peer READY object
 *     -> persist ACK object -> send ACK
 *     -> receive/verify peer ACK -> persist peer ACK object
 *     -> CONNECTED_LOBBY (only via project_link_control_state_v1)
 *
 * Every effect carries a monotonic operation id, a token scoped to this link
 * generation and the exact expected payload kind. Every accepted peer message
 * is durably persisted before this side answers with an ACK.
 */

#include "../ports/provider_operation_journal.hpp"
#include "../wire/link_hello.hpp"
#include "../wire/link_ready.hpp"
#include "link_control_contract.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class LinkHandshakeEffectKind : std::uint8_t
{
    ReadLocalBindingObject,
    SignHello,
    PersistHelloObject,
    SendHello,
    VerifyPeerSignature,
    PersistPeerHelloObject,
    PersistNegotiatedResult,
    SignReady,
    PersistReadyObject,
    SendReady,
    PersistPeerReadyObject,
    SignAck,
    PersistAckObject,
    SendAck,
    PersistPeerAckObject
};

/*
 * The observable stage sequence. The tests assert the exact trace, so a missing
 * durable write or a reordered send is visible as a stage-order failure rather
 * than only as a broken end state.
 *
 * Every received peer message passes through its own AwaitPeerXSignature stage:
 * the scheduler parses the exact bytes, asks the crypto port to verify the
 * sender's signature asynchronously, and only then persists and answers. The
 * signature is therefore never checked by a synchronous callback inside the
 * decoder.
 */
enum class LinkHandshakeStageV1 : std::uint8_t
{
    Empty = 0,
    ReadLocalBinding,
    SignHello,
    PersistHello,
    SendHello,
    AwaitPeerHello,
    AwaitPeerHelloSignature,
    PersistPeerHello,
    PersistNegotiatedResult,
    SignReady,
    PersistReady,
    SendReady,
    AwaitPeerReady,
    AwaitPeerReadySignature,
    PersistPeerReady,
    SignAck,
    PersistAck,
    SendAck,
    AwaitPeerAck,
    AwaitPeerAckSignature,
    PersistPeerAck,
    Connected,
    Failed
};

inline constexpr std::size_t kLinkHandshakeTraceCapacityV1 = 48;

struct LinkHandshakeStartV1 final
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::array<std::uint8_t, 16> session_id{};
    /* Connection generation: the token scope and the stale-link discriminator. */
    std::uint64_t generation = 0;
    std::uint64_t link_generation = 0;
    /* The 16-byte channel identity owned by InitialQuicBindScheduler
     * (wire::derive_channel_id_v1). The former u64 channel_id / channel_bind_id
     * pair was invented by the first contract revision and is deleted: the bind
     * is identified by channel_bind_hash alone. */
    std::array<std::uint8_t, 16> channel_id{};
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role = wire::PairRoleV1::Initiator;
    std::uint8_t determinism_profile = 0;
    std::uint8_t core_state_format = 0;
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> pair_transcript_object_hash{};
    std::array<std::uint8_t, 32> selected_plan_hash{};
    std::array<std::uint8_t, 32> endpoint_offer_hash{};
    /* wire::channel_bind_proof_hash_v1 of the authenticated bind proof, owned by
     * InitialQuicBindScheduler. This single value is the whole channel-bind
     * binding READY carries. */
    std::array<std::uint8_t, 32> channel_bind_hash{};
    /* Locally persisted 0x0212 binding, referenced by hash only. */
    std::array<std::uint8_t, 32> local_binding_hash{};
    std::array<std::uint8_t, 65> local_identity_public_key{};
    std::array<std::uint8_t, 65> local_session_signing_public_key{};
    /* The peer's binding as accepted through the pair transcript. */
    std::array<std::uint8_t, 32> peer_binding_hash{};
    std::array<std::uint8_t, 32> peer_identity_key_id{};
    std::array<std::uint8_t, 65> peer_identity_public_key{};
    std::array<std::uint8_t, 65> peer_session_signing_public_key{};
    /* The negotiated result is NOT an input any more (owner decision
     * 2026-09-16). It is derived inside the scheduler from the two HELLO object
     * hashes, which are the only real negotiation evidence both sides hold:
     *
     *   preimage = initiator_hello_object_hash || responder_hello_object_hash
     *   hash     = domain_hash("flynes-link-negotiated-result-v1", preimage, 64)
     *
     * Deriving it here removes the second source of truth a caller-supplied
     * value would create, and it is independently checkable from READY, which
     * already carries both HELLO object hashes. */
    std::array<std::uint8_t, 32> local_summary_hash{};
    std::array<std::uint8_t, 32> peer_summary_hash{};
    std::array<std::uint8_t, 32> merge_result_hash{};
    fly_session_resource_handle_v2 session_signing_key = 0;
    fly_session_resource_handle_v2 control_stream = 0;
};

struct LinkHandshakeEffect final
{
    LinkHandshakeEffectKind kind{};
    fly_session_op_token_v2 token{};
    std::uint32_t expected_payload_kind = 0;
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t key_purpose = 0;
    std::uint32_t object_kind = 0;
    std::array<std::uint8_t, 32> expected_hash{};
    std::array<std::uint8_t, 32> digest{};
    /* VerifyPeerSignature only: the exact public key whose signature must be
     * verified (the peer's session signing key, taken from the message itself),
     * plus the canonical low-S signature the peer sent, so the engine can hand
     * both to the asynchronous crypto port unchanged. */
    std::array<std::uint8_t, 65> signer_public_key{};
    std::array<std::uint8_t, 64> signature{};
    std::vector<std::uint8_t> domain{};
    std::vector<std::uint8_t> name_space{};
    std::vector<std::uint8_t> record_key{};
    std::vector<std::uint8_t> value{};
    std::uint64_t expected_revision = 0;
};

class LinkHandshakeScheduler final
{
public:
    /* Starts a fresh attempt, or replaces an older attempt when start.generation
     * is strictly newer. An older or equal generation is rejected as STALE and
     * changes nothing. */
    bool begin(const LinkHandshakeStartV1& start);

    [[nodiscard]] std::optional<LinkHandshakeEffect> poll_effect() const;
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 cancel_pending() noexcept;
    /* QUIC terminal: the transport died under a live attempt. */
    fly_session_result_v2 on_link_terminal(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 shutdown() noexcept;

    fly_session_result_v2 accept_peer_hello(const std::uint8_t* bytes,
                                            std::size_t size);
    fly_session_result_v2 accept_peer_ready(const std::uint8_t* bytes,
                                            std::size_t size);
    fly_session_result_v2 accept_peer_ack(const std::uint8_t* bytes,
                                          std::size_t size);

    /*
     * gap 3 owner: the peer's ACCEPTED 0x0212 binding.
     *
     * LINK_HELLO can only be accepted against the three values the peer's own
     * session signing binding authenticated: its binding object hash, its
     * identity_key_id and the session signing public key that binding carries.
     * This method is their single producer. It is deliberately NOT a setter for
     * caller-supplied values: it takes the exact bytes of the peer's 0x0212
     * object as read back from the ObjectStore plus the content hash they must
     * have, and derives every field through
     * wire::decode_session_signing_binding_v1 — so a value that was not actually
     * decoded from a genuine, self-consistent peer binding can never be
     * installed:
     *
     *   - the bytes must be exactly wire::kSessionSigningBindingSizeV1 (312);
     *   - the recomputed object hash must equal expected_binding_hash, which the
     *     handle_peer_hello() path supplies from the peer's own HELLO, so peer
     *     binding and peer HELLO are cross-checked rather than both trusted;
     *   - the embedded pair_transcript_hash, session_id and pair role must equal
     *     this attempt's, and the embedded long-term identity public key must be
     *     the peer identity the accepted pair transcript pinned.
     *
     * The engine still has no peer 0x0212 bytes to hand over (there is no inbound
     * Control stream), so this has no production caller yet; it fails closed on
     * malformed input and leaves the scheduler untouched on every failure.
     */
    fly_session_result_v2 accept_peer_binding(
        const std::uint8_t* bytes, std::size_t size,
        const std::array<std::uint8_t, 32>& expected_binding_hash);

    /* True once accept_peer_binding() installed a decoded, accepted peer
     * binding. */
    [[nodiscard]] bool peer_binding_accepted() const noexcept
    { return peer_binding_accepted_; }
    [[nodiscard]] const wire::SessionSigningBindingV1& peer_binding() const
        noexcept
    { return peer_binding_; }

    [[nodiscard]] bool begun() const noexcept { return begun_; }
    [[nodiscard]] bool failed() const noexcept { return progress_.failed; }
    /* True when the attempt stopped because this build has no producer for a
     * start input the protocol needs further along the sequence (today: the
     * negotiated result and, until accept_peer_binding() runs, the peer's
     * accepted 0x0212 binding). That is a seam that is not wired yet, never a
     * protocol failure, and the engine must not publish a failed link for it.
     * The channel id and the channel-bind binding hash used to be on this list;
     * InitialQuicBindScheduler produces both now. */
    [[nodiscard]] bool missing_inputs() const noexcept { return missing_inputs_; }
    [[nodiscard]] bool has_pending_operation() const noexcept
    { return operations_.has_pending(); }
    [[nodiscard]] bool connected() const noexcept
    { return link::project_link_control_state_v1(progress_) ==
             link::LinkControlStateV1::ConnectedLobby; }
    [[nodiscard]] link::LinkControlStateV1 projected_state() const noexcept
    { return link::project_link_control_state_v1(progress_); }
    [[nodiscard]] const link::LinkControlProgressV1& progress() const noexcept
    { return progress_; }
    [[nodiscard]] LinkHandshakeStageV1 stage() const noexcept { return stage_; }
    [[nodiscard]] std::size_t stage_trace_size() const noexcept
    { return trace_size_; }
    [[nodiscard]] LinkHandshakeStageV1 stage_trace_at(std::size_t index) const
        noexcept;
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }
    [[nodiscard]] const link::LinkHelloV1& local_hello() const noexcept
    { return local_hello_; }
    [[nodiscard]] const link::LinkReadyV1& local_ready() const noexcept
    { return local_ready_; }
    [[nodiscard]] const link::LinkReadyV1& local_ack() const noexcept
    { return local_ack_; }
    [[nodiscard]] const std::array<std::uint8_t, link::kLinkHelloSizeV1>&
    local_hello_bytes() const noexcept { return local_hello_bytes_; }
    [[nodiscard]] const std::array<std::uint8_t, link::kLinkReadySizeV1>&
    local_ready_bytes() const noexcept { return local_ready_bytes_; }
    [[nodiscard]] const std::array<std::uint8_t, link::kLinkReadySizeV1>&
    local_ack_bytes() const noexcept { return local_ack_bytes_; }
    [[nodiscard]] const link::LinkHelloV1& peer_hello() const noexcept
    { return peer_hello_; }
    [[nodiscard]] const link::LinkReadyV1& peer_ready() const noexcept
    { return peer_ready_; }
    [[nodiscard]] const link::LinkReadyV1& peer_ack() const noexcept
    { return peer_ack_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>&
    negotiated_result_hash() const noexcept { return negotiated_result_hash_; }
    /* The exact 64-byte preimage that hash covers, produced by
     * request_negotiated_result(). Zero before that step. */
    [[nodiscard]] const std::array<std::uint8_t,
                                   link::kLinkNegotiatedResultPreimageSizeV1>&
    negotiated_result_preimage() const noexcept
    { return negotiated_result_preimage_; }
    /* This side's own persisted HELLO object hash and the verified peer HELLO
     * object hash. Zero until the corresponding step completed. These are the
     * two inputs of the negotiated result, exposed so a caller (or a test) can
     * recompute and check the derivation rather than trust it. */
    [[nodiscard]] const std::array<std::uint8_t, 32>&
    local_hello_object_hash() const noexcept
    { return local_hello_object_hash_; }
    [[nodiscard]] const std::array<std::uint8_t, 32>&
    peer_hello_object_hash() const noexcept
    { return peer_hello_object_hash_; }

private:
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    void record(LinkHandshakeStageV1 stage) noexcept;
    void reset_attempt(const LinkHandshakeStartV1& start) noexcept;
    fly_session_result_v2 fail(fly_session_result_v2 result) noexcept;
    /* Fails closed when an input with no producer in this build is absent,
     * recording that fact so the engine can distinguish "not wired yet" from a
     * real protocol failure. */
    fly_session_result_v2 require_inputs(bool present) noexcept;
    fly_session_result_v2 issue(LinkHandshakeEffect effect,
                                LinkHandshakeStageV1 stage);
    fly_session_result_v2 read_buffer(const ParsedProviderEvent& event,
                                      std::vector<std::uint8_t>& out) const;
    fly_session_result_v2 persist_object(std::uint32_t object_kind,
                                         const std::array<std::uint8_t, 32>& hash,
                                         const std::uint8_t* bytes,
                                         std::size_t size,
                                         LinkHandshakeStageV1 stage);
    fly_session_result_v2 send_bytes(const std::uint8_t* bytes, std::size_t size,
                                     LinkHandshakeStageV1 stage);
    fly_session_result_v2 sign(std::uint32_t purpose,
                               const std::array<std::uint8_t, 32>& digest,
                               const char* domain, LinkHandshakeStageV1 stage);
    fly_session_result_v2 request_local_binding();
    fly_session_result_v2 request_hello_signature();
    fly_session_result_v2 request_negotiated_result();
    fly_session_result_v2 request_ready_signature();
    fly_session_result_v2 request_ack_signature();
    fly_session_result_v2 accept_peer_message(const std::uint8_t* bytes,
                                              std::size_t size,
                                              LinkHandshakeStageV1 awaiting,
                                              std::uint8_t ready_phase,
                                              LinkHandshakeStageV1 persist_stage);
    /* Stage 1 of a received control message: hold the exact bytes plus the
     * parsed value and issue the asynchronous VerifyPeerSignature effect.
     * Nothing is persisted and nothing is answered before stage 2 accepts it. */
    fly_session_result_v2 store_pending_peer_message(
        const std::uint8_t* bytes, std::size_t size,
        const wire::LinkControlParsedV1& parsed,
        const wire::LinkControlVerifyRequestV1& request,
        LinkHandshakeStageV1 verify_stage, std::uint32_t peer_value,
        std::uint32_t peer_kind,
        const std::array<std::uint8_t, 32>& peer_hash,
        LinkHandshakeStageV1 persist_stage);
    /* Stage 2: the asynchronous verification terminal has arrived. Accept and
     * persist, or fail the link closed. */
    fly_session_result_v2 complete_verify_peer_signature();
    fly_session_result_v2 persist_peer_message();

    ProviderOperationJournal operations_{1};
    LinkHandshakeStartV1 start_{};
    std::optional<LinkHandshakeEffect> pending_{};
    fly_session_result_v2 cancel_result_ = FLY_SESSION_V2_CANCELLED;
    link::LinkControlProgressV1 progress_{};
    link::LinkHelloV1 local_hello_{};
    link::LinkReadyV1 local_ready_{};
    link::LinkReadyV1 local_ack_{};
    link::LinkHelloV1 peer_hello_{};
    link::LinkReadyV1 peer_ready_{};
    link::LinkReadyV1 peer_ack_{};
    std::array<std::uint8_t, link::kLinkHelloSizeV1> local_hello_bytes_{};
    std::array<std::uint8_t, link::kLinkReadySizeV1> local_ready_bytes_{};
    std::array<std::uint8_t, link::kLinkReadySizeV1> local_ack_bytes_{};
    std::array<std::uint8_t, 32> local_hello_object_hash_{};
    std::array<std::uint8_t, 32> peer_hello_object_hash_{};
    std::array<std::uint8_t, 32> negotiated_result_hash_{};
    std::array<std::uint8_t, link::kLinkNegotiatedResultPreimageSizeV1>
        negotiated_result_preimage_{};
    std::array<std::uint8_t, link::kLinkHelloPretagSizeV1> hello_pretag_{};
    std::array<std::uint8_t, link::kLinkReadyPretagSizeV1> ready_pretag_{};
    std::array<std::uint8_t, link::kLinkReadyPretagSizeV1> ack_pretag_{};
    std::array<std::uint8_t, link::kLinkHelloSizeV1> peer_hello_bytes_{};
    std::array<std::uint8_t, link::kLinkReadySizeV1> peer_ready_bytes_{};
    std::array<std::uint8_t, link::kLinkReadySizeV1> peer_ack_bytes_{};
    std::array<LinkHandshakeStageV1, kLinkHandshakeTraceCapacityV1> trace_{};
    std::size_t trace_size_ = 0;
    std::uint64_t next_operation_id_ = 0;
    LinkHandshakeStageV1 stage_ = LinkHandshakeStageV1::Empty;
    LinkHandshakeStageV1 persist_stage_ = LinkHandshakeStageV1::Empty;
    /* The outstanding asynchronous verification of a received control message,
     * plus everything needed to resume the sequence once it returns. */
    std::optional<wire::LinkControlVerifyRequestV1> pending_verification_{};
    std::vector<std::uint8_t> pending_peer_preimage_{};
    std::array<std::uint8_t, 32> pending_peer_hash_{};
    std::uint32_t pending_peer_kind_ = 0;
    std::uint32_t pending_peer_value_ = 0;
    LinkHandshakeStageV1 pending_persist_stage_ = LinkHandshakeStageV1::Empty;
    /* The peer's decoded, accepted 0x0212 binding: the single owner of
     * peer_binding_hash / peer_identity_key_id / peer_session_signing_public_key.
     * Written only by accept_peer_binding(), which derives every field through
     * the wire decoder. */
    wire::SessionSigningBindingV1 peer_binding_{};
    bool peer_binding_accepted_ = false;
    bool begun_ = false;
    bool missing_inputs_ = false;
};

} // namespace flynes::session

#endif
