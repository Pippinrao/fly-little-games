// Behavioural tests for the public session path (slice C2).
//
// Boundary under test:
//  - raw QUIC bytes -> still failing closed, because the shared codec dispatches
//    by an out-of-band type name and reads no in-band family/type tag, and
//    because a validated-and-hashed envelope is NOT authentication;
//  - public poll/complete -> the private initial-plan reducer seam, with the
//    seam's local monotonic command id mapped and the 64-bit transition_id left
//    explicitly unused/zero (never a truncated 128-bit wire id);
//  - public snapshot -> real reducer state instead of a constant.
#include <flynes/flynes_session.h>

#include "session_initial_plan.hpp"
#include "wire/session_codec.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

using flynes::session::InitialPlanCommand;
using flynes::session::InitialPlanCommandKind;
using flynes::session::PairRole;
using flynes::session::PlanHash;
using flynes::session::VerifiedPairEvidence;
using flynes::session::VerifiedPlanEvidence;
namespace wire = flynes::session::wire;

namespace {

int failures = 0;

void check(bool condition, const std::string& message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message.c_str());
        ++failures;
    }
}

std::vector<std::uint8_t> golden(const std::string& relative)
{
    const std::string path = std::string(FLYNES_SESSION_GOLDEN_DIR) + "/" + relative;
    std::ifstream in(path, std::ios::binary);
    std::vector<std::uint8_t> bytes;
    if (!in)
    {
        check(false, "missing golden file " + path);
        return bytes;
    }
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (end < 0)
    {
        check(false, "bad golden size " + path);
        return bytes;
    }
    in.seekg(0, std::ios::beg);
    bytes.assign(static_cast<std::size_t>(end), 0u);
    if (end != 0 && !in.read(reinterpret_cast<char*>(bytes.data()), end))
    {
        check(false, "unreadable golden " + path);
    }
    return bytes;
}

bool codec_accepts(const char* type_name, const std::vector<std::uint8_t>& bytes)
{
    std::uint8_t hash[32] = {};
    return wire::check(type_name, bytes.data(), bytes.size(), hash) == wire::Status::Ok;
}

fly_session_t* create_session()
{
    fly_session_config config{};
    config.struct_size = FLY_SESSION_CONFIG_V1_SIZE;
    config.version = FLY_SESSION_CONFIG_VERSION_1;
    fly_session_t* session = nullptr;
    check(fly_session_create(&config, &session) == FLY_RESULT_OK, "create session");
    return session;
}

fly_session_snapshot snapshot_of(fly_session_t* session)
{
    fly_session_snapshot snapshot{};
    snapshot.struct_size = FLY_SESSION_SNAPSHOT_V1_SIZE;
    snapshot.version = FLY_SESSION_SNAPSHOT_VERSION_1;
    check(fly_session_get_snapshot(session, &snapshot) == FLY_RESULT_OK, "snapshot");
    return snapshot;
}

fly_session_command poll_of(fly_session_t* session)
{
    fly_session_command command{};
    command.struct_size = FLY_SESSION_COMMAND_V1_SIZE;
    command.version = FLY_SESSION_COMMAND_VERSION_1;
    check(fly_session_poll_command(session, &command) == FLY_RESULT_OK, "poll");
    return command;
}

fly_session_command_result result_of(std::uint64_t command_id)
{
    fly_session_command_result result{};
    result.struct_size = FLY_SESSION_COMMAND_RESULT_V1_SIZE;
    result.version = FLY_SESSION_COMMAND_RESULT_VERSION_1;
    result.command_id = command_id;
    return result;
}

// Synthetic verified evidence is test-only: this fixture performs no
// authentication and exists only to drive the private reducer to a pending
// command, which the public path must then expose and complete.
PlanHash hash(std::uint8_t n)
{
    PlanHash h{};
    h[0] = n;
    return h;
}

struct ReducerFixture
{
    fly_session_t* session = nullptr;
    VerifiedPairEvidence pair{};
    VerifiedPlanEvidence plan{};

