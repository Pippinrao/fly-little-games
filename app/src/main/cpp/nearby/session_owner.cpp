#include "session_owner.hpp"
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
#include "product_quic_port.hpp"
#endif

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <memory>
#include <new>
#include <thread>
#include <vector>

namespace flynes::android::nearby {
#ifdef FLYNES_SESSION_OWNER_PROJECTION_TEST
// Test binary only: acquires an owned reference to a real engine's immutable view.
fly_session_result_v2 acquire_owner_projection_test_view(fly_session_view_v2_t**);
#endif
#ifdef FLYNES_SESSION_OWNER_TEST_OBSERVER
// Defined by the host test only; absent from all product builds.
void observe_session_owner(const SessionOwner*, const char*);
void observe_prepared_content(const SessionOwner*, PreparedContent*);
#define OWNER_OBSERVE(event) observe_session_owner(this, event)
#else
#define OWNER_OBSERVE(event) ((void)0)
#endif
namespace {

std::uint64_t monotonic_ns()
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

SessionOwner* SessionOwner::create(ContentPort::Callbacks content)
{
    auto* owner = new (std::nothrow) SessionOwner();
    if (owner == nullptr)
        return nullptr;
    owner->content_callbacks_ = std::move(content);
    if (!owner->start())
    {
        delete owner;
        return nullptr;
    }
    return owner;
}

bool SessionOwner::start()
{
    worker_ = std::thread([this] {
        worker_id_ = std::this_thread::get_id();
        bool success = false;
        try { success = initialize_on_worker(); }
        catch (...) { success = false; }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            started_ = success;
            initialized_ = true;
        }
        cv_.notify_all();
        if (success) worker_loop();
        shutdown_on_worker();
    });
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return initialized_; });
    return started_;
}

fly_session_result_v2 SessionOwner::begin_content_preparation(const std::array<std::uint8_t, 16>& source, std::uint64_t* ticket) {
    if (!ticket) return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (!on_worker()) return call_worker([this, source, ticket] { return begin_content_preparation(source, ticket); });
    *ticket = 0;
    if (!engine_ || !prepared_) return FLY_SESSION_V2_CLOSED;
    prepared_->cancel(); pump();
    fly_session_game_choice_v2 choice{}; fly_session_snapshot_v2 snap{};
    if (!current_choice(source, &choice, &snap)) return FLY_SESSION_V2_STALE;
    return prepared_->begin(snap, choice, ticket);
}
fly_session_result_v2 SessionOwner::complete_content_preparation(std::uint64_t ticket, const std::array<std::uint8_t, 16>& source,
    const std::array<std::uint8_t, 32>& hash, std::shared_ptr<const std::vector<std::uint8_t>> bytes) {
    if (!on_worker()) return call_worker([this, ticket, source, hash, bytes] {
        return complete_content_preparation(ticket, source, hash, bytes);
    });
    if (!engine_ || !prepared_) return FLY_SESSION_V2_CLOSED;
    pump();
    fly_session_game_choice_v2 choice{}; fly_session_snapshot_v2 snap{};
    if (!current_choice(source, &choice, &snap) || std::memcmp(choice.content_id, hash.data(), 32)) {
        prepared_->cancel(ticket); return FLY_SESSION_V2_STALE;
    }
    auto result = prepared_->stage(ticket, source.data(), hash.data(), std::move(bytes), snap);
    if (result != FLY_SESSION_V2_OK) return result;
    fly_session_notice_v2 receipt{}; std::uint64_t request = 0;
    result = submit_action_on_worker(FLY_SESSION_ACTION_SELECT_CONTENT_V2, source.data(), source.size(), 0, true, &receipt, &request);
    if (!engine_snapshot(&snap)) { prepared_->cancel(ticket); return FLY_SESSION_V2_UNAVAILABLE; }
    // Missing receipt does not claim cancellation of admitted metadata SELECT;
    // it clears preparation so no later CONFIRM/START can use that selection.
    return prepared_->finish_selection(result, request, receipt, snap);
}
fly_session_result_v2 SessionOwner::cancel_content_preparation(std::uint64_t ticket) {
    // Revocation must not compete for the bounded engine command queue. Retain
    // only independent metadata under the owner mutex; never hold it in cancel.
    // As for every C++ owner entry, callers must own its lifetime at admission
    // (the Java API holds a handle lease); the retained state then outlives us.
    std::shared_ptr<PreparedContent> prepared;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prepared = prepared_;
    }
    if (!prepared) return FLY_SESSION_V2_CLOSED;
    prepared->cancel(ticket); return FLY_SESSION_V2_OK;
}

bool SessionOwner::engine_snapshot(fly_session_snapshot_v2* snap) {
    if (!engine_ || !snap) return false;
    fly_session_view_v2_t* view = nullptr;
    if (fly_session_acquire_view_v2(engine_, &view) != FLY_SESSION_V2_OK) return false;
    snap->struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE; snap->abi_version = 2;
    const auto result = fly_session_view_read_v2(view, snap); fly_session_view_release_v2(view);
    return result == FLY_SESSION_V2_OK;
}
bool SessionOwner::current_choice(const std::array<std::uint8_t, 16>& source,
    fly_session_game_choice_v2* choice, fly_session_snapshot_v2* snap) {
    fly_session_view_v2_t* view = nullptr;
    if (!engine_ || fly_session_acquire_view_v2(engine_, &view) != FLY_SESSION_V2_OK) return false;
    const std::unique_ptr<fly_session_view_v2_t, decltype(&fly_session_view_release_v2)> retained(view, fly_session_view_release_v2);
    snap->struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE; snap->abi_version = 2;
    bool found = false;
    if (fly_session_view_read_v2(view, snap) == FLY_SESSION_V2_OK && snap->game_choice_count <= 4096) {
        std::vector<fly_session_game_choice_v2> choices(snap->game_choice_count);
        if (!choices.empty()) { choices.front().struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE; choices.front().abi_version = 2; }
        std::uint32_t count = 0;
        if (fly_session_view_copy_game_choices_v2(view, 0, choices.data(), static_cast<std::uint32_t>(choices.size()), &count) == FLY_SESSION_V2_OK)
            for (std::uint32_t i = 0; i < count; ++i)
                if (choices[i].selectable && !std::memcmp(choices[i].source_choice_ref, source.data(), source.size())) {
                    *choice = choices[i]; found = true; break;
                }
    }
    return found;
}
void SessionOwner::reconcile_preparation() {
    if (!prepared_) return;
    fly_session_snapshot_v2 snap{};
    if (engine_snapshot(&snap)) prepared_->reconcile(snap); else prepared_->cancel();
}

