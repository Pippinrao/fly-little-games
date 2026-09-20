#include "invite_coordinator.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <string>
#include <vector>

using flynes::session::InviteChildMaterial;
using flynes::session::InviteCoordinator;
using flynes::session::InviteCoordinatorResult;
using flynes::session::InviteRouteV2;
using flynes::session::JoinLookupRound;

namespace {

constexpr std::uint64_t kSecond = UINT64_C(1000000000);
int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

template <std::size_t N>
std::array<std::uint8_t, N> filled(std::uint8_t value)
{
    std::array<std::uint8_t, N> out{};
    out.fill(value);
    return out;
}

InviteChildMaterial child(std::uint8_t seed)
{
    InviteChildMaterial out{};
    out.child_id = filled<16>(seed);
    out.nonce = filled<16>(static_cast<std::uint8_t>(seed + 1));
    out.commitment = filled<32>(static_cast<std::uint8_t>(seed + 2));
    return out;
}

void test_uniform_code_mapping()
{
    std::array<std::uint8_t, 6> code{};
    check(flynes::session::invite_code_from_uniform_u32(0, &code) ==
              InviteCoordinatorResult::Ok && code == std::array<std::uint8_t, 6>{{'0','0','0','0','0','0'}},
          "zero maps to 000000");
    check(flynes::session::invite_code_from_uniform_u32(12345, &code) ==
              InviteCoordinatorResult::Ok && code == std::array<std::uint8_t, 6>{{'0','1','2','3','4','5'}},
          "leading zeros preserved");
    check(flynes::session::invite_code_from_uniform_u32(UINT32_C(4293999999), &code) ==
              InviteCoordinatorResult::Ok,
          "last unbiased draw accepted");
    check(flynes::session::invite_code_from_uniform_u32(UINT32_C(4294000000), &code) ==
              InviteCoordinatorResult::Retry,
          "rejection-sampling boundary rejected");
    check(flynes::session::normalize_invite_code(" \t012345\r\n", &code) ==
              InviteCoordinatorResult::Ok && code[0] == '0',
          "ASCII edge whitespace trims and leading zero survives");
    check(flynes::session::normalize_invite_code("12345", &code) ==
              InviteCoordinatorResult::Invalid &&
              flynes::session::normalize_invite_code("12 345", &code) ==
              InviteCoordinatorResult::Invalid,
          "incomplete or internal whitespace sends nothing");
}

void test_generation_deadline_and_route_consumption()
{
    InviteCoordinator coordinator;
    const auto code = std::array<std::uint8_t, 6>{{'0','1','2','3','4','5'}};
    check(coordinator.create(1, 10, code, child(1), child(10)) ==
              InviteCoordinatorResult::Ok,
          "create invitation");
    check(coordinator.deadline_ns() == 10 + 60 * kSecond,
          "deadline is a fixed continuous-clock 60 seconds");
    check(coordinator.lookup(code, 10 + 60 * kSecond - 1) ==
              InviteCoordinatorResult::MatchPendingApproval,
          "lookup before deadline matches but does not authenticate");
    check(!coordinator.produces_verified_pair_evidence(),
          "route match cannot mint authentication evidence");
    check(coordinator.accept(1, InviteRouteV2::BleAnonymous, child(1).child_id,
                             10 + 1 * kSecond) == InviteCoordinatorResult::Accepted,
          "first exact child route consumes invitation");
    check(coordinator.accept(1, InviteRouteV2::Qr, child(10).child_id,
                             10 + 1 * kSecond) == InviteCoordinatorResult::Consumed,
          "other child route cannot continue");
    check(!coordinator.route_active(InviteRouteV2::BleAnonymous) &&
              !coordinator.route_active(InviteRouteV2::Qr),
          "both child routes become inactive atomically");

    check(coordinator.create(2, 100, code, child(30), child(40)) ==
              InviteCoordinatorResult::Ok,
          "explicit refresh creates a new generation");
    check(coordinator.accept(1, InviteRouteV2::BleAnonymous, child(1).child_id, 101) ==
              InviteCoordinatorResult::Stale,
          "old generation cannot revive");
    check(coordinator.lookup(code, 100 + 60 * kSecond) ==
              InviteCoordinatorResult::Expired,
          "deadline itself is expired");
    check(coordinator.deadline_ns() == 100 + 60 * kSecond,
          "lookup does not slide deadline");
}

void test_rate_limits_and_one_inflight_per_gatt()
{
    InviteCoordinator coordinator;
    for (unsigned i = 0; i < 5; ++i)
        check(coordinator.begin_join_submission(100 + i) == InviteCoordinatorResult::Accepted,
              "first five local submissions accepted");
    check(coordinator.begin_join_submission(200) == InviteCoordinatorResult::RateLimited,
          "sixth local submission in sixty seconds rejected");
    check(coordinator.begin_join_submission(100 + 60 * kSecond) ==
              InviteCoordinatorResult::Accepted,
          "fixed local rate window releases at sixty seconds");

    const auto code = std::array<std::uint8_t, 6>{{'1','2','3','4','5','6'}};
    check(coordinator.create(1, 0, code, child(1), child(10)) ==
              InviteCoordinatorResult::Ok,
          "host limit fixture");
    check(coordinator.begin_host_lookup(7, 1) == InviteCoordinatorResult::Accepted,
          "first GATT lookup accepted");
    check(coordinator.begin_host_lookup(7, 2) == InviteCoordinatorResult::Busy,
          "same GATT allows one in-flight lookup");
    coordinator.end_host_lookup(7);
    for (unsigned i = 1; i < 20; ++i)
    {
        check(coordinator.begin_host_lookup(100 + i, 3 + i) ==
                  InviteCoordinatorResult::Accepted,
              "host process lookup within budget");
        coordinator.end_host_lookup(100 + i);
    }
    check(coordinator.begin_host_lookup(999, 50) == InviteCoordinatorResult::RateLimited,
          "host process twentieth-plus-one lookup rejected");
    check(coordinator.begin_host_lookup(999, 1 + 60 * kSecond) ==
              InviteCoordinatorResult::Accepted,
          "host fixed rate window releases at sixty seconds");
}

void test_bounded_candidate_round_and_ambiguity()
{
    std::vector<std::uint64_t> candidates{1,2,3,4,5,6,7,8};
    JoinLookupRound round;
    check(round.start(1, 100, 100 + 60 * kSecond, candidates) ==
              InviteCoordinatorResult::Accepted,
          "eight candidates accepted");
    check(round.deadline_ns() == 100 + 16 * kSecond,
          "round capped at sixteen seconds");
    for (std::size_t i = 0; i < candidates.size(); ++i)
    {
        std::uint64_t handle = 0;
        std::uint64_t candidate_deadline = 0;
        const auto now = 100 + i * 2 * kSecond;
        check(round.next_candidate(now, &handle, &candidate_deadline) ==
                  InviteCoordinatorResult::Accepted && handle == candidates[i],
              "candidate order is finite and deterministic");
        check(candidate_deadline == now + 2 * kSecond,
              "each candidate gets at most two seconds");
    }
    std::uint64_t handle = 0;
    std::uint64_t candidate_deadline = 0;
    check(round.next_candidate(100 + 16 * kSecond, &handle, &candidate_deadline) ==
              InviteCoordinatorResult::Expired,
          "round deadline does not slide");

    candidates.push_back(9);
    check(round.start(2, 0, 60 * kSecond, candidates) ==
              InviteCoordinatorResult::Invalid,
          "nine candidates rejected rather than silently truncated");

    candidates.resize(2);
    check(round.start(3, 0, 60 * kSecond, candidates) ==
              InviteCoordinatorResult::Accepted,
          "ambiguity fixture starts");
    const auto first_context = filled<32>(1);
    const auto second_context = filled<32>(2);
    check(round.record_match(1, first_context) == InviteCoordinatorResult::MatchPendingApproval,
          "first context match remains pending");
    check(round.record_match(2, first_context) == InviteCoordinatorResult::Duplicate,
          "same context from another observation is idempotent");
    check(round.record_match(2, second_context) == InviteCoordinatorResult::Ambiguous,
          "different matching contexts never auto-select");
}

} // namespace

int main()
{
    test_uniform_code_mapping();
    test_generation_deadline_and_route_consumption();
    test_rate_limits_and_one_inflight_per_gatt();
    test_bounded_candidate_round_and_ambiguity();
    if (failures != 0)
        return 1;
    std::cout << "InviteCoordinator finite route contract passed\n";
    return 0;
}
