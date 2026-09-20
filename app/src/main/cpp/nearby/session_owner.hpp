#pragma once

#include <flynes/flynes_session.h>
#include <flynes/product/dual_runtime_port.hpp>
#include "content_port.hpp"
#include "prepared_content.hpp"

#include <array>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace flynes::android::nearby {

struct OwnerSnapshot final
{
    std::uint32_t abi_version = 0;
    std::uint32_t link_state = 0;
    std::uint32_t game_state = 0;
    std::uint32_t pending_config_local_confirmed = 0;
    std::uint32_t pending_config_peer_confirmed = 0;
    std::uint8_t pending_config_id[32]{};
    std::uint64_t pending_config_revision = 0;
    std::uint64_t last_action_request_id = 0;
    fly_session_result_v2 last_action_result = FLY_SESSION_V2_OK;
    std::uint32_t last_action_outcome = 0;
    std::uint32_t shutdown_complete = 0;
    bool content_query_attempted = false;
    fly_session_result_v2 last_content_query_result = FLY_SESSION_V2_OK;
    char primary_reason_key[64]{};
};

class SessionOwner final
{
public:
    struct DiscoveryCallbacks final {
        std::function<fly_session_result_v2(bool, std::uint64_t, std::uint64_t)> start;
        std::function<fly_session_result_v2(std::uint64_t)> stop;
        std::function<void()> close;
    };
    static SessionOwner* create(ContentPort::Callbacks content = {}, DiscoveryCallbacks discovery = {});
    ~SessionOwner();

    SessionOwner(const SessionOwner&) = delete;
    SessionOwner& operator=(const SessionOwner&) = delete;

    bool read_snapshot(OwnerSnapshot* out);
    bool read_game_choices(std::vector<fly_session_game_choice_v2>* out);
    bool read_candidates(std::vector<fly_session_candidate_v2>* out);
    bool report_discovery_candidate(std::uint64_t generation, std::uint64_t candidate,
                                    std::int32_t rssi_bucket, std::uint64_t observed_ns);
    bool report_discovery_end(std::uint64_t generation, fly_session_result_v2 result);
    int v2_create_count() const noexcept { return v2_create_count_; }
    fly_session_result_v2 begin_content_preparation(const std::array<std::uint8_t, 16>&, std::uint64_t*);
    fly_session_result_v2 complete_content_preparation(std::uint64_t, const std::array<std::uint8_t, 16>&,
        const std::array<std::uint8_t, 32>&, std::shared_ptr<const std::vector<std::uint8_t>>);
    fly_session_result_v2 cancel_content_preparation(std::uint64_t);
    fly_session_result_v2 submit_action(std::uint32_t action_kind,
                                        const std::uint8_t* code,
                                        std::size_t code_size,
                                        std::uint64_t pending_config_revision = 0);
    const char* quic_provider_type() const noexcept;
    bool quic_ready() const noexcept;
    const char* quic_listen_address() const noexcept;
    fly_session_result_v2 quic_engine_listen();
    fly_session_result_v2 quic_engine_connect(SessionOwner* peer);
    fly_session_result_v2 quic_control_roundtrip(SessionOwner* connector,
                                                 std::int32_t* listen_result,
                                                 std::int32_t* connect_result,
                                                 std::int32_t* write_result,
                                                 std::uint64_t* bytes_written,
                                                 std::uint64_t* bytes_read);
    fly_session_result_v2 arm_test_timer(std::uint64_t timer_id,
                                         std::uint64_t delay_ms);
    fly_session_result_v2 cancel_test_timer(std::uint64_t timer_id);
    int test_timer_fires(std::uint64_t timer_id);
    bool wait_test_timer(std::uint64_t timer_id, std::uint64_t timeout_ms);

private:
    SessionOwner() = default;
    bool start();
    bool initialize_on_worker();
    void shutdown_on_worker();
    bool on_worker() const noexcept;
    fly_session_result_v2 call_worker(std::function<fly_session_result_v2()> action);
    void pump();
    void worker_loop();
    void wake();
    void publish_platform_ready();
    fly_session_result_v2 submit_action_on_worker(std::uint32_t, const std::uint8_t*, std::size_t,
        std::uint64_t, bool, fly_session_notice_v2*, std::uint64_t*);
    bool engine_snapshot(fly_session_snapshot_v2*);
    bool current_choice(const std::array<std::uint8_t, 16>&, fly_session_game_choice_v2*, fly_session_snapshot_v2*);
    void reconcile_preparation();
    fly_session_result_v2 quic_engine_open_stream(bool accept);
    fly_session_result_v2 quic_engine_inspect();
    fly_session_result_v2 quic_engine_write_control();
    fly_session_result_v2 quic_engine_grant_read();
    struct QuicFacts final {
        std::uint64_t connection = 0;
        std::uint64_t stream = 0;
        std::uint64_t written = 0;
        std::uint64_t read = 0;
        std::int32_t stream_result = 0;
        int pending = 0;
        bool inspected = false;
    };
    bool read_quic_facts(QuicFacts* out);

