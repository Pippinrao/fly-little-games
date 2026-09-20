#ifndef FLYNES_SESSION_DUAL_DUAL_SESSION_CONTROLLER_HPP
#define FLYNES_SESSION_DUAL_DUAL_SESSION_CONTROLLER_HPP

/*
 * Task 10: the engine-owned DUAL session after CONNECTED_LOBBY.
 *
 * Catalog walk, SELECT_CONTENT, START_DUAL, DualRunScheduler and the State
 * Commit bidi stream live here. Tokens are minted by the engine; this object
 * only names the next provider effect. Digest acknowledgement and resume stay
 * fail-closed (Task 12).
 */

#include "canonical_input_wire.hpp"
#include "dual_run_scheduler.hpp"
#include "dual_runtime_adapter.hpp"

#include "../ports/provider_events.hpp"
#include "../recovery/link_activity_watchdog.hpp"
#include "../wire/pair_handshake.hpp"
#include "flynes/flynes_session.h"

#include <array>
#include <cstdint>
#include <deque>
#include <memory>
#include <optional>
#include <vector>

namespace flynes::session::dual {

struct DualStartInputsV1 final
{
    std::array<std::uint8_t, 16> session_id{};
    std::array<std::uint8_t, 16> branch_id{};
    wire::PairRoleV1 local_role = wire::PairRoleV1::Initiator;
    std::array<std::uint8_t, 65> local_signing_public{};
    std::array<std::uint8_t, 65> peer_signing_public{};
    fly_session_resource_handle_v2 quic_connection = 0;
    bool local_is_listener = false;
    const fly_session_dual_runtime_port_v2* runtime = nullptr;
};

class DualSessionController final
{
public:
    enum class EffectKind : std::uint8_t
    {
        QueryContent = 1,
        OpenStream = 2,
        GrantRead = 3,
        Write = 4
    };

    struct Effect final
    {
        EffectKind kind = EffectKind::QueryContent;
        std::uint32_t content_index = 0;
        bool accept = false;
        std::uint32_t opener_role = 0;
        fly_session_resource_handle_v2 connection = 0;
        fly_session_resource_handle_v2 stream = 0;
        std::vector<std::uint8_t> bytes{};
        std::uint64_t read_credit = 0;
        std::uint32_t expected_payload_kind = 0;
    };

    DualSessionController() = default;
    DualSessionController(const DualSessionController&) = delete;
    DualSessionController& operator=(const DualSessionController&) = delete;

    void begin_catalog() noexcept;
    fly_session_result_v2 on_content_empty() noexcept;
    fly_session_result_v2 on_content_choice(const ParsedProviderEvent& parsed)
        noexcept;
    fly_session_result_v2 ingest_content_choice_record(
        const std::uint8_t* bytes, std::size_t size,
        const std::array<std::uint8_t, 32>& record_hash) noexcept;
    void set_observed_local_seat(std::uint8_t seat) noexcept;

    fly_session_result_v2 select_content(const std::uint8_t choice_id[16])
        noexcept;
    fly_session_result_v2 confirm_local_pending() noexcept;
    fly_session_result_v2 apply_peer_pending_confirm(const std::uint8_t id[32],
                                                     std::uint64_t revision)
        noexcept;
    fly_session_result_v2 ingest_verified_pending_confirm(
        const std::uint8_t* bytes, std::size_t size) noexcept;
    fly_session_result_v2 ingest_verified_suspend(const std::uint8_t* bytes,
                                                  std::size_t size) noexcept;
    fly_session_result_v2 publish_imported_choice(
        const fly_session_game_choice_v2& choice) noexcept;
    fly_session_result_v2 start_dual(const DualStartInputsV1& inputs) noexcept;
    fly_session_result_v2 read_start_ref(
        const DualStartInputsV1& inputs,
        fly_session_dual_start_ref_v2& out_ref) const noexcept;
    fly_session_result_v2 submit_local(const fly_session_input_v2& input)
        noexcept;
    fly_session_result_v2 pause() noexcept;
    fly_session_result_v2 disconnect() noexcept;
    void on_clock(std::uint64_t continuous_ns) noexcept;

    [[nodiscard]] std::optional<Effect> poll_effect();
    fly_session_result_v2 complete(EffectKind kind,
                                   const fly_session_port_event_v2& event,
                                   const ParsedProviderEvent& parsed) noexcept;

