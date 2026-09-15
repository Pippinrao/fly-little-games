#ifndef FLYNES_SESSION_LINK_PAIR_REVEAL_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_REVEAL_SCHEDULER_HPP

#include "pair_material_scheduler.hpp"
#include "../wire/pair_reveal.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairRevealEffectKind : std::uint8_t
{
    AgreeKey,
    DeriveKey,
    RandomBytes,
    AeadSeal,
    AeadOpen
};

struct PairRevealStartV1 final
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairRoleV1 local_role{};
    wire::PairContextV1 context{};
    wire::PairCommitV1 initiator_commit{};
    wire::PairCommitV1 responder_commit{};
    PairLocalMaterialV1 local_material{};
};

struct PairRevealEffect final
{
    fly_session_op_token_v2 token{};
    PairRevealEffectKind kind{};
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t byte_count = 0;
    std::vector<std::uint8_t> peer_public_key;
    std::vector<std::uint8_t> salt;
    std::vector<std::uint8_t> info;
    std::vector<std::uint8_t> nonce;
    std::vector<std::uint8_t> aad;
    std::vector<std::uint8_t> input;
};

struct PairRevealSecretsV1 final
{
    fly_session_resource_handle_v2 ecdh_secret = 0;
    fly_session_resource_handle_v2 i2r_key = 0;
    fly_session_resource_handle_v2 r2i_key = 0;
};

class PairRevealScheduler final
{
public:
    PairRevealScheduler();

    bool begin(const PairRevealStartV1& start);
    [[nodiscard]] std::optional<PairRevealEffect> poll_effect() const;
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 accept_peer_envelope(
        const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    fly_session_result_v2 mark_local_reveal_sent() noexcept;
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool local_reveal_ready() const noexcept;
    [[nodiscard]] bool local_reveal_sent() const noexcept { return local_sent_; }
    [[nodiscard]] bool peer_reveal_verified() const noexcept;
    [[nodiscard]] bool ready() const noexcept;
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::optional<std::array<std::uint8_t,
        wire::kPairRevealBodySizeV1>> local_envelope() const;
    [[nodiscard]] std::optional<wire::PairContributionV1>
        peer_contribution() const;
    [[nodiscard]] const std::array<std::uint8_t, 32>&
        peer_logical_hash() const noexcept { return peer_logical_hash_; }
    [[nodiscard]] PairRevealSecretsV1 owned_secrets() const noexcept
    {
        return secrets_;
    }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        Agree,
        DeriveI2r,
        DeriveR2i,
        WaitPeer,
        RandomLocal,
        SealLocal,
        OpenPeer,
        WaitLocalSent,
        Ready,
        Failed
    };

    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare(Stage stage);
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ParsedProviderEvent& payload, std::vector<std::uint8_t>& out) const;
    bool start_valid() const noexcept;
    bool local_is_initiator() const noexcept;

    ProviderOperationJournal operations_{1};
    PairRevealStartV1 start_{};
    PairRevealSecretsV1 secrets_{};
    Stage stage_ = Stage::Empty;
    std::uint64_t next_operation_id_ = 0;
    std::optional<PairRevealEffect> pending_{};
    std::optional<wire::PairRevealEnvelopeV1> peer_envelope_{};
    std::optional<wire::PairContributionV1> peer_contribution_{};
    std::optional<std::array<std::uint8_t, wire::kPairRevealBodySizeV1>>
        local_envelope_{};
    std::array<std::uint8_t, 12> local_nonce_{};
    std::array<std::uint8_t, 32> peer_logical_hash_{};
    bool local_sent_ = false;
    bool begun_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
