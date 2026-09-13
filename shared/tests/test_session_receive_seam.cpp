// C2b step 6 — the private receive seam.
//
// fly_session_receive_stream/datagram MUST keep returning FLY_RESULT_INVALID_STATE:
// a validated-and-hashed envelope is not authentication (design spec:713) and no
// authenticated decoder exists. This test pins the diagnostic half of step 6:
// a well-formed object that IS legal on its channel is identified and recorded,
// a record that is not legal (or not framed) is not recorded, and no received
// byte — identified or not — changes any trusted reducer state.

#include <flynes/flynes_session.h>

#include "session_initial_plan.hpp"
#include "session_receive.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <string>
#include <vector>

namespace wire = flynes::session::wire;
using flynes::session::ReceivedFrameSummary;

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
        check(false, "missing golden " + path);
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
        check(false, "unreadable golden " + path);
    return bytes;
}

std::vector<std::uint8_t> framed(std::uint16_t tag, const std::vector<std::uint8_t>& body)
{
    std::vector<std::uint8_t> out(6u + body.size(), 0u);
    std::size_t written = 0u;
    const wire::Status status = wire::encode_app_frame(
        tag, body.empty() ? nullptr : body.data(), body.size(), out.data(), out.size(), &written);
    check(status == wire::Status::Ok, "encode frame");
    return out;
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

ReceivedFrameSummary last_of(fly_session_t* session)
{
    return flynes::session::last_received_frame(session);
}

void identity_and_trusted_state_are_untouched()
{
    fly_session_t* session = create_session();
    check(session != nullptr, "session exists");
    if (session == nullptr)
        return;

    const std::vector<std::uint8_t> key_binding = golden("session_signing_key_binding_v1/legal.bin");
    const std::vector<std::uint8_t> record = framed(0x0212u, key_binding);

    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, record.data(), record.size()) ==
              FLY_RESULT_INVALID_STATE,
          "the stream entry point still fails closed");

    const ReceivedFrameSummary summary = last_of(session);
    check(summary.identified, "a well-formed Control object is identified");
    check(summary.channel == static_cast<std::uint32_t>(FLY_SESSION_QUIC_CONTROL), "channel recorded");
    check(summary.frame_type_tag == 0x0212u, "frame type tag recorded");
    check(summary.type_name != nullptr && std::strcmp(summary.type_name, "0x0212") == 0,
          "codec type name recorded");
    check(summary.object_size == key_binding.size(), "object size recorded");
    check(summary.has_app_frame_hash, "classification hash recorded");

    std::uint8_t expected[32] = {};
    wire::compute_app_frame_hash(wire::QuicChannel::Control, 0x0212u, key_binding.data(),
                                 key_binding.size(), expected);
    check(std::memcmp(summary.app_frame_hash, expected, 32u) == 0,
          "classification hash matches the documented preimage");

    // Nothing about the diagnostic may reach the trusted reducer.
    const auto state = flynes::session::initial_plan_snapshot(session);
    check(state.has_value(), "private snapshot exists");
    if (state.has_value())
    {
        check(!state->started, "received bytes do not start an attempt");
        check(!state->active, "received bytes do not activate an attempt");
        check(!state->failed, "received bytes do not fail an attempt");
        check(!state->prompt_consumed, "received bytes consume no prompt");
    }
    fly_session_command command{};
    command.struct_size = FLY_SESSION_COMMAND_V1_SIZE;
    command.version = FLY_SESSION_COMMAND_VERSION_1;
    check(fly_session_poll_command(session, &command) == FLY_RESULT_OK, "poll");
    check(command.kind == FLY_SESSION_COMMAND_NONE, "received bytes issue no command");

    fly_session_destroy(session);
}