bool SessionOwner::on_worker() const noexcept
{
    return std::this_thread::get_id() == worker_id_;
}

fly_session_result_v2 SessionOwner::call_worker(
    std::function<fly_session_result_v2()> action)
{
    if (on_worker()) return action();
    auto command = std::make_shared<Command>();
    command->action = std::move(action);
    std::unique_lock<std::mutex> lock(mutex_);
    if (stop_ || !started_) return FLY_SESSION_V2_CLOSED;
    if (commands_.size() >= kCommandCapacity) return FLY_SESSION_V2_BACKPRESSURE;
    commands_.push_back(command);
    ++pending_waiters_;
    cv_.notify_all();
    lock.unlock();
    OWNER_OBSERVE("queued");
    lock.lock();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(250);
    if (!cv_.wait_until(lock, deadline, [&] { return command->complete; }))
    {
        // Starting and cancelling use the same queue lock. A timed-out command
        // is removed before returning, so it cannot produce a late side effect.
        if (!command->started)
        {
            const auto found = std::find(commands_.begin(), commands_.end(), command);
            if (found != commands_.end()) commands_.erase(found);
            command->complete = true;
            command->result = FLY_SESSION_V2_TIMEOUT;
        }
        else
        {
            cv_.wait(lock, [&] { return command->complete; });
        }
    }
    const auto result = command->result;
    --pending_waiters_;
    cv_.notify_all();
    return result;
}

bool SessionOwner::initialize_on_worker()
{
    OWNER_OBSERVE("create");
    boot_generation_[0] = 1;
    clock_.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE;
    clock_.abi_version = FLY_SESSION_ABI_VERSION_2;
    clock_.context = this;
    clock_.retain = retain;
    clock_.release = release;
    clock_.read_continuous = read_clock;

    executor_.struct_size = FLY_SESSION_EXECUTOR_PORT_V2_SIZE;
    executor_.abi_version = FLY_SESSION_ABI_VERSION_2;
    executor_.context = this;
    executor_.retain = retain;
    executor_.release = release;
    executor_.post = post_task;
    executor_.arm_timer = arm_timer;
    executor_.cancel_timer = cancel_timer;

    platform_.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE;
    platform_.abi_version = FLY_SESSION_ABI_VERSION_2;
    platform_.context = this;
    platform_.retain = retain;
    platform_.release = release;
    platform_.watch = watch_platform;
    platform_.stop = stop_platform;

    /* Labeled emulator discovery: advertise is a local invite slot, not BLE.
     * scan/connect/GATT stay UNAVAILABLE so join cannot fake a wireless peer. */
    discovery_.struct_size = FLY_SESSION_DISCOVERY_PORT_V2_SIZE;
    discovery_.abi_version = FLY_SESSION_ABI_VERSION_2;
    discovery_.context = this;
    discovery_.retain = retain;
    discovery_.release = release;
    discovery_.scan = discovery_scan;
    discovery_.advertise = discovery_advertise;
    discovery_.stop = discovery_stop;
    discovery_.connect = discovery_unavailable_connection;
    discovery_.disconnect = discovery_unavailable_connection;
    discovery_.write = discovery_unavailable_write;
    discovery_.indicate = discovery_unavailable_write;
    discovery_.subscribe = discovery_unavailable_subscribe;

    ports_.struct_size = FLY_SESSION_PORTS_V2_SIZE;
    ports_.abi_version = FLY_SESSION_ABI_VERSION_2;
    ports_.clock = &clock_;
    ports_.executor = &executor_;
    ports_.platform_state = &platform_;
    ports_.discovery = &discovery_;
    content_impl_ = std::make_unique<ContentPort>(std::move(content_callbacks_));
    content_ = content_impl_->port();
    ports_.content = &content_;
    prepared_ = std::make_shared<PreparedContent>(content_);
#ifdef FLYNES_SESSION_OWNER_TEST_OBSERVER
    observe_prepared_content(this, prepared_.get());
#endif
    OWNER_OBSERVE("prepared_registered");
    fly_runtime_config runtime_config{};
    runtime_config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
    runtime_config.version = FLY_RUNTIME_CONFIG_VERSION_1;
    runtime_config.sample_rate = 48000;
    if (fly_runtime_create(&runtime_config, &runtime_) != FLY_RESULT_OK || !runtime_)
        return false;
    OWNER_OBSERVE("runtime_create");
    runtime_impl_ = std::make_unique<flynes::product::ProductDualRuntimePort>(
        runtime_, [prepared = prepared_](const fly_session_dual_content_ref_v2& reference) {
            return prepared->resolve(reference);
        });
    runtime_port_ = runtime_impl_->port();
    ports_.dual_runtime = &runtime_port_;
    OWNER_OBSERVE("runtime_registered");
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    quic_impl_ = std::make_unique<ProductQuicPort>();
    if (quic_impl_ && quic_impl_->ready())
    {
        quic_ = quic_impl_->port();
        ports_.quic = &quic_;
        quic_ready_ = true;
        quic_provider_type_ = quic_impl_->provider_type();
        quic_listen_address_ = quic_impl_->listen_address();
        quic_spki_hash_ = quic_impl_->der_spki_hash();
    }
#endif

    fly_session_config_v2 config{};
    config.struct_size = FLY_SESSION_CONFIG_V2_SIZE;
    config.abi_version = FLY_SESSION_ABI_VERSION_2;
    config.action_queue_capacity = 8;
    config.notice_queue_capacity = 8;
    if (fly_session_create_v2(&config, &ports_, &engine_) != FLY_SESSION_V2_OK ||
        engine_ == nullptr)
        return false;
    v2_create_count_ = 1;
    pump();
    publish_platform_ready();
    pump();
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    if (quic_impl_) quic_impl_->set_wakeup([this] { wake(); });
#endif
    return true;
}