    static void retain(void*) {}
    static void release(void*) {}
    static fly_session_result_v2 read_clock(void* context,
                                            fly_session_clock_sample_v2* out);
    static fly_session_result_v2 post_task(void* context,
                                           fly_session_task_v2_t* task);
    static fly_session_result_v2 arm_timer(void* context, std::uint64_t deadline_ns,
                                           const std::uint8_t boot_generation[32],
                                           std::uint64_t timer_id,
                                           fly_session_task_v2_t* task);
    static fly_session_result_v2 cancel_timer(void* context, std::uint64_t timer_id);
    static fly_session_result_v2 watch_platform(void* context,
                                                const fly_session_op_token_v2* token,
                                                fly_session_inbox_v2_t* inbox);
    static fly_session_result_v2 stop_platform(void*, const fly_session_op_token_v2*);
    static fly_session_result_v2 discovery_advertise(
        void* context, const fly_session_op_token_v2* token,
        fly_session_bytes_v2, std::uint64_t, fly_session_inbox_v2_t* inbox);
    static fly_session_result_v2 discovery_scan(
        void*, const fly_session_op_token_v2*, fly_session_bytes_v2, std::uint64_t,
        fly_session_inbox_v2_t*);
    static fly_session_result_v2 discovery_stop(void*, const fly_session_op_token_v2*);
    static fly_session_result_v2 discovery_unavailable_connection(
        void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
        std::uint64_t, fly_session_inbox_v2_t*);
    static fly_session_result_v2 discovery_unavailable_write(
        void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
        std::uint32_t, fly_session_buffer_v2_t*, fly_session_inbox_v2_t*);
    static fly_session_result_v2 discovery_unavailable_subscribe(
        void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
        std::uint32_t, fly_session_inbox_v2_t*);

    fly_session_clock_port_v2 clock_{};
    fly_session_executor_port_v2 executor_{};
    fly_session_platform_state_port_v2 platform_{};
    fly_session_discovery_port_v2 discovery_{};
    DiscoveryCallbacks discovery_callbacks_;
    fly_session_quic_port_v2 quic_{};
    ContentPort::Callbacks content_callbacks_;
    std::unique_ptr<ContentPort> content_impl_;
    fly_session_content_port_v2 content_{};
    std::shared_ptr<PreparedContent> prepared_;
    fly_runtime_t* runtime_ = nullptr;
    std::unique_ptr<flynes::product::ProductDualRuntimePort> runtime_impl_;
    fly_session_dual_runtime_port_v2 runtime_port_{};
    fly_session_ports_v2 ports_{};
    fly_session_v2_t* engine_ = nullptr;
    std::array<std::uint8_t, 32> boot_generation_{};
    std::mutex mutex_;
    std::condition_variable cv_;
    bool stop_ = false;
    bool initialized_ = false;
    bool started_ = false;
    bool provider_work_ = false;
    std::thread worker_;
    std::thread::id worker_id_;
    struct Command final {
        std::function<fly_session_result_v2()> action;
        bool started = false;
        bool complete = false;
        fly_session_result_v2 result = FLY_SESSION_V2_CLOSED;
    };
    static constexpr std::size_t kCommandCapacity = 8;
    std::deque<std::shared_ptr<Command>> commands_;
    std::size_t pending_waiters_ = 0;
    // Published once, after worker initialization; getters never touch provider state.
    bool quic_ready_ = false;
    std::string quic_provider_type_;
    std::string quic_listen_address_;
    std::array<std::uint8_t, 32> quic_spki_hash_{};
    std::vector<fly_session_task_v2_t*> tasks_;
    struct ArmedTimer final
    {
        std::uint64_t deadline_ns = 0;
        std::array<std::uint8_t, 32> boot{};
        fly_session_task_v2_t* task = nullptr;
    };
    std::unordered_map<std::uint64_t, ArmedTimer> timers_;
    std::unordered_map<std::uint64_t, int> test_fires_;
    fly_session_op_token_v2 platform_token_{};
    fly_session_inbox_v2_t* platform_inbox_ = nullptr;
    fly_session_op_token_v2 discovery_token_{};
    fly_session_inbox_v2_t* discovery_inbox_ = nullptr;
    std::uint64_t discovery_event_sequence_ = 0;
    bool discovery_scan_active_ = false;
    bool discovery_advertise_active_ = false;
    int v2_create_count_ = 0;
    std::uint64_t next_request_id_ = 1;
    // Bounded diagnostics for the two notice kinds in the V2 contract.
    fly_session_notice_v2 last_action_result_{};
    bool shutdown_complete_ = false;
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    std::unique_ptr<class ProductQuicPort> quic_impl_;
#endif
};

} // namespace flynes::android::nearby
