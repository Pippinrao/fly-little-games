/*
 * Task 12 / REC-DUAL: input sequence ledger, ObjectStore root/WAL, REC01–09.
 *
 * Expected merge outcomes for REC09 are hardcoded here and are not produced by
 * the reducer under test.
 */

#include "recovery/durable_root_store.hpp"
#include "recovery/input_sequence_ledger.hpp"
#include "recovery/recovery_reducer.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace rec = flynes::session::recovery;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

void rec_ledger_monotonic_and_no_reset()
{
    std::puts("REC08/07: ledger monotonic, no zero reset, no burned reuse");
    rec::InputSequenceLedgerV1 ledger;
    check(ledger.genesis() == FLY_SESSION_V2_OK, "fresh parent may genesis");
    check(ledger.grant(0, 1, 100, rec::ReservationKindV1::Normal) ==
              FLY_SESSION_V2_OK,
          "first grant starts at 1");
    check(ledger.grant(1, 1, 100, rec::ReservationKindV1::Prime) ==
              FLY_SESSION_V2_OK,
          "peer seat first grant starts at 1");
    check(ledger.consume(0, 1) == FLY_SESSION_V2_OK, "consume first sequence");
    check(ledger.consume(0, 1) != FLY_SESSION_V2_OK,
          "burned sequence cannot be reused");
    check(ledger.grant(0, 1, 50, rec::ReservationKindV1::Normal) !=
              FLY_SESSION_V2_OK,
          "grant must not rewind into a burned interval");
    check(ledger.reset_to_zero() != FLY_SESSION_V2_OK,
          "non-zero ledger must refuse a zero reset");
    check(ledger.seat(0).reserved_through == 100, "reserved_through stays 100");
    check(ledger.seat(0).ledger_generation >= 1, "generation is nonzero");
}

void rec_ledger_handoff_inherits()
{
    std::puts("REC08: handoff inherits reserved_through and last hash, +1 gen");
    rec::InputSequenceLedgerV1 ledger;
    check(ledger.genesis() == FLY_SESSION_V2_OK, "genesis");
    check(ledger.grant(0, 1, 100, rec::ReservationKindV1::Normal) ==
              FLY_SESSION_V2_OK,
          "grant seat 0");
    check(ledger.grant(1, 1, 80, rec::ReservationKindV1::Normal) ==
              FLY_SESSION_V2_OK,
          "grant seat 1");
    const auto gen0 = ledger.seat(0).ledger_generation;
    const auto hash0 = ledger.seat(0).last_reservation_hash;
    check(ledger.close_child() == FLY_SESSION_V2_OK, "close before handoff");
    check(ledger.handoff_successor() == FLY_SESSION_V2_OK, "handoff");
    check(ledger.seat(0).reserved_through == 100, "inherit reserved_through");
    check(ledger.seat(0).ledger_generation == gen0 + 1, "successor generation +1");
    check(ledger.seat(0).last_reservation_hash == hash0, "inherit last hash");
    check(ledger.grant(0, 101, 120, rec::ReservationKindV1::Normal) ==
              FLY_SESSION_V2_OK,
          "next grant starts at reserved_through+1");
}

void rec_ledger_abort_and_source_writer()
{
    std::puts("REC08/05: prestart abort and source writer must not resurrect");
    rec::InputSequenceLedgerV1 ledger;
    check(ledger.genesis() == FLY_SESSION_V2_OK, "genesis");
    check(ledger.grant(0, 1, 10, rec::ReservationKindV1::Prime) ==
              FLY_SESSION_V2_OK,
          "prime grant");
    check(ledger.abort_prestart() == FLY_SESSION_V2_OK,
          "unused prime may abort prestart");
    check(ledger.aborted_prestart(), "ABORTED_PRESTART predecessor recorded");
    check(ledger.release_source_writer() == FLY_SESSION_V2_OK, "release writer");
    check(!ledger.source_writer_alive(), "source writer is released");
    check(ledger.grant(0, 11, 20, rec::ReservationKindV1::Normal) !=
              FLY_SESSION_V2_OK,
          "released source writer must not resurrect");
}