    ReducerFixture()
    {
        session = create_session();
        wire::BearerPlanBytes selected{};
        selected[0] = 2;
        selected[1] = 1;
        selected[2] = 1;
        selected[3] = 2;
        selected[4] = 1;
        selected[5] = 1;
        selected[6] = 10;
        selected[11] = 1;
        selected[12] = 1;
        pair.local_role = PairRole::Initiator;
        pair.generation = 7;
        pair.transcript = hash(1);
        pair.initiator_reveal = hash(2);
        pair.responder_reveal = hash(3);
        auto& summary = pair.initiator_summary;
        summary[1] = 1;
        summary[8] = 1;
        summary[9] = 1;
        summary[32] = 1;
        summary[64] = 1;
        std::copy(selected.begin(), selected.end(), summary.begin() + 96);
        pair.responder_summary = summary;
        plan.generation = 7;
        plan.sender = PairRole::Initiator;
        plan.receiver = PairRole::Responder;
        plan.transcript = pair.transcript;
        plan.initiator_reveal = pair.initiator_reveal;
        plan.responder_reveal = pair.responder_reveal;
        plan.selected_plan = selected;
        plan.selected_plan_hash =
            wire::domain_hash("flynes-selected-bearer-plan-v1", selected.data(), selected.size());
        plan.plan_logical_hash = hash(4);
    }

    void to_pending_plan()
    {
        check(fly_session_tick(session, 100) == FLY_RESULT_OK, "tick");
        check(flynes::session::start_initial_pair_attempt(session, pair.generation, 100), "start");
        check(flynes::session::begin_initial_verified_pair(session, pair), "begin");
        check(flynes::session::accept_initial_plan(session, plan), "accept plan");
    }

    ~ReducerFixture() { fly_session_destroy(session); }
};

// 1. Even bytes the shared codec would accept cannot be decoded here: the codec
//    takes an out-of-band type name and reads no in-band family/type tag, so the
//    receive path has no honest way to know what it was handed and must stay
//    failing closed. Well-formed-but-unauthenticated bytes are rejected too.
void receive_path_stays_failing_closed()
{
    const auto control = golden("channel_bind_v1_initial/legal.bin");
    const auto input = golden("canonical_input_bundle_v1/legal.bin");
    check(control.size() == 136u, "channel bind golden size");
    check(input.size() == 154u, "input bundle golden size");
    check(codec_accepts("ChannelBindV1", control),
          "channel bind golden is codec-legal (so this is not a garbage-only test)");
    check(codec_accepts("CanonicalInputBundleV1", input),
          "input bundle golden is codec-legal (so this is not a garbage-only test)");

    fly_session_t* session = create_session();
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, control.data(),
                                     control.size()) == FLY_RESULT_INVALID_STATE,
          "codec-legal but undecodable stream bytes stay rejected");
    check(fly_session_receive_datagram(session, FLY_SESSION_QUIC_INPUT, input.data(),
                                       input.size()) == FLY_RESULT_INVALID_STATE,
          "codec-legal but undecodable datagram bytes stay rejected");
    // Rejection is not channel-form dependent either.
    check(fly_session_receive_datagram(session, FLY_SESSION_QUIC_CONTROL, control.data(),
                                       control.size()) == FLY_RESULT_INVALID_STATE,
          "control bytes on the datagram entry point stay rejected");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_INPUT, input.data(),
                                     input.size()) == FLY_RESULT_INVALID_STATE,
          "input bytes on the stream entry point stay rejected");

    const auto reserved = golden("channel_bind_v1_initial/nonzero_reserved.bin");
    const auto truncated = golden("channel_bind_v1_initial/truncate.bin");
    const auto trailing = golden("channel_bind_v1_initial/trailing.bin");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, reserved.data(),
                                     reserved.size()) == FLY_RESULT_INVALID_STATE,
          "nonzero reserved rejected");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, truncated.data(),
                                     truncated.size()) == FLY_RESULT_INVALID_STATE,
          "truncated rejected");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, trailing.data(),
                                     trailing.size()) == FLY_RESULT_INVALID_STATE,
          "trailing bytes rejected");

    const std::uint8_t byte = 0u;
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, &byte, 1u) ==
              FLY_RESULT_INVALID_STATE,
          "single byte rejected");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, nullptr, 0u) ==
              FLY_RESULT_INVALID_STATE,
          "empty envelope rejected");
    check(fly_session_receive_stream(session, 99u, control.data(), control.size()) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "undeclared channel argument rejected");
    check(fly_session_receive_datagram(session, 99u, control.data(), control.size()) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "undeclared datagram channel argument rejected");
    check(fly_session_receive_stream(nullptr, FLY_SESSION_QUIC_CONTROL, control.data(),
                                     control.size()) == FLY_RESULT_INVALID_ARGUMENT,
          "null session rejected");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, nullptr, 1u) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "null bytes with nonzero size rejected");
    fly_session_destroy(session);
}

