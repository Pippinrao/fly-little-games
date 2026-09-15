#include "initial_plan_lock.hpp"
#include "nearby/harness/verified_pair_evidence.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <string>
#include <type_traits>

using namespace flynes::session;
namespace {
void check(bool condition, const std::string& message)
{
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}

PlanHash hash(std::uint8_t value) { PlanHash h{}; h[0] = value; return h; }

struct Fixture {
    InitialPlanLock reducer;
    VerifiedPairEvidence pair{};
    VerifiedPlanEvidence plan{};
    VerifiedPlanEvidence ack{};
    VerifiedPlanEvidence final{};
    std::uint64_t last_id = 0;
    explicit Fixture(PairRole role = PairRole::Initiator, PairRole creator = PairRole::Initiator,
                     std::uint8_t confirmation = 0)
    {
        wire::BearerPlanBytes selected{};
        selected[0] = 2; selected[1] = static_cast<std::uint8_t>(creator);
        selected[2] = 1; selected[3] = 2; selected[4] = 1;
        selected[5] = confirmation; selected[6] = 10; selected[11] = 1;
        selected[12] = 1;
        pair.local_role = role; pair.generation = 7;
        pair.transcript = hash(1); pair.initiator_reveal = hash(2); pair.responder_reveal = hash(3);
        pair.initiator_capability = hash(4); pair.responder_capability = hash(5);
        auto& summary = pair.initiator_summary;
        summary[1] = 1; summary[8] = 1; summary[9] = 1; summary[32] = 1; summary[64] = 1;
        std::copy(selected.begin(), selected.end(), summary.begin() + 96);
        pair.responder_summary = summary;
        pair = VerifiedPairEvidenceTestFactory::seal(pair);
        plan.generation = pair.generation; plan.sender = PairRole::Initiator; plan.receiver = PairRole::Responder;
        plan.transcript = pair.transcript; plan.initiator_reveal = pair.initiator_reveal;
        plan.responder_reveal = pair.responder_reveal;
        plan.initiator_capability = pair.initiator_capability;
        plan.responder_capability = pair.responder_capability; plan.selected_plan = selected;
        plan.selected_plan_hash = wire::domain_hash("flynes-selected-bearer-plan-v1", selected.data(), selected.size());
        plan.plan_logical_hash = hash(4);
        ack = plan; ack.sender = PairRole::Responder; ack.receiver = PairRole::Initiator; ack.ack_logical_hash = hash(5);
        final = plan; final.ack_logical_hash = ack.ack_logical_hash; final.final_logical_hash = hash(6);
    }
    InitialPlanCommand command(InitialPlanCommandKind kind)
    {
        const auto value = reducer.poll();
        check(value.has_value(), "command exists");
        check(value->kind == kind, "expected command kind " + std::to_string(static_cast<int>(kind)));
        check(value->generation == pair.generation && value->id > last_id, "monotonic local ID and generation");
        const auto repeated = reducer.poll();
        check(repeated && repeated->id == value->id && repeated->kind == value->kind,
              "poll retries identical outstanding command");
        return *value;
    }
    void finish(InitialPlanCommandKind kind)
    {
        auto cmd = command(kind); last_id = cmd.id;
        check(reducer.complete(cmd.id, cmd.generation, true), "command completion succeeds");
    }
    void to_ack()
    {
        check(reducer.begin(pair), "begin verified pair");
        check(reducer.selected_plan() == plan.selected_plan, "independent exact selection");
        check(reducer.accept_plan(plan), "accept verified plan");
        check(!reducer.locked(), "plan does not lock before persistence");
        if (pair.local_role == PairRole::Initiator) {
            finish(InitialPlanCommandKind::PersistPlan); finish(InitialPlanCommandKind::SendPlan);
        } else {
            finish(InitialPlanCommandKind::PersistPlanAndLock);
            check(reducer.locked(), "responder atomically locks with plan persistence");
        }
    }
    void to_final()
    {
        to_ack(); check(reducer.accept_ack(ack), "accept exact ACK");
        if (pair.local_role == PairRole::Initiator) finish(InitialPlanCommandKind::PersistAckAndLock);
        else { finish(InitialPlanCommandKind::PersistAck); finish(InitialPlanCommandKind::SendAck); }
        check(reducer.locked() && !reducer.mutually_locked(), "ACK locks but never mutually locks");
        check(!reducer.poll(), "no bearer or FINAL before verified FINAL evidence");
    }
    void to_mutual_pending()
    {
        to_final(); check(reducer.accept_final(final), "accept exact FINAL");
        finish(InitialPlanCommandKind::PersistFinal);
        check(!reducer.mutually_locked(), "FINAL persistence alone does not publish mutual state");
        if (pair.local_role == PairRole::Initiator) finish(InitialPlanCommandKind::SendFinal);
        command(InitialPlanCommandKind::PersistMutualLock);
    }
    void to_mutual()
    {
        to_mutual_pending(); finish(InitialPlanCommandKind::PersistMutualLock);
        check(reducer.mutually_locked(), "mutual state follows durable mutual record");
    }
    VerifiedCredentialEvidence credentials() const
    {
        auto binding = final;
        binding.sender = static_cast<PairRole>(plan.selected_plan[1]);
        binding.receiver = binding.sender == PairRole::Initiator ? PairRole::Responder : PairRole::Initiator;
        return {binding, hash(8)};
    }
};

void happy_paths()
{
    for (auto role : {PairRole::Initiator, PairRole::Responder}) {
        for (auto creator : {PairRole::Initiator, PairRole::Responder}) {
            for (std::uint8_t confirmation : {std::uint8_t{0}, std::uint8_t{1}, std::uint8_t{2}}) {
                Fixture f(role, creator, confirmation); f.to_mutual();
                const bool local_prompt = confirmation == static_cast<std::uint8_t>(role);
                if (role != creator) {
                    check(!f.reducer.poll(), "noncreator waits for credentials");
                    check(f.reducer.accept_credentials(f.credentials()), "matching credentials accepted");
                    f.finish(InitialPlanCommandKind::PersistCredentials);
                }
                if (local_prompt) {
                    check(!f.reducer.prompt_consumed(), "budget not consumed before durable completion");
                    f.finish(InitialPlanCommandKind::PersistPromptConsumed);
                }
                check(f.reducer.prompt_consumed() == local_prompt, "only designated role consumes prompt");
                const auto action_kind = role == creator ? InitialPlanCommandKind::CreateBearer : InitialPlanCommandKind::JoinBearer;
                const auto action = f.command(action_kind);
                check(action.may_prompt == local_prompt, "bearer action carries exact prompt permission");
                check(action.evidence.selected_plan == f.plan.selected_plan, "action bound to locked plan");
                f.finish(action_kind);
                if (role == creator) {
                    check(!f.reducer.poll(), "creator waits for prepared exact credentials after create");
                    check(f.reducer.accept_credentials(f.credentials()), "creator submits prepared credential evidence");
                    auto persist = f.command(InitialPlanCommandKind::PersistCredentials);
                    check(persist.credential_logical_hash == hash(8), "persist binds exact type8 bytes");
                    f.finish(InitialPlanCommandKind::PersistCredentials);
                    auto publish = f.command(InitialPlanCommandKind::PublishCredentials);
                    check(publish.credential_logical_hash == persist.credential_logical_hash && !publish.may_prompt,
                          "publish reuses exact persisted type8 without another prompt");
                    f.finish(InitialPlanCommandKind::PublishCredentials);
                }
                check(!f.reducer.poll(), "no repeated side effect after success");
            }
        }
    }
}

void rejection_cases()
{
    for (int mutation = 0; mutation < 7; ++mutation) {
        Fixture f;
        if (mutation == 0) f.pair.generation = 0;
        if (mutation == 1) f.pair.local_role = static_cast<PairRole>(3);
        if (mutation == 2) f.pair.transcript = {};
        if (mutation == 3) f.pair.initiator_reveal = {};
        if (mutation == 4) f.pair.responder_reveal = {};
        if (mutation == 5) f.pair.initiator_summary[1] = 2;
        if (mutation == 6) f.pair.responder_summary[1] = 2;
        check(!f.reducer.begin(f.pair) && f.reducer.failed() && !f.reducer.poll(), "invalid pair fails closed");
    }
    {
        Fixture f; f.pair.responder_summary[107] = 2;
        f.pair = VerifiedPairEvidenceTestFactory::seal(f.pair);
        check(!f.reducer.begin(f.pair) && f.reducer.not_supported(), "no intersection is NOT_SUPPORTED");
    }
    for (auto role : {PairRole::Initiator, PairRole::Responder}) {
        for (int stage = 0; stage < 3; ++stage) {
            for (int mutation = 0; mutation < 12; ++mutation) {
                Fixture f(role);
                if (stage == 0) check(f.reducer.begin(f.pair), "begin before rejected plan");
                if (stage == 1) f.to_ack();
                if (stage == 2) f.to_final();
                auto e = stage == 0 ? f.plan : (stage == 1 ? f.ack : f.final);
                if (mutation == 0) ++e.generation;
                if (mutation == 1) e.sender = e.receiver;
                if (mutation == 2) e.receiver = e.sender;
                if (mutation == 3) e.transcript = hash(99);
                if (mutation == 4) e.initiator_reveal = hash(99);
                if (mutation == 5) e.responder_reveal = hash(99);
                if (mutation == 6) e.selected_plan[11] = 2;
                if (mutation == 7) e.selected_plan_hash = hash(99);
                if (mutation == 8) e.plan_logical_hash = {};
                if (mutation == 9) { if (stage == 0) e.ack_logical_hash = hash(99); else e.ack_logical_hash = {}; }
                if (mutation == 10) { if (stage == 2) e.final_logical_hash = {}; else e.final_logical_hash = hash(99); }
                if (mutation == 11) { if (stage == 0) e.transcript = {}; else e.plan_logical_hash = hash(99); }
                const bool accepted = stage == 0 ? f.reducer.accept_plan(e) :
                    (stage == 1 ? f.reducer.accept_ack(e) : f.reducer.accept_final(e));
                check(!accepted && f.reducer.failed() && !f.reducer.poll(), "altered/stale/reflected evidence fails closed");
            }
        }
    }
    {
        Fixture f; f.to_final(); f.final.ack_logical_hash = hash(99);
        check(!f.reducer.accept_final(f.final) && f.reducer.failed(), "FINAL binds exact durable ACK");
    }
    {
        Fixture f; check(f.reducer.begin(f.pair), "begin");
        check(!f.reducer.accept_ack(f.ack) && f.reducer.failed(), "early ACK rejected");
    }
    {
        Fixture f; f.to_ack();
        check(!f.reducer.accept_final(f.final) && f.reducer.failed(), "early FINAL rejected");
    }
    {
        Fixture f; f.to_final();
        check(!f.reducer.accept_credentials(f.credentials()) && f.reducer.failed(), "credentials before mutual lock rejected");
    }
    {
        Fixture f; f.to_mutual();
        check(!f.reducer.accept_credentials(f.credentials()) && f.reducer.failed(), "creator cannot join on type8");
    }
    for (int mutation = 0; mutation < 5; ++mutation) {
        Fixture f(PairRole::Responder); f.to_mutual(); auto e = f.credentials();
        if (mutation == 0) e.credential_logical_hash = {};
        if (mutation == 1) e.binding.sender = PairRole::Responder;
        if (mutation == 2) e.binding.selected_plan[3] = 1;
        if (mutation == 3) e.binding.final_logical_hash = hash(99);
        if (mutation == 4) ++e.binding.generation;
        check(!f.reducer.accept_credentials(e) && f.reducer.failed(), "mismatched credentials rejected");
    }
}

void failures_and_replay()
{
    for (int mutation = 0; mutation < 3; ++mutation) {
        Fixture f; f.to_final(); check(f.reducer.accept_final(f.final), "accept FINAL");
        f.finish(InitialPlanCommandKind::PersistFinal);
        const auto command = f.command(InitialPlanCommandKind::SendFinal);
        const auto id = mutation == 0 ? command.id + 1 : command.id;
        const auto generation = mutation == 1 ? command.generation + 1 : command.generation;
        check(!f.reducer.complete(id, generation, mutation != 2), "wrong completion or failed send rejected");
        check(f.reducer.failed() && !f.reducer.mutually_locked() && !f.reducer.poll(), "no mutual lock after failed FINAL send");
    }
    {
        Fixture f; f.to_ack(); check(f.reducer.accept_ack(f.ack), "ACK");
        auto c = f.command(InitialPlanCommandKind::PersistAckAndLock);
        check(!f.reducer.complete(c.id, c.generation, false) && !f.reducer.locked(), "failed ACK persistence never locks");
    }
    {
        Fixture f; f.to_mutual_pending(); auto c = f.command(InitialPlanCommandKind::PersistMutualLock);
        check(!f.reducer.complete(c.id, c.generation, false) && !f.reducer.mutually_locked(), "failed mutual persistence never authorizes");
    }
    {
        Fixture f(PairRole::Initiator, PairRole::Initiator, 1); f.to_mutual();
        f.finish(InitialPlanCommandKind::PersistPromptConsumed); f.reducer.invalidate();
        check(f.reducer.prompt_consumed() && f.reducer.failed() && !f.reducer.poll(), "invalidation retains consumed prompt and cancels effects");
        ++f.pair.generation;
        check(!f.reducer.begin(f.pair), "reconnect cannot reset budget or choose another plan");
    }
    {
        Fixture f; f.to_ack();
        check(!f.reducer.accept_plan(f.plan) && f.reducer.failed(), "second plan rejected even if identical");
    }
    {
        Fixture f; check(f.reducer.begin(f.pair), "begin"); check(f.reducer.accept_plan(f.plan), "plan");
        auto c = f.command(InitialPlanCommandKind::PersistPlan); f.finish(InitialPlanCommandKind::PersistPlan);
        check(!f.reducer.complete(c.id, c.generation, true) && f.reducer.failed(), "replayed completion cannot advance next operation");
    }
    {
        Fixture f; check(!f.reducer.complete(1, 7, true) && f.reducer.failed(), "unsolicited completion rejected");
    }
}

void command_failure_matrix()
{
    constexpr std::size_t command_count = static_cast<std::size_t>(InitialPlanCommandKind::JoinBearer) + 1;
    std::array<bool, command_count> seen{};
    for (auto role : {PairRole::Initiator, PairRole::Responder}) {
        for (auto creator : {PairRole::Initiator, PairRole::Responder}) {
            for (std::size_t target = 0; target < command_count; ++target) {
                for (int failure_mode = 0; failure_mode < 4; ++failure_mode) {
                    Fixture f(role, creator, static_cast<std::uint8_t>(role));
                    check(f.reducer.begin(f.pair) && f.reducer.accept_plan(f.plan), "begin failure matrix");
                    for (int step = 0; step < 20; ++step) {
                        const auto pending = f.reducer.poll();
                        if (!pending) break;
                        const auto c = *pending;
                        if (static_cast<std::size_t>(c.kind) == target) {
                            seen[target] = true;
                            const bool consumed = f.reducer.prompt_consumed();
                            if (failure_mode == 3) f.reducer.invalidate();
                            else check(!f.reducer.complete(c.id + (failure_mode == 1 ? 1 : 0),
                                                           c.generation + (failure_mode == 2 ? 1 : 0),
                                                           failure_mode != 0), "command failure rejected");
                            check(f.reducer.failed() && !f.reducer.poll(), "every command failure revokes all effects");
                            check(f.reducer.prompt_consumed() == consumed, "failure does not refill consumed budget");
                            check(!f.reducer.complete(c.id, c.generation, true), "late success cannot revive failed attempt");
                            break;
                        }
                        check(f.reducer.complete(c.id, c.generation, true), "advance failure matrix");
                        if (f.reducer.poll()) continue;
                        if (c.kind == InitialPlanCommandKind::SendPlan || c.kind == InitialPlanCommandKind::PersistPlanAndLock)
                            check(f.reducer.accept_ack(f.ack), "matrix ACK");
                        else if (c.kind == InitialPlanCommandKind::SendAck || c.kind == InitialPlanCommandKind::PersistAckAndLock)
                            check(f.reducer.accept_final(f.final), "matrix FINAL");
                        else if (c.kind == InitialPlanCommandKind::CreateBearer || c.kind == InitialPlanCommandKind::PersistMutualLock)
                            check(f.reducer.accept_credentials(f.credentials()), "matrix credentials");
                    }
                }
            }
        }
    }
    check(std::all_of(seen.begin(), seen.end(), [](bool value) { return value; }), "failure injection reaches every command kind");
}

void immutable_evidence_and_selection()
{
    {
        Fixture f; check(f.reducer.begin(f.pair), "begin snapshot");
        check(f.reducer.accept_plan(f.plan), "accept snapshot");
        auto command = f.command(InitialPlanCommandKind::PersistPlan);
        f.plan.plan_logical_hash = hash(99); f.plan.selected_plan[0] = 4;
        f.pair.transcript = hash(99); command.evidence.plan_logical_hash = hash(100);
        const auto retained = f.reducer.poll();
        check(retained && retained->evidence.plan_logical_hash == hash(4) && retained->evidence.transcript == hash(1),
              "input and poll mutations cannot change retained immutable evidence");
    }
    {
        Fixture f; auto second = f.plan.selected_plan; second[11] = 2;
        for (auto* summary : {&f.pair.initiator_summary, &f.pair.responder_summary}) {
            (*summary)[9] = 2; std::copy(second.begin(), second.end(), summary->begin() + 144);
        }
        f.pair = VerifiedPairEvidenceTestFactory::seal(f.pair);
        check(f.reducer.begin(f.pair), "multiple common entries accepted");
        check(f.reducer.selected_plan() == f.plan.selected_plan, "reducer selects unique minimum independently");
        f.plan.selected_plan = second;
        f.plan.selected_plan_hash = wire::domain_hash("flynes-selected-bearer-plan-v1", second.data(), second.size());
        check(!f.reducer.accept_plan(f.plan) && f.reducer.failed(), "valid common but nonminimal plan rejected");
    }
    {
        Fixture f; check(f.reducer.begin(f.pair) && f.reducer.accept_plan(f.plan), "pending plan");
        check(!f.reducer.accept_ack(f.ack) && f.reducer.failed(), "semantic ACK cannot bypass pending plan persistence/send");
    }
    volatile bool copyable = std::is_copy_constructible<InitialPlanLock>::value || std::is_copy_assignable<InitialPlanLock>::value;
    check(!copyable,
          "attempt ownership cannot be copied to duplicate command or prompt authority");
}
} // namespace

int main()
{
    happy_paths(); rejection_cases(); failures_and_replay(); command_failure_matrix(); immutable_evidence_and_selection();
    std::cout << "initial plan lock tests passed\n";
}
