#pragma once
#include "content_port_v2.hpp"
#include <functional>
#include <memory>

namespace flynes::harmony::nearby {
class OwnerTestAccess;
// Dormant component: no NAPI/UI composition, runtime, media or platform-ready claims.
class SessionOwnerV2 final {
public:
    struct Snapshot {
        fly_session_snapshot_v2 session{};
        std::vector<fly_session_game_choice_v2> choices;
        ContentPortV2::QueryStatus content_query;
        fly_session_notice_v2 last_action{};
        bool runtime_registered = false;
    };
    // Synchronous creation copies/retains injected tables before returning. Missing
    // mandatory ports fail closed. executor/content are supplied by this owner;
    // dual_runtime must be absent. Providers must honour cancellation/lifetime ABI.
    static std::shared_ptr<SessionOwnerV2> create(const fly_session_ports_v2&,
                                               std::shared_ptr<ContentPortV2>,
                                               fly_session_result_v2* result);
    ~SessionOwnerV2();
    SessionOwnerV2(const SessionOwnerV2&) = delete;
    SessionOwnerV2& operator=(const SessionOwnerV2&) = delete;
    fly_session_result_v2 read(Snapshot*);
    // Returns engine admission; does not mean selection or a game has completed.
    fly_session_result_v2 select(const ContentPortV2::Ref&);
    // Safe concurrent close with active calls if callers retain this shared owner.
    // Provider/worker reentrancy is rejected with BUSY, never waits on itself.
    // Last shared-owner destruction on the worker instead requests shutdown and
    // relinquishes its thread handle; retained State drains the engine to finish.
    fly_session_result_v2 close();
private:
    friend class OwnerTestAccess; // private host test access, no product entry point
    struct State;
    std::shared_ptr<State> state_;
    SessionOwnerV2();
    fly_session_executor_port_v2 executor_port() const;
    fly_session_result_v2 call(std::function<fly_session_result_v2()>);
    static fly_session_result_v2 project(const fly_session_view_v2_t*, Snapshot*);
    static fly_session_result_v2 select_view(fly_session_v2_t*, const fly_session_view_v2_t*,
        const ContentPortV2&, const ContentPortV2::Ref&, std::uint64_t request_id);
};
}
