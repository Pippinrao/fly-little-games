#ifndef FLYNES_SESSION_ENGINE_SESSION_ENGINE_HPP
#define FLYNES_SESSION_ENGINE_SESSION_ENGINE_HPP

#include "../ports/session_ports.hpp"
#include "../link/pair_material_scheduler.hpp"
#include "../link/pair_reveal_scheduler.hpp"
#include "../link/pair_signature_scheduler.hpp"
#include "../link/pair_sas_scheduler.hpp"
#include "../link/pair_key_confirm_scheduler.hpp"
#include "../link/pair_known_envelope_queue.hpp"
#include "../link/pair_known_scheduler.hpp"
#include "../link/pair_capability_scheduler.hpp"
#include "../link/initial_plan_scheduler.hpp"
#include "../link/initial_bearer_scheduler.hpp"
#include "../link/endpoint_offer_scheduler.hpp"
#include "../link/initial_quic_bind_scheduler.hpp"
#include "../link/session_signing_scheduler.hpp"
#include "../link/link_handshake_scheduler.hpp"
#include "../dual/dual_session_controller.hpp"
#include "../content/content_transfer_controller.hpp"
#include "../view/session_view.hpp"
#include "../wire/gatt_fragment.hpp"
#include "../wire/pair_handshake.hpp"

#include <cstdint>
#include <array>
#include <deque>
#include <atomic>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>
#include <vector>

namespace flynes::session {

class SessionEngine final : public std::enable_shared_from_this<SessionEngine>
{
public:
    SessionEngine(const fly_session_config_v2& config,
                  const fly_session_ports_v2& ports);
    ~SessionEngine();

    SessionEngine(const SessionEngine&) = delete;
    SessionEngine& operator=(const SessionEngine&) = delete;

    fly_session_result_v2 start();
    fly_session_result_v2 submit_action(const fly_session_action_v2& action);
    fly_session_result_v2 submit_input(const fly_session_input_v2& input);
    fly_session_result_v2 deliver(const fly_session_port_event_v2& event);
    fly_session_result_v2 read_notice(fly_session_notice_v2& out_notice);
    fly_session_result_v2 acquire_view(fly_session_view_v2_t** out_view);
    fly_session_result_v2 begin_shutdown(std::uint64_t request_id);
    bool can_destroy() const noexcept;
    void detach_handle() noexcept;
    void run_work() noexcept;

private:
    /*
     * Every scheduler receives a contiguous block of operation ids that is at
     * least as large as the number of provider operations it can issue, and the
     * engine mark moves one past the END of the block before the scheduler
     * mints anything. A block is reserved, never shared: see the invariant
     * comment on reserve_operation_ids_locked() in session_engine.cpp.
     */
    static constexpr std::uint64_t kSchedulerOperationBlockV1 = 16;

