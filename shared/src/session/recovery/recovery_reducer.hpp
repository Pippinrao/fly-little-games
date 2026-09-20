#ifndef FLYNES_SESSION_RECOVERY_RECOVERY_REDUCER_HPP
#define FLYNES_SESSION_RECOVERY_RECOVERY_REDUCER_HPP

/*
 * Task 12 / REC-DUAL step 4: reconnect, handoff, crash and merge reducer.
 * STREAM is unavailable in this release; a failed STREAM leave DUAL frozen.
 */

#include "durable_root_store.hpp"
#include "input_sequence_ledger.hpp"
#include "link_activity_watchdog.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace flynes::session::recovery {

enum class ChildPhaseV1 : std::uint8_t
{
    None = 1,
    Pending = 2,
    Active = 3,
    Frozen = 4,
    Ending = 5,
    Ended = 6,
    AbortedPrestart = 7,
    RepairBlocked = 8
};

enum class ResumePhaseV1 : std::uint8_t
{
    Idle = 1,
    RouteOnly = 2,
    TailPrelude = 3,
    ReadRoot = 4,
    Reconcile = 5,
    Ready = 6
};

class RecoveryReducerV1 final
{
public:
    DurableRootStoreV1 store{};
    InputSequenceLedgerV1 ledger{};
    LinkActivityWatchdogV1 watch{};

    fly_session_result_v2 genesis_link() noexcept;
    fly_session_result_v2 grant_and_start() noexcept;
    fly_session_result_v2 step_game() noexcept;
    fly_session_result_v2 request_stream() noexcept;
    fly_session_result_v2 pause_game() noexcept;
    fly_session_result_v2 end_game() noexcept;
    fly_session_result_v2 export_user_save() noexcept;
    fly_session_result_v2 close_then_handoff() noexcept;
    fly_session_result_v2 abort_prestart() noexcept;
    fly_session_result_v2 note_clock(std::uint64_t continuous_ns) noexcept;
    fly_session_result_v2 reconnect(std::uint64_t now_ns,
                                    std::uint64_t clock_generation,
                                    ChildPhaseV1 peer_phase) noexcept;
    fly_session_result_v2 publish_root(const std::uint8_t* bytes,
                                       std::size_t size,
                                       CrashPointV1 crash) noexcept;
    fly_session_result_v2 cas_after_conflict() noexcept;
    void install_child_fixture(ChildPhaseV1 local) noexcept { local_ = local; }

    [[nodiscard]] ChildPhaseV1 local_phase() const noexcept { return local_; }
    [[nodiscard]] ChildPhaseV1 peer_phase() const noexcept { return peer_; }
    [[nodiscard]] ResumePhaseV1 resume_phase() const noexcept { return resume_; }
    [[nodiscard]] bool paused() const noexcept { return paused_; }
    [[nodiscard]] bool prelude_complete() const noexcept
    {
        return prelude_complete_;
    }
    [[nodiscard]] bool ready_bound() const noexcept
    {
        return resume_ == ResumePhaseV1::Ready;
    }
    [[nodiscard]] bool may_step() const noexcept
    {
        return local_ == ChildPhaseV1::Active && !paused_ &&
               !watch.frozen() && ledger.may_start();
    }

private:
    ChildPhaseV1 local_ = ChildPhaseV1::None;
    ChildPhaseV1 peer_ = ChildPhaseV1::None;
    ResumePhaseV1 resume_ = ResumePhaseV1::Idle;
    bool paused_ = false;
    bool prelude_complete_ = false;
    bool end_proof_ = false;
    bool close_proof_ = false;
    std::uint64_t clock_generation_ = 1;
    std::uint64_t last_clock_ns_ = 0;
    std::uint64_t expected_root_revision_ = 0;
    std::array<std::uint8_t, 32> expected_root_hash_{};
};

} // namespace flynes::session::recovery

#endif
