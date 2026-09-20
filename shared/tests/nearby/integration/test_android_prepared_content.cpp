#include "prepared_content.hpp"
#include "../harness/two_engine_loopback_fixture.hpp"
#include <memory>
#include <fstream>
#include "nes/nes.h"

using namespace flynes::android::nearby;
using flynes::session::loopback::check;

namespace { unsigned core_loads = 0; }
#ifdef FLYNES_TEST_WRAP_ROM_LOAD
extern "C" int __real_nes_load_rom(nes_t*, const std::uint8_t*, std::size_t, nes_rom_info*);
extern "C" int __wrap_nes_load_rom(nes_t* core, const std::uint8_t* bytes, std::size_t size, nes_rom_info* info) {
    ++core_loads; return __real_nes_load_rom(core, bytes, size, info);
}
#endif

namespace {
using namespace flynes::session::loopback;
struct ActualRuntime {
    std::shared_ptr<PreparedContent> prepared;
    fly_runtime_t* runtime = nullptr;
    std::unique_ptr<flynes::product::ProductDualRuntimePort> adapter;
    explicit ActualRuntime(fly_session_content_port_v2 content) : prepared(std::make_shared<PreparedContent>(content)) {
        fly_runtime_config config{}; config.struct_size = FLY_RUNTIME_CONFIG_V1_SIZE;
        config.version = FLY_RUNTIME_CONFIG_VERSION_1; config.sample_rate = 48000;
        check(fly_runtime_create(&config, &runtime) == FLY_RESULT_OK, "actual sole 48000 runtime creates");
        adapter = std::make_unique<flynes::product::ProductDualRuntimePort>(runtime,
            [state = prepared](const fly_session_dual_content_ref_v2& ref) { return state->resolve(ref); });
    }
    ~ActualRuntime() { prepared->close(); adapter.reset(); fly_runtime_destroy(runtime); }
};
ContentPort::Callbacks metadata(std::uint8_t source) {
    return {[source](std::uint32_t index, std::vector<std::uint8_t>* out) {
        if (index >= 2) return FLY_SESSION_V2_EMPTY;
        out->assign(57, 0); (*out)[1] = 1; (*out)[4] = static_cast<std::uint8_t>(source + index);
        auto hash = loopback_content_id_v1(); std::copy(hash.begin(), hash.end(), out->begin() + 20);
        (*out)[55] = 1; (*out)[56] = 'R'; return FLY_SESSION_V2_OK;
    }, [source](const std::uint8_t* ref) { return ref && (ref[0] == source || ref[0] == source + 1); }, {}};
}
struct Pair {
    ContentPort left_content, right_content;
    fly_session_content_port_v2 left_table, right_table;
    ActualRuntime left_runtime, right_runtime;
    LoopbackWorld world;
    EngineFixture left, right;
    LoopbackTransport transport;
    PumpState lp, rp; PumpLimits limits{}; RelayReport report;
    explicit Pair(std::uint8_t source) : left_content(metadata(source)), right_content(metadata(source)),
        left_table(left_content.port()), right_table(right_content.port()),
        left_runtime(left_table), right_runtime(right_table),
        left(world, LoopbackSide::Initiator, true, false, true, 0, nullptr, &left_table),
        right(world, LoopbackSide::Responder, true, false, true, 0, nullptr, &right_table) {
        left.dual_runtime.override = left_runtime.adapter->port();
        right.dual_runtime.override = right_runtime.adapter->port();
        reset_loopback_clock_ns();
    }
    void pump(bool long_wait = false) {
        relay_and_pump_until_idle(transport, left, lp, right, rp, limits,
            long_wait ? 4000 : 800, long_wait ? 8 : 4, &report, false);
    }
    void lobby() {
        left.platform.ready(); right.platform.ready(); left.executor.run_all(); right.executor.run_all();
        std::vector<fly_session_action_descriptor_v2> la, ra;
        left.snapshot(&la); right.snapshot(&ra);
        auto* create = find_action(la, FLY_SESSION_ACTION_CREATE_INVITE_V2);
        auto* join = find_action(ra, FLY_SESSION_ACTION_JOIN_CODE_V2);
        check(create && join, "actual pair publishes create/join");
        if (create && join) { submit(left, *create, 6101, false); submit(right, *join, 6102, true); }
        for (auto& a : la) fly_session_approval_token_release_v2(a.approval_token);
        for (auto& a : ra) fly_session_approval_token_release_v2(a.approval_token);
        transport.attach(LoopbackRole::AdvertiserPeripheral, left);
        transport.attach(LoopbackRole::ScannerCentral, right);
        check(transport.connect_ends() == 2, "actual pair transport connects");
        pump_engine(left, lp, limits); pump_engine(right, rp, limits); pump(true);
        relay_and_pump_until_idle(transport, left, lp, right, rp, limits, 4000, 8, &report, true);
        check(left.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
              right.snapshot().link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2, "actual handshake reaches lobby");
    }
    ~Pair() { pump(true); shutdown_engine_with_the_pump(left, lp, limits); shutdown_engine_with_the_pump(right, rp, limits); }
};
std::shared_ptr<const std::vector<std::uint8_t>> fixture_bytes() {
    std::ifstream file(FLYNES_RUNTIME_ROM_FIXTURE, std::ios::binary);
    return std::make_shared<const std::vector<std::uint8_t>>(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
}
void select_prepared(Pair& pair, EngineFixture& engine, ActualRuntime& runtime) {
    auto choices = engine.game_choices();
    check(choices.size() == 2, "actual metadata publishes two equal-hash exact sources");
    if (choices.size() != 2) return;
    auto choice = choices[1]; std::uint64_t ticket = 0;
    check(runtime.prepared->begin(engine.snapshot(), choice, &ticket) == FLY_SESSION_V2_OK, "actual source begins preparation");
    check(runtime.prepared->stage(ticket, choice.source_choice_ref, choice.content_id, fixture_bytes(), engine.snapshot()) == FLY_SESSION_V2_OK,
          "actual exact ROM is prepared before SELECT");
    std::vector<fly_session_action_descriptor_v2> actions; engine.snapshot(&actions);
    auto* select = find_action(actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
    check(select != nullptr, "actual SELECT approval exists");
    if (select) submit_choice(engine, *select, 6201, choice.source_choice_ref);
    for (auto& a : actions) fly_session_approval_token_release_v2(a.approval_token);
    pair.pump();
    fly_session_notice_v2 receipt{}; receipt.struct_size = FLY_SESSION_NOTICE_V2_SIZE; receipt.abi_version = 2;
    fly_session_notice_v2 matching{};
    while (fly_session_read_notice_v2(engine.engine, &receipt) == FLY_SESSION_V2_OK)
        if (receipt.request_id == 6201) matching = receipt;
    check(runtime.prepared->finish_selection(FLY_SESSION_V2_ACCEPTED, 6201, matching, engine.snapshot()) == FLY_SESSION_V2_OK,
          "actual SELECT APPLIED matching receipt binds pending config");
}
void confirm(Pair& pair, EngineFixture& engine, ActualRuntime& runtime) {
    check(runtime.prepared->can_confirm(engine.snapshot()), "actual confirmation requires prepared source");
    std::vector<fly_session_action_descriptor_v2> actions; engine.snapshot(&actions);
    auto* action = find_action(actions, FLY_SESSION_ACTION_CONFIRM_GAME_CONFIG_V2);
    check(action != nullptr, "actual confirm approval exists");
    if (action) submit(engine, *action, 6301, false);
    for (auto& a : actions) fly_session_approval_token_release_v2(a.approval_token);
    pair.pump();
}
fly_session_dual_content_ref_v2 start(Pair& pair, EngineFixture& engine, ActualRuntime& runtime) {
    std::vector<fly_session_action_descriptor_v2> actions; engine.snapshot(&actions);
    auto* start = find_action(actions, FLY_SESSION_ACTION_START_DUAL_V2);
    check(start != nullptr, "bilateral actual START approval exists");
    fly_session_dual_start_ref_v2 expected{}; expected.struct_size = FLY_SESSION_DUAL_START_REF_V2_SIZE; expected.abi_version = 2;
    if (start) {
        check(fly_session_read_dual_start_ref_v2(engine.engine, start->approval_token, &expected) == FLY_SESSION_V2_OK,
              "actual getter exposes complete authoritative tuple");
        std::uint64_t revision = 0;
        auto armed = runtime.prepared->authorize_start(engine.engine, start->approval_token, &revision);
        check(armed == FLY_SESSION_V2_OK && revision == expected.view_revision, "production helper arms actual SAME START token");
        auto table = runtime.adapter->port();
        const auto before = core_loads;
        for (int field = 0; field != 4; ++field) {
            auto wrong = expected.content;
            if (field == 0) wrong.session_id[0] ^= 1;
            if (field == 1) wrong.branch_id[0] ^= 1;
            if (field == 2) wrong.content_hash[0] ^= 1;
            if (field == 3) ++wrong.timeline_epoch;
            check(table.load(table.context, &wrong) == FLY_SESSION_V2_PERMISSION_DENIED,
                  "wrong FIRST session branch content epoch rejects before actual runtime");
        }
        check(core_loads == before, "wrong first references never touch nes_load_rom");
        if (armed == FLY_SESSION_V2_OK) {
            fly_session_action_v2 request{}; request.struct_size = FLY_SESSION_ACTION_V2_SIZE; request.abi_version = 2;
            request.request_id = 6401; request.expected_view_revision = revision; request.approval_token = start->approval_token;
            check(fly_session_submit_action_v2(engine.engine, &request) == FLY_SESSION_V2_ACCEPTED, "same captured START token admitted");
        }
        pair.pump(true);
        check(table.load(table.context, &expected.content) == FLY_SESSION_V2_PERMISSION_DENIED, "consumed fullref cannot load twice");
#ifdef FLYNES_TEST_WRAP_ROM_LOAD
        check(core_loads == before + 1, "exact authorized START touches the real core exactly once");
#endif
    }
    for (auto& a : actions) fly_session_approval_token_release_v2(a.approval_token);
    return expected.content;
}
void actual_pair(std::uint8_t source, const fly_session_dual_content_ref_v2* previous,
                 fly_session_dual_content_ref_v2* captured) {
    Pair pair(source); pair.lobby();
    if (previous) {
        auto port = pair.left_runtime.adapter->port(); const auto before = core_loads;
        check(port.load(port.context, previous) == FLY_SESSION_V2_PERMISSION_DENIED && before == core_loads,
              "fresh second pair cannot reuse prior source ROM or fullref");
    }
    select_prepared(pair, pair.left, pair.left_runtime); select_prepared(pair, pair.right, pair.right_runtime);
    confirm(pair, pair.left, pair.left_runtime); confirm(pair, pair.right, pair.right_runtime); pair.pump(true);
    // Exact hash equality cannot substitute for the independently selected source/config.
    std::vector<fly_session_action_descriptor_v2> approvals;
    const auto real_snapshot = pair.left.snapshot(&approvals);
    const auto choices = pair.left.game_choices();
    auto* approval = find_action(approvals, FLY_SESSION_ACTION_START_DUAL_V2);
    check(approval && choices.size() == 2, "negative binding checks use an actual ready START view");
    if (approval && choices.size() == 2) for (int mismatch = 0; mismatch < 3; ++mismatch) {
        PreparedContent wrong(pair.left_table); std::uint64_t ticket = 0, revision = 0;
        const auto& choice = choices[mismatch == 0 ? 0 : 1];
        check(wrong.begin(real_snapshot, choice, &ticket) == FLY_SESSION_V2_OK &&
              wrong.stage(ticket, choice.source_choice_ref, choice.content_id, fixture_bytes(), real_snapshot) == FLY_SESSION_V2_OK,
              "negative metadata gate prepares equal-hash bytes without modifying actual engine selection");
        auto bound = real_snapshot;
        if (mismatch == 1) bound.pending_config_id[0] ^= 1;
        if (mismatch == 2) ++bound.pending_config_revision;
        check(wrong.bind_config(bound) == FLY_SESSION_V2_OK, "negative gate captures its mismatched owner metadata");
        check(wrong.authorize_start(pair.left.engine, approval->approval_token, &revision) == FLY_SESSION_V2_STALE,
              "authoritative getter rejects equal-hash wrong source and wrong config id/revision");
    }
    for (auto& item : approvals) fly_session_approval_token_release_v2(item.approval_token);
    *captured = start(pair, pair.left, pair.left_runtime); (void)start(pair, pair.right, pair.right_runtime); pair.pump(true);
    auto left = pair.left.snapshot(), right = pair.right.snapshot();
    check(left.game_state == FLY_SESSION_GAME_RUNNING_V2 && right.game_state == FLY_SESSION_GAME_RUNNING_V2,
          "actual public engines load ProductDualRuntimePort into NES RUNNING");
    if (left.game_state != FLY_SESSION_GAME_RUNNING_V2 || right.game_state != FLY_SESSION_GAME_RUNNING_V2) return;
    for (std::uint64_t frame = 0; frame < 12; ++frame) {
        for (EngineFixture* engine : {&pair.left, &pair.right}) {
            auto snap = engine->snapshot(); fly_session_input_v2 input{};
            input.struct_size = FLY_SESSION_INPUT_V2_SIZE; input.abi_version = 2; input.scope = snap.scope;
            input.expected_seat_revision = snap.dual_seat_revision; input.local_device_input_id = 6500 + frame;
            input.buttons = 1; input.port_mask[snap.dual_local_seat] = 1;
            input.capture_clock.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE; input.capture_clock.abi_version = 2;
            input.capture_clock.continuous_ns = 1; input.capture_clock.suspend_inclusive = 1; input.capture_clock.boot_generation[0] = 1;
            check(fly_session_submit_input_v2(engine->engine, &input) == FLY_SESSION_V2_OK, "actual engine controls runtime step");
        }
        pair.pump();
    }
    left = pair.left.snapshot(); right = pair.right.snapshot();
    std::fprintf(stdout, "prepared runtime committed indexes=%llu/%llu core_loads=%u\n",
        static_cast<unsigned long long>(left.dual_frame_index), static_cast<unsigned long long>(right.dual_frame_index), core_loads);
    // dual_frame_index is the zero-based committed frame, not the number of steps.
    check(left.dual_frame_index == 11 && left.dual_frame_index == right.dual_frame_index &&
          std::any_of(left.dual_state_digest, left.dual_state_digest + 32, [](std::uint8_t v) { return v != 0; }) &&
          std::memcmp(left.dual_state_digest, right.dual_state_digest, 32) == 0 &&
          std::memcmp(left.dual_frame_digest, right.dual_frame_digest, 32) == 0 &&
          std::memcmp(left.dual_pcm_digest, right.dual_pcm_digest, 32) == 0,
          "both actual NES runtimes commit matching stepped state/frame digests");
}
void stale_start_and_shutdown() {
    Pair pair(50); pair.lobby();
    select_prepared(pair, pair.left, pair.left_runtime); select_prepared(pair, pair.right, pair.right_runtime);
    confirm(pair, pair.left, pair.left_runtime); confirm(pair, pair.right, pair.right_runtime); pair.pump(true);
    std::vector<fly_session_action_descriptor_v2> actions; pair.left.snapshot(&actions);
    auto* approval = find_action(actions, FLY_SESSION_ACTION_START_DUAL_V2);
    auto* select = find_action(actions, FLY_SESSION_ACTION_SELECT_CONTENT_V2);
    check(approval && select, "stale START test uses actual legal actions");
    if (approval && select) {
        std::uint64_t revision = 0;
        check(pair.left_runtime.prepared->authorize_start(pair.left.engine, approval->approval_token, &revision) == FLY_SESSION_V2_OK,
              "START captured before real view invalidation");
        const auto source = pair.left.game_choices()[1];
        submit_choice(pair.left, *select, 7201, source.source_choice_ref); pair.pump();
        fly_session_action_v2 action{}; action.struct_size = FLY_SESSION_ACTION_V2_SIZE; action.abi_version = 2;
        action.request_id = 7202; action.expected_view_revision = revision; action.approval_token = approval->approval_token;
        const auto loads = core_loads;
        check(fly_session_submit_action_v2(pair.left.engine, &action) == FLY_SESSION_V2_STALE,
              "engine rejects SAME captured START token after view publication");
        pair.left_runtime.prepared->cancel(); // same fail-closed cleanup as production owner submission failure
        check(core_loads == loads && !pair.left_runtime.prepared->can_confirm(pair.left.snapshot()),
              "stale submit never loads and cleanup prevents later confirmation");
    }
    for (auto& item : actions) fly_session_approval_token_release_v2(item.approval_token);
    actions.clear();
    auto choices = pair.left.game_choices(); std::uint64_t pending = 0;
    check(!choices.empty() && pair.left_runtime.prepared->begin(pair.left.snapshot(), choices[0], &pending) == FLY_SESSION_V2_OK,
          "prepare request exists before actual shutdown");
    // Diagnostic for the separately recorded existing streaming-read error gap.
    // This is the product provider's typed error shape, on an actual read token.
    check(!pair.left.quic.granted_reads.empty(), "actual lobby has an outstanding control read");
    fly_session_port_event_v2 failed_read{};
    failed_read.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE; failed_read.abi_version = 2;
    failed_read.token = pair.left.quic.last_read_token; failed_read.event_sequence = 1;
    failed_read.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2; failed_read.terminal = 1;
    failed_read.result = FLY_SESSION_V2_IO_FAILED; failed_read.payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
    fly_session_provider_end_event_v2 end{}; end.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE; end.abi_version = 2;
    failed_read.payload_size = sizeof(end); std::memcpy(failed_read.payload, &end, sizeof(end));
    std::fprintf(stdout, "known streaming-read error admission=%d (expected typed kind DATA terminal1 END)\n",
        fly_session_deliver_v2(pair.left.quic.inbox, &failed_read));
    // Match actual owner shutdown ordering, without claiming lobby DISCONNECT exists.
    pair.left_runtime.prepared->close();
    check(fly_session_begin_shutdown_v2(pair.left.engine, 900) == FLY_SESSION_V2_ACCEPTED,
          "actual public engine begins shutdown after revocation");
    pair.pump(true);
    pair.left_runtime.prepared->reconcile(pair.left.snapshot());
    if (!choices.empty()) check(pair.left_runtime.prepared->stage(pending, choices[0].source_choice_ref,
          choices[0].content_id, fixture_bytes(), pair.left.snapshot()) == FLY_SESSION_V2_CLOSED,
          "actual shutdown fences a late successful ROM read");
    for (auto& item : actions) fly_session_approval_token_release_v2(item.approval_token);
}
}

void verify_prepared_content() {
    bool granted = true;
    ContentPort provider({{}, [&](const std::uint8_t*) { return granted; }, {}});
    PreparedContent prepared(provider.port());
    fly_session_snapshot_v2 snap{};
    snap.link_state = FLY_SESSION_LINK_CONNECTED_LOBBY_V2;
    snap.game_state = FLY_SESSION_GAME_NOT_STARTED_V2;
    snap.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    snap.scope.link_id[0] = 7;
    fly_session_game_choice_v2 choice{};
    choice.selectable = 1; choice.source_choice_ref[0] = 1; choice.content_id[0] = 2;
    auto bytes = std::make_shared<const std::vector<std::uint8_t>>(16, 3);
    std::uint64_t ticket = 0;
    check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK && ticket != 0,
          "preparation starts an exact visible source ticket");
    check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_OK,
          "matching current ticket stages one bounded immutable ROM");
    check(!prepared.can_confirm(snap), "unbound prepared content cannot confirm");
    snap.pending_config_id[0] = 4; snap.pending_config_revision = 5;
    std::copy_n(choice.content_id, 32, snap.dual_content_hash);
    check(prepared.bind_config(snap) == FLY_SESSION_V2_OK && prepared.can_confirm(snap),
          "prepared source binds the actual selected pending configuration");
    granted = false;
    check(!prepared.can_confirm(snap), "revoked source cannot confirm");
    granted = true;
    check(!prepared.can_confirm(snap), "observed revoked prepared slot never revives");
    const auto old = ticket;
    check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK && ticket > old,
          "new preparation receives a newer ticket");
    check(prepared.stage(old, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_STALE,
          "late old completion is stale");
    check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_OK,
          "old callback cannot erase the new ticket");
    check(prepared.bind_config(snap) == FLY_SESSION_V2_OK, "replacement binds config");
    fly_session_notice_v2 missing{};
    check(prepared.finish_selection(FLY_SESSION_V2_ACCEPTED, 123, missing, snap) == FLY_SESSION_V2_UNAVAILABLE,
          "SELECT admission without exact terminal receipt is not preparation success");
    check(!prepared.can_confirm(snap) && prepared.bind_config(snap) == FLY_SESSION_V2_STALE,
          "missing receipt clears authorization even if metadata SELECT was admitted");
    check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK &&
          prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_OK &&
          prepared.bind_config(snap) == FLY_SESSION_V2_OK, "prepare anew after unproven selection");
    auto changed = snap; ++changed.pending_config_revision;
    prepared.reconcile(changed);
    check(!prepared.can_confirm(snap), "configuration change invalidates prepared ROM");
    check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK, "new preparation after invalidation");
    prepared.cancel(ticket);
    check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_STALE,
          "cancel fences late successful read");
    for (bool scope_change : {false, true}) {
        check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK, "new pending read before link invalidation");
        auto changed_link = snap;
        if (scope_change) ++changed_link.scope.link_id[0];
        else changed_link.link_state = FLY_SESSION_LINK_IDLE_V2;
        prepared.reconcile(changed_link);
        check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) == FLY_SESSION_V2_STALE,
              "independent local scope or link-state invalidation rejects late bytes");
    }
    for (const auto& invalid_bytes : {std::shared_ptr<const std::vector<std::uint8_t>>{},
            std::make_shared<const std::vector<std::uint8_t>>(),
            std::make_shared<const std::vector<std::uint8_t>>(8u * 1024u * 1024u + 1, 0)}) {
        check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_OK, "prepare before byte-bound rejection");
        check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, invalid_bytes, snap) ==
              FLY_SESSION_V2_INVALID_ARGUMENT, "null empty and oversized bytes reject");
        check(prepared.stage(ticket, choice.source_choice_ref, choice.content_id, bytes, snap) ==
              FLY_SESSION_V2_STALE, "invalid bytes consume ticket so late correction cannot authorize");
    }
    prepared.close();
    check(prepared.begin(snap, choice, &ticket) == FLY_SESSION_V2_CLOSED,
          "closed prepared state never accepts new work");
    fly_session_dual_content_ref_v2 first{}, second{};
    actual_pair(10, nullptr, &first);
    actual_pair(30, &first, &second);
    stale_start_and_shutdown();
}