    void complete_shutdown_locked() noexcept;
    void dispatch_retiring_quic_close() noexcept;
    void publish_link_view_locked(std::uint32_t link_state);
    fly_session_op_token_v2 make_link_operation_token_locked();
    /*
     * Reserves `block` consecutive ids for one scheduler and returns the first
     * of them, or 0 when the id space is exhausted. This is the only way a
     * scheduler is given its first_operation_id, so the engine mark is always
     * past every block a live scheduler can still draw from.
     */
    std::uint64_t reserve_operation_ids_locked(std::uint64_t block) noexcept;
    bool accept_completed_gatt_locked(
        const std::vector<std::uint8_t>& logical) noexcept;
    bool queue_gatt_ack_locked(
        const wire::GattCompletedMetadata& completed) noexcept;
    void cancel_pair_material_locked() noexcept;
    void release_pair_material_locked() noexcept;
    void cancel_pair_reveal_locked() noexcept;
    void release_pair_reveal_locked() noexcept;
    void cancel_pair_signature_locked() noexcept;
    void release_pair_signature_locked() noexcept;
    void cancel_pair_known_locked() noexcept;
    void cancel_pair_sas_locked() noexcept;
    void release_pair_sas_locked() noexcept;
    void cancel_pair_key_confirm_locked() noexcept;
    void cancel_pair_capability_locked() noexcept;
    void cancel_initial_plan_locked() noexcept;
    void cancel_initial_bearer_locked() noexcept;
    void cancel_endpoint_offer_locked() noexcept;
    void cancel_initial_quic_bind_locked() noexcept;
    void cancel_session_signing_locked() noexcept;
    void cancel_link_handshake_locked() noexcept;
    bool start_pair_reveal_locked() noexcept;
    bool start_pair_signature_locked() noexcept;
    bool start_pair_known_locked() noexcept;
    bool drain_pair_known_envelopes_locked() noexcept;
    bool buffer_pair_known_envelope_locked(
        std::uint8_t type, const std::uint8_t* body, std::size_t size,
        const std::array<std::uint8_t, 32>& logical_hash);
    bool start_pair_sas_locked() noexcept;
    bool start_pair_key_confirm_locked() noexcept;
    bool start_pair_capability_locked() noexcept;
    bool queue_local_pair_commit_locked() noexcept;
    bool queue_local_pair_reveal_locked() noexcept;
    bool queue_local_pair_signature_locked() noexcept;
    bool queue_local_pair_known_locked() noexcept;
    bool queue_local_pair_key_confirm_locked() noexcept;
    bool queue_local_pair_capability_locked() noexcept;
    bool queue_local_initial_plan_locked() noexcept;
    bool finish_pair_capability_locked() noexcept;
    bool start_initial_plan_locked() noexcept;
    bool start_initial_bearer_locked() noexcept;
    bool queue_local_initial_bearer_locked() noexcept;
    bool start_endpoint_offer_locked() noexcept;
    bool queue_local_endpoint_offer_locked() noexcept;
    bool start_initial_quic_bind_locked() noexcept;
    bool start_session_signing_locked() noexcept;
    bool start_link_handshake_locked() noexcept;
    void ensure_dual_controller_locked() noexcept;
    void ensure_content_controller_locked() noexcept;
    void apply_dual_projection_locked(fly_session_view_v2_t* view) noexcept;
    void cancel_dual_locked() noexcept;
    void cancel_content_locked() noexcept;

    struct PendingAction final
    {
        std::uint64_t request_id = 0;
        std::uint64_t expected_view_revision = 0;
        fly_session_approval_token_v2_t* token = nullptr;
        std::uint32_t choice_size = 0;
        fly_session_action_choice_v2 choice{};
    };

    struct RequestRecord final
    {
        std::uint64_t request_id = 0;
        fly_session_approval_token_v2_t* token = nullptr;
        std::uint32_t choice_size = 0;
        fly_session_action_choice_v2 choice{};
    };

    struct CompletionRecord final
    {
        fly_session_port_event_v2 event{};
    };

    struct PendingPortEvent final
    {
        fly_session_port_event_v2 event{};
        fly_session_buffer_v2_t* retained_buffer = nullptr;

        PendingPortEvent() noexcept = default;
        PendingPortEvent(const fly_session_port_event_v2& value,
                         fly_session_buffer_v2_t* buffer) noexcept
            : event(value), retained_buffer(buffer)
        {
            fly_session_buffer_retain_v2(retained_buffer);
        }
        ~PendingPortEvent()
        {
            fly_session_buffer_release_v2(retained_buffer);
        }
        PendingPortEvent(const PendingPortEvent&) = delete;
        PendingPortEvent& operator=(const PendingPortEvent&) = delete;
        PendingPortEvent(PendingPortEvent&& other) noexcept
            : event(other.event),
              retained_buffer(std::exchange(other.retained_buffer, nullptr))
        {
        }
        PendingPortEvent& operator=(PendingPortEvent&& other) noexcept
        {
            if (this == &other) return *this;
            fly_session_buffer_release_v2(retained_buffer);
            event = other.event;
            retained_buffer = std::exchange(other.retained_buffer, nullptr);
            return *this;
        }
    };

