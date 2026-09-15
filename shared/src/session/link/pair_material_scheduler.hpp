#ifndef FLYNES_SESSION_LINK_PAIR_MATERIAL_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_PAIR_MATERIAL_SCHEDULER_HPP

#include "../ports/provider_operation_journal.hpp"
#include "../wire/pair_handshake.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class PairMaterialEffectKind : std::uint8_t
{
    GenerateKey,
    ReadPublicKey,
    CreateTlsMaterial,
    RandomBytes
};

struct PairMaterialStartV1 final
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    wire::PairContextV1 context{};
    wire::PairRoleV1 role{};
    std::array<std::uint8_t, 32> capability_summary_hash{};
};

struct PairMaterialEffect final
{
    fly_session_op_token_v2 token{};
    PairMaterialEffectKind kind{};
    std::uint32_t key_purpose = 0;
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t public_key_encoding = 0;
    std::uint32_t byte_count = 0;
    std::vector<std::uint8_t> exact_bytes;
};

struct PairLocalMaterialV1 final
{
    fly_session_resource_handle_v2 identity_key = 0;
    fly_session_resource_handle_v2 ecdh_key = 0;
    fly_session_resource_handle_v2 tls_key = 0;
    fly_session_resource_handle_v2 tls_material = 0;
    wire::PairContributionV1 contribution{};
    std::array<std::uint8_t, 152> commit_bytes{};
};

// Produces the local contribution only from exact, fenced public-provider
// terminals. One operation is active at a time, so a stale or reordered key,
// public-key, TLS, or random completion cannot populate another field.
class PairMaterialScheduler final
{
public:
    PairMaterialScheduler();

    bool begin(const PairMaterialStartV1& start);
    [[nodiscard]] std::optional<PairMaterialEffect> poll_effect() const;
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 cancel_pending() noexcept;

    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] std::optional<PairLocalMaterialV1> material() const;
    [[nodiscard]] PairLocalMaterialV1 owned_resources() const noexcept
    {
        return material_;
    }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        GenerateIdentity,
        GenerateEcdh,
        GenerateTls,
        ReadIdentityPublic,
        ReadEcdhPublic,
        ReadTlsSpki,
        CreateTlsMaterial,
        RandomNonce,
        Ready,
        Failed
    };

    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare(Stage stage);
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    fly_session_result_v2 read_buffer(
        const ParsedProviderEvent& payload, std::vector<std::uint8_t>& out) const;
    fly_session_result_v2 seal();
    static bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept;
    static bool trusted_generated_point(void*, const std::uint8_t point[65]);

    ProviderOperationJournal operations_{1};
    PairMaterialStartV1 start_{};
    Stage stage_ = Stage::Empty;
    std::optional<PairMaterialEffect> pending_{};
    PairLocalMaterialV1 material_{};
    std::array<std::uint8_t, 65> identity_public_{};
    std::array<std::uint8_t, 65> ecdh_public_{};
    std::array<std::uint8_t, 32> tls_spki_hash_{};
    std::vector<std::uint8_t> tls_spki_{};
    std::array<std::uint8_t, 32> contribution_nonce_{};
    bool begun_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
