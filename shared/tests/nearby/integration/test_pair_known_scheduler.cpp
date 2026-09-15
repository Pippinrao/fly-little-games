#include "link/pair_known_scheduler.hpp"
#include "wire/gatt_fragment.hpp"
#include "wire/p256_point.hpp"
#include "wire/pair_secure.hpp"
#include "wire/sha256.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

using flynes::session::PairKnownEffectKind;
using flynes::session::PairKnownScheduler;
using flynes::session::PairKnownStartV1;
using flynes::session::wire::PairRoleV1;

std::array<std::uint8_t, 65> point(std::uint8_t seed)
{
    static constexpr char hex[] =
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
    std::array<std::uint8_t, 65> value{};
    const auto nibble = [](char c) -> std::uint8_t {
        return c <= '9' ? static_cast<std::uint8_t>(c - '0')
                        : static_cast<std::uint8_t>(c - 'a' + 10);
    };
    for (std::size_t i = 0; i < value.size(); ++i)
        value[i] = static_cast<std::uint8_t>(
            (nibble(hex[i * 2]) << 4u) | nibble(hex[i * 2 + 1]));
    (void)seed;
    return value;
}

PairKnownStartV1 start(PairRoleV1 role, bool known)
{
    PairKnownStartV1 value{};
    value.engine_instance_id[0] = 1;
    value.link_id[0] = 2;
    value.generation = 3;
    value.first_operation_id = 10;
    value.local_role = role;
    value.transcript_hash[0] = 4;
    value.initiator_public_key = point(5);
    value.responder_public_key = point(7);
    value.local_identity_key = role == PairRoleV1::Initiator ? 101 : 102;
    value.gatt_i2r_key = 201;
    value.gatt_r2i_key = 202;
    value.control_i2r_key = 301;
    value.control_r2i_key = 302;
    value.local_known = known;
    return value;
}

fly_session_port_event_v2 event_for(
    const flynes::session::PairKnownEffect& effect,
    const std::vector<std::uint8_t>& bytes,
    fly_session_buffer_v2_t** owned)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = effect.token;
    event.event_sequence = effect.token.operation_id;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    if (effect.kind == PairKnownEffectKind::Verify)
    {
        event.payload_kind = FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2;
        fly_session_provider_end_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        event.payload_size = sizeof(payload);
        std::memcpy(event.payload, &payload, sizeof(payload));
        return event;
    }
    const fly_session_bytes_v2 source{bytes.data(),
        static_cast<std::uint32_t>(bytes.size()), 0};
    check(fly_session_buffer_create_copy_v2(source, owned) == FLY_SESSION_V2_OK,
          "fixture creates provider buffer");
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = *owned;
    payload.logical_size = bytes.size();
    event.payload_kind = effect.kind == PairKnownEffectKind::Sign
        ? FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2
        : effect.kind == PairKnownEffectKind::Hmac
            ? FLY_SESSION_PROVIDER_CRYPTO_MAC_V2
            : FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

bool finish_effect(PairKnownScheduler& scheduler,
                   const std::vector<std::uint8_t>& output)
{
    const auto effect = scheduler.poll_effect();
    if (!effect) return false;
    fly_session_buffer_v2_t* owned = nullptr;
    const auto event = event_for(*effect, output, &owned);
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(owned);
    return result == FLY_SESSION_V2_OK;
}

std::vector<std::uint8_t> bytes(std::size_t size, std::uint8_t marker)
{
    std::vector<std::uint8_t> result(size, 0);
    if (size > 0) result[0] = marker;
    if (size > 32) result[32] = static_cast<std::uint8_t>(marker + 1);
    return result;
}

void drive_local_until_envelope(PairKnownScheduler& scheduler,
                                std::uint8_t marker)
{
    for (int guard = 0; guard < 8 && !scheduler.local_envelope_ready(); ++guard)
    {
        const auto effect = scheduler.poll_effect();
        check(effect.has_value(), "local known phase exposes next effect");
        if (!effect) return;
        if (effect->kind == PairKnownEffectKind::Hmac)
            check(finish_effect(scheduler, bytes(32, marker)), "known HMAC completes");
        else if (effect->kind == PairKnownEffectKind::Sign)
            check(finish_effect(scheduler, bytes(64, marker)), "known signature completes");
        else if (effect->kind == PairKnownEffectKind::AeadSeal)
        {
            auto output = effect->input;
            output.insert(output.end(), 16, marker);
            check(finish_effect(scheduler, output), "known AEAD seal completes");
        }
        else check(false, "unexpected effect while producing local envelope");
    }
}