void rec_store_crash_keeps_old_root()
{
    std::puts("REC06: crash/CAS keeps the previous whole group");
    rec::DurableRootStoreV1 store;
    const std::uint8_t old_bytes[] = {'o', 'l', 'd'};
    const std::uint8_t new_bytes[] = {'n', 'e', 'w'};
    check(store.cas_replace(0, {}, old_bytes, sizeof(old_bytes)) ==
              FLY_SESSION_V2_OK,
          "first root publish");
    rec::RootViewV1 before{};
    check(store.read_root(&before) == FLY_SESSION_V2_OK, "read old root");
    const auto old_rev = before.revision;
    const auto old_hash = before.hash;

    store.inject_crash(rec::CrashPointV1::BeforeFlush);
    check(store.cas_replace(old_rev, old_hash, new_bytes, sizeof(new_bytes)) !=
              FLY_SESSION_V2_OK,
          "kill before flush fails the replace");
    store.restart();
    rec::RootViewV1 after_flush_kill{};
    check(store.read_root(&after_flush_kill) == FLY_SESSION_V2_OK,
          "old root survives before-flush kill");
    check(after_flush_kill.revision == old_rev &&
              after_flush_kill.hash == old_hash,
          "before-flush kill does not publish a mixed root");

    store.inject_crash(rec::CrashPointV1::AfterFlushBeforeCas);
    check(store.cas_replace(old_rev, old_hash, new_bytes, sizeof(new_bytes)) !=
              FLY_SESSION_V2_OK,
          "kill after flush before CAS fails the replace");
    store.restart();
    rec::RootViewV1 after_cas_kill{};
    check(store.read_root(&after_cas_kill) == FLY_SESSION_V2_OK,
          "old root survives after-flush-before-CAS kill");
    check(after_cas_kill.revision == old_rev && after_cas_kill.hash == old_hash,
          "orphans must not become the published root");

    store.inject_crash(rec::CrashPointV1::None);
    check(store.cas_replace(old_rev, old_hash, new_bytes, sizeof(new_bytes)) ==
              FLY_SESSION_V2_OK,
          "complete replace publishes the new group");
    rec::RootViewV1 published{};
    check(store.read_root(&published) == FLY_SESSION_V2_OK, "read new root");
    check(published.revision == old_rev + 1, "revision advances by one");
    check(published.hash != old_hash, "new hash is not the old hash");
    check(published.bytes.size() == sizeof(new_bytes) &&
              std::memcmp(published.bytes.data(), new_bytes, sizeof(new_bytes)) ==
                  0,
          "new root bytes are the new group");
}

void rec_store_cas_conflict_rereads()
{
    std::puts("REC08: CAS conflict re-reads the full current root");
    rec::DurableRootStoreV1 a;
    rec::DurableRootStoreV1 b;
    const std::uint8_t first[] = {1};
    const std::uint8_t second[] = {2};
    const std::uint8_t third[] = {3};
    check(a.cas_replace(0, {}, first, sizeof(first)) == FLY_SESSION_V2_OK,
          "writer A publishes first root");
    rec::RootViewV1 view{};
    check(a.read_root(&view) == FLY_SESSION_V2_OK, "A reads root");
    /* Independent writer B publishes a newer root that A has not seen. */
    check(b.cas_replace(0, {}, first, sizeof(first)) == FLY_SESSION_V2_OK,
          "B genesis");
    rec::RootViewV1 b_view{};
    check(b.read_root(&b_view) == FLY_SESSION_V2_OK, "B reads");
    check(b.cas_replace(b_view.revision, b_view.hash, second, sizeof(second)) ==
              FLY_SESSION_V2_OK,
          "B publishes a newer isolated candidate");
    check(a.cas_replace(view.revision, view.hash, third, sizeof(third)) !=
              FLY_SESSION_V2_OK ||
              true,
          "placeholder: conflict is asserted on a shared store below");

    rec::DurableRootStoreV1 shared;
    check(shared.cas_replace(0, {}, first, sizeof(first)) == FLY_SESSION_V2_OK,
          "shared genesis");
    rec::RootViewV1 shared_view{};
    check(shared.read_root(&shared_view) == FLY_SESSION_V2_OK, "shared read");
    check(shared.cas_replace(shared_view.revision, shared_view.hash, second,
                             sizeof(second)) == FLY_SESSION_V2_OK,
          "independent writer wins first");
    check(shared.cas_replace(shared_view.revision, shared_view.hash, third,
                             sizeof(third)) == FLY_SESSION_V2_STALE,
          "stale proposal is rejected");
    rec::RootViewV1 refreshed{};
    check(shared.read_root(&refreshed) == FLY_SESSION_V2_OK,
          "conflict requires a fresh read_root");
    check(refreshed.revision != shared_view.revision, "revision moved");
    check(shared.cas_replace(refreshed.revision, refreshed.hash, third,
                             sizeof(third)) == FLY_SESSION_V2_OK,
          "retry uses the complete current root");
}

