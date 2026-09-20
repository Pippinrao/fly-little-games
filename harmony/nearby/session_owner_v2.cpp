#include "session_owner_v2.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <limits>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace flynes::harmony::nearby {
struct SessionOwnerV2::State : std::enable_shared_from_this<State> {
    struct Command {
        std::function<fly_session_result_v2()> work;
        bool done = false;
        fly_session_result_v2 result = FLY_SESSION_V2_CLOSED;
    };
    struct Timer { std::uint64_t deadline; std::array<std::uint8_t, 32> boot; fly_session_task_v2_t* task; };
    // Port context survives its owner when retained by a late external caller.
    struct Context {
        std::atomic<unsigned> references{1};
        std::weak_ptr<State> state;
        static void retain(void* p) { ++static_cast<Context*>(p)->references; }
        static void release(void* p) { auto* c = static_cast<Context*>(p); if (--c->references == 0) delete c; }
        static fly_session_result_v2 post(void* p, fly_session_task_v2_t* task) {
            if (!p || !task) return FLY_SESSION_V2_INVALID_ARGUMENT;
            auto s = static_cast<Context*>(p)->state.lock();
            if (!s) return FLY_SESSION_V2_CLOSED;
            std::lock_guard<std::mutex> lock(s->mutex);
            if (s->executor_closed) return FLY_SESSION_V2_CLOSED;
            if (s->tasks.size() == 64) return FLY_SESSION_V2_BACKPRESSURE;
            try { s->tasks.push_back(task); } catch (...) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
            s->cv.notify_all(); return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 arm(void* p, std::uint64_t deadline, const std::uint8_t* boot,
                                         std::uint64_t id, fly_session_task_v2_t* task) {
            if (!p || !boot || !id || !task) return FLY_SESSION_V2_INVALID_ARGUMENT;
            auto s = static_cast<Context*>(p)->state.lock();
            if (!s) return FLY_SESSION_V2_CLOSED;
            fly_session_task_v2_t* old = nullptr;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                if (s->closing || s->executor_closed) return FLY_SESSION_V2_CLOSED;
                auto it = s->timers.find(id);
                if (it == s->timers.end() && s->timers.size() == 64) return FLY_SESSION_V2_BACKPRESSURE;
                Timer timer{deadline, {}, task}; std::copy_n(boot, 32, timer.boot.begin());
                try {
                    if (it != s->timers.end()) { old = it->second.task; it->second = timer; }
                    else s->timers.emplace(id, timer);
                } catch (...) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
            }
            fly_session_task_release_v2(old);
            s->cv.notify_all(); return FLY_SESSION_V2_ACCEPTED;
        }
        static fly_session_result_v2 cancel(void* p, std::uint64_t id) {
            if (!p || !id) return FLY_SESSION_V2_INVALID_ARGUMENT;
            auto s = static_cast<Context*>(p)->state.lock();
            if (!s) return FLY_SESSION_V2_CLOSED;
            fly_session_task_v2_t* old = nullptr;
            {
                std::lock_guard<std::mutex> lock(s->mutex);
                if (s->executor_closed) return FLY_SESSION_V2_CLOSED;
                auto it = s->timers.find(id);
                if (it != s->timers.end()) { old = it->second.task; s->timers.erase(it); }
            }
            fly_session_task_release_v2(old);
            s->cv.notify_all(); return FLY_SESSION_V2_OK;
        }
    };
    mutable std::mutex mutex;
    std::mutex join_mutex;
    std::condition_variable cv;
    std::deque<std::shared_ptr<Command>> commands;
    std::deque<fly_session_task_v2_t*> tasks;
    std::unordered_map<std::uint64_t, Timer> timers;
    std::thread worker;
    std::thread::id worker_id;
    bool initialized = false, closing = false, executor_closed = false, finished = false;
    bool detached = false; // guarded by join_mutex, only for last-owner worker destruction
    fly_session_result_v2 initialized_result = FLY_SESSION_V2_UNAVAILABLE, close_result = FLY_SESSION_V2_UNAVAILABLE;
    fly_session_v2_t* engine = nullptr;
    std::shared_ptr<ContentPortV2> content;
    fly_session_clock_port_v2 clock{};
    Context* context = nullptr;
    std::uint64_t request_id = 1;
    fly_session_notice_v2 last_action{};
    ~State() { if (context) Context::release(context); }
    bool on_worker() const { std::lock_guard<std::mutex> lock(mutex); return worker_id == std::this_thread::get_id(); }
    void request_shutdown() {
        std::array<std::shared_ptr<Command>, 8> cancelled{};
        {
            std::lock_guard<std::mutex> lock(mutex);
            closing = true;
            std::size_t i = 0;
            for (const auto& command : commands) {
                command->result = FLY_SESSION_V2_CLOSED; command->done = true;
                cancelled[i++] = command;
            }
            commands.clear();
        }
        cv.notify_all(); // callable destructors in cancelled run outside mutex
    }
    fly_session_executor_port_v2 executor() {
        fly_session_executor_port_v2 table{};
        table.struct_size = FLY_SESSION_EXECUTOR_PORT_V2_SIZE; table.abi_version = 2;
        table.context = context; table.retain = Context::retain; table.release = Context::release;
        table.post = Context::post; table.arm_timer = Context::arm; table.cancel_timer = Context::cancel;
        return table;
    }
    void drain() {
        for (unsigned i = 0; i < 64; ++i) {
            fly_session_task_v2_t* task = nullptr;
            { std::lock_guard<std::mutex> lock(mutex); if (tasks.empty()) break; task = tasks.front(); tasks.pop_front(); }
            fly_session_task_run_v2(task); // consumes task
        }
        if (engine) {
            fly_session_notice_v2 notice{};
            notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE; notice.abi_version = 2;
            while (fly_session_read_notice_v2(engine, &notice) == FLY_SESSION_V2_OK)
                if (notice.kind == FLY_SESSION_NOTICE_ACTION_RESULT_V2) last_action = notice;
        }
    }
    void timers_due() {
        bool any = false;
        { std::lock_guard<std::mutex> lock(mutex); any = !timers.empty(); }
        if (!any) return;
        fly_session_clock_sample_v2 sample{};
        sample.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE; sample.abi_version = 2;
        if (clock.read_continuous(clock.context, &sample) != FLY_SESSION_V2_OK || !sample.suspend_inclusive) return;
        // Fixed-size collection, so removing accepted tasks cannot throw/lose ownership.
        std::array<fly_session_task_v2_t*, 64> ready{};
        std::array<fly_session_task_v2_t*, 64> retired{};
        std::size_t size = 0, retired_size = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            for (auto it = timers.begin(); it != timers.end();) {
                if (std::memcmp(it->second.boot.data(), sample.boot_generation, 32) != 0) {
                    retired[retired_size++] = it->second.task; it = timers.erase(it);
                } else if (it->second.deadline <= sample.continuous_ns) {
                    ready[size++] = it->second.task; it = timers.erase(it);
                } else ++it;
            }
        }
        for (std::size_t i = 0; i < retired_size; ++i) fly_session_task_release_v2(retired[i]);
        for (std::size_t i = 0; i < size; ++i) fly_session_task_run_v2(ready[i]);
    }
    void run(fly_session_ports_v2 ports, fly_session_executor_port_v2 executor_port) {
        { std::lock_guard<std::mutex> lock(mutex); worker_id = std::this_thread::get_id(); }
        auto content_port = content->port();
        ports.executor = &executor_port; ports.content = &content_port;
        clock = *ports.clock;
        fly_session_config_v2 config{};
        config.struct_size = FLY_SESSION_CONFIG_V2_SIZE; config.abi_version = 2;
        config.action_queue_capacity = 8; config.notice_queue_capacity = 8;
        const auto created = fly_session_create_v2(&config, &ports, &engine);
        if (created == FLY_SESSION_V2_OK) drain();
        { std::lock_guard<std::mutex> lock(mutex); initialized_result = created; initialized = true; }
        cv.notify_all();
        if (created == FLY_SESSION_V2_OK) {
            for (;;) {
                drain(); timers_due();
                std::shared_ptr<Command> command;
                {
                    std::unique_lock<std::mutex> lock(mutex);
                    if (closing) break;
                    if (!commands.empty()) { command = commands.front(); commands.pop_front(); }
                    else if (!tasks.empty()) continue;
                    else {
                        if (timers.empty()) cv.wait(lock, [&] { return closing || !commands.empty() || !tasks.empty() || !timers.empty(); });
                        else cv.wait_for(lock, std::chrono::milliseconds(10));
                        continue;
                    }
                }
                fly_session_result_v2 result = FLY_SESSION_V2_UNAVAILABLE;
                try { result = command->work(); } catch (...) {}
                { std::lock_guard<std::mutex> lock(mutex); command->result = result; command->done = true; }
                cv.notify_all();
            }
            std::array<fly_session_task_v2_t*, 64> retired{};
            std::size_t retired_size = 0;
            {
                std::lock_guard<std::mutex> lock(mutex);
                for (const auto& item : timers) retired[retired_size++] = item.second.task;
                timers.clear();
            }
            for (std::size_t i = 0; i < retired_size; ++i) fly_session_task_release_v2(retired[i]);
            fly_session_begin_shutdown_v2(engine, request_id);
            for (;;) {
                drain();
                const auto destroyed = fly_session_destroy_v2(engine);
                if (destroyed == FLY_SESSION_V2_OK) { engine = nullptr; close_result = destroyed; break; }
                // Wait for the provider's actual terminal cancellation; no fabricated
                // shutdown success and no destruction while the engine reports BUSY.
                std::unique_lock<std::mutex> lock(mutex);
                if (tasks.empty()) cv.wait_for(lock, std::chrono::milliseconds(10));
            }
            content->close();
        } else close_result = FLY_SESSION_V2_OK;
        std::array<fly_session_task_v2_t*, 64> retired{};
        std::size_t retired_size = 0;
        {
            std::lock_guard<std::mutex> lock(mutex);
            executor_closed = true;
            for (auto* task : tasks) retired[retired_size++] = task;
            tasks.clear();
        }
        for (std::size_t i = 0; i < retired_size; ++i) fly_session_task_release_v2(retired[i]);
        { std::lock_guard<std::mutex> lock(mutex); finished = true; }
        cv.notify_all();
    }
};
SessionOwnerV2::SessionOwnerV2() = default;
SessionOwnerV2::~SessionOwnerV2() {
    const auto s = state_;
    if (!s || !s->on_worker()) { close(); return; }
    // Public worker close remains BUSY, but a destructor cannot reject shutdown.
    // This is the final shared owner: a legal concurrent close retains an owner,
    // so it cannot coexist with this branch. Serialize handle access regardless.
    std::lock_guard<std::mutex> join(s->join_mutex);
    s->request_shutdown();
    if (s->worker.joinable()) { s->worker.detach(); s->detached = true; }
    // run's strong State capture now lives through the normal engine drain; its
    // final release cannot destroy a joinable std::thread on the worker itself.
}
fly_session_executor_port_v2 SessionOwnerV2::executor_port() const { return state_->executor(); }
std::shared_ptr<SessionOwnerV2> SessionOwnerV2::create(const fly_session_ports_v2& ports,
    std::shared_ptr<ContentPortV2> content, fly_session_result_v2* result) {
    if (!result) return {};
    *result = FLY_SESSION_V2_INVALID_ARGUMENT;
    if (ports.struct_size != FLY_SESSION_PORTS_V2_SIZE || ports.abi_version != 2 ||
        !ports.clock || !ports.platform_state || !content || ports.dual_runtime) return {};
    try {
        auto owner = std::shared_ptr<SessionOwnerV2>(new SessionOwnerV2);
        owner->state_ = std::make_shared<State>();
        const auto s = owner->state_;
        s->content = std::move(content); s->context = new State::Context; s->context->state = s;
        const auto executor = owner->executor_port();
        s->worker = std::thread([s, ports, executor] { s->run(ports, executor); });
        { std::unique_lock<std::mutex> lock(s->mutex); s->cv.wait(lock, [&] { return s->initialized; }); *result = s->initialized_result; }
        if (*result != FLY_SESSION_V2_OK) return {};
        return owner;
    } catch (...) { *result = FLY_SESSION_V2_UNAVAILABLE; return {}; }
}
fly_session_result_v2 SessionOwnerV2::call(std::function<fly_session_result_v2()> action) {
    const auto s = state_;
    if (!s) return FLY_SESSION_V2_CLOSED;
    if (s->on_worker()) return FLY_SESSION_V2_BUSY;
    std::unique_lock<std::mutex> lock(s->mutex);
    if (s->closing || s->finished) return FLY_SESSION_V2_CLOSED;
    if (s->commands.size() == 8) return FLY_SESSION_V2_BACKPRESSURE;
    std::shared_ptr<State::Command> command;
    try { command = std::make_shared<State::Command>(); command->work = std::move(action); s->commands.push_back(command); }
    catch (...) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    s->cv.notify_all(); s->cv.wait(lock, [&] { return command->done; });
    return command->result;
}
fly_session_result_v2 SessionOwnerV2::project(const fly_session_view_v2_t* view, Snapshot* out) {
    if (!view || !out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    Snapshot next;
    next.session.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE; next.session.abi_version = 2;
    auto result = fly_session_view_read_v2(view, &next.session);
    if (result != FLY_SESSION_V2_OK) return result;
    if (next.session.game_choice_count > 4096) return FLY_SESSION_V2_UNAVAILABLE;
    next.choices.resize(next.session.game_choice_count);
    if (!next.choices.empty()) {
        next.choices[0].struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE; next.choices[0].abi_version = 2;
        std::uint32_t written = 0;
        result = fly_session_view_copy_game_choices_v2(view, 0, next.choices.data(), static_cast<std::uint32_t>(next.choices.size()), &written);
        if (result != FLY_SESSION_V2_OK || written != next.choices.size()) return FLY_SESSION_V2_UNAVAILABLE;
    }
    *out = std::move(next); return FLY_SESSION_V2_OK;
}
fly_session_result_v2 SessionOwnerV2::read(Snapshot* out) {
    if (!out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto s = state_;
    return call([s, out] {
        fly_session_view_v2_t* raw = nullptr;
        auto result = fly_session_acquire_view_v2(s->engine, &raw);
        if (result != FLY_SESSION_V2_OK) return result;
        std::unique_ptr<fly_session_view_v2_t, decltype(&fly_session_view_release_v2)> view(raw, fly_session_view_release_v2);
        result = project(view.get(), out);
        if (result == FLY_SESSION_V2_OK) { out->content_query = s->content->query_status(); out->last_action = s->last_action; }
        return result;
    });
}
fly_session_result_v2 SessionOwnerV2::select_view(fly_session_v2_t* engine, const fly_session_view_v2_t* view,
    const ContentPortV2& content, const ContentPortV2::Ref& ref, std::uint64_t request_id) {
    ContentPortV2::Selection selection;
    if (!content.resolve(ref, &selection)) return FLY_SESSION_V2_STALE;
    Snapshot projected;
    const auto result = project(view, &projected);
    if (result != FLY_SESSION_V2_OK) return result;
    const auto chosen = std::find_if(projected.choices.begin(), projected.choices.end(), [&](const auto& row) {
        return row.selectable && std::memcmp(row.source_choice_ref, ref.data(), 16) == 0;
    });
    if (chosen == projected.choices.end()) return FLY_SESSION_V2_UNAVAILABLE;
    if (projected.session.action_count > 256) return FLY_SESSION_V2_UNAVAILABLE;
    std::vector<fly_session_action_descriptor_v2> actions(projected.session.action_count);
    std::uint32_t written = 0;
    const auto copied = fly_session_view_copy_actions_v2(view, 0, actions.data(), static_cast<std::uint32_t>(actions.size()), &written);
    if (copied != FLY_SESSION_V2_OK) return copied;
    for (std::uint32_t i = 0; i < written; ++i) if (actions[i].action_kind == FLY_SESSION_ACTION_SELECT_CONTENT_V2 &&
        actions[i].enabled && actions[i].approval_token) {
        fly_session_action_v2 action{};
        action.struct_size = FLY_SESSION_ACTION_V2_SIZE; action.abi_version = 2;
        action.request_id = request_id; action.expected_view_revision = projected.session.view_revision;
        action.approval_token = actions[i].approval_token; // borrowed until retained view is released
        action.choice_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE;
        action.choice.struct_size = FLY_SESSION_ACTION_CHOICE_V2_SIZE; action.choice.abi_version = 2;
        action.choice.choice_kind = FLY_SESSION_CHOICE_REFERENCE_V2;
        std::copy(ref.begin(), ref.end(), action.choice.choice_id);
        return fly_session_submit_action_v2(engine, &action);
    }
    return FLY_SESSION_V2_UNAVAILABLE;
}
fly_session_result_v2 SessionOwnerV2::select(const ContentPortV2::Ref& ref) {
    const auto s = state_;
    return call([s, ref] {
        if (s->request_id == std::numeric_limits<std::uint64_t>::max()) return fly_session_result_v2(FLY_SESSION_V2_UNAVAILABLE);
        fly_session_view_v2_t* raw = nullptr;
        auto result = fly_session_acquire_view_v2(s->engine, &raw);
        if (result != FLY_SESSION_V2_OK) return result;
        std::unique_ptr<fly_session_view_v2_t, decltype(&fly_session_view_release_v2)> view(raw, fly_session_view_release_v2);
        return select_view(s->engine, view.get(), *s->content, ref, s->request_id++);
    });
}
fly_session_result_v2 SessionOwnerV2::close() {
    const auto s = state_;
    if (!s) return FLY_SESSION_V2_OK;
    if (s->on_worker()) return FLY_SESSION_V2_BUSY;
    std::lock_guard<std::mutex> join(s->join_mutex);
    s->request_shutdown();
    if (s->worker.joinable()) s->worker.join();
    else if (s->detached) {
        std::unique_lock<std::mutex> lock(s->mutex);
        s->cv.wait(lock, [&] { return s->finished; });
    }
    return s->close_result;
}
}
