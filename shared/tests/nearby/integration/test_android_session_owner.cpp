#include "session_owner.hpp"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <condition_variable>
#include <future>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <stdexcept>
#include <thread>
#include <vector>

using flynes::android::nearby::OwnerSnapshot;
using flynes::android::nearby::SessionOwner;

namespace {
std::mutex observations_mutex;
std::map<std::string, std::set<std::thread::id>> observations;
std::map<const SessionOwner*, std::map<std::string, std::set<std::thread::id>>> owner_observations;
std::map<const SessionOwner*, flynes::android::nearby::PreparedContent*> prepared_observations;
std::condition_variable observations_cv;
bool block_snapshot = false;
bool snapshot_blocked = false;
bool release_snapshot = false;
int queued_commands = 0;
bool throw_snapshot = false;
bool reenter_submit = false;
bool reentered = false;
bool block_queued = false;
bool queued_blocked = false;
bool release_queued = false;
bool close_started = false;
bool timer_wait_entered = false;
bool block_timer_return = false;
bool timer_return_blocked = false;
bool release_timer_return = false;
bool throw_timer_wait = false;
bool timer_reentry_nonblocking = false;
int failures = 0;
void check(bool condition, const char* message)
{
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}
}

namespace flynes::android::nearby {
void observe_prepared_content(const SessionOwner* owner, PreparedContent* prepared) {
    std::lock_guard<std::mutex> lock(observations_mutex); prepared_observations[owner] = prepared;
}
void observe_session_owner(const SessionOwner* owner, const char* event)
{
    std::unique_lock<std::mutex> lock(observations_mutex);
    observations[event].insert(std::this_thread::get_id());
    owner_observations[owner][event].insert(std::this_thread::get_id());
    if (std::string(event) == "closing") {
        close_started = true;
        observations_cv.notify_all();
    }
    if (std::string(event) == "timer_wait_entered") {
        timer_wait_entered = true;
        observations_cv.notify_all();
        if (throw_timer_wait) {
            throw_timer_wait = false;
            throw std::runtime_error("injected timer wait exception");
        }
    }
    if (std::string(event) == "timer_wait_returning" && block_timer_return) {
        block_timer_return = false;
        timer_return_blocked = true;
        observations_cv.notify_all();
        if (!observations_cv.wait_for(lock, std::chrono::seconds(3), [] { return release_timer_return; })) {
            std::fprintf(stderr, "FAIL: timer return barrier exceeded bounded test deadline\n");
            std::_Exit(1);
        }
    }
    if (std::string(event) == "snapshot" && throw_snapshot) {
        throw_snapshot = false;
        throw std::runtime_error("injected command exception");
    }
    if (std::string(event) == "submit" && reenter_submit) {
        reenter_submit = false;
        lock.unlock();
        OwnerSnapshot snapshot{};
        const bool result = const_cast<SessionOwner*>(owner)->read_snapshot(&snapshot);
        const auto before = std::chrono::steady_clock::now();
        const bool timer_result = const_cast<SessionOwner*>(owner)->wait_test_timer(777, 500);
        const bool timer_nonblocking = !timer_result &&
            std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250);
        lock.lock();
        reentered = result;
        timer_reentry_nonblocking = timer_nonblocking;
    }
    if (std::string(event) == "queued") {
        ++queued_commands;
        observations_cv.notify_all();
        if (block_queued) {
            block_queued = false;
            queued_blocked = true;
            observations_cv.notify_all();
            observations_cv.wait(lock, [] { return release_queued; });
        }
    }
    if (std::string(event) == "snapshot" && block_snapshot) {
        block_snapshot = false;
        snapshot_blocked = true;
        observations_cv.notify_all();
        observations_cv.wait(lock, [] { return release_snapshot; });
    }
}
}