fly_session_result_v2 SessionOwner::submit_action(std::uint32_t action_kind,
                                                  const std::uint8_t* code,
                                                  std::size_t code_size,
                                                  std::uint64_t pending_config_revision)
{
    if (!on_worker())
    {
        // Only these bounded choices are supported by the owner ABI.
        std::array<std::uint8_t, 32> choice{};
        const bool has_code = code != nullptr && code_size <= choice.size();
        if (has_code) std::copy_n(code, code_size, choice.data());
        return call_worker([this, action_kind, choice, has_code, code_size, pending_config_revision] {
            return submit_action(action_kind, has_code ? choice.data() : nullptr,
                                 code_size, pending_config_revision);
        });
    }
    return submit_action_on_worker(action_kind, code, code_size, pending_config_revision, false, nullptr, nullptr);
}

fly_session_result_v2 SessionOwner::submit_action_on_worker(std::uint32_t action_kind,
    const std::uint8_t* code, std::size_t code_size, std::uint64_t pending_config_revision,
    bool prepared_select, fly_session_notice_v2* terminal, std::uint64_t* out_request)
{
    OWNER_OBSERVE("submit");
    if (engine_ == nullptr)
        return FLY_SESSION_V2_CLOSED;
    if (action_kind == FLY_SESSION_ACTION_SELECT_CONTENT_V2) {
        if (!prepared_select && prepared_) prepared_->cancel();
        if (!code || code_size != 16) return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (!ContentPort::validate(content_, code)) return FLY_SESSION_V2_STALE;
    }
    pump();
    fly_session_view_v2_t* view = nullptr;
    if (fly_session_acquire_view_v2(engine_, &view) != FLY_SESSION_V2_OK ||
        view == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    fly_session_snapshot_v2 snap{};
    snap.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    snap.abi_version = FLY_SESSION_ABI_VERSION_2;
    if (fly_session_view_read_v2(view, &snap) != FLY_SESSION_V2_OK)
    {
        fly_session_view_release_v2(view);
        return FLY_SESSION_V2_UNAVAILABLE;
    }
    if (action_kind == FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2)
    {
        // The approval token belongs to this view; never silently rebind a click
        // from an older displayed configuration to the latest configuration.
        const std::uint8_t empty_id[32]{};
        fly_session_result_v2 validation = FLY_SESSION_V2_OK;
        if (code == nullptr || code_size != sizeof(snap.pending_config_id) ||
            pending_config_revision == 0 ||
            std::memcmp(code, empty_id, sizeof(empty_id)) == 0)
            validation = FLY_SESSION_V2_INVALID_ARGUMENT;
        else if (pending_config_revision != snap.pending_config_revision ||
                 std::memcmp(code, snap.pending_config_id, sizeof(empty_id)) != 0)
            validation = FLY_SESSION_V2_STALE;
        else if (snap.pending_config_local_confirmed != 0)
            validation = FLY_SESSION_V2_INVALID_STATE;
        if (validation != FLY_SESSION_V2_OK)
        {
            fly_session_view_release_v2(view);
            return validation;
        }
        if (!prepared_ || !prepared_->can_confirm(snap)) {
            fly_session_view_release_v2(view); return FLY_SESSION_V2_PERMISSION_DENIED;
        }
    }
    std::vector<fly_session_action_descriptor_v2> actions(snap.action_count);
    std::uint32_t written = 0;
    const auto copy = fly_session_view_copy_actions_v2(
        view, 0, actions.data(), static_cast<std::uint32_t>(actions.size()), &written);
    if (copy != FLY_SESSION_V2_OK)
    {
        fly_session_view_release_v2(view);
        return copy;
    }
    for (std::uint32_t i = 0; i < written; ++i)
        fly_session_approval_token_retain_v2(actions[i].approval_token);
    fly_session_view_release_v2(view);
    const fly_session_action_descriptor_v2* chosen = nullptr;
    for (std::uint32_t i = 0; i < written; ++i)
    {
        if (actions[i].action_kind == action_kind && actions[i].enabled != 0)
            chosen = &actions[i];
    }
    fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
    std::uint64_t submitted_request_id = 0;
    if (chosen != nullptr)
    {
        fly_session_action_v2 action{};
        action.struct_size = FLY_SESSION_ACTION_V2_SIZE;
        action.abi_version = FLY_SESSION_ABI_VERSION_2;
        action.request_id = next_request_id_++;
        submitted_request_id = action.request_id;
        if (out_request) *out_request = submitted_request_id;
        action.expected_view_revision = snap.view_revision;
        action.approval_token = chosen->approval_token;
        if (action_kind == FLY_SESSION_ACTION_SELECT_CONTENT_V2)
        {
            action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
            action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
            action.choice.abi_version = FLY_SESSION_ABI_VERSION_2;
            action.choice.choice_kind = FLY_SESSION_CHOICE_REFERENCE_V2;
            std::memcpy(action.choice.choice_id, code, 16);
            result = fly_session_submit_action_v2(engine_, &action);
        }
        else if (action_kind == FLY_SESSION_ACTION_JOIN_CODE_V2)
        {
            if (code == nullptr || code_size != 6)
                result = FLY_SESSION_V2_INVALID_ARGUMENT;
            else
            {
                action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
                action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
                action.choice.abi_version = FLY_SESSION_ABI_VERSION_2;
                action.choice.choice_kind = FLY_SESSION_CHOICE_INVITE_CODE_V2;
                std::memcpy(action.choice.choice_id, code, 6);
                result = fly_session_submit_action_v2(engine_, &action);
            }
        }
        else if (action_kind == FLY_SESSION_ACTION_START_DUAL_V2)
        {
            result = prepared_ ? prepared_->authorize_start(engine_, chosen->approval_token, &action.expected_view_revision)
                               : FLY_SESSION_V2_UNAVAILABLE;
            if (result == FLY_SESSION_V2_OK) result = fly_session_submit_action_v2(engine_, &action);
        }
        else
        {
            result = fly_session_submit_action_v2(engine_, &action);
        }
    }
    for (std::uint32_t i = 0; i < written; ++i)
        fly_session_approval_token_release_v2(actions[i].approval_token);
    pump();
    if (result == FLY_SESSION_V2_ACCEPTED || result == FLY_SESSION_V2_OK)
    {
        // pump executes the accepted action on this same serial owner before
        // returning. Reading its terminal receipt releases the reserved result
        // slot; otherwise eight ordinary create/cancel actions fill the queue.
        fly_session_notice_v2 notice{};
        notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
        notice.abi_version = FLY_SESSION_ABI_VERSION_2;
        while (fly_session_read_notice_v2(engine_, &notice) == FLY_SESSION_V2_OK)
        {
            if (notice.kind == FLY_SESSION_NOTICE_ACTION_RESULT_V2)
            {
                last_action_result_ = notice;
                if (terminal && notice.request_id == submitted_request_id) *terminal = notice;
                // Successful submissions retain the existing ACCEPTED contract;
                // admission never masks the engine's terminal rejection.
                if (notice.request_id == submitted_request_id &&
                    notice.outcome == FLY_SESSION_ACTION_REJECTED_V2)
                    result = notice.result;
            }
            else if (notice.kind == FLY_SESSION_NOTICE_SHUTDOWN_COMPLETE_V2)
            {
                shutdown_complete_ = true;
            }
            else
            {
                // A future notice kind needs its own consumer; fail visibly
                // instead of silently dropping it or accumulating a queue.
                result = FLY_SESSION_V2_CONTRACT_VIOLATION;
                break;
            }
        }
    }
    if (action_kind == FLY_SESSION_ACTION_START_DUAL_V2 && result < 0 && prepared_) prepared_->cancel();
    return result;
}

const char* SessionOwner::quic_provider_type() const noexcept
{
    return quic_provider_type_.c_str();
}

bool SessionOwner::quic_ready() const noexcept
{
    return quic_ready_;
}

const char* SessionOwner::quic_listen_address() const noexcept
{
    return quic_listen_address_.c_str();
}

fly_session_result_v2 SessionOwner::quic_engine_listen()
{
    if (!on_worker()) return call_worker([this] { return quic_engine_listen(); });
    OWNER_OBSERVE("quic_listen");
    if (quic_.listen == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    fly_session_bytes_v2 endpoint{};
    return quic_.listen(quic_.context, &token, 0, endpoint, 0, nullptr, nullptr);
}

fly_session_result_v2 SessionOwner::quic_engine_connect(SessionOwner* peer)
{
    if (!on_worker()) return call_worker([this, peer] { return quic_engine_connect(peer); });
    OWNER_OBSERVE("quic_connect");
    if (quic_.connect == nullptr || peer == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    const char* peer_listen = peer->quic_listen_address();
    if (peer_listen == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
    unsigned a = 0, b = 0, c = 0, d = 0, port = 0;
#ifdef _MSC_VER
    const int matched = sscanf_s(peer_listen, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#else
    const int matched = std::sscanf(peer_listen, "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#endif
    if (matched != 5)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    std::array<std::uint8_t, 18> endpoint{};
    endpoint[12] = static_cast<std::uint8_t>(a);
    endpoint[13] = static_cast<std::uint8_t>(b);
    endpoint[14] = static_cast<std::uint8_t>(c);
    endpoint[15] = static_cast<std::uint8_t>(d);
    endpoint[16] = static_cast<std::uint8_t>(port >> 8);
    endpoint[17] = static_cast<std::uint8_t>(port);
    fly_session_bytes_v2 bytes{};
    bytes.data = endpoint.data();
    bytes.size = static_cast<std::uint32_t>(endpoint.size());
    fly_session_quic_connect_policy_v2 policy{};
    policy.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
    policy.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::memcpy(policy.expected_der_spki_hash, peer->quic_spki_hash_.data(), 32);
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    return quic_.connect(quic_.context, &token, 0, bytes, 0, &policy, nullptr);
}

fly_session_result_v2 SessionOwner::quic_engine_inspect()
{
    if (!on_worker()) return call_worker([this] { return quic_engine_inspect(); });
    OWNER_OBSERVE("quic_inspect");
    if (quic_.inspect_handshake == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    const std::uint64_t connection =
        quic_impl_ ? quic_impl_->connection() : 0;
    if (connection == 0)
        return FLY_SESSION_V2_UNAVAILABLE;
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    return quic_.inspect_handshake(quic_.context, &token, connection, nullptr);
#else
    return FLY_SESSION_V2_UNAVAILABLE;
#endif
}

fly_session_result_v2 SessionOwner::quic_engine_open_stream(bool accept)
{
    if (!on_worker()) return call_worker([this, accept] { return quic_engine_open_stream(accept); });
    OWNER_OBSERVE("quic_stream");
    if (accept)
    {
        if (quic_.accept_bidi == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
    }
    else if (quic_.open_bidi == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    const std::uint64_t connection =
        quic_impl_ ? quic_impl_->connection() : 0;
    if (connection == 0)
        return FLY_SESSION_V2_UNAVAILABLE;
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    if (accept)
        return quic_.accept_bidi(quic_.context, &token, connection, 0, 0, nullptr);
    return quic_.open_bidi(quic_.context, &token, connection, 0, 0, nullptr);
#else
    return FLY_SESSION_V2_UNAVAILABLE;
#endif
}

fly_session_result_v2 SessionOwner::quic_engine_write_control()
{
    if (!on_worker()) return call_worker([this] { return quic_engine_write_control(); });
    OWNER_OBSERVE("quic_write");
    if (quic_.write == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    const std::uint64_t stream = quic_impl_ ? quic_impl_->control_stream() : 0;
    if (stream == 0)
        return FLY_SESSION_V2_UNAVAILABLE;
    static const std::uint8_t kControl[] = {0x02, 0x10, 'C', 'T', 'R', 'L'};
    fly_session_bytes_v2 source{};
    source.data = kControl;
    source.size = static_cast<std::uint32_t>(sizeof(kControl));
    fly_session_buffer_v2_t* buffer = nullptr;
    if (fly_session_buffer_create_copy_v2(source, &buffer) != FLY_SESSION_V2_OK ||
        buffer == nullptr)
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    const auto result =
        quic_.write(quic_.context, &token, stream, buffer, 0, nullptr);
    fly_session_buffer_release_v2(buffer);
    return result;
#else
    return FLY_SESSION_V2_UNAVAILABLE;
#endif
}

fly_session_result_v2 SessionOwner::quic_engine_grant_read()
{
    if (!on_worker()) return call_worker([this] { return quic_engine_grant_read(); });
    OWNER_OBSERVE("quic_read");
    if (quic_.grant_read_credit == nullptr)
        return FLY_SESSION_V2_UNAVAILABLE;
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    const std::uint64_t stream = quic_impl_ ? quic_impl_->control_stream() : 0;
    if (stream == 0)
        return FLY_SESSION_V2_UNAVAILABLE;
    fly_session_op_token_v2 token{};
    token.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    token.abi_version = FLY_SESSION_ABI_VERSION_2;
    token.operation_id = next_request_id_++;
    return quic_.grant_read_credit(quic_.context, &token, stream, 65536, nullptr);
#else
    return FLY_SESSION_V2_UNAVAILABLE;
#endif
}

bool SessionOwner::read_quic_facts(QuicFacts* out)
{
    if (out == nullptr) return false;
    return call_worker([this, out] {
        OWNER_OBSERVE("quic_facts");
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
        if (!quic_impl_) return FLY_SESSION_V2_UNAVAILABLE;
        out->connection = quic_impl_->connection();
        out->stream = quic_impl_->control_stream();
        out->written = quic_impl_->bytes_written();
        out->read = quic_impl_->bytes_read();
        out->stream_result = quic_impl_->last_stream_result();
        out->pending = quic_impl_->pending_count();
        out->inspected = quic_impl_->handshake_inspected();
        return FLY_SESSION_V2_OK;
#else
        return FLY_SESSION_V2_UNAVAILABLE;
#endif
    }) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionOwner::quic_control_roundtrip(
    SessionOwner* connector, std::int32_t* listen_result,
    std::int32_t* connect_result, std::int32_t* write_result,
    std::uint64_t* bytes_written, std::uint64_t* bytes_read)
{
    if (connector == nullptr || connector == this || listen_result == nullptr ||
        connect_result == nullptr || write_result == nullptr ||
        bytes_written == nullptr || bytes_read == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    // This diagnostic coordinates two independent workers. Never hold an owner
    // lock or run this polling loop inside either worker.
    if (on_worker() || connector->on_worker()) return FLY_SESSION_V2_INVALID_STATE;
    *listen_result = FLY_SESSION_V2_UNAVAILABLE;
    *connect_result = FLY_SESSION_V2_UNAVAILABLE;
    *write_result = FLY_SESSION_V2_UNAVAILABLE;
    *bytes_written = 0;
    *bytes_read = 0;
#ifndef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    return FLY_SESSION_V2_UNAVAILABLE;
#else
    if (!quic_ready() || !connector->quic_ready())
        return FLY_SESSION_V2_UNAVAILABLE;
    QuicFacts listener_facts{}, connector_facts{};
    const auto refresh = [&] {
        return read_quic_facts(&listener_facts) && connector->read_quic_facts(&connector_facts);
    };
    const auto timeout = std::chrono::seconds(8);
    *listen_result = quic_engine_listen();
    *connect_result = connector->quic_engine_connect(this);
    if (*listen_result != FLY_SESSION_V2_ACCEPTED ||
        *connect_result != FLY_SESSION_V2_ACCEPTED)
        return FLY_SESSION_V2_OK;
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
        if (listener_facts.connection != 0 && connector_facts.connection != 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
    if (listener_facts.connection == 0 || connector_facts.connection == 0)
    {
        *write_result = FLY_SESSION_V2_TIMEOUT;
        return FLY_SESSION_V2_OK;
    }
    if (quic_engine_inspect() != FLY_SESSION_V2_ACCEPTED ||
        connector->quic_engine_inspect() != FLY_SESSION_V2_ACCEPTED)
    {
        *write_result = FLY_SESSION_V2_AUTH_FAILED;
        return FLY_SESSION_V2_OK;
    }
    {
        const auto inspect_deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < inspect_deadline)
        {
            if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
            if (listener_facts.inspected && connector_facts.inspected)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
    const auto stream_deadline = std::chrono::steady_clock::now() + timeout;
    const auto accept = quic_engine_open_stream(true);
    const auto open = connector->quic_engine_open_stream(false);
    if (accept != FLY_SESSION_V2_ACCEPTED || open != FLY_SESSION_V2_ACCEPTED)
    {
        *write_result = accept != FLY_SESSION_V2_ACCEPTED ? accept : open;
        return FLY_SESSION_V2_OK;
    }
    bool control_sent = false;
    while (std::chrono::steady_clock::now() < stream_deadline)
    {
        if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
        // Quinn exposes an opened stream to its peer only after a write. Waiting
        // for peer accept before writing deadlocks this component diagnostic.
        if (!control_sent && connector_facts.stream != 0)
        {
            *write_result = connector->quic_engine_write_control();
            control_sent = true;
        }
        if (listener_facts.stream != 0 && connector_facts.stream != 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
    if (listener_facts.stream == 0 || connector_facts.stream == 0)
    {
        *bytes_written = static_cast<std::uint64_t>(
            (static_cast<std::uint32_t>(connector_facts.stream_result) << 16) |
            static_cast<std::uint32_t>(connector_facts.pending));
        *bytes_read = static_cast<std::uint64_t>(
            (static_cast<std::uint32_t>(listener_facts.stream_result) << 16) |
            static_cast<std::uint32_t>(listener_facts.pending));
        *write_result = FLY_SESSION_V2_INVALID_STATE;
        return FLY_SESSION_V2_OK;
    }
    (void)quic_engine_grant_read();
    const auto read_deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < read_deadline)
    {
        if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
        if (listener_facts.read > 0)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    if (!refresh()) return FLY_SESSION_V2_UNAVAILABLE;
    *bytes_written = connector_facts.written;
    *bytes_read = listener_facts.read;
    return FLY_SESSION_V2_OK;
#endif
}

SessionOwner::~SessionOwner()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        for (const auto& command : commands_)
        {
            command->result = FLY_SESSION_V2_CLOSED;
            command->complete = true;
        }
        commands_.clear();
    }
    OWNER_OBSERVE("closing");
    cv_.notify_all();
    if (worker_.joinable())
        worker_.join();
    // Joining only settles the worker. Admitted callers may still be waking
    // from cancellation and must leave the mutex/CV before those are destroyed.
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return pending_waiters_ == 0; });
}

void SessionOwner::shutdown_on_worker()
{
    if (prepared_) prepared_->close();
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    if (quic_impl_) quic_impl_->set_wakeup({});
#endif
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& item : timers_)
        {
            if (item.second.task != nullptr)
                fly_session_task_release_v2(item.second.task);
        }
        timers_.clear();
    }
    if (engine_ != nullptr)
    {
        OWNER_OBSERVE("shutdown");
        fly_session_begin_shutdown_v2(engine_, 1);
        pump();
        fly_session_notice_v2 notice{};
        notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
        notice.abi_version = FLY_SESSION_ABI_VERSION_2;
        while (fly_session_read_notice_v2(engine_, &notice) == FLY_SESSION_V2_OK)
            if (notice.kind == FLY_SESSION_NOTICE_SHUTDOWN_COMPLETE_V2) shutdown_complete_ = true;
        fly_session_destroy_v2(engine_);
        engine_ = nullptr;
    }
    content_impl_.reset();
    runtime_impl_.reset();
    {
        std::lock_guard<std::mutex> lock(mutex_);
        prepared_.reset();
    }
    if (runtime_) {
        fly_runtime_destroy(runtime_);
        runtime_ = nullptr;
        OWNER_OBSERVE("runtime_destroy");
    }
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
    OWNER_OBSERVE("provider_destroy");
    quic_impl_.reset();
#endif
    fly_session_inbox_release_v2(platform_inbox_);
    platform_inbox_ = nullptr;
    fly_session_inbox_release_v2(discovery_inbox_);
    discovery_inbox_ = nullptr;
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto* task : tasks_)
        fly_session_task_release_v2(task);
    tasks_.clear();
}

void SessionOwner::pump()
{
    for (;;)
    {
#ifdef FLYNES_ENABLE_RUST_QUIC_PROVIDER
        OWNER_OBSERVE("provider");
        if (quic_impl_) quic_impl_->drain();
#endif
        fly_session_task_v2_t* task = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!tasks_.empty()) {
                task = tasks_.front();
                tasks_.erase(tasks_.begin());
            }
        }
        if (!task) { reconcile_preparation(); return; }
        OWNER_OBSERVE("task");
        fly_session_task_run_v2(task);
    }
}

void SessionOwner::wake()
{
    std::lock_guard<std::mutex> lock(mutex_);
    provider_work_ = true;
    cv_.notify_all();
}

void SessionOwner::worker_loop()
{
    for (;;)
    {
        std::shared_ptr<Command> command;
        std::vector<fly_session_task_v2_t*> due_tasks;
        std::vector<std::uint64_t> due_tests;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            if (stop_)
                return;
            if (!commands_.empty())
            {
                command = commands_.front();
                commands_.pop_front();
                command->started = true;
            }
            const std::uint64_t now = monotonic_ns();
            std::uint64_t next_deadline = 0;
            for (const auto& item : timers_)
            {
                if (next_deadline == 0 || item.second.deadline_ns < next_deadline)
                    next_deadline = item.second.deadline_ns;
            }
            const bool has_posted = command || !tasks_.empty() || provider_work_;
            if (!has_posted && (next_deadline == 0 || next_deadline > now))
            {
                if (next_deadline == 0)
                {
                    cv_.wait(lock, [this] {
                        return stop_ || provider_work_ || !commands_.empty() || !tasks_.empty() || !timers_.empty();
                    });
                }
                else
                {
                    const std::uint64_t wait_ns = next_deadline - now;
                    const auto wait_ms = std::chrono::milliseconds(
                        static_cast<std::uint64_t>(
                            std::min<std::uint64_t>(wait_ns / 1000000ull + 1ull, 50ull)));
                    cv_.wait_for(lock, wait_ms);
                }
                continue;
            }
            provider_work_ = false;
            for (auto it = timers_.begin(); it != timers_.end();)
            {
                if (it->second.deadline_ns > now)
                {
                    ++it;
                    continue;
                }
                if (it->second.boot != boot_generation_)
                {
                    if (it->second.task != nullptr)
                        fly_session_task_release_v2(it->second.task);
                    it = timers_.erase(it);
                    continue;
                }
                if (it->second.task != nullptr)
                    due_tasks.push_back(it->second.task);
                else
                    due_tests.push_back(it->first);
                it = timers_.erase(it);
            }
        }
        if (command)
        {
            fly_session_result_v2 result = FLY_SESSION_V2_UNAVAILABLE;
            try { result = command->action(); }
            catch (const std::bad_alloc&) { result = FLY_SESSION_V2_OUT_OF_MEMORY; }
            catch (...) { result = FLY_SESSION_V2_CONTRACT_VIOLATION; }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                command->result = result;
                command->complete = true;
            }
            cv_.notify_all();
        }
        for (auto* task : due_tasks)
        {
            OWNER_OBSERVE("task");
            fly_session_task_run_v2(task);
        }
        if (!due_tests.empty())
        {
            OWNER_OBSERVE("timer");
            std::lock_guard<std::mutex> lock(mutex_);
            for (auto id : due_tests)
                test_fires_[id] += 1;
            cv_.notify_all();
        }
        pump();
    }
}

fly_session_result_v2 SessionOwner::arm_test_timer(std::uint64_t timer_id,
                                                   std::uint64_t delay_ms)
{
    if (!on_worker()) return call_worker([this, timer_id, delay_ms] { return arm_test_timer(timer_id, delay_ms); });
    const std::uint64_t deadline = monotonic_ns() + delay_ms * 1000000ull;
    return arm_timer(this, deadline, boot_generation_.data(), timer_id, nullptr);
}

fly_session_result_v2 SessionOwner::cancel_test_timer(std::uint64_t timer_id)
{
    if (!on_worker()) return call_worker([this, timer_id] { return cancel_test_timer(timer_id); });
    return cancel_timer(this, timer_id);
}

int SessionOwner::test_timer_fires(std::uint64_t timer_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    auto found = test_fires_.find(timer_id);
    return found == test_fires_.end() ? 0 : found->second;
}

bool SessionOwner::wait_test_timer(std::uint64_t timer_id, std::uint64_t timeout_ms)
{
    // The serial owner cannot wait for timer work that only it can execute.
    if (on_worker()) return test_timer_fires(timer_id) > 0;
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeout_ms);
    std::unique_lock<std::mutex> lock(mutex_);
    if (stop_) return false;
    ++pending_waiters_;
    bool fired = false;
    try
    {
        (void)cv_.wait_until(lock, deadline, [this, timer_id] {
            OWNER_OBSERVE("timer_wait_entered");
            const auto found = test_fires_.find(timer_id);
            return stop_ || (found != test_fires_.end() && found->second > 0);
        });
        const auto found = test_fires_.find(timer_id);
        fired = !stop_ && found != test_fires_.end() && found->second > 0;
#ifdef FLYNES_SESSION_OWNER_TEST_OBSERVER
        // Let the host test pause a registered caller without holding the owner
        // lock, proving destruction waits for admission rather than lock timing.
        lock.unlock();
        OWNER_OBSERVE("timer_wait_returning");
        lock.lock();
#endif
    }
    catch (...)
    {
        if (!lock.owns_lock()) lock.lock();
        fired = false;
    }
    --pending_waiters_;
    cv_.notify_all();
    return fired;
}

void SessionOwner::publish_platform_ready()
{
    if (platform_inbox_ == nullptr)
        return;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = platform_token_;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2;
    event.terminal = 0;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PLATFORM_STATE_SNAPSHOT_V2;
    fly_session_platform_state_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.state_revision = 1;
    payload.foreground = 1;
    payload.network_ready = 1;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)fly_session_deliver_v2(platform_inbox_, &event);
}

bool SessionOwner::read_game_choices(std::vector<fly_session_game_choice_v2>* out)
{
    if (!out) return false;
    if (!on_worker()) return call_worker([this, out] {
        return read_game_choices(out) ? FLY_SESSION_V2_OK : FLY_SESSION_V2_UNAVAILABLE;
    }) == FLY_SESSION_V2_OK;
    if (!engine_) return false;
    pump();
    fly_session_view_v2_t* view = nullptr;
#ifdef FLYNES_SESSION_OWNER_PROJECTION_TEST
    if (acquire_owner_projection_test_view(&view) != FLY_SESSION_V2_OK || !view) return false;
#else
    if (fly_session_acquire_view_v2(engine_, &view) != FLY_SESSION_V2_OK || !view) return false;
#endif
    fly_session_snapshot_v2 snapshot{};
    snapshot.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE; snapshot.abi_version = 2;
    auto result = fly_session_view_read_v2(view, &snapshot);
    try {
        if (result == FLY_SESSION_V2_OK) {
            out->resize(snapshot.game_choice_count);
            if (!out->empty()) {
                // The ABI uses the first row's declaration as the packed stride.
                // A zero declaration requests legacy R0 and omits the identity tail.
                out->front().struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE;
                out->front().abi_version = FLY_SESSION_ABI_VERSION_2;
            }
            std::uint32_t written = 0;
            result = fly_session_view_copy_game_choices_v2(view, 0, out->data(),
                static_cast<std::uint32_t>(out->size()), &written);
            out->resize(written);
        }
    } catch (...) { result = FLY_SESSION_V2_OUT_OF_MEMORY; }
    fly_session_view_release_v2(view);
    return result == FLY_SESSION_V2_OK;
}

bool SessionOwner::read_snapshot(OwnerSnapshot* out)
{
    if (out == nullptr) return false;
    if (!on_worker()) return call_worker([this, out] {
        return read_snapshot(out) ? FLY_SESSION_V2_OK : FLY_SESSION_V2_UNAVAILABLE;
    }) == FLY_SESSION_V2_OK;
    OWNER_OBSERVE("snapshot");
    if (out == nullptr || engine_ == nullptr)
        return false;
    pump();
    fly_session_view_v2_t* view = nullptr;
    if (fly_session_acquire_view_v2(engine_, &view) != FLY_SESSION_V2_OK ||
        view == nullptr)
        return false;
    fly_session_snapshot_v2 snap{};
    snap.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE;
    snap.abi_version = FLY_SESSION_ABI_VERSION_2;
    const auto result = fly_session_view_read_v2(view, &snap);
    fly_session_view_release_v2(view);
    if (result != FLY_SESSION_V2_OK)
        return false;
    out->abi_version = snap.abi_version;
    out->link_state = snap.link_state;
    out->game_state = snap.game_state;
    out->pending_config_local_confirmed = snap.pending_config_local_confirmed;
    out->pending_config_peer_confirmed = snap.pending_config_peer_confirmed;
    std::memcpy(out->pending_config_id, snap.pending_config_id,
                sizeof(out->pending_config_id));
    out->pending_config_revision = snap.pending_config_revision;
    out->last_action_request_id = last_action_result_.request_id;
    out->last_action_result = last_action_result_.result;
    out->last_action_outcome = last_action_result_.outcome;
    out->shutdown_complete = shutdown_complete_ ? 1u : 0u;
    if (content_impl_) {
        const auto status = content_impl_->query_status();
        out->content_query_attempted = status.attempted;
        out->last_content_query_result = status.result;
    }
    std::memcpy(out->primary_reason_key, snap.primary_reason_key,
                sizeof(out->primary_reason_key));
    return true;
}

fly_session_result_v2 SessionOwner::read_clock(void* context,
                                               fly_session_clock_sample_v2* out)
{
    if (context == nullptr || out == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* self = static_cast<SessionOwner*>(context);
    out->struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
    out->abi_version = FLY_SESSION_ABI_VERSION_2;
    out->continuous_ns = monotonic_ns();
    std::memcpy(out->boot_generation, self->boot_generation_.data(), 32);
    out->suspend_inclusive = 1;
    out->reserved_zero = 0;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionOwner::post_task(void* context,
                                              fly_session_task_v2_t* task)
{
    if (context == nullptr || task == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* self = static_cast<SessionOwner*>(context);
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->stop_ && !self->on_worker())
        return FLY_SESSION_V2_CLOSED;
    self->tasks_.push_back(task);
    self->cv_.notify_all();
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 SessionOwner::arm_timer(void* context, std::uint64_t deadline_ns,
                                              const std::uint8_t* boot_generation,
                                              std::uint64_t timer_id,
                                              fly_session_task_v2_t* task)
{
    if (context == nullptr)
    {
        if (task != nullptr)
            fly_session_task_release_v2(task);
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    auto* self = static_cast<SessionOwner*>(context);
    std::lock_guard<std::mutex> lock(self->mutex_);
    if (self->stop_ && !self->on_worker())
    {
        if (task != nullptr)
            fly_session_task_release_v2(task);
        return FLY_SESSION_V2_CLOSED;
    }
    auto found = self->timers_.find(timer_id);
    if (found != self->timers_.end())
    {
        if (found->second.task != nullptr)
            fly_session_task_release_v2(found->second.task);
        self->timers_.erase(found);
    }
    ArmedTimer armed{};
    armed.deadline_ns = deadline_ns;
    if (boot_generation != nullptr)
        std::memcpy(armed.boot.data(), boot_generation, armed.boot.size());
    else
        armed.boot = self->boot_generation_;
    armed.task = task;
    self->timers_[timer_id] = armed;
    self->test_fires_[timer_id] = 0;
    self->cv_.notify_all();
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 SessionOwner::cancel_timer(void* context, std::uint64_t timer_id)
{
    if (context == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* self = static_cast<SessionOwner*>(context);
    std::lock_guard<std::mutex> lock(self->mutex_);
    auto found = self->timers_.find(timer_id);
    if (found == self->timers_.end())
        return FLY_SESSION_V2_OK;
    if (found->second.task != nullptr)
        fly_session_task_release_v2(found->second.task);
    self->timers_.erase(found);
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionOwner::watch_platform(
    void* context, const fly_session_op_token_v2* token,
    fly_session_inbox_v2_t* inbox)
{
    if (context == nullptr || token == nullptr || inbox == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* self = static_cast<SessionOwner*>(context);
    self->platform_token_ = *token;
    fly_session_inbox_retain_v2(inbox);
    fly_session_inbox_release_v2(self->platform_inbox_);
    self->platform_inbox_ = inbox;
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 SessionOwner::stop_platform(void*,
                                                  const fly_session_op_token_v2*)
{
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionOwner::discovery_advertise(
    void* context, const fly_session_op_token_v2* token, fly_session_bytes_v2,
    std::uint64_t, fly_session_inbox_v2_t* inbox)
{
    if (context == nullptr || token == nullptr || inbox == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* self = static_cast<SessionOwner*>(context);
    self->discovery_token_ = *token;
    fly_session_inbox_retain_v2(inbox);
    fly_session_inbox_release_v2(self->discovery_inbox_);
    self->discovery_inbox_ = inbox;
    return FLY_SESSION_V2_ACCEPTED;
}

fly_session_result_v2 SessionOwner::discovery_scan(
    void*, const fly_session_op_token_v2*, fly_session_bytes_v2, std::uint64_t,
    fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 SessionOwner::discovery_stop(void*,
                                                   const fly_session_op_token_v2*)
{
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionOwner::discovery_unavailable_connection(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 SessionOwner::discovery_unavailable_write(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_buffer_v2_t*, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 SessionOwner::discovery_unavailable_subscribe(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}

} // namespace flynes::android::nearby
