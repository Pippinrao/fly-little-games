#include "session_owner_v2.hpp"
#include "nearby/harness/two_engine_loopback_fixture.hpp"
#include "session/engine/session_engine.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <mutex>
#include <thread>

namespace harmony_v2_tests {
extern int failures;
void check(bool, const char*);
fly_catalog_snapshot_t* make_snapshot(std::uint32_t, bool zipped = false);
}
namespace flynes::harmony::nearby {
class OwnerTestAccess {
public:
    static fly_session_executor_port_v2 executor(SessionOwnerV2& owner) { return owner.executor_port(); }
    // RED-path rescue owns State, not the owner under test, so a broken destructor
    // can be observed without leaving a thread running after the bounded test.
    static std::function<void()> rescue(SessionOwnerV2& owner) {
        auto state = owner.state_;
        return [state] { SessionOwnerV2 cleanup; cleanup.state_ = state; cleanup.close(); };
    }
    static std::function<bool()> state_expired(SessionOwnerV2& owner) {
        std::weak_ptr<SessionOwnerV2::State> state = owner.state_;
        return [state] { return state.expired(); };
    }
    static fly_session_result_v2 call(SessionOwnerV2& owner, std::function<fly_session_result_v2()> f) { return owner.call(std::move(f)); }
    static fly_session_result_v2 project(SessionOwnerV2& owner, const fly_session_view_v2_t* view, SessionOwnerV2::Snapshot* out) {
        return owner.call([=] { return SessionOwnerV2::project(view, out); });
    }
    static fly_session_result_v2 select(SessionOwnerV2& owner, fly_session_v2_t* engine,
        const fly_session_view_v2_t* view, const ContentPortV2& content, const ContentPortV2::Ref& ref) {
        return owner.call([&] { return SessionOwnerV2::select_view(engine, view, content, ref, 3001); });
    }
};
}
namespace {
using namespace flynes::harmony::nearby;
using harmony_v2_tests::check;
struct Ports {
    std::atomic<unsigned> retained{0}, released{0}, watched{0}, stopped{0};
    std::thread::id worker;
    std::mutex mutex;
    fly_session_inbox_v2_t* inbox = nullptr;
    fly_session_op_token_v2 token{};
    std::function<void()> on_release;
    fly_session_clock_port_v2 clock{};
    fly_session_platform_state_port_v2 platform{};
    fly_session_ports_v2 table{};
    Ports() {
        clock.struct_size = FLY_SESSION_CLOCK_PORT_V2_SIZE; clock.abi_version = 2;
        clock.context = this; clock.retain = retain; clock.release = release;
        clock.read_continuous = [](void*, fly_session_clock_sample_v2* out) -> fly_session_result_v2 {
            *out = {}; out->struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE; out->abi_version = 2;
            out->continuous_ns = std::chrono::steady_clock::now().time_since_epoch().count();
            out->suspend_inclusive = 1; out->boot_generation[0] = 7;
            return FLY_SESSION_V2_OK; // controlled host clock only, not a platform certificate
        };
        platform.struct_size = FLY_SESSION_PLATFORM_STATE_PORT_V2_SIZE; platform.abi_version = 2;
        platform.context = this; platform.retain = retain; platform.release = release;
        platform.watch = [](void* p, const fly_session_op_token_v2* token, fly_session_inbox_v2_t* inbox) -> fly_session_result_v2 {
            auto& self = *static_cast<Ports*>(p);
            std::lock_guard<std::mutex> lock(self.mutex);
            self.worker = std::this_thread::get_id(); ++self.watched;
            self.token = *token; self.inbox = inbox; fly_session_inbox_retain_v2(inbox);
            return FLY_SESSION_V2_ACCEPTED;
        };
        platform.stop = [](void* p, const fly_session_op_token_v2*) -> fly_session_result_v2 {
            ++static_cast<Ports*>(p)->stopped; return FLY_SESSION_V2_OK;
        };
        table.struct_size = FLY_SESSION_PORTS_V2_SIZE; table.abi_version = 2;
        table.clock = &clock; table.platform_state = &platform;
    }
    ~Ports() { fly_session_inbox_release_v2(inbox); }
    fly_session_result_v2 late_callback() {
        fly_session_port_event_v2 event{};
        event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE; event.abi_version = 2;
        event.token = token; event.event_sequence = 1;
        event.event_kind = FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2;
        event.result = FLY_SESSION_V2_OK; event.payload_kind = FLY_SESSION_PLATFORM_STATE_SNAPSHOT_V2;
        fly_session_platform_state_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE; payload.abi_version = 2; payload.state_revision = 1;
        event.payload_size = sizeof(payload); std::memcpy(event.payload, &payload, sizeof(payload));
        return fly_session_deliver_v2(inbox, &event);
    }
    static void retain(void* p) { ++static_cast<Ports*>(p)->retained; }
    static void release(void* p) {
        auto& self = *static_cast<Ports*>(p); ++self.released;
        if (self.on_release) self.on_release();
    }
};
std::shared_ptr<ContentPortV2> content(std::uint32_t count) {
    auto result = std::make_shared<ContentPortV2>([](ContentPortV2::Ref& out) { out.fill(0x31); return true; });
    auto* snapshot = harmony_v2_tests::make_snapshot(count);
    check(result->publish(snapshot, 1) == FLY_SESSION_V2_OK, "owner test publication");
    fly_catalog_snapshot_release(snapshot);
    return result;
}
void lifecycle() {
    Ports ports;
    fly_session_result_v2 result = FLY_SESSION_V2_OK;
    auto rows = content(1);
    auto owner = SessionOwnerV2::create(ports.table, rows, &result);
    check(owner && result == FLY_SESSION_V2_OK, "real V2 owner creates successfully");
    if (!owner) return;
    check(ports.watched == 1 && ports.worker != std::this_thread::get_id(), "one actual engine watch on worker");
    SessionOwnerV2::Snapshot snapshot;
    check(owner->read(&snapshot) == FLY_SESSION_V2_OK && snapshot.session.abi_version == 2 &&
          !snapshot.runtime_registered && snapshot.session.dual_mode == 0 &&
          snapshot.session.link_state != FLY_SESSION_LINK_CONNECTED_LOBBY_V2 && snapshot.choices.empty(),
          "real snapshot does not manufacture link or DUAL capability");
    ContentPortV2::Ref unknown{}; unknown[0] = 9;
    check(owner->select(unknown) == FLY_SESSION_V2_STALE, "unknown exact ref rejected before action submission");
    check(OwnerTestAccess::call(*owner, [&] { return owner->close(); }) == FLY_SESSION_V2_BUSY,
          "close on worker rejects without self join");
    check(OwnerTestAccess::call(*owner, [&] { return owner->read(&snapshot); }) == FLY_SESSION_V2_BUSY,
          "reentrant read rejects without deadlock");
    std::vector<std::future<fly_session_result_v2>> reads;
    for (unsigned i = 0; i < 6; ++i) reads.push_back(std::async(std::launch::async, [owner] {
        SessionOwnerV2::Snapshot value; return owner->read(&value);
    }));
    std::vector<std::future<fly_session_result_v2>> closes;
    for (unsigned i = 0; i < 4; ++i) closes.push_back(std::async(std::launch::async, [owner] { return owner->close(); }));
    for (auto& closed : closes) check(closed.get() == FLY_SESSION_V2_OK, "concurrent close serializes join and actual engine shutdown");
    for (auto& read : reads) { const auto r = read.get(); check(r == FLY_SESSION_V2_OK || r == FLY_SESSION_V2_CLOSED, "admitted read settles across close"); }
    check(owner->close() == FLY_SESSION_V2_OK && owner->read(&snapshot) == FLY_SESSION_V2_CLOSED, "idempotent close seals admission");
    check(ports.stopped == 1 && ports.retained == ports.released, "provider stop and balanced retention");
    check(ports.late_callback() == FLY_SESSION_V2_CLOSED, "actual retained inbox rejects provider callback after close");
    std::vector<std::uint8_t> closed_record;
    check(rows->query_record(0, &closed_record) == FLY_SESSION_V2_CLOSED, "owner close invalidates its retained content publication");
    auto missing = ports.table; missing.clock = nullptr;
    check(!SessionOwnerV2::create(missing, content(0), &result), "mandatory clock cannot be synthesized");
    fly_session_dual_runtime_port_v2 runtime{};
    auto with_runtime = ports.table; with_runtime.dual_runtime = &runtime;
    check(!SessionOwnerV2::create(with_runtime, content(0), &result), "H1 refuses runtime injection");
}
void backpressure() {
    Ports ports; fly_session_result_v2 result;
    auto owner = SessionOwnerV2::create(ports.table, content(0), &result);
    check(bool(owner), "bounded owner created"); if (!owner) return;
    std::promise<void> entered, unblock;
    auto released = unblock.get_future().share();
    auto active = std::async(std::launch::async, [&] { return OwnerTestAccess::call(*owner, [&] {
        entered.set_value(); released.wait(); return FLY_SESSION_V2_OK;
    }); });
    entered.get_future().wait();
    std::atomic<unsigned> rejected{0};
    std::vector<std::future<fly_session_result_v2>> jobs;
    for (unsigned i = 0; i < 9; ++i) jobs.push_back(std::async(std::launch::async, [&, owner] {
        const auto r = OwnerTestAccess::call(*owner, [] { return FLY_SESSION_V2_OK; });
        if (r == FLY_SESSION_V2_BACKPRESSURE) ++rejected;
        return r;
    }));
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!rejected && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(rejected == 1, "exactly eight queued commands admitted");
    unblock.set_value(); active.get();
    for (auto& job : jobs) { const auto r = job.get(); check(r == FLY_SESSION_V2_OK || r == FLY_SESSION_V2_BACKPRESSURE, "queue completes or rejects explicitly"); }
    check(owner->close() == FLY_SESSION_V2_OK, "bounded owner closed");
}
void executor_lifetime() {
    Ports ports; fly_session_result_v2 result;
    auto owner = SessionOwnerV2::create(ports.table, content(0), &result);
    check(bool(owner), "executor owner created"); if (!owner) return;
    auto executor = OwnerTestAccess::executor(*owner); executor.retain(executor.context);
    std::atomic<unsigned> consumed{0};
    // Real task handle with a null engine and an observable shared ownership token.
    // run/release both consume it through the actual ABI; no fake task pointers.
    const auto task = [&] {
        auto* value = new fly_session_task_v2_t;
        value->engine = std::shared_ptr<flynes::session::SessionEngine>(nullptr,
            [&](flynes::session::SessionEngine*) {
                const auto r = executor.cancel_timer(executor.context, 999);
                check(r == FLY_SESSION_V2_OK || r == FLY_SESSION_V2_CLOSED, "task retirement can reenter executor outside queue lock");
                ++consumed;
            });
        return value;
    };
    std::array<std::uint8_t, 32> boot{}; boot[0] = 7;
    check(executor.arm_timer(executor.context, UINT64_MAX, boot.data(), 1, task()) == FLY_SESSION_V2_ACCEPTED, "timer retained");
    check(executor.arm_timer(executor.context, UINT64_MAX, boot.data(), 1, task()) == FLY_SESSION_V2_ACCEPTED && consumed == 1, "timer replacement consumes only replaced task");
    check(executor.cancel_timer(executor.context, 1) == FLY_SESSION_V2_OK && consumed == 2, "cancel consumes task once");
    check(executor.cancel_timer(executor.context, 1) == FLY_SESSION_V2_OK && consumed == 2, "duplicate cancel cannot consume twice");
    check(executor.arm_timer(executor.context, 0, boot.data(), 2, task()) == FLY_SESSION_V2_ACCEPTED, "due timer accepted");
    boot[0] = 8;
    check(executor.arm_timer(executor.context, UINT64_MAX, boot.data(), 3, task()) == FLY_SESSION_V2_ACCEPTED, "different boot timer admitted for retirement");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (consumed < 4 && std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(consumed == 4, "due and old-boot tasks both consumed exactly once");
    boot[0] = 7;
    check(executor.arm_timer(executor.context, UINT64_MAX, boot.data(), 4, task()) == FLY_SESSION_V2_ACCEPTED, "shutdown owns pending timer");
    // Final provider release posts a task after the last drain, exercising the
    // sealed executor's leftover queue retirement, not a synthetic engine state.
    std::atomic<bool> posted_at_release{false};
    ports.on_release = [&] {
        if (posted_at_release.exchange(true)) return;
        auto* late = task();
        const auto posted = executor.post(executor.context, late);
        check(posted == FLY_SESSION_V2_ACCEPTED, "engine destruction may post a final provider task before executor seals");
        if (posted != FLY_SESSION_V2_ACCEPTED) fly_session_task_release_v2(late);
    };
    check(owner->close() == FLY_SESSION_V2_OK && consumed == 6, "shutdown releases timer and final provider task outside lock");
    owner.reset();
    auto* late = task();
    check(executor.post(executor.context, late) == FLY_SESSION_V2_CLOSED && consumed == 6, "late post refused without stealing caller task");
    fly_session_task_release_v2(late);
    check(executor.cancel_timer(executor.context, 4) == FLY_SESSION_V2_CLOSED && consumed == 7, "retained context survives owner safely");
    executor.release(executor.context);
}
void last_worker_reference() {
    Ports ports; fly_session_result_v2 result;
    auto rows = content(0);
    auto owner = SessionOwnerV2::create(ports.table, rows, &result);
    check(bool(owner), "last worker reference owner created"); if (!owner) return;
    auto rescue = OwnerTestAccess::rescue(*owner);
    auto state_expired = OwnerTestAccess::state_expired(*owner);
    auto executor = OwnerTestAccess::executor(*owner); executor.retain(executor.context);
    std::weak_ptr<SessionOwnerV2> weak = owner;
    std::promise<void> destroyed;
    auto destruction = destroyed.get_future();
    auto* task = new fly_session_task_v2_t;
    task->engine = std::shared_ptr<flynes::session::SessionEngine>(nullptr,
        [last = std::move(owner), &destroyed](flynes::session::SessionEngine*) mutable {
            last.reset(); destroyed.set_value();
        });
    check(executor.post(executor.context, task) == FLY_SESSION_V2_ACCEPTED,
          "worker accepts last owner reference task");
    check(destruction.wait_for(std::chrono::seconds(3)) == std::future_status::ready && weak.expired(),
          "last owner destructor returns on worker without self join");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while ((ports.retained != ports.released || executor.cancel_timer(executor.context, 7) != FLY_SESSION_V2_CLOSED) &&
           std::chrono::steady_clock::now() < deadline) std::this_thread::yield();
    check(ports.stopped == 1 && ports.retained == ports.released,
          "last worker reference still shuts down engine and releases providers");
    auto* late = new fly_session_task_v2_t;
    const auto posted = executor.post(executor.context, late);
    check(posted == FLY_SESSION_V2_CLOSED, "late callback rejects after worker owner destruction");
    if (posted != FLY_SESSION_V2_ACCEPTED) fly_session_task_release_v2(late);
    check(executor.cancel_timer(executor.context, 7) == FLY_SESSION_V2_CLOSED,
          "retained executor closes after worker owner destruction");
    check(ports.late_callback() == FLY_SESSION_V2_CLOSED, "retained provider inbox closes after worker owner destruction");
    rescue(); // Required on RED, harmless and joined/finished on GREEN.
    rescue = {};
    const auto state_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!state_expired() && std::chrono::steady_clock::now() < state_deadline) std::this_thread::yield();
    check(state_expired(), "worker run releases State without leak or joinable-thread destruction");
    executor.release(executor.context);
}
void projection(std::uint32_t count) {
    namespace lb = flynes::session::loopback;
    auto rows = content(count); auto table = rows->port();
    lb::LoopbackWorld world;
    lb::EngineFixture inviter(world, lb::LoopbackSide::Initiator, true, false, false, 0, nullptr, &table);
    lb::EngineFixture joiner(world, lb::LoopbackSide::Responder, true, false, false);
    lb::LoopbackTransport transport; lb::PumpState left, right; lb::PumpLimits limits{}; lb::RelayReport report;
    lb::reset_loopback_clock_ns(); inviter.platform.ready(); joiner.platform.ready();
    inviter.executor.run_all(); joiner.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> a, b;
    inviter.snapshot(&a); joiner.snapshot(&b);
    const auto* create = lb::find_action(a, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join = lb::find_action(b, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create && join, "real engine test handshake actions");
    if (create && join) {
        lb::submit(inviter, *create, 1001, false); lb::submit(joiner, *join, 1002, true);
        transport.attach(lb::LoopbackRole::AdvertiserPeripheral, inviter);
        transport.attach(lb::LoopbackRole::ScannerCentral, joiner);
        check(transport.connect_ends() == 2, "test loopback connects two engines");
        lb::pump_engine(inviter, left, limits); lb::pump_engine(joiner, right, limits);
        lb::relay_and_pump_until_idle(transport, inviter, left, joiner, right, limits, 4000, 8, &report, false);
        lb::relay_and_pump_until_idle(transport, inviter, left, joiner, right, limits, 4000, 8, &report, true);
        const auto expected = inviter.game_choices();
        check(expected.size() == count, "actual engine consumed Harmony content port");
        Ports ports; fly_session_result_v2 result;
        auto owner = SessionOwnerV2::create(ports.table, rows, &result);
        check(bool(owner), "projection worker created");
        fly_session_view_v2_t* view = nullptr;
        check(fly_session_acquire_view_v2(inviter.engine, &view) == FLY_SESSION_V2_OK, "retain producing engine view");
        if (owner && view) {
            SessionOwnerV2::Snapshot projected;
            check(OwnerTestAccess::project(*owner, view, &projected) == FLY_SESSION_V2_OK && projected.choices.size() == count,
                  "actual worker projection of zero one three rows");
            if (projected.choices.size() == expected.size()) for (std::size_t i = 0; i < expected.size(); ++i)
                check(std::memcmp(&projected.choices[i], &expected[i], sizeof(expected[i])) == 0,
                      "complete ABI stride including identity and source ref");
            if (count == 3 && projected.choices.size() == 3) {
                ContentPortV2::Ref ref{}; std::copy_n(projected.choices[1].source_choice_ref, 16, ref.begin());
                check(OwnerTestAccess::select(*owner, inviter.engine, view, *rows, ref) == FLY_SESSION_V2_ACCEPTED,
                      "typed owner selection returns second ref to producing engine");
                inviter.executor.run_all();
                check(inviter.snapshot().pending_config_revision != 0, "real engine binds selected config");
                rows->invalidate_policy();
                check(OwnerTestAccess::select(*owner, inviter.engine, view, *rows, ref) == FLY_SESSION_V2_STALE,
                      "source invalidation precedes stale view selection");
            }
        }
        fly_session_view_release_v2(view);
        if (owner) check(owner->close() == FLY_SESSION_V2_OK, "projection owner closes");
    }
    for (auto& action : a) fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : b) fly_session_approval_token_release_v2(action.approval_token);
    lb::shutdown_engine_with_the_pump(inviter, left, limits);
    lb::shutdown_engine_with_the_pump(joiner, right, limits);
    check(lb::failures == 0, "two-engine harness assertions passed");
}
}
int main() { lifecycle(); backpressure(); executor_lifetime(); last_worker_reference(); projection(0); projection(1); projection(3); return harmony_v2_tests::failures ? 1 : 0; }