// 2. Nothing about the receive path may disturb the trusted evidence reducer.
void receive_path_never_reaches_the_trusted_seam()
{
    const auto control = golden("channel_bind_v1_initial/legal.bin");
    fly_session_t* session = create_session();
    (void)fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, control.data(),
                                     control.size());
    const auto state = flynes::session::initial_plan_snapshot(session);
    check(state.has_value(), "private snapshot exists");
    check(!state->started && !state->active && !state->failed && !state->locked &&
              !state->mutually_locked && !state->prompt_consumed,
          "received bytes do not touch the trusted evidence reducer");
    check(!flynes::session::poll_initial_plan_command(session).has_value(),
          "received bytes issue no reducer command");
    fly_session_destroy(session);
}

// 3. Public poll exposes the seam's local monotonic command id, never a wire
//    transition id.
void poll_and_complete_round_trip()
{
    ReducerFixture fixture;
    fixture.to_pending_plan();
    const auto seam = flynes::session::poll_initial_plan_command(fixture.session);
    check(seam.has_value(), "seam command pending");
    check(seam->id != 0u, "seam id nonzero");

    const fly_session_command first = poll_of(fixture.session);
    check(first.command_id == seam->id, "public poll exposes the local seam id");
    check(first.transition_id == 0u, "transition_id stays unused/zero");
    check(poll_of(fixture.session).command_id == first.command_id, "repeat poll repeats the id");

    fly_session_command_result result = result_of(first.command_id);
    check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_OK,
          "completing the polled command succeeds");

    const auto next_seam = flynes::session::poll_initial_plan_command(fixture.session);
    check(next_seam.has_value(), "seam issued the next command");
    const fly_session_command next = poll_of(fixture.session);
    check(next.command_id == next_seam->id && next.command_id > first.command_id,
          "seam issued a new monotonic command id");
    check(next.transition_id == 0u, "next command also leaves transition_id zero");
}

// 4. Stale, duplicate, un-polled, failed or wire-id-carrying completions stay
//    rejected, exactly as the seam's one-outstanding-command contract specifies.
void completion_contract_is_enforced()
{
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        const fly_session_command first = poll_of(fixture.session);
        fly_session_command_result result = result_of(first.command_id);
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_OK,
              "first completion accepted");
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_INVALID_STATE,
              "duplicate completion of a stale id rejected");
    }
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        fly_session_command_result result = result_of(1u);
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_INVALID_STATE,
              "completion without a public poll rejected");
    }
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        const fly_session_command first = poll_of(fixture.session);
        fly_session_command_result result = result_of(first.command_id);
        result.transition_id = 1u; // a 64-bit stand-in for a 128-bit wire id
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_INVALID_ARGUMENT,
              "a wire transition id cannot be carried through the public result");
    }
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        const fly_session_command first = poll_of(fixture.session);
        fly_session_command_result result = result_of(first.command_id);
        result.result = -1;
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_INVALID_STATE,
              "reported executor failure is rejected and cannot authorize an effect");
    }
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        fly_session_command_result result{};
        result.struct_size = FLY_SESSION_COMMAND_RESULT_V1_SIZE;
        result.version = FLY_SESSION_COMMAND_RESULT_VERSION_1;
        check(fly_session_complete_command(fixture.session, &result) == FLY_RESULT_INVALID_STATE,
              "idle completion rejected");
    }
    {
        fly_session_t* session = create_session();
        fly_session_command_result result = result_of(1u);
        check(fly_session_complete_command(session, &result) == FLY_RESULT_INVALID_STATE,
              "completion on a handle with no attempt rejected");
        check(poll_of(session).command_id == 0u && poll_of(session).kind ==
                                                       FLY_SESSION_COMMAND_NONE,
              "idle poll reports no command");
        fly_session_destroy(session);
    }
}