    SessionPorts ports_;
    std::shared_ptr<AuthorizationState> authorization_;
    mutable std::mutex mutex_;
    fly_session_view_v2_t* current_view_ = nullptr;
    fly_session_view_v2_t* shutdown_complete_view_ = nullptr;
    std::vector<PendingAction> pending_actions_;
    std::vector<fly_session_notice_v2> notices_;
    std::vector<RequestRecord> request_records_;
    std::vector<PendingPortEvent> pending_events_;
    std::vector<CompletionRecord> completion_records_;
    std::vector<fly_session_candidate_v2> candidates_;
    std::uint32_t action_capacity_ = 0;
    std::uint32_t notice_capacity_ = 0;
    std::uint32_t request_record_capacity_ = 0;
    std::uint32_t reserved_results_ = 0;
    std::uint64_t next_notice_sequence_ = 1;
    bool worker_scheduled_ = false;
    bool shutdown_requested_ = false;
    bool shutdown_complete_ = false;
    bool handle_detached_ = false;
    bool platform_watch_terminal_ = false;
    fly_session_inbox_v2_t* inbox_ = nullptr;
    fly_session_op_token_v2 platform_watch_token_{};
    fly_session_op_token_v2 discovery_token_{};
    fly_session_op_token_v2 gatt_subscription_token_{};
    fly_session_op_token_v2 discovery_disconnect_token_{};
    fly_session_op_token_v2 pair_context_random_token_{};
    fly_session_op_token_v2 bearer_probe_token_{};
    fly_session_op_token_v2 pair_material_token_{};
    fly_session_op_token_v2 pair_reveal_token_{};
    fly_session_op_token_v2 pair_signature_token_{};
    fly_session_op_token_v2 pair_known_token_{};
    fly_session_op_token_v2 pair_sas_token_{};
    fly_session_op_token_v2 pair_key_confirm_token_{};
    fly_session_op_token_v2 pair_capability_token_{};
    fly_session_op_token_v2 initial_plan_token_{};
    fly_session_op_token_v2 initial_bearer_token_{};
    fly_session_op_token_v2 endpoint_offer_token_{};
    fly_session_op_token_v2 initial_quic_bind_token_{};
    struct QuicCloseDebt final
    {
        fly_session_op_token_v2 token{};
        fly_session_resource_handle_v2 connection = 0;
        bool dispatch_pending = true;
        bool active = false;
    };
    std::optional<QuicCloseDebt> quic_close_debt_{};
    fly_session_op_token_v2 session_signing_token_{};
    fly_session_op_token_v2 link_handshake_token_{};
    fly_session_op_token_v2 gatt_write_token_{};
    /*
     * The engine-wide operation-id high-water mark. It is only ever advanced:
     * any id below it has already been handed to a consumer, either directly
     * by make_link_operation_token_locked() or inside a scheduler block reserved
     * by reserve_operation_ids_locked().
     */
    std::uint64_t available_operation_id_ = 2;
    std::uint64_t link_generation_ = 1;
    bool discovery_active_ = false;
    bool gatt_subscribe_pending_ = false;
    bool discovery_disconnect_pending_ = false;
    bool gatt_subscription_active_ = false;
    bool discovery_disconnect_active_ = false;
    bool pair_context_random_pending_ = false;
    bool pair_context_random_active_ = false;
    bool bearer_probe_pending_ = false;
    bool bearer_probe_active_ = false;
    bool pair_material_dispatch_pending_ = false;
    bool pair_material_active_ = false;
    std::uint32_t pair_material_expected_kind_ = 0;
    bool pair_reveal_dispatch_pending_ = false;
    bool pair_reveal_active_ = false;
    std::uint32_t pair_reveal_expected_kind_ = 0;
    bool pair_signature_dispatch_pending_ = false;
    bool pair_signature_active_ = false;
    std::uint32_t pair_signature_expected_kind_ = 0;
    bool pair_known_dispatch_pending_ = false;
    bool pair_known_active_ = false;
    std::uint32_t pair_known_expected_kind_ = 0;
    bool pair_sas_dispatch_pending_ = false;
    bool pair_sas_active_ = false;
    std::uint32_t pair_sas_expected_kind_ = 0;
    bool pair_key_confirm_dispatch_pending_ = false;
    bool pair_key_confirm_active_ = false;
    std::uint32_t pair_key_confirm_expected_kind_ = 0;
    bool pair_capability_dispatch_pending_ = false;
    bool pair_capability_active_ = false;
    std::uint32_t pair_capability_expected_kind_ = 0;
    bool initial_plan_dispatch_pending_ = false;
    bool initial_plan_active_ = false;
    std::uint32_t initial_plan_expected_kind_ = 0;
    bool initial_bearer_dispatch_pending_ = false;
    bool initial_bearer_active_ = false;
    std::uint32_t initial_bearer_expected_kind_ = 0;
    bool endpoint_offer_dispatch_pending_ = false;
    bool endpoint_offer_active_ = false;
    std::uint32_t endpoint_offer_expected_kind_ = 0;
    bool initial_quic_bind_dispatch_pending_ = false;
    bool initial_quic_bind_active_ = false;
    std::uint32_t initial_quic_bind_expected_kind_ = 0;
    bool session_signing_dispatch_pending_ = false;
    bool session_signing_active_ = false;
    std::uint32_t session_signing_expected_kind_ = 0;
    bool link_handshake_dispatch_pending_ = false;
    bool link_handshake_active_ = false;
    std::uint32_t link_handshake_expected_kind_ = 0;
    bool gatt_write_pending_ = false;
    bool gatt_write_active_ = false;
    fly_session_resource_handle_v2 discovery_connection_ = 0;
    std::uint32_t discovery_physical_role_ = 0;
    std::size_t discovery_att_value_cap_ = 20;
    std::uint16_t next_gatt_message_id_ = 1;
    std::size_t gatt_write_index_ = 0;
    std::uint8_t gatt_write_logical_type_ = 0;
    std::vector<std::vector<std::uint8_t>> gatt_write_fragments_;
    std::vector<std::vector<std::uint8_t>> deferred_gatt_write_fragments_;
    std::uint8_t deferred_gatt_write_logical_type_ = 0;
    std::deque<wire::GattCompletedMetadata> pending_gatt_acks_;
    std::unique_ptr<wire::GattReassembler> gatt_reassembler_;
    wire::PairRoleV1 local_pair_role_{};
    std::optional<wire::PairContextV1> pair_context_{};
    std::unique_ptr<PairMaterialScheduler> pair_material_{};
    std::unique_ptr<PairRevealScheduler> pair_reveal_{};
    std::unique_ptr<PairSignatureScheduler> pair_signature_{};
    std::unique_ptr<PairKnownScheduler> pair_known_{};
    // A peer can legitimately send at most one known status and one known
    // branch before this side is ready, so four records of 1 KiB total is two
    // full exchanges of headroom before the link fails closed.
    link::PairKnownEnvelopeQueue pending_pair_known_envelopes_{1024, 4};
    std::unique_ptr<PairSasScheduler> pair_sas_{};
    std::unique_ptr<PairKeyConfirmScheduler> pair_key_confirm_{};
    std::unique_ptr<PairCapabilityScheduler> pair_capability_{};
    std::unique_ptr<InitialPlanScheduler> initial_plan_{};
    std::unique_ptr<InitialBearerScheduler> initial_bearer_{};
    std::unique_ptr<EndpointOfferScheduler> endpoint_offer_{};
    std::unique_ptr<InitialQuicBindScheduler> initial_quic_bind_{};
    std::unique_ptr<SessionSigningScheduler> session_signing_{};
    std::optional<LinkHandshakeScheduler> link_handshake_{};
    std::unique_ptr<dual::DualSessionController> dual_{};
    fly_session_op_token_v2 dual_token_{};
    std::uint32_t dual_expected_kind_ = 0;
    dual::DualSessionController::EffectKind dual_effect_kind_{};
    bool dual_active_ = false;
    bool dual_dispatch_pending_ = false;
    std::unique_ptr<content::ContentTransferControllerV1> content_xfer_{};
    fly_session_op_token_v2 content_token_{};
    std::uint32_t content_expected_kind_ = 0;
    content::ContentTransferControllerV1::EffectKind content_effect_kind_{};
    bool content_active_ = false;
    bool content_dispatch_pending_ = false;
    std::optional<CapabilitySummary> local_pair_capability_{};
    std::array<std::uint8_t, 32> pending_local_capability_hash_{};
    std::array<std::uint8_t, 32> pending_local_known_hash_{};
    std::optional<VerifiedPairEvidence> verified_pair_evidence_{};
    std::optional<wire::BearerPlanBytes> selected_pair_plan_{};
    std::vector<std::uint8_t> pending_peer_key_confirm_{};
    std::array<std::uint8_t, 32> pending_peer_key_confirm_hash_{};
    std::unique_ptr<wire::PairExchangeV1> pair_exchange_{};
};

} // namespace flynes::session

struct fly_session_task_v2_handle
{
    std::shared_ptr<flynes::session::SessionEngine> engine;
};

struct fly_session_inbox_v2_handle
{
    std::atomic<std::uint32_t> references{1};
    std::weak_ptr<flynes::session::SessionEngine> engine;
    std::atomic<bool> closed{false};
};

#endif