    [[nodiscard]] bool catalog_complete() const noexcept
    {
        return catalog_complete_;
    }
    [[nodiscard]] bool catalog_started() const noexcept
    {
        return catalog_started_;
    }
    [[nodiscard]] bool has_selection() const noexcept { return selected_; }
    [[nodiscard]] bool pending_start_conditions_bound() const noexcept
    {
        return pending_start_bound_;
    }
    [[nodiscard]] bool local_pending_confirmed() const noexcept
    {
        return pending_config_local_confirmed_ != 0;
    }
    [[nodiscard]] bool peer_pending_confirmed() const noexcept
    {
        return pending_config_peer_confirmed_ != 0;
    }
    [[nodiscard]] bool runtime_ready() const noexcept
    {
        return local_runtime_ready_;
    }
    [[nodiscard]] bool running() const noexcept
    {
        return running_ && !frozen_ && !paused_;
    }
    [[nodiscard]] bool frozen() const noexcept { return frozen_ || paused_; }
    [[nodiscard]] bool view_dirty() const noexcept { return view_dirty_; }
    void clear_view_dirty() noexcept { view_dirty_ = false; }

    [[nodiscard]] std::uint32_t game_state() const noexcept;
    [[nodiscard]] std::uint32_t freeze_reason() const noexcept;
    [[nodiscard]] const std::vector<fly_session_game_choice_v2>& game_choices()
        const noexcept
    {
        return choices_;
    }

    void fill_snapshot(fly_session_snapshot_v2& snapshot) const noexcept;
    void fill_scope(fly_session_scope_v2& scope) const noexcept;

    void cancel() noexcept;

private:
    fly_session_result_v2 parse_choice_record(
        const std::uint8_t* bytes, std::size_t size,
        const std::array<std::uint8_t, 32>& expected_hash) noexcept;
    fly_session_result_v2 queue_local_bundle(
        const DualPortInputArrayV1& samples) noexcept;
    fly_session_result_v2 queue_runtime_ready_beacon() noexcept;
    void maybe_enter_running() noexcept;
    fly_session_result_v2 ingest_remote_bytes(const std::uint8_t* bytes,
                                              std::size_t size) noexcept;
    fly_session_result_v2 try_step() noexcept;
    void mark_frozen(std::uint32_t reason) noexcept;
    void bind_pending_config(const fly_session_game_choice_v2& choice) noexcept;
    std::uint64_t revision_for_bind(
        const std::array<std::uint8_t, 32>& id) noexcept;
    static std::array<std::uint8_t, 32> owner_key(
        const std::array<std::uint8_t, 65>& public_key) noexcept;

    std::vector<fly_session_game_choice_v2> choices_{};
    std::uint32_t catalog_index_ = 0;
    bool catalog_started_ = false;
    bool catalog_complete_ = false;
    bool catalog_query_outstanding_ = false;
    bool selected_ = false;
    std::array<std::uint8_t, 16> selected_ref_{};
    std::array<std::uint8_t, 32> selected_content_{};
    std::array<std::uint8_t, 32> pending_config_id_{};
    std::uint64_t pending_config_revision_ = 0;
    std::uint32_t pending_config_local_confirmed_ = 0;
    std::uint32_t pending_config_peer_confirmed_ = 0;
    bool pending_start_bound_ = false;
    struct BindRevision final
    {
        std::array<std::uint8_t, 32> id{};
        std::uint64_t count = 0;
    };
    static constexpr std::uint8_t kMaxBindRevisions = 16;
    BindRevision bind_revisions_[kMaxBindRevisions]{};
    std::uint8_t bind_revision_used_ = 0;

    std::unique_ptr<CAbiDualRuntimePortV1> adapter_;
    std::unique_ptr<DualRunSchedulerV1> scheduler_;
    DualInputKeyV1 context_{};
    DualContentRefV1 content_{};
    std::array<DualOwnerKeyV1, kDualPortCountV1> owners_{};
    std::uint8_t local_seat_ = 0;
    std::uint64_t local_sequence_ = 1;
    std::uint64_t batch_sequence_ = 1;
    DualStateDigestV1 last_digest_{};

    fly_session_resource_handle_v2 connection_ = 0;
    fly_session_resource_handle_v2 send_stream_ = 0;
    fly_session_resource_handle_v2 receive_stream_ = 0;
    bool local_is_listener_ = false;
    bool want_stream_ = false;
    bool stream_open_ = false;
    bool open_outstanding_ = false;
    bool write_outstanding_ = false;
    bool read_outstanding_ = false;
    bool want_read_ = false;
    std::deque<std::vector<std::uint8_t>> write_queue_{};
    std::vector<std::uint8_t> read_accumulator_{};

    bool local_runtime_ready_ = false;
    bool peer_runtime_ready_ = false;
    bool running_ = false;
    bool paused_ = false;
    bool frozen_ = false;
    std::uint32_t freeze_reason_ = FLY_SESSION_DUAL_FREEZE_NONE_V2;
    bool view_dirty_ = false;
    flynes::session::recovery::LinkActivityWatchdogV1 watchdog_{};
    std::uint64_t last_clock_ns_ = 0;
};

} // namespace flynes::session::dual

#endif
