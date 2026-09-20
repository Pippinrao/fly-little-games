#ifndef FLYNES_SESSION_LINK_SESSION_SIGNING_SCHEDULER_HPP
#define FLYNES_SESSION_LINK_SESSION_SIGNING_SCHEDULER_HPP

#include "../ports/provider_operation_journal.hpp"
#include "../wire/session_signing_binding.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace flynes::session {

enum class SessionSigningEffectKind : std::uint8_t
{
    GenerateKey,
    ReadPublicKey,
    SignBinding,
    PersistBinding,
    PersistBindingObject
};

struct SessionSigningStartV1 final
{
    std::array<std::uint8_t, 16> engine_instance_id{};
    std::array<std::uint8_t, 16> link_id{};
    std::uint64_t generation = 0;
    std::uint64_t first_operation_id = 0;
    std::array<std::uint8_t, 32> pair_transcript_hash{};
    std::array<std::uint8_t, 16> session_id{};
    wire::PairRoleV1 local_role{};
    fly_session_resource_handle_v2 identity_key = 0;
    std::array<std::uint8_t, 65> identity_public_key{};
};

struct SessionSigningEffect final
{
    fly_session_op_token_v2 token{};
    SessionSigningEffectKind kind{};
    std::uint32_t key_purpose = 0;
    fly_session_resource_handle_v2 resource = 0;
    std::uint32_t public_key_encoding = 0;
    std::vector<std::uint8_t> scope_binding{};
    std::vector<std::uint8_t> domain{};
    std::array<std::uint8_t, 32> digest{};
    std::vector<std::uint8_t> name_space{};
    std::vector<std::uint8_t> record_key{};
    std::vector<std::uint8_t> value{};
    std::uint64_t expected_revision = 0;
    std::uint32_t object_kind = 0;
    std::array<std::uint8_t, 32> expected_hash{};
};

struct SessionSigningMaterialV1 final
{
    fly_session_resource_handle_v2 key = 0;
    std::uint64_t record_revision = 0;
    fly_session_resource_handle_v2 binding_object_ref = 0;
    std::array<std::uint8_t, 65> public_key{};
    std::array<std::uint8_t, wire::kSessionSigningBindingSizeV1> binding{};
    std::array<std::uint8_t, 32> binding_hash{};
};

class SessionSigningScheduler final
{
public:
    bool begin(const SessionSigningStartV1& start);
    [[nodiscard]] std::optional<SessionSigningEffect> poll_effect() const;
    fly_session_result_v2 complete(const fly_session_port_event_v2& event);
    fly_session_result_v2 cancel_pending() noexcept;
    [[nodiscard]] bool ready() const noexcept { return ready_; }
    [[nodiscard]] bool failed() const noexcept { return failed_; }
    [[nodiscard]] const SessionSigningMaterialV1& material() const noexcept
    { return material_; }
    [[nodiscard]] std::uint64_t next_operation_id() const noexcept
    { return next_operation_id_; }

private:
    enum class Stage : std::uint8_t
    {
        Empty,
        GenerateKey,
        ReadPublicKey,
        SignBinding,
        PersistBinding,
        PersistBindingObject,
        Ready,
        Failed
    };
    fly_session_op_token_v2 token(std::uint64_t operation_id) const noexcept;
    fly_session_result_v2 prepare(Stage stage);
    fly_session_result_v2 read_buffer(
        const ParsedProviderEvent& event, std::vector<std::uint8_t>& out) const;
    fly_session_result_v2 reject(fly_session_result_v2 result) noexcept;
    static bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept;

    ProviderOperationJournal operations_{1};
    SessionSigningStartV1 start_{};
    std::optional<SessionSigningEffect> pending_{};
    SessionSigningMaterialV1 material_{};
    std::vector<std::uint8_t> durable_key_ref_{};
    std::array<std::uint8_t, 32> expected_public_hash_{};
    std::array<std::uint8_t,
               wire::kSessionSigningBindingPretagSizeV1> pretag_{};
    std::array<std::uint8_t, 32> binding_digest_{};
    std::uint64_t next_operation_id_ = 0;
    Stage stage_ = Stage::Empty;
    bool begun_ = false;
    bool ready_ = false;
    bool failed_ = false;
};

} // namespace flynes::session

#endif
