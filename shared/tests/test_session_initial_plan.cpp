#include "session_initial_plan.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>

using namespace flynes::session;
namespace {
constexpr std::uint64_t timeout_ns = UINT64_C(60000000000);
void check(bool condition, const char* message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
PlanHash hash(std::uint8_t n) { PlanHash h{}; h[0] = n; return h; }

// Synthetic verified evidence is test-only; this fixture does no authentication.
struct Fixture {
    std::unique_ptr<fly_session_t, decltype(&fly_session_destroy)> owner{nullptr, fly_session_destroy};
    fly_session_t* session = nullptr;
    VerifiedPairEvidence pair{};
    VerifiedPlanEvidence plan{}, ack{}, final{};
    explicit Fixture(PairRole role = PairRole::Initiator, std::uint64_t generation = 7,
                     PairRole prompt = PairRole::Initiator)
    {
        fly_session_config config{};
        config.struct_size = FLY_SESSION_CONFIG_V1_SIZE; config.version = FLY_SESSION_CONFIG_VERSION_1;
        check(fly_session_create(&config, &session) == FLY_RESULT_OK, "create owned session");
        owner.reset(session);
        wire::BearerPlanBytes selected{};
        selected[0] = 2; selected[1] = 1; selected[2] = 1; selected[3] = 2;
        selected[4] = 1; selected[5] = static_cast<std::uint8_t>(prompt);
        selected[6] = 10; selected[11] = 1; selected[12] = 1;
        pair.local_role = role; pair.generation = generation;
        pair.transcript = hash(1); pair.initiator_reveal = hash(2); pair.responder_reveal = hash(3);
        auto& summary = pair.initiator_summary;
        summary[1] = 1; summary[8] = 1; summary[9] = 1; summary[32] = 1; summary[64] = 1;
        std::copy(selected.begin(), selected.end(), summary.begin() + 96); pair.responder_summary = summary;
        plan.generation = generation; plan.sender = PairRole::Initiator; plan.receiver = PairRole::Responder;
        plan.transcript = pair.transcript; plan.initiator_reveal = pair.initiator_reveal;
        plan.responder_reveal = pair.responder_reveal; plan.selected_plan = selected;
        plan.selected_plan_hash = wire::domain_hash("flynes-selected-bearer-plan-v1", selected.data(), selected.size());
        plan.plan_logical_hash = hash(4);
        ack = plan; ack.sender = PairRole::Responder; ack.receiver = PairRole::Initiator; ack.ack_logical_hash = hash(5);
        final = plan; final.ack_logical_hash = hash(5); final.final_logical_hash = hash(6);
    }
    SessionInitialPlanSnapshot snapshot()
    {
        auto result = initial_plan_snapshot(session); check(result.has_value(), "private snapshot exists"); return *result;
    }
    void start(std::uint64_t original = 100, std::uint64_t now = 100)
    {
        check(fly_session_tick(session, now) == FLY_RESULT_OK, "tick before start");
        check(start_initial_pair_attempt(session, pair.generation, original), "start original PairContext attempt");
    }
    void begin() { check(begin_initial_verified_pair(session, pair), "begin verified pair on handle"); }
    InitialPlanCommand command(InitialPlanCommandKind kind)
    {
        auto c = poll_initial_plan_command(session);
        check(c && c->kind == kind && c->generation == pair.generation, "typed pending command bound to handle generation");
        auto repeated = poll_initial_plan_command(session);
        check(repeated && repeated->id == c->id, "poll repeats same id"); return *c;
    }
    void finish(InitialPlanCommandKind kind)
    {
        auto c = command(kind);
        check(complete_initial_plan_command(session, c.id, c.generation, true), "complete owned command");
    }
    void to_plan() { start(); begin(); check(accept_initial_plan(session, plan), "accept plan on handle"); }
    void to_mutual_pending()
    {
        to_plan();
        if (pair.local_role == PairRole::Initiator) {
            finish(InitialPlanCommandKind::PersistPlan); finish(InitialPlanCommandKind::SendPlan);
        } else finish(InitialPlanCommandKind::PersistPlanAndLock);
        check(accept_initial_ack(session, ack), "accept ACK on handle");
        if (pair.local_role == PairRole::Initiator) finish(InitialPlanCommandKind::PersistAckAndLock);
        else { finish(InitialPlanCommandKind::PersistAck); finish(InitialPlanCommandKind::SendAck); }
        check(accept_initial_final(session, final), "accept FINAL on handle");
        finish(InitialPlanCommandKind::PersistFinal);
        if (pair.local_role == PairRole::Initiator) finish(InitialPlanCommandKind::SendFinal);
    }
    void to_mutual() { to_mutual_pending(); finish(InitialPlanCommandKind::PersistMutualLock); }
    VerifiedCredentialEvidence credentials() { return {final, hash(8)}; }
    void failed()
    {
        auto s = snapshot(); check(s.failed && !s.active && !poll_initial_plan_command(session), "terminal state revokes every pending command");
    }
};

void independent_handles()
{
    for (auto prompt : {PairRole::Initiator, PairRole::Responder}) {
        Fixture creator(PairRole::Initiator, 7, prompt), joiner(PairRole::Responder, 19, prompt);
        check(creator.session != joiner.session, "distinct opaque handles");
        creator.to_mutual(); joiner.to_mutual();
        int prompts = 0;
        if (prompt == PairRole::Initiator) creator.finish(InitialPlanCommandKind::PersistPromptConsumed);
        prompts += creator.command(InitialPlanCommandKind::CreateBearer).may_prompt ? 1 : 0;
        creator.finish(InitialPlanCommandKind::CreateBearer);
        const auto creator_credentials = creator.credentials();
        check(accept_initial_credentials(creator.session, creator_credentials), "creator prepared credentials after bearer creation");
        const auto creator_persist = creator.command(InitialPlanCommandKind::PersistCredentials);
        check(creator_persist.credential_logical_hash == creator_credentials.credential_logical_hash &&
              creator_persist.evidence.selected_plan == creator_credentials.binding.selected_plan,
              "creator persistence carries exact supplied credential hash and selected plan");
        creator.finish(InitialPlanCommandKind::PersistCredentials);
        const auto publish = creator.command(InitialPlanCommandKind::PublishCredentials);
        check(publish.credential_logical_hash == creator_credentials.credential_logical_hash &&
              publish.evidence.selected_plan == creator_credentials.binding.selected_plan,
              "publication carries exact supplied credential hash and selected plan");
        check(!publish.may_prompt, "publication cannot prompt");
        creator.finish(InitialPlanCommandKind::PublishCredentials);
        check(!poll_initial_plan_command(joiner.session), "joiner waits for received credentials");
        const auto joiner_credentials = joiner.credentials();
        check(accept_initial_credentials(joiner.session, joiner_credentials), "joiner verified credentials");
        const auto joiner_persist = joiner.command(InitialPlanCommandKind::PersistCredentials);
        check(joiner_persist.credential_logical_hash == joiner_credentials.credential_logical_hash &&
              joiner_persist.evidence.selected_plan == joiner_credentials.binding.selected_plan,
              "joiner persistence carries exact supplied credential hash and selected plan");
        joiner.finish(InitialPlanCommandKind::PersistCredentials);
        if (prompt == PairRole::Responder) joiner.finish(InitialPlanCommandKind::PersistPromptConsumed);
        const auto join = joiner.command(InitialPlanCommandKind::JoinBearer);
        check(join.credential_logical_hash == joiner_credentials.credential_logical_hash &&
              join.evidence.selected_plan == joiner_credentials.binding.selected_plan,
              "join carries exact supplied credential hash and selected plan");
        prompts += join.may_prompt ? 1 : 0;
        joiner.finish(InitialPlanCommandKind::JoinBearer);
        check(prompts == 1, "exactly one designated prompt across both handles");
        check(creator.snapshot().mutually_locked && joiner.snapshot().mutually_locked, "independent durable mutual locks");
        check(creator.snapshot().generation == 7 && joiner.snapshot().generation == 19, "generations remain local");
        check(!poll_initial_plan_command(creator.session) && !poll_initial_plan_command(joiner.session), "completed effects do not repeat");
    }
}

void start_and_time_boundaries()
{
    { Fixture f; check(!start_initial_pair_attempt(f.session, 7, 0), "start requires prior current tick"); f.failed(); }
    for (int mutation = 0; mutation < 4; ++mutation) {
        Fixture f; check(fly_session_tick(f.session, timeout_ns + 100) == FLY_RESULT_OK, "idle tick");
        const auto origin = mutation == 0 ? timeout_ns + 101 : (mutation == 1 ? 100 : 99);
        check(!start_initial_pair_attempt(f.session, mutation == 3 ? 0 : 7, origin), "future, expired, or zero-generation start rejected"); f.failed();
    }
    { Fixture f; f.start(); check(!start_initial_pair_attempt(f.session, 8, 100), "second start terminal"); f.failed(); }
    { Fixture f; f.start(); ++f.pair.generation; check(!begin_initial_verified_pair(f.session, f.pair), "verified pair must match latched generation"); f.failed(); }
    { Fixture f; f.start(); f.begin(); check(!begin_initial_verified_pair(f.session, f.pair), "second verified pair terminal"); f.failed(); }
    {
        Fixture f; f.start(100, timeout_ns + 99); f.begin();
        check(f.snapshot().original_context_start_ns == 100, "delayed verified evidence retains original context start");
        check(accept_initial_plan(f.session, f.plan), "plan just before original deadline");
        check(fly_session_tick(f.session, timeout_ns + 100) == FLY_RESULT_INVALID_STATE, "exact original deadline expires"); f.failed();
        check(!accept_initial_ack(f.session, f.ack), "expired callback rejected");
    }
    {
        Fixture f; f.to_plan();
        check(fly_session_tick(f.session, 100) == FLY_RESULT_OK, "equal tick permitted");
        f.command(InitialPlanCommandKind::PersistPlan);
        check(fly_session_tick(f.session, 99) == FLY_RESULT_INVALID_STATE, "backwards active tick terminal"); f.failed();
    }
    {
        Fixture f; const auto maximum = std::numeric_limits<std::uint64_t>::max();
        f.start(maximum - timeout_ns, maximum - 1); f.begin();
        check(fly_session_tick(f.session, maximum) == FLY_RESULT_INVALID_STATE, "near UINT64_MAX exact deadline without overflow"); f.failed();
    }
    {
        Fixture f; const auto maximum = std::numeric_limits<std::uint64_t>::max();
        f.start(maximum - 10, maximum); f.begin();
        check(f.snapshot().active, "near UINT64_MAX unexpired attempt stays active");
    }
}

void nulls_and_before_start()
{
    Fixture f;
    check(!start_initial_pair_attempt(nullptr, 7, 0) && !begin_initial_verified_pair(nullptr, f.pair), "null start/begin");
    check(!accept_initial_plan(nullptr, f.plan) && !accept_initial_ack(nullptr, f.ack) &&
          !accept_initial_final(nullptr, f.final) && !accept_initial_credentials(nullptr, f.credentials()), "null evidence");
    check(!poll_initial_plan_command(nullptr) && !initial_plan_snapshot(nullptr) &&
          !complete_initial_plan_command(nullptr, 1, 7, true), "null read/completion");
    invalidate_initial_pair_attempt(nullptr);
    check(!f.snapshot().started && !f.snapshot().failed && !poll_initial_plan_command(f.session), "idle private reads harmless");
    for (int entry = 0; entry < 6; ++entry) {
        Fixture idle; bool accepted = false;
        if (entry == 0) accepted = begin_initial_verified_pair(idle.session, idle.pair);
        if (entry == 1) accepted = accept_initial_plan(idle.session, idle.plan);
        if (entry == 2) accepted = accept_initial_ack(idle.session, idle.ack);
        if (entry == 3) accepted = accept_initial_final(idle.session, idle.final);
        if (entry == 4) accepted = accept_initial_credentials(idle.session, idle.credentials());
        if (entry == 5) accepted = complete_initial_plan_command(idle.session, 1, 7, true);
        check(!accepted, "mutation before start rejected"); idle.failed();
        check(fly_session_tick(idle.session, 100) == FLY_RESULT_OK, "failed idle clock can tick");
        check(!start_initial_pair_attempt(idle.session, 7, 100), "invalidated idle handle cannot restart");
    }
}

void cancellation_and_failures()
{
    for (int stage = 0; stage < 4; ++stage) {
        for (bool expire : {false, true}) {
            Fixture f;
            if (stage < 2) { f.to_plan(); if (stage == 1) f.finish(InitialPlanCommandKind::PersistPlan); }
            else { f.to_mutual(); if (stage == 3) f.finish(InitialPlanCommandKind::PersistPromptConsumed); }
            auto pending = poll_initial_plan_command(f.session); check(pending.has_value(), "cancellable effect pending");
            const auto before = f.snapshot();
            if (expire) check(fly_session_tick(f.session, timeout_ns + 100) == FLY_RESULT_INVALID_STATE, "deadline cancels pending effect");
            else invalidate_initial_pair_attempt(f.session);
            f.failed(); const auto after = f.snapshot();
            check(after.locked == before.locked && after.mutually_locked == before.mutually_locked &&
                  after.prompt_consumed == before.prompt_consumed, "historical durable flags survive invalidation");
            check(!complete_initial_plan_command(f.session, pending->id, pending->generation, true), "late completion cannot revive cancelled effect");
            check(!start_initial_pair_attempt(f.session, 8, 100) && !begin_initial_verified_pair(f.session, f.pair), "cancelled handle cannot reconnect");
        }
    }
    for (int mutation = 0; mutation < 3; ++mutation) {
        Fixture f; f.to_plan(); auto c = f.command(InitialPlanCommandKind::PersistPlan);
        check(!complete_initial_plan_command(f.session, c.id + (mutation == 0 ? 1 : 0),
                                            c.generation + (mutation == 1 ? 1 : 0), mutation != 2), "stale id/generation or storage failure terminal"); f.failed();
        check(!f.snapshot().mutually_locked, "failed storage cannot reach mutual lock");
    }
    { Fixture f; f.to_mutual_pending(); auto c = f.command(InitialPlanCommandKind::PersistMutualLock);
      check(!complete_initial_plan_command(f.session, c.id, c.generation, false), "mutual storage failure terminal");
      check(!f.snapshot().mutually_locked, "failed mutual storage never authorizes bearer"); f.failed(); }
}

void raw_public_paths_stay_untrusted()
{
    Fixture f; f.to_plan(); auto pending = f.command(InitialPlanCommandKind::PersistPlan);
    const auto* raw = reinterpret_cast<const std::uint8_t*>(&f.pair);
    check(fly_session_receive_stream(f.session, FLY_SESSION_QUIC_CONTROL, raw, sizeof(f.pair)) == FLY_RESULT_INVALID_STATE, "raw stream cannot submit verified evidence");
    check(fly_session_receive_datagram(f.session, FLY_SESSION_QUIC_INPUT, raw, sizeof(f.pair)) == FLY_RESULT_INVALID_STATE, "raw datagram cannot submit verified evidence");
    fly_session_event event{}; event.struct_size = FLY_SESSION_EVENT_V1_SIZE; event.version = FLY_SESSION_EVENT_VERSION_1; event.kind = FLY_SESSION_EVENT_TRANSPORT;
    check(fly_session_submit_event(f.session, &event) == FLY_RESULT_INVALID_STATE, "public event remains stub");
    fly_session_command command{}; command.struct_size = FLY_SESSION_COMMAND_V1_SIZE; command.version = FLY_SESSION_COMMAND_VERSION_1;
    check(fly_session_poll_command(f.session, &command) == FLY_RESULT_OK && command.kind == FLY_SESSION_COMMAND_NONE && command.command_id == 0 && command.transition_id == 0, "public poll does not expose trusted effect or truncated identities");
    fly_session_command_result result{}; result.struct_size = FLY_SESSION_COMMAND_RESULT_V1_SIZE; result.version = FLY_SESSION_COMMAND_RESULT_VERSION_1;
    check(fly_session_complete_command(f.session, &result) == FLY_RESULT_INVALID_STATE, "public completion remains stub");
    fly_session_snapshot snapshot{}; snapshot.struct_size = FLY_SESSION_SNAPSHOT_V1_SIZE; snapshot.version = FLY_SESSION_SNAPSHOT_VERSION_1;
    check(fly_session_get_snapshot(f.session, &snapshot) == FLY_RESULT_OK && snapshot.ui_state == FLY_SESSION_UI_IDLE && snapshot.authority_role == 0, "public snapshot remains idle");
    check(f.command(InitialPlanCommandKind::PersistPlan).id == pending.id && !f.snapshot().locked && !f.snapshot().failed, "public paths never mutate trusted reducer");
}
} // namespace

int main()
{
    independent_handles(); start_and_time_boundaries(); nulls_and_before_start();
    cancellation_and_failures(); raw_public_paths_stay_untrusted();
    std::cout << "session initial plan tests passed\n";
}
