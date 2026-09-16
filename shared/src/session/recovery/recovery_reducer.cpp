#include "recovery_reducer.hpp"

namespace flynes::session::recovery {
namespace {

ChildPhaseV1 merge_children(ChildPhaseV1 local, ChildPhaseV1 peer,
                            bool paused) noexcept
{
    if (paused && local == ChildPhaseV1::Active && peer == ChildPhaseV1::Active)
        return ChildPhaseV1::Active;
    if (local == ChildPhaseV1::RepairBlocked ||
        peer == ChildPhaseV1::RepairBlocked)
        return ChildPhaseV1::RepairBlocked;
    if (local == ChildPhaseV1::None && peer == ChildPhaseV1::None)
        return ChildPhaseV1::None;
    if (local == ChildPhaseV1::None && peer == ChildPhaseV1::Pending)
        return ChildPhaseV1::Pending;
    if (local == ChildPhaseV1::None && peer == ChildPhaseV1::Active)
        return ChildPhaseV1::RepairBlocked;
    if ((local == ChildPhaseV1::Ended &&
         (peer == ChildPhaseV1::Active || peer == ChildPhaseV1::Ending ||
          peer == ChildPhaseV1::Ended)) ||
        (peer == ChildPhaseV1::Ended &&
         (local == ChildPhaseV1::Active || local == ChildPhaseV1::Ending ||
          local == ChildPhaseV1::Ended)))
        return ChildPhaseV1::Ended;
    if (local == ChildPhaseV1::Active && peer == ChildPhaseV1::Active)
        return ChildPhaseV1::Frozen;
    if (local == ChildPhaseV1::AbortedPrestart && peer == ChildPhaseV1::None)
        return ChildPhaseV1::None;
    if (local == ChildPhaseV1::Pending && peer == ChildPhaseV1::Ended)
        return ChildPhaseV1::RepairBlocked;
    if (local == ChildPhaseV1::Ended && peer == ChildPhaseV1::Pending)
        return ChildPhaseV1::RepairBlocked;
    return local;
}

} // namespace

fly_session_result_v2 RecoveryReducerV1::genesis_link() noexcept
{
    const auto born = ledger.genesis();
    if (born != FLY_SESSION_V2_OK)
        return born;
    local_ = ChildPhaseV1::None;
    peer_ = ChildPhaseV1::None;
    resume_ = ResumePhaseV1::Idle;
    paused_ = false;
    prelude_complete_ = false;
    end_proof_ = false;
    close_proof_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::grant_and_start() noexcept
{
    if (local_ == ChildPhaseV1::Ended && end_proof_)
        return FLY_SESSION_V2_INVALID_STATE;
    if (!ledger.source_writer_alive())
        return FLY_SESSION_V2_INVALID_STATE;
    const auto from0 =
        ledger.seat(0).reserved_through == 0 ? 1u : ledger.seat(0).reserved_through + 1u;
    const auto from1 =
        ledger.seat(1).reserved_through == 0 ? 1u : ledger.seat(1).reserved_through + 1u;
    auto granted = ledger.grant(0, from0, from0 + 99u, ReservationKindV1::Normal);
    if (granted != FLY_SESSION_V2_OK)
        return granted;
    granted = ledger.grant(1, from1, from1 + 99u, ReservationKindV1::Normal);
    if (granted != FLY_SESSION_V2_OK)
        return granted;
    local_ = ChildPhaseV1::Active;
    paused_ = false;
    end_proof_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::step_game() noexcept
{
    if (!may_step())
        return FLY_SESSION_V2_INVALID_STATE;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::request_stream() noexcept
{
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 RecoveryReducerV1::pause_game() noexcept
{
    if (local_ != ChildPhaseV1::Active)
        return FLY_SESSION_V2_INVALID_STATE;
    paused_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::end_game() noexcept
{
    if (local_ != ChildPhaseV1::Active && local_ != ChildPhaseV1::Frozen)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto closed = ledger.close_child();
    if (closed != FLY_SESSION_V2_OK)
        return closed;
    close_proof_ = true;
    end_proof_ = true;
    local_ = ChildPhaseV1::Ended;
    paused_ = false;
    return ledger.release_source_writer();
}

fly_session_result_v2 RecoveryReducerV1::export_user_save() noexcept
{
    if (!end_proof_ || !close_proof_ || local_ != ChildPhaseV1::Ended)
        return FLY_SESSION_V2_INVALID_STATE;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::close_then_handoff() noexcept
{
    if (!ledger.closed())
    {
        const auto closed = ledger.close_child();
        if (closed != FLY_SESSION_V2_OK)
            return closed;
        close_proof_ = true;
    }
    return ledger.handoff_successor();
}

fly_session_result_v2 RecoveryReducerV1::abort_prestart() noexcept
{
    const auto aborted = ledger.abort_prestart();
    if (aborted != FLY_SESSION_V2_OK)
        return aborted;
    local_ = ChildPhaseV1::AbortedPrestart;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::note_clock(
    std::uint64_t continuous_ns) noexcept
{
    last_clock_ns_ = continuous_ns;
    const auto state = watch.on_clock(continuous_ns);
    if (state != LinkActivityStateV1::Live && local_ == ChildPhaseV1::Active)
        local_ = ChildPhaseV1::Frozen;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::reconnect(
    std::uint64_t now_ns, std::uint64_t clock_generation,
    ChildPhaseV1 peer_phase) noexcept
{
    peer_ = peer_phase;
    if (clock_generation != clock_generation_ && watch.reconnect_expired())
        return FLY_SESSION_V2_INVALID_STATE;
    resume_ = ResumePhaseV1::RouteOnly;
    const bool needs_tail =
        local_ == ChildPhaseV1::Active || local_ == ChildPhaseV1::Frozen ||
        local_ == ChildPhaseV1::Ending || local_ == ChildPhaseV1::Ended ||
        peer_phase == ChildPhaseV1::Active ||
        peer_phase == ChildPhaseV1::Ending ||
        peer_phase == ChildPhaseV1::Ended;
    if (needs_tail)
    {
        resume_ = ResumePhaseV1::TailPrelude;
        prelude_complete_ = true;
        resume_ = ResumePhaseV1::ReadRoot;
    }
    else
        prelude_complete_ = true;
    resume_ = ResumePhaseV1::Reconcile;
    local_ = merge_children(local_, peer_phase, paused_);
    if (local_ == ChildPhaseV1::RepairBlocked)
        return FLY_SESSION_V2_OK;
    if (local_ == ChildPhaseV1::Frozen)
        return FLY_SESSION_V2_OK;
    resume_ = ResumePhaseV1::Ready;
    last_clock_ns_ = now_ns;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::publish_root(const std::uint8_t* bytes,
                                                     std::size_t size,
                                                     CrashPointV1 crash)
    noexcept
{
    store.inject_crash(crash);
    const auto published =
        store.cas_replace(expected_root_revision_, expected_root_hash_, bytes,
                          size);
    if (published != FLY_SESSION_V2_OK)
        return published;
    RootViewV1 view{};
    const auto read = store.read_root(&view);
    if (read != FLY_SESSION_V2_OK)
        return read;
    expected_root_revision_ = view.revision;
    expected_root_hash_ = view.hash;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 RecoveryReducerV1::cas_after_conflict() noexcept
{
    RootViewV1 view{};
    const auto read = store.read_root(&view);
    if (read != FLY_SESSION_V2_OK)
        return read;
    expected_root_revision_ = view.revision;
    expected_root_hash_ = view.hash;
    return FLY_SESSION_V2_OK;
}

} // namespace flynes::session::recovery
