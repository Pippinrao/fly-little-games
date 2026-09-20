// Component integration: real session engines + production Android ContentPort,
// existing labeled loopback crypto/bearer transport. Not Android app E2E.
#include "content_port.hpp"
#include "session_owner.hpp"
#include <flynes/product/dual_start_identity.hpp>
#include "../harness/two_engine_loopback_fixture.hpp"
#include <cstring>
#include <atomic>
#include <memory>
#include <vector>

using flynes::android::nearby::ContentPort;
using namespace flynes::session::loopback;

namespace {
std::atomic<fly_session_v2_t*> projection_engine{nullptr};
}
namespace flynes::android::nearby {
fly_session_result_v2 acquire_owner_projection_test_view(fly_session_view_v2_t** out) {
    return fly_session_acquire_view_v2(projection_engine.load(), out);
}
}

namespace {
void verify(bool unavailable, std::uint32_t choice_count = 3) {
    std::vector<std::uint32_t> queried;
    ContentPort provider({[&](std::uint32_t index, std::vector<std::uint8_t>* out) {
        queried.push_back(index);
        if (unavailable) return FLY_SESSION_V2_UNAVAILABLE;
        if (index >= choice_count) return FLY_SESSION_V2_EMPTY;
        out->assign(57, 0);
        (*out)[1] = 1; (*out)[4] = static_cast<std::uint8_t>(index + 1);
        (*out)[20] = static_cast<std::uint8_t>(7 + index);
        (*out)[55] = 1; (*out)[56] = static_cast<std::uint8_t>('A' + index);
        return FLY_SESSION_V2_OK;
    }, [](const std::uint8_t*) { return true; }, {}});
    auto table = provider.port();
    check(!provider.query_status().attempted, "fresh provider is not an empty or failed query");
    LoopbackWorld world;
    EngineFixture inviter(world, LoopbackSide::Initiator, true, false, false, 0, nullptr, &table);
    EngineFixture joiner(world, LoopbackSide::Responder, true, false, false);
    LoopbackTransport transport;
    PumpState left, right;
    PumpLimits limits{};
    RelayReport report;
    reset_loopback_clock_ns();
    inviter.platform.ready(); joiner.platform.ready();
    inviter.executor.run_all(); joiner.executor.run_all();
    std::vector<fly_session_action_descriptor_v2> left_actions, right_actions;
    inviter.snapshot(&left_actions); joiner.snapshot(&right_actions);
    const auto* create = find_action(left_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
    const auto* join = find_action(right_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
    check(create && join, "real engines expose create/join actions");
    if (create && join) {
        submit(inviter, *create, 9001, false); submit(joiner, *join, 9002, true);
        transport.attach(LoopbackRole::AdvertiserPeripheral, inviter);
        transport.attach(LoopbackRole::ScannerCentral, joiner);
        check(transport.connect_ends() == 2, "two-engine harness connects both ends");
        pump_engine(inviter, left, limits); pump_engine(joiner, right, limits);
        relay_and_pump_until_idle(transport, inviter, left, joiner, right, limits, 4000, 8, &report, false);
        relay_and_pump_until_idle(transport, inviter, left, joiner, right, limits, 4000, 8, &report, true);
        if (unavailable) {
            check(queried == std::vector<std::uint32_t>{0}, "provider failure is not enumerated as EMPTY");
            check(provider.query_status().attempted && provider.query_status().result == FLY_SESSION_V2_UNAVAILABLE,
                  "provider failure remains observable independently of engine link projection");
            check(inviter.game_choices().empty(), "failed batch publishes no truncated choices");
        } else {
            std::vector<std::uint32_t> expected_queries;
            for (std::uint32_t i = 0; i <= choice_count; ++i) expected_queries.push_back(i);
            check(queried == expected_queries, "real engine finishes catalog on synchronous EMPTY");
            check(provider.query_status().attempted && provider.query_status().result == FLY_SESSION_V2_EMPTY,
                  "successful enumeration completion differs from unqueried and unavailable");
            const auto expected = inviter.game_choices();
            fly_session_view_v2_t* retained_view = nullptr;
            check(fly_session_acquire_view_v2(inviter.engine, &retained_view) == FLY_SESSION_V2_OK,
                  "test holds a real engine view reference throughout owner projection");
            projection_engine.store(inviter.engine);
            std::vector<fly_session_game_choice_v2> choices;
            {
                std::unique_ptr<flynes::android::nearby::SessionOwner> owner(flynes::android::nearby::SessionOwner::create());
                check(owner && owner->read_game_choices(&choices), "actual owner worker projects the retained real-engine view");
            }
            projection_engine.store(nullptr);
            fly_session_view_release_v2(retained_view);
            check(choices.size() == choice_count, "owner projects zero, one or three exact-source choices");
            const auto identity = flynes::product::canonical_dual_start_identity_v1();
            if (choices.size() == expected.size()) for (std::size_t i = 0; i < choices.size(); ++i) {
                const auto& row = choices[i];
                check(row.struct_size == FLY_SESSION_GAME_CHOICE_V2_SIZE && row.abi_version == 2,
                      "owner projection preserves full current ABI for every row");
                check(std::memcmp(row.source_choice_ref, expected[i].source_choice_ref, 16) == 0 &&
                      std::memcmp(row.content_id, expected[i].content_id, 32) == 0 &&
                      row.catalog_revision == expected[i].catalog_revision && row.progress_revision == expected[i].progress_revision &&
                      row.selectable == expected[i].selectable && row.display_name_size == expected[i].display_name_size &&
                      std::memcmp(row.display_name, expected[i].display_name, sizeof(row.display_name)) == 0,
                      "owner projection preserves exact reference hash name and revisions for every row");
                check(std::memcmp(row.core_id, identity.core_id.data(), 32) == 0 &&
                      std::memcmp(row.profile_id, identity.profile_id.data(), 32) == 0 &&
                      std::memcmp(row.options_id, identity.options_id.data(), 32) == 0,
                      "owner projection preserves complete core/profile/options identity for every row");
            }
            check(inviter.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2,
                  "normal metadata enumeration remains connected lobby");
            std::vector<fly_session_action_descriptor_v2> actions;
            inviter.snapshot(&actions);
            auto* select = find_action(actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
            check((select != nullptr) == (choice_count != 0), "EMPTY completion enables typed SELECT only for nonempty catalog");
            if (select && choices.size() >= 2) {
                check(choices[0].source_choice_ref[0] == 1 && choices[1].source_choice_ref[0] == 2 &&
                      choices[1].content_id[0] == 8,
                      "projected second choice preserves its exact source and content");
                submit_choice(inviter, *select, 9003, choices[1].source_choice_ref);
                inviter.executor.run_all();
                auto snapshot = inviter.snapshot();
                check(snapshot.pending_config_revision != 0 && snapshot.pending_config_local_confirmed == 0 &&
                      snapshot.game_state == FLY_SESSION_GAME_NOT_STARTED_V2 &&
                      std::memcmp(snapshot.dual_content_hash, expected[1].content_id, 32) == 0,
                      "owner-projected second ref selects precisely that content in its producing real engine");
            }
            for (auto& action : actions) fly_session_approval_token_release_v2(action.approval_token);
        }
    }
    for (auto& action : left_actions) fly_session_approval_token_release_v2(action.approval_token);
    for (auto& action : right_actions) fly_session_approval_token_release_v2(action.approval_token);
    shutdown_engine_with_the_pump(inviter, left, limits);
    shutdown_engine_with_the_pump(joiner, right, limits);
}
}
void verify_prepared_content();
int main() { verify_prepared_content(); verify(false, 0); verify(false, 1); verify(false, 3); verify(true); return failures ? 1 : 0; }