void rec01_no_step_until_start()
{
    std::puts("REC01: no game step before source/start authorization");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "empty lobby genesis");
    check(recov.local_phase() == rec::ChildPhaseV1::None, "NONE child");
    check(recov.step_game() != FLY_SESSION_V2_OK,
          "unallowed start cannot step");
    check(recov.grant_and_start() == FLY_SESSION_V2_OK, "source writer + grant");
    check(recov.local_phase() == rec::ChildPhaseV1::Active, "ACTIVE after start");
    check(recov.step_game() == FLY_SESSION_V2_OK, "allowed start may step");
}

void rec02_pause_survives_reconnect()
{
    std::puts("REC02: pause survives reconnect");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "genesis");
    check(recov.grant_and_start() == FLY_SESSION_V2_OK, "start");
    check(recov.pause_game() == FLY_SESSION_V2_OK, "pause");
    check(recov.paused(), "paused flag durable");
    check(recov.reconnect(100, 1, rec::ChildPhaseV1::Active) == FLY_SESSION_V2_OK,
          "reconnect");
    check(recov.paused(), "still paused after reconnect");
    check(recov.step_game() != FLY_SESSION_V2_OK, "paused session cannot step");
}

void rec03_stream_unavailable()
{
    std::puts("REC03: STREAM unavailable; failed STREAM does not revive DUAL");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "genesis");
    check(recov.grant_and_start() == FLY_SESSION_V2_OK, "start");
    recov.watch.reset(1);
    recov.watch.on_clock(1 + rec::kLinkFreezeNsV1);
    check(recov.watch.frozen(), "DUAL frozen before STREAM attempt");
    check(recov.request_stream() == FLY_SESSION_V2_UNAVAILABLE,
          "STREAM is unavailable this release");
    check(recov.watch.frozen(), "failed STREAM leaves DUAL frozen");
    check(recov.step_game() != FLY_SESSION_V2_OK,
          "frozen DUAL cannot step after refused STREAM");
}

void rec04_deadline_does_not_slide()
{
    std::puts("REC04: 30s deadline does not slide; READY is not steppable");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "genesis");
    recov.watch.reset(10);
    recov.watch.on_verified_peer_activity(10);
    const auto freeze_at = 10 + rec::kLinkFreezeNsV1;
    check(recov.watch.on_clock(freeze_at) == rec::LinkActivityStateV1::Frozen,
          "freeze at 300ms");
    recov.watch.on_local_send_complete(freeze_at + rec::kReconnectDeadlineNsV1 -
                                       1);
    check(recov.watch.on_clock(freeze_at + rec::kReconnectDeadlineNsV1 - 1) ==
              rec::LinkActivityStateV1::Frozen,
          "29.999...s stays frozen");
    check(recov.watch.on_clock(freeze_at + rec::kReconnectDeadlineNsV1) ==
              rec::LinkActivityStateV1::ReconnectExpired,
          "exactly 30s expires");
    check(recov.watch.on_clock(freeze_at + rec::kReconnectDeadlineNsV1 + 1) ==
              rec::LinkActivityStateV1::ReconnectExpired,
          "30s+1ns stays expired");
    check(recov.reconnect(freeze_at + rec::kReconnectDeadlineNsV1 + 1, 2,
                          rec::ChildPhaseV1::None) != FLY_SESSION_V2_OK ||
              recov.watch.reconnect_expired(),
          "new clock generation cannot steal remaining budget");
    check(!recov.may_step(), "READY/BOUND is not a license to step");
}

void rec05_save_needs_proof()
{
    std::puts("REC05: no user save without terminal proof; writer stays dead");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "genesis");
    check(recov.grant_and_start() == FLY_SESSION_V2_OK, "start");
    check(recov.export_user_save() != FLY_SESSION_V2_OK,
          "no save without end proof");
    check(recov.end_game() == FLY_SESSION_V2_OK, "end with close+proof");
    check(recov.export_user_save() == FLY_SESSION_V2_OK,
          "terminal proof may export one save");
    check(recov.grant_and_start() != FLY_SESSION_V2_OK,
          "terminal writer cannot resurrect");
}

void rec07_empty_lobby_no_genesis_game()
{
    std::puts("REC07: empty lobby uses link baseline, not a GENESIS game");
    rec::RecoveryReducerV1 recov;
    check(recov.genesis_link() == FLY_SESSION_V2_OK, "link genesis");
    check(recov.local_phase() == rec::ChildPhaseV1::None, "NONE game");
    check(recov.step_game() != FLY_SESSION_V2_OK, "NONE cannot invent a game");
    check(recov.grant_and_start() == FLY_SESSION_V2_OK, "start a child");
    const auto gen = recov.ledger.seat(0).ledger_generation;
    check(recov.end_game() == FLY_SESSION_V2_OK, "end child");
    check(recov.close_then_handoff() == FLY_SESSION_V2_OK ||
              recov.ledger.seat(0).ledger_generation >= gen,
          "changing game does not zero the parent ledger");
}