void test_concurrent_receipts()
{
    std::unique_ptr<SessionOwner> owner(SessionOwner::create());
    std::vector<std::future<bool>> readers;
    for (int i = 0; i < 3; ++i) {
        readers.emplace_back(std::async(std::launch::async, [&] {
            for (int j = 0; j < 40; ++j) {
                OwnerSnapshot snapshot{};
                if (!owner->read_snapshot(&snapshot) || snapshot.abi_version != 2) return false;
            }
            return true;
        }));
    }
    for (int cycle = 0; cycle < 12; ++cycle) {
        check(owner->submit_action(5, nullptr, 0) == FLY_SESSION_V2_ACCEPTED, "repeated invite receipt");
        OwnerSnapshot snapshot{};
        check(owner->read_snapshot(&snapshot) && snapshot.link_state == 3, "submit completes before snapshot");
        check(owner->submit_action(7, nullptr, 0) == FLY_SESSION_V2_ACCEPTED, "repeated cancel receipt");
    }
    for (auto& reader : readers) check(reader.get(), "concurrent snapshots complete");
    const std::uint8_t invalid[] = {'1','2','X','4','5','6'};
    check(owner->submit_action(9, invalid, sizeof(invalid)) == FLY_SESSION_V2_INVALID_STATE,
          "terminal invalid state must not become ACCEPTED");
    OwnerSnapshot rejected{};
    check(owner->read_snapshot(&rejected) && rejected.last_action_outcome == 2,
          "rejected terminal receipt remains visible");
    check(owner->submit_action(5, nullptr, 0) == FLY_SESSION_V2_ACCEPTED, "next request succeeds");
}

void test_command_exception_and_reentry()
{
    std::unique_ptr<SessionOwner> owner(SessionOwner::create());
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        throw_snapshot = true;
        reenter_submit = true;
        reentered = false;
    }
    OwnerSnapshot snapshot{};
    check(!owner->read_snapshot(&snapshot), "command exception returns failure without stranding waiter");
    check(owner->submit_action(5, nullptr, 0) == FLY_SESSION_V2_ACCEPTED, "worker continues after command exception");
    check(reentered, "worker self-call executes directly without waiting on its own queue");
    check(timer_reentry_nonblocking, "worker timer self-call never waits for its own timer");
}

void test_shutdown_cancels_queued_commands()
{
    auto* owner = SessionOwner::create();
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        block_snapshot = true;
        snapshot_blocked = false;
        release_snapshot = false;
        queued_commands = 0;
    }
    auto blocked = std::async(std::launch::async, [owner] {
        OwnerSnapshot snapshot{};
        return owner->read_snapshot(&snapshot);
    });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return snapshot_blocked; }),
              "shutdown barrier reached");
        queued_commands = 0;
    }
    auto queued = std::async(std::launch::async, [owner] { return owner->submit_action(5, nullptr, 0); });
    std::size_t submits_before = 0;
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::milliseconds(150), [] { return queued_commands == 1; }),
              "shutdown victim is queued before close");
        submits_before = owner_observations[owner]["submit"].size();
    }
    auto closing = std::async(std::launch::async, [owner] { delete owner; });
    check(queued.get() == FLY_SESSION_V2_CLOSED, "close wakes queued caller with CLOSED");
    check(closing.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
          "close waits for executing command and worker cleanup");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        release_snapshot = true;
        observations_cv.notify_all();
    }
    check(blocked.get(), "already executing command finishes on close");
    closing.get();
    check(owner_observations[owner]["submit"].size() == submits_before,
          "close never executes the cancelled queued invite");
}

void test_quic_workers()
{
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        owner_observations.clear();
    }
    std::unique_ptr<SessionOwner> listener(SessionOwner::create());
    std::unique_ptr<SessionOwner> connector(SessionOwner::create());
    std::int32_t listen = 0, connect = 0, write = 0;
    std::uint64_t written = 0, read = 0;
    check(listener->quic_control_roundtrip(connector.get(), &listen, &connect, &write, &written, &read)
              == FLY_SESSION_V2_OK, "QUIC roundtrip completes while each worker drains independently");
    check(listen == FLY_SESSION_V2_ACCEPTED && connect == FLY_SESSION_V2_ACCEPTED &&
              write == FLY_SESSION_V2_ACCEPTED && written == 6 && read == 6,
          "real Quinn stream writes before peer accept and carries exact control bytes");
    listener.reset();
    connector.reset();
    std::set<std::thread::id> workers;
    for (const auto& owner : owner_observations) {
        std::set<std::thread::id> threads;
        for (const auto& event : owner.second) {
            if (event.first != "queued" && event.first != "closing")
                threads.insert(event.second.begin(), event.second.end());
        }
        check(threads.size() == 1 && threads.count(std::this_thread::get_id()) == 0,
              "all QUIC operations/facts/drains/destruction use the corresponding owner worker");
        workers.insert(threads.begin(), threads.end());
    }
    check(workers.size() == 2, "two owners retain independent worker threads");
}

void test_discovery_scan_requires_a_real_provider()
{
    std::unique_ptr<SessionOwner> owner(SessionOwner::create());
    check(owner->submit_action(FLY_SESSION_ACTION_START_DISCOVERY_V2, nullptr, 0)
              == FLY_SESSION_V2_ACCEPTED,
          "start discovery is accepted only when the owner has a scanning provider");
    OwnerSnapshot snapshot{};
    check(owner->read_snapshot(&snapshot) && snapshot.link_state == FLY_SESSION_LINK_DISCOVERING_V2,
          "an accepted scan publishes DISCOVERING, not a local invite");
}

