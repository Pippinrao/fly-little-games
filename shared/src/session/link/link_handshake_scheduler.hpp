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
 */
enum class LinkHandshakeStageV1 : std::uint8_t
{
    Empty = 0,
    ReadLocalBinding,
    SignHello,
    PersistHello,
    SendHello,
    AwaitPeerHello,
    PersistPeerHello,
    PersistNegotiatedResult,
    SignReady,
    PersistReady,
    SendReady,
    AwaitPeerReady,
    PersistPeerReady,
    SignAck,
    PersistAck,
    SendAck,
    AwaitPeerAck,
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
    std::uint64_t channel_id = 0;
    std::uint64_t channel_bind_id = 0;
    std::uint64_t reconnect_attempt = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role = wire::PairRoleV1::Initiator;
    std::uint8_t determinism_profile = 0;
    std::uint8_t core_state_format = 0;
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 32> pair_transcript_object_hash{};
    std::array<std::uint8_t, 32> selected_plan_hash{};
    std::array<std::uint8_t, 32> endpoint_offer_hash{};
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
    /* Negotiated capability/runtime result, persisted verbatim; its hash is
     * derived locally and must match negotiated_result_hash. */
    std::vector<std::uint8_t> negotiated_result{};
    std::array<std::uint8_t, 32> negotiated_result_hash{};
    std::array<std::uint8_t, 32> local_summary_hash{};
    std::array<std::uint8_t, 32> peer_summary_hash{};
    std::array<std::uint8_t, 32> merge_result_hash{};
    fly_session_resource_handle_v2 session_signing_key = 0;
    fly_session_resource_handle_v2 control_stream = 0;
    wire::LinkControlSignatureVerifierV1 verify_peer_signature = nullptr;
    void* verify_context = nullptr;
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

    [[nodiscard]] bool begun() const noexcept { return begun_; }
    [[nodiscard]] bool failed() const noexcept { return progress_.failed; }
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

private:
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    void record(LinkHandshakeStageV1 stage) noexcept;
    void reset_attempt(const LinkHandshakeStartV1& start) noexcept;
    fly_session_result_v2 fail(fly_session_result_v2 result) noexcept;
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
    bool begun_ = false;
};

} // namespace flynes::session

#endif