// 5. The snapshot reflects real reducer state instead of a constant.
void snapshot_projects_reducer_state()
{
    {
        fly_session_t* session = create_session();
        check(fly_session_tick(session, 100) == FLY_RESULT_OK, "tick");
        const fly_session_snapshot idle = snapshot_of(session);
        check(idle.ui_state == FLY_SESSION_UI_IDLE, "fresh handle is idle");
        check(idle.authority_role == 0u && idle.mode == 0u, "no invented authority or mode");
        fly_session_destroy(session);
    }
    {
        ReducerFixture fixture;
        fixture.to_pending_plan();
        const fly_session_snapshot live = snapshot_of(fixture.session);
        check(live.ui_state == FLY_SESSION_UI_UNSPECIFIED,
              "an in-flight initial plan is reported as unspecified, never idle");
        check(live.authority_role == 0u && live.mode == 0u, "no invented authority or mode");
        check(live.state_verified_through.kind == FLY_EVIDENCE_CURSOR_NONE,
              "no unverified evidence asserted");
        check(live.committed_through.has_frame == 0u && live.committed_through.frame_index == 0u,
              "no claimed committed frame");
    }
}

// 6. submit_event stays fail-closed for every v1 kind: the v1 event struct has no
//    payload, so no private reducer entry point can be reached from it.
void submit_event_stays_fail_closed()
{
    fly_session_t* session = create_session();
    for (std::uint32_t kind = FLY_SESSION_EVENT_USER; kind <= FLY_SESSION_EVENT_TRANSPORT; ++kind)
    {
        fly_session_event event{};
        event.struct_size = FLY_SESSION_EVENT_V1_SIZE;
        event.version = FLY_SESSION_EVENT_VERSION_1;
        event.kind = kind;
        check(fly_session_submit_event(session, &event) == FLY_RESULT_INVALID_STATE,
              "payload-less event kind rejected");
    }
    fly_session_event bad{};
    bad.struct_size = FLY_SESSION_EVENT_V1_SIZE;
    bad.version = FLY_SESSION_EVENT_VERSION_1;
    bad.kind = FLY_SESSION_EVENT_USER;
    bad.reserved = 1u;
    check(fly_session_submit_event(session, &bad) == FLY_RESULT_INVALID_ARGUMENT,
          "nonzero reserved rejected");
    check(fly_session_submit_event(nullptr, &bad) == FLY_RESULT_INVALID_ARGUMENT,
          "null session rejected");
    fly_session_destroy(session);
}

} // namespace

static_assert(FLY_SESSION_UI_IDLE == 1, "UI_IDLE keeps its v1 value");
static_assert(FLY_SESSION_UI_UNSPECIFIED == 0, "UNSPECIFIED is the documented unset value");
static_assert(FLY_SESSION_COMMAND_NONE == 0, "COMMAND_NONE keeps its v1 value");
static_assert(FLY_RESULT_OK == 0, "success convention is fly_result_code OK");

int main()
{
    receive_path_stays_failing_closed();
    receive_path_never_reaches_the_trusted_seam();
    poll_and_complete_round_trip();
    completion_contract_is_enforced();
    snapshot_projects_reducer_state();
    submit_event_stays_fail_closed();
    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_public_path_test: %d failure(s)\n", failures);
        return 1;
    }
    std::puts("flynes_session_public_path_test: PASS");
    return 0;
}