void test_shutdown_waiter_lifetime()
{
    auto* owner = SessionOwner::create();
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        block_snapshot = true;
        snapshot_blocked = false;
        release_snapshot = false;
    }
    auto executing = std::async(std::launch::async, [owner] {
        OwnerSnapshot snapshot{};
        return owner->read_snapshot(&snapshot);
    });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return snapshot_blocked; }),
              "lifetime barrier holds worker");
        block_queued = true;
        queued_blocked = false;
        release_queued = false;
        close_started = false;
    }
    auto queued = std::async(std::launch::async, [owner] { return owner->submit_action(5, nullptr, 0); });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return queued_blocked; }),
              "lifetime barrier holds admitted caller before it can observe cancellation");
    }
    auto closing = std::async(std::launch::async, [owner] { delete owner; });
    // The callback barrier makes this ordering deterministic without accessing
    // an owner after close. Returning early would destroy this caller's mutex.
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return close_started; }),
              "close stops admission before releasing worker barrier");
        release_snapshot = true;
        observations_cv.notify_all();
    }
    if (closing.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
        std::fprintf(stderr, "FAIL: close destroyed owner before admitted waiter returned\n");
        std::_Exit(1); // Do not resume the waiter against a destroyed mutex.
    }
    check(executing.get(), "executing snapshot finishes during close");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        release_queued = true;
        observations_cv.notify_all();
    }
    check(queued.get() == FLY_SESSION_V2_CLOSED, "held waiter observes CLOSED before destruction");
    closing.get();
}

void test_shutdown_wakes_timer_waiter()
{
    auto* owner = SessionOwner::create();
    check(owner->arm_test_timer(99, 60000) == FLY_SESSION_V2_ACCEPTED,
          "long timer is armed before close");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        block_snapshot = true;
        snapshot_blocked = false;
        release_snapshot = false;
        timer_wait_entered = false;
        close_started = false;
    }
    // Keep worker shutdown blocked until the waiter has returned, even on the
    // unfixed implementation. The red case therefore cannot destroy a live CV.
    auto executing = std::async(std::launch::async, [owner] {
        OwnerSnapshot snapshot{};
        return owner->read_snapshot(&snapshot);
    });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return snapshot_blocked; }),
              "timer-close worker barrier reached");
    }
    auto waiting = std::async(std::launch::async, [owner] { return owner->wait_test_timer(99, 2000); });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return timer_wait_entered; }),
              "timer waiter entered owner synchronization before close");
    }
    auto closing = std::async(std::launch::async, [owner] { delete owner; });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return close_started; }),
              "close began with a live timer waiter");
    }
    check(waiting.wait_for(std::chrono::milliseconds(300)) == std::future_status::ready,
          "close promptly wakes admitted timer waiter before its timeout");
    check(!waiting.get(), "closing an unfired timer returns false");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        release_snapshot = true;
        observations_cv.notify_all();
    }
    check(executing.get(), "timer-close executing snapshot finishes");
    closing.get();
}

void test_timer_wait_exception_and_exit()
{
    auto* owner = SessionOwner::create();
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        throw_timer_wait = true;
    }
    bool threw = false;
    bool timer_result = true;
    try { timer_result = owner->wait_test_timer(98, 100); }
    catch (...) { threw = true; }
    check(!threw && !timer_result, "timer wait exception returns false and releases waiter admission");
    check(!owner->wait_test_timer(98, 1), "ordinary timer wait timeout releases waiter admission");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        timer_wait_entered = false;
        block_timer_return = true;
        timer_return_blocked = false;
        release_timer_return = false;
    }
    auto waiting = std::async(std::launch::async, [owner] { return owner->wait_test_timer(98, 2000); });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return timer_wait_entered; }),
              "exit test timer waiter entered owner before close");
    }
    auto closing = std::async(std::launch::async, [owner] { delete owner; });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return timer_return_blocked; }),
              "timer waiter reaches synchronized return barrier during close");
    }
    if (closing.wait_for(std::chrono::milliseconds(100)) == std::future_status::ready) {
        std::fprintf(stderr, "FAIL: close destroyed synchronization before admitted timer waiter exited\n");
        std::_Exit(1); // Never resume a deliberately held caller against freed memory.
    }
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        release_timer_return = true;
        observations_cv.notify_all();
    }
    check(!waiting.get(), "timer exit waiter returns false during close");
    closing.get();
}