void drain_peer_verification(PairKnownScheduler& scheduler,
                             std::uint8_t message_type, std::uint8_t marker)
{
    const auto open = scheduler.poll_effect();
    check(open && open->kind == PairKnownEffectKind::AeadOpen,
          "peer known envelope requests AEAD open");
    if (!open || open->kind != PairKnownEffectKind::AeadOpen) return;
    check(open->input.size() >= 16, "encrypted known payload has AEAD tag");
    const auto end = open->input.size() >= 16
        ? open->input.end() - 16 : open->input.end();
    check(finish_effect(scheduler,
            std::vector<std::uint8_t>(open->input.begin(), end)),
          "peer known AEAD opens");
    if (message_type == 21)
    {
        const auto verify = scheduler.poll_effect();
        check(verify && verify->kind == PairKnownEffectKind::Hmac,
              "peer status requests HMAC verification");
        if (verify && verify->kind == PairKnownEffectKind::Hmac)
            check(finish_effect(scheduler, bytes(32, marker)),
                  "peer status HMAC verifies");
    }
    else if (scheduler.known_path())
    {
        const auto verify = scheduler.poll_effect();
        check(verify && verify->kind == PairKnownEffectKind::Verify,
              "verified friend branch requests identity verification");
        if (verify && verify->kind == PairKnownEffectKind::Verify)
            check(finish_effect(scheduler, {}),
                  "peer branch signature verifies");
    }
}

void deliver(PairKnownScheduler& receiver, PairKnownScheduler& sender,
             std::uint8_t marker)
{
    const auto envelope = sender.local_envelope();
    check(envelope.has_value(), "sender has exact known envelope");
    if (!envelope) return;
    const auto type = sender.local_message_type();
    std::vector<std::uint8_t> logical;
    check(flynes::session::wire::encode_gatt_logical_message(
              type, envelope->data(), envelope->size(), &logical) ==
              flynes::session::wire::GattFragmentResult::Accepted,
          "known envelope wraps in exact GATT logical message");
    const auto hash = flynes::session::wire::sha256(logical.data(), logical.size());
    check(sender.mark_local_sent(hash) == FLY_SESSION_V2_OK,
          "sender records exact logical hash");
    check(receiver.accept_peer_envelope(type, envelope->data(), envelope->size(), hash) ==
              FLY_SESSION_V2_ACCEPTED,
          "receiver accepts expected known phase");
    drain_peer_verification(receiver, type, marker);
}

void run_pair(bool initiator_known, bool responder_known, bool expected_verified)
{
    PairKnownScheduler initiator;
    PairKnownScheduler responder;
    check(initiator.begin(start(PairRoleV1::Initiator, initiator_known)),
          "initiator known scheduler begins");
    check(responder.begin(start(PairRoleV1::Responder, responder_known)),
          "responder known scheduler begins");
    drive_local_until_envelope(initiator, 0x31);
    check(initiator.local_message_type() == 21, "status is type21");
    deliver(responder, initiator, 0x31);
    drive_local_until_envelope(responder, 0x32);
    deliver(initiator, responder, 0x32);
    drive_local_until_envelope(initiator, 0x41);
    check(initiator.local_message_type() == 17, "branch is type17");
    deliver(responder, initiator, 0x41);
    drive_local_until_envelope(responder, 0x42);
    deliver(initiator, responder, 0x42);
    check(initiator.ready() && responder.ready(),
          "both known schedulers reach the same terminal branch");
    check(initiator.known_path() == expected_verified &&
              responder.known_path() == expected_verified,
          "known path requires the logical AND of both private records");
}

} // namespace

int main()
{
    run_pair(false, false, false);
    run_pair(true, false, false);
    run_pair(false, true, false);
    run_pair(true, true, true);
    if (failures == 0) std::puts("pair known scheduler tests passed");
    return failures == 0 ? 0 : 1;
}