void only_legal_and_well_framed_records_are_recorded()
{
    fly_session_t* session = create_session();
    if (session == nullptr)
        return;

    const std::vector<std::uint8_t> key_binding = golden("session_signing_key_binding_v1/legal.bin");
    const std::vector<std::uint8_t> bundle = golden("canonical_input_bundle_v1/legal.bin");
    const std::vector<std::uint8_t> key_record = framed(0x0212u, key_binding);
    const std::vector<std::uint8_t> bundle_record = framed(0xFF02u, bundle);

    // CanonicalInputBundleV1 is a State Commit object only: on Control it is
    // rejected, and the previous identification must not linger.
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, key_record.data(),
                                     key_record.size()) == FLY_RESULT_INVALID_STATE,
          "legal Control record");
    check(last_of(session).identified, "recorded");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, bundle_record.data(),
                                     bundle_record.size()) == FLY_RESULT_INVALID_STATE,
          "bundle on Control still fails closed");
    check(!last_of(session).identified, "a record illegal on the channel is not recorded");

    // A datagram carries exactly one record.
    check(fly_session_receive_datagram(session, FLY_SESSION_QUIC_STATE_COMMIT, bundle_record.data(),
                                       bundle_record.size()) == FLY_RESULT_INVALID_STATE,
          "bundle on State Commit fails closed");
    const ReceivedFrameSummary datagram = last_of(session);
    check(datagram.identified, "datagram record identified");
    check(datagram.frame_type_tag == 0xFF02u, "datagram message tag");
    check(datagram.type_name != nullptr &&
              std::strcmp(datagram.type_name, "CanonicalInputBundleV1") == 0,
          "datagram message type name");

    std::vector<std::uint8_t> trailing = bundle_record;
    trailing.push_back(0u);
    check(fly_session_receive_datagram(session, FLY_SESSION_QUIC_STATE_COMMIT, trailing.data(),
                                       trailing.size()) == FLY_RESULT_INVALID_STATE,
          "datagram with trailing bytes fails closed");
    check(!last_of(session).identified, "a datagram with trailing bytes is not recorded");

    // A stream identifies the first complete record of the chunk; reassembly is
    // not part of this slice.
    std::vector<std::uint8_t> two = key_record;
    two.insert(two.end(), bundle_record.begin(), bundle_record.end());
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, two.data(), two.size()) ==
              FLY_RESULT_INVALID_STATE,
          "two records on Control fail closed");
    check(last_of(session).frame_type_tag == 0x0212u,
          "the first complete stream record is the identified one");

    // A partial trailing record does not disturb the first identification.
    std::vector<std::uint8_t> partial = key_record;
    partial.insert(partial.end(), 2u, 0u);
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, partial.data(),
                                     partial.size()) == FLY_RESULT_INVALID_STATE,
          "partial trailing record fails closed");
    check(last_of(session).frame_type_tag == 0x0212u, "first record still identified");

    // The argument contract is unchanged.
    check(fly_session_receive_stream(session, 99u, key_record.data(), key_record.size()) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "invalid channel is INVALID_ARGUMENT");
    check(fly_session_receive_stream(nullptr, FLY_SESSION_QUIC_CONTROL, key_record.data(),
                                     key_record.size()) == FLY_RESULT_INVALID_ARGUMENT,
          "null session is INVALID_ARGUMENT");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, nullptr, 1u) ==
              FLY_RESULT_INVALID_ARGUMENT,
          "null bytes with a size is INVALID_ARGUMENT");
    check(fly_session_receive_stream(session, FLY_SESSION_QUIC_CONTROL, nullptr, 0u) ==
              FLY_RESULT_INVALID_STATE,
          "empty input is still INVALID_STATE");
    check(!last_of(session).identified, "empty input identifies nothing");
    check(!last_of(nullptr).identified, "the seam is null-safe");

    fly_session_destroy(session);
}

} // namespace

int main()
{
    identity_and_trusted_state_are_untouched();
    only_legal_and_well_framed_records_are_recorded();

    if (failures != 0)
    {
        std::fprintf(stderr, "flynes_session_receive_seam: FAIL (%d)\n", failures);
        return 1;
    }
    std::printf("flynes_session_receive_seam: PASS\n");
    return 0;
}