void test_queue_timeout_and_backpressure()
{
    std::unique_ptr<SessionOwner> owner(SessionOwner::create());
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        block_snapshot = true;
        snapshot_blocked = false;
        release_snapshot = false;
        queued_commands = 0;
    }
    auto blocked = std::async(std::launch::async, [&] {
        OwnerSnapshot snapshot{};
        return owner->read_snapshot(&snapshot);
    });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return snapshot_blocked; }),
              "worker snapshot barrier reached");
        queued_commands = 0;
    }
    std::vector<std::future<fly_session_result_v2>> pending;
    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    int ready_count = 0;
    std::promise<void> start_signal;
    const auto start = start_signal.get_future().share();
    for (int i = 0; i < 8; ++i) pending.emplace_back(std::async(std::launch::async, [&] {
        {
            std::lock_guard<std::mutex> lock(ready_mutex);
            ++ready_count;
            ready_cv.notify_all();
        }
        start.wait();
        return owner->submit_action(5, nullptr, 0);
    }));
    {
        std::unique_lock<std::mutex> lock(ready_mutex);
        check(ready_cv.wait_for(lock, std::chrono::seconds(2), [&] { return ready_count == 8; }),
              "all bounded-queue callers are ready");
    }
    start_signal.set_value();
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::milliseconds(150), [] { return queued_commands == 8; }),
              "eight commands fill bounded owner queue");
    }
    check(owner->submit_action(5, nullptr, 0) == FLY_SESSION_V2_BACKPRESSURE,
          "ninth queued command reports backpressure");
    for (auto& result : pending) check(result.get() == FLY_SESSION_V2_TIMEOUT,
                                       "unstarted command atomically cancels on timeout");
    check(blocked.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout,
          "started command waits for its real result beyond admission deadline");
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        release_snapshot = true;
        observations_cv.notify_all();
    }
    check(blocked.get(), "started snapshot returns actual result");
    OwnerSnapshot after{};
    check(owner->read_snapshot(&after) && after.link_state == 1 && after.last_action_request_id == 0,
          "cancelled queued invites never execute late");
    check(owner->submit_action(5, nullptr, 0) == FLY_SESSION_V2_ACCEPTED,
          "cancelled commands free queue capacity");
}

void test_prepared_cancel_bypasses_saturated_command_queue()
{
    // Synthetic metadata exercises the real production slot ONLY. The owner's
    // actual engine stays IDLE; this is not a fake connected-owner test.
    std::unique_ptr<SessionOwner> owner(SessionOwner::create({{}, [](const std::uint8_t*) { return true; }, {}}));
    flynes::android::nearby::PreparedContent* prepared = nullptr;
    {
        std::lock_guard<std::mutex> lock(observations_mutex);
        prepared = prepared_observations[owner.get()];
        block_snapshot = true; snapshot_blocked = false; release_snapshot = false; queued_commands = 0;
    }
    auto blocked = std::async(std::launch::async, [&] { OwnerSnapshot out{}; return owner->read_snapshot(&out); });
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::seconds(2), [] { return snapshot_blocked; }), "cancel test owner is blocked");
        queued_commands = 0;
    }
    fly_session_snapshot_v2 snap{}; snap.link_state = FLY_SESSION_LINK_CONNECTED_LOBBY_V2;
    snap.game_state = FLY_SESSION_GAME_NOT_STARTED_V2; snap.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    snap.scope.link_id[0] = 1; snap.pending_config_id[0] = 3; snap.pending_config_revision = 1; snap.dual_content_hash[0] = 2;
    fly_session_game_choice_v2 choice{}; choice.selectable = 1; choice.source_choice_ref[0] = 1; choice.content_id[0] = 2;
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(1, std::uint8_t{7}); std::uint64_t ticket = 0;
    check(prepared && prepared->begin(snap, choice, &ticket) == FLY_SESSION_V2_OK &&
          prepared->stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_OK &&
          prepared->bind_config(snap) == FLY_SESSION_V2_OK && prepared->can_confirm(snap),
          "real owner slot contains a prepared authorization before queue saturation");
    std::vector<std::future<fly_session_result_v2>> queued;
    std::mutex ready_mutex;
    std::condition_variable ready_cv;
    int ready_count = 0;
    std::promise<void> start_signal;
    const auto start = start_signal.get_future().share();
    for (int i = 0; i < 8; ++i) queued.emplace_back(std::async(std::launch::async, [&] {
        {
            std::lock_guard<std::mutex> lock(ready_mutex);
            ++ready_count;
            ready_cv.notify_all();
        }
        start.wait();
        return owner->submit_action(5, nullptr, 0);
    }));
    {
        std::unique_lock<std::mutex> lock(ready_mutex);
        check(ready_cv.wait_for(lock, std::chrono::seconds(2), [&] { return ready_count == 8; }),
              "all cancel-test queue callers are ready");
    }
    start_signal.set_value();
    {
        std::unique_lock<std::mutex> lock(observations_mutex);
        check(observations_cv.wait_for(lock, std::chrono::milliseconds(150), [] { return queued_commands == 8; }), "cancel test queue saturates");
    }
    check(owner->cancel_content_preparation(ticket) == FLY_SESSION_V2_OK, "revocation does not depend on command queue admission");
    check(!prepared->can_confirm(snap), "saturated queue cannot retain old prepared authority");
    check(prepared->stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_STALE,
          "cancelled late stage cannot resurrect old authority");
    std::uint64_t newer = 0;
    check(prepared->begin(snap, choice, &newer) == FLY_SESSION_V2_OK && newer != ticket, "replacement receives a new slot ticket");
    check(owner->cancel_content_preparation(ticket) == FLY_SESSION_V2_OK &&
          prepared->stage(newer, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_OK &&
          prepared->bind_config(snap) == FLY_SESSION_V2_OK, "old cancellation cannot erase new ticket");
    for (auto& task : queued) check(task.get() == FLY_SESSION_V2_TIMEOUT, "blocked commands expire without side effects");
    check(owner->cancel_content_preparation(newer) == FLY_SESSION_V2_OK && !prepared->can_confirm(snap),
          "revocation also bypasses an unstarted command timeout");
    {
        std::lock_guard<std::mutex> lock(observations_mutex); release_snapshot = true; observations_cv.notify_all();
    }
    check(blocked.get(), "cancel test releases blocked owner");
}