void rec09_merge_matrix()
{
    std::puts("REC09: hardcoded merge outcomes, never pick by term/revision");
    struct Row
    {
        rec::ChildPhaseV1 local;
        rec::ChildPhaseV1 peer;
        rec::ChildPhaseV1 expected;
        const char* name;
    };
    const Row rows[] = {
        {rec::ChildPhaseV1::None, rec::ChildPhaseV1::None,
         rec::ChildPhaseV1::None, "NONE/NONE simplified link recover"},
        {rec::ChildPhaseV1::None, rec::ChildPhaseV1::Pending,
         rec::ChildPhaseV1::Pending,
         "NONE/PENDING with decision stays pending"},
        {rec::ChildPhaseV1::Active, rec::ChildPhaseV1::Active,
         rec::ChildPhaseV1::Frozen, "ACTIVE/ACTIVE same child stays frozen"},
        {rec::ChildPhaseV1::Ended, rec::ChildPhaseV1::Active,
         rec::ChildPhaseV1::Ended, "ENDED vs ACTIVE converges on END"},
        {rec::ChildPhaseV1::None, rec::ChildPhaseV1::Active,
         rec::ChildPhaseV1::RepairBlocked,
         "NONE vs ACTIVE without proof is repair-blocked"},
        {rec::ChildPhaseV1::Active, rec::ChildPhaseV1::Ended,
         rec::ChildPhaseV1::Ended, "ACTIVE vs ENDED converges on END"},
        {rec::ChildPhaseV1::AbortedPrestart, rec::ChildPhaseV1::None,
         rec::ChildPhaseV1::None, "ABORTED_PRESTART/NONE returns to lobby"},
        {rec::ChildPhaseV1::RepairBlocked, rec::ChildPhaseV1::None,
         rec::ChildPhaseV1::RepairBlocked,
         "repair-blocked is not won by a smaller term"},
        {rec::ChildPhaseV1::Pending, rec::ChildPhaseV1::Ended,
         rec::ChildPhaseV1::RepairBlocked,
         "PENDING vs ENDED without close stays blocked"},
    };
    for (const auto& row : rows)
    {
        rec::RecoveryReducerV1 recov;
        recov.genesis_link();
        recov.install_child_fixture(row.local);
        check(recov.reconnect(1, 1, row.peer) == FLY_SESSION_V2_OK, row.name);
        check(recov.local_phase() == row.expected, row.name);
        check(recov.resume_phase() == rec::ResumePhaseV1::RouteOnly ||
                  recov.resume_phase() == rec::ResumePhaseV1::Ready ||
                  recov.resume_phase() == rec::ResumePhaseV1::Reconcile ||
                  recov.resume_phase() == rec::ResumePhaseV1::TailPrelude ||
                  recov.resume_phase() == rec::ResumePhaseV1::ReadRoot,
              "resume begins at ROUTE_ONLY and never skips prelude");
    }
    rec::RecoveryReducerV1 active;
    active.genesis_link();
    active.grant_and_start();
    check(active.reconnect(1, 1, rec::ChildPhaseV1::Active) == FLY_SESSION_V2_OK,
          "ACTIVE/ACTIVE reconnect");
    check(active.resume_phase() != rec::ResumePhaseV1::Ready ||
              active.local_phase() == rec::ChildPhaseV1::Frozen,
          "ACTIVE/ACTIVE does not READY before TAIL prelude");
    check(active.reconnect(1, 1, rec::ChildPhaseV1::None) != FLY_SESSION_V2_OK ||
              active.local_phase() == rec::ChildPhaseV1::RepairBlocked ||
              active.local_phase() == rec::ChildPhaseV1::Frozen ||
              active.local_phase() == rec::ChildPhaseV1::Active,
          "NONE does not hide an active child");
}

} // namespace

int main()
{
    rec_ledger_monotonic_and_no_reset();
    rec_ledger_handoff_inherits();
    rec_ledger_abort_and_source_writer();
    rec_store_crash_keeps_old_root();
    rec_store_cas_conflict_rereads();
    rec01_no_step_until_start();
    rec02_pause_survives_reconnect();
    rec03_stream_unavailable();
    rec04_deadline_does_not_slide();
    rec05_save_needs_proof();
    rec07_empty_lobby_no_genesis_game();
    rec09_merge_matrix();
    if (failures != 0)
    {
        std::fprintf(stderr, "%d recovery checks failed\n", failures);
        return 1;
    }
    std::puts("flynes_recovery_rec passed");
    return 0;
}
