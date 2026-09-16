#ifndef FLYNES_SESSION_LINK_INITIAL_QUIC_BIND_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_INITIAL_QUIC_BIND_SCHEDULER_HPP

#include "endpoint_offer_scheduler.hpp"
#include "../wire/channel_bind.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class InitialQuicBindEffectKind : std::uint8_t
{
    DeriveKey,
    StartConnection,
    InspectHandshake,
    Exporter,
    OpenBindStream,
    AcceptBindStream,
    Hmac,
    Write,
    Read
};

struct InitialQuicBindStartV1
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    std::array<std::uint8_t, 32> transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    wire::BearerPlanBytes selected_plan{};
    std::array<std::uint8_t, 32> listener_spki_hash{};
    std::array<std::uint8_t, 65> local_identity_public{};
    std::array<std::uint8_t, 65> peer_identity_public{};
    fly_session_resource_handle_v2 ecdh_secret = 0;
    fly_session_resource_handle_v2 bearer_path = 0;
    fly_session_resource_handle_v2 tls_material = 0;
};

struct InitialQuicBindEffect
{
    InitialQuicBindEffectKind kind{};
    fly_session_op_token_v2 token{};
    std::uint32_t expected_payload_kind = 0;
    fly_session_resource_handle_v2 resource = 0;
    fly_session_resource_handle_v2 tls_material = 0;
    bool listener = false;
    bool accept = false;
    bool finish = false;
    std::uint32_t opener_role = 0;
    std::uint64_t read_credit = 0;
    std::vector<std::uint8_t> endpoint;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> exporter_context;
    fly_session_quic_connect_policy_v2 policy{};
};

struct InitialQuicBindResourcesV1
{
    fly_session_resource_handle_v2 connection = 0;
    fly_session_resource_handle_v2 send_stream = 0;
    fly_session_resource_handle_v2 receive_stream = 0;
    fly_session_resource_handle_v2 i2r_key = 0;
    fly_session_resource_handle_v2 r2i_key = 0;
};

class InitialQuicBindScheduler final
{
public:
    explicit InitialQuicBindScheduler(EndpointOfferScheduler& endpoint) noexcept
        : endpoint_(&endpoint) {}

    bool begin(const InitialQuicBindStartV1& start);
    [[nodiscard]] std::optional<InitialQuicBindEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool channel_bound() const noexcept
    { return channel_bound_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool listener() const noexcept { return local_listener_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }
    [[nodiscard]] const std::array<std::uint8_t, 16>& channel_id() const noexcept
    { return channel_id_; }
    /*
     * The two proof hashes the ACK binds, computed exactly as
     * wire::encode_channel_bind_ack_body_v1 / decode_channel_bind_ack_v1 do.
     * Both roles end up holding the same pair: the connector proof is the one
     * this side built when it is the connector and the one it verified when it
     * is the listener, and vice versa. The bind is therefore identified by
     * (channel_id, connector_proof_hash, listener_proof_hash) and nothing else,
     * which is what wire::channel_bind_binding_hash_v1 canonicalises for
     * LINK_READY. Legal to read once channel_bound() is true.
     */
    [[nodiscard]] std::array<std::uint8_t, 32> connector_proof_hash()
        const noexcept
    { return wire::channel_bind_proof_hash_v1(connector_proof_); }
    [[nodiscard]] std::array<std::uint8_t, 32> listener_proof_hash()
        const noexcept
    { return wire::channel_bind_proof_hash_v1(listener_proof_); }
    /* The session id this bind was locked under. Kept here because the owner of
     * pair_context_ releases it well before the LINK_HELLO mount point, while
     * the bind scheduler survives the whole link. */
    [[nodiscard]] const std::array<std::uint8_t, 16>& session_id() const noexcept
    { return start_.session_id; }
    [[nodiscard]] InitialQuicBindResourcesV1 owned_resources() const noexcept
    { return resources_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty, DeriveI2r, DeriveR2i, StartConnection, InspectHandshake,
        Exporter, OpenStream, AcceptStream, MakeLocalProof,
        WriteConnectorProof, ReadConnectorProof, VerifyConnectorProof,
        WriteListenerProof, ReadListenerProof, VerifyListenerProof, MakeAck,
        WriteAckAndFin, ReadAck, VerifyAck, ReadConnectorFin,
        WriteListenerFin, ReadListenerFin, Bound, Failed
    };

    fly_session_op_token_v2 token(std::uint64_t id) const noexcept;
    fly_session_result_v2 issue(InitialQuicBindEffect effect, Stage stage);
    fly_session_result_v2 derive(bool i2r);
    fly_session_result_v2 start_connection();
    fly_session_result_v2 inspect_handshake();
    fly_session_result_v2 request_exporter();
    fly_session_result_v2 open_or_accept_stream();
    fly_session_result_v2 make_local_proof();
    fly_session_result_v2 verify_peer_proof();
    fly_session_result_v2 make_ack();
    fly_session_result_v2 verify_ack();
    fly_session_result_v2 write_bytes(std::vector<std::uint8_t> bytes,
                                      bool finish, Stage stage);
    fly_session_result_v2 read_bytes(std::uint64_t maximum, Stage stage);
    fly_session_result_v2 read_buffer(fly_session_buffer_v2_t* buffer,
                                      std::vector<std::uint8_t>* out) const;
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;

    EndpointOfferScheduler* endpoint_ = nullptr;
    InitialQuicBindStartV1 start_{};
    std::optional<InitialQuicBindEffect> pending_{};
    InitialQuicBindResourcesV1 resources_{};
    std::array<std::uint8_t, 32> exporter_{};
    std::array<std::uint8_t, 16> channel_id_{};
    std::array<std::uint8_t, wire::kChannelBindSizeV1> bind_{};
    std::array<std::uint8_t, wire::kChannelBindProofSizeV1> connector_proof_{};
    std::array<std::uint8_t, wire::kChannelBindProofSizeV1> listener_proof_{};
    std::array<std::uint8_t, wire::kChannelBindAckSizeV1> ack_{};
    std::vector<std::uint8_t> pending_hmac_body_{};
    std::vector<std::uint8_t> read_accumulator_{};
    std::size_t expected_read_size_ = 0;
    std::array<std::uint8_t, 32> expected_peer_tag_{};
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool local_listener_ = false;
    bool begun_ = false;
    bool channel_bound_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