int main()
{
    const auto caller = std::this_thread::get_id();
    auto* owner = SessionOwner::create();
    if (!owner) return 2;
    std::array<std::uint8_t, 16> missing_source{};
    std::array<std::uint8_t, 32> missing_hash{};
    std::uint64_t ticket = 0;
    check(owner->begin_content_preparation(missing_source, &ticket) == FLY_SESSION_V2_STALE && ticket == 0,
          "real owner refuses unknown preparation without selection");
    check(owner->complete_content_preparation(1, missing_source, missing_hash,
              std::make_shared<const std::vector<std::uint8_t>>(1, std::uint8_t{})) == FLY_SESSION_V2_STALE,
          "real owner refuses late unadmitted preparation completion");
    check(owner->cancel_content_preparation(1) == FLY_SESSION_V2_OK, "owner cancellation is idempotent on worker");
    check(owner->submit_action(FLY_SESSION_ACTION_CREATE_INVITE_V2, nullptr, 0)
              == FLY_SESSION_V2_ACCEPTED, "invite accepted");
    OwnerSnapshot snapshot{};
    check(owner->read_snapshot(&snapshot), "snapshot available");
    check(owner->arm_test_timer(7, 10) == FLY_SESSION_V2_ACCEPTED, "timer armed");
    check(owner->wait_test_timer(7, 1500), "timer runs without snapshot pump");
    delete owner;
    std::set<std::thread::id> execution_threads;
    for (const auto* event : {"create", "submit", "snapshot", "task", "provider", "timer", "shutdown", "runtime_create", "runtime_registered", "runtime_destroy", "prepared_registered"}) {
        const auto& threads = observations[event];
        check(!threads.empty(), (std::string(event) + " was observed").c_str());
        check(threads.count(caller) == 0, (std::string(event) + " must not execute on caller").c_str());
        execution_threads.insert(threads.begin(), threads.end());
    }
    check(execution_threads.size() == 1, "all engine/provider work uses one owner worker");
    if (failures == 0) {
        test_concurrent_receipts();
        test_queue_timeout_and_backpressure();
        test_prepared_cancel_bypasses_saturated_command_queue();
        test_command_exception_and_reentry();
        test_shutdown_cancels_queued_commands();
        test_shutdown_waiter_lifetime();
        test_shutdown_wakes_timer_waiter();
        test_timer_wait_exception_and_exit();
        test_quic_workers();
        test_discovery_scan_requires_a_real_provider();
    }
    if (failures == 0) std::puts("Android SessionOwner worker tests passed");
    return failures == 0 ? 0 : 1;
}
