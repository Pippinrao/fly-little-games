#ifndef FLYNES_SESSION_LINK_ENDPOINT_OFFER_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_ENDPOINT_OFFER_SCHEDULER_HPP

#include "initial_bearer_scheduler.hpp"
#include "../wire/endpoint_offer.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

struct EndpointOfferSchedulerTestFactory;

enum class EndpointOfferEffectKind : std::uint8_t
{
    DeriveKey, Random, ResolveEndpoint, AeadSeal, AeadOpen, Persist
};

struct EndpointOfferStartV1
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
    fly_session_resource_handle_v2 ecdh_secret = 0;
    fly_session_resource_handle_v2 bearer_path = 0;
};

struct EndpointOfferEffect
{
    EndpointOfferEffectKind kind{};
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t byte_count = 0;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
    std::vector<std::uint8_t> listener_token;
};

class EndpointOfferScheduler final
{
public:
    explicit EndpointOfferScheduler(InitialBearerScheduler& bearer) noexcept
        : bearer_(&bearer) {}
    bool begin(const EndpointOfferStartV1& start);
    [[nodiscard]] std::optional<EndpointOfferEffect> poll_effect() const
    { return pending_; }
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_logical(const std::uint8_t* bytes,
                                               std::size_t size);
    [[nodiscard]] std::optional<std::vector<std::uint8_t>> local_logical() const
    { return local_send_; }
    fly_session_result_v2 mark_local_sent() noexcept;
    fly_session_result_v2 cancel_pending() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_ && !failed_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] bool listener() const noexcept { return local_listener_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }
    [[nodiscard]] fly_session_resource_handle_v2 i2r_key() const noexcept
    { return i2r_key_; }
    [[nodiscard]] fly_session_resource_handle_v2 r2i_key() const noexcept
    { return r2i_key_; }
    [[nodiscard]] const std::vector<std::uint8_t>& endpoint() const noexcept
    { return endpoint_; }

private:
    friend struct EndpointOfferSchedulerTestFactory;
    enum class Stage : std::uint8_t
    {
        Empty, DeriveI2r, DeriveR2i, RandomToken, Resolve, RandomNonce,
        Seal, WaitPeer, Open, Persist, WaitSend, Ready, Failed
    };
    fly_session_op_token_v2 token(std::uint64_t id) const noexcept;
    fly_session_result_v2 issue(EndpointOfferEffect effect,
                                std::uint32_t payload, Stage stage);
    fly_session_result_v2 derive(bool i2r);
    fly_session_result_v2 random(std::uint32_t size, Stage stage);
    fly_session_result_v2 resolve();
    fly_session_result_v2 seal();
    fly_session_result_v2 persist();
    fly_session_result_v2 read_buffer(const ProviderOperationCompletion& value,
                                      std::vector<std::uint8_t>& out) const;
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;

    InitialBearerScheduler* bearer_ = nullptr;
    ProviderOperationJournal operations_{1};
    EndpointOfferStartV1 start_{};
    std::optional<EndpointOfferEffect> pending_{};
    std::optional<std::vector<std::uint8_t>> local_send_{};
    std::vector<std::uint8_t> peer_exact_{};
    std::vector<std::uint8_t> endpoint_{};
    std::vector<std::uint8_t> listener_token_{};
    std::array<std::uint8_t, 12> nonce_{};
    fly_session_resource_handle_v2 i2r_key_ = 0;
    fly_session_resource_handle_v2 r2i_key_ = 0;
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool local_listener_ = false;
    bool begun_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
