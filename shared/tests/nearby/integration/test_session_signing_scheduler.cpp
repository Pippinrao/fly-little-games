#include "link/session_signing_scheduler.hpp"
#include "buffer_handle.hpp"
#include "wire/p256_point.hpp"
#include "wire/sha256.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

using flynes::session::SessionSigningEffectKind;
using flynes::session::SessionSigningScheduler;
using flynes::session::SessionSigningStartV1;
using flynes::session::wire::PairRoleV1;

int failures = 0;

void check(bool condition, const char* message)
{
    if (!condition)
    {
        std::fprintf(stderr, "FAIL: %s\n", message);
        ++failures;
    }
}

std::array<std::uint8_t, 65> point(const char* hex)
{
    std::array<std::uint8_t, 65> out{};
    const auto nibble = [](char value) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            value <= '9' ? value - '0' : value - 'a' + 10);
    };
    for (std::size_t index = 0; index < out.size(); ++index)
        out[index] = static_cast<std::uint8_t>(
            (nibble(hex[index * 2]) << 4u) |
            nibble(hex[index * 2 + 1]));
    return out;
}

const std::array<std::uint8_t, 65>& identity_point()
{
    static const auto value = point(
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5");
    return value;
}

const std::array<std::uint8_t, 65>& session_point()
{
    static const auto value = point(
        "047cf27b188d034f7e8a52380304b51ac3c08969e277f21b35a60b48fc47669978"
        "07775510db8ed040293d9ac69f7430dbba7dade63ce982299e04b79d227873d1");
    return value;
}

SessionSigningStartV1 start()
{
    SessionSigningStartV1 value{};
    value.engine_instance_id[0] = 1;
    value.link_id[0] = 2;
    value.generation = 3;
    value.first_operation_id = 40;
    value.pair_transcript_hash.fill(std::uint8_t{0x11});
    value.session_id.fill(std::uint8_t{0x22});
    value.local_role = PairRoleV1::Initiator;
    value.identity_key = 100;
    value.identity_public_key = identity_point();
    return value;
}

fly_session_port_event_v2 hash_event(
    const fly_session_op_token_v2& token,
    fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash,
    const std::uint8_t* reference, std::size_t reference_size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        reference, static_cast<std::uint32_t>(reference_size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "fixture creates durable key reference");
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), payload.hash);
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_KEY_HANDLE_V2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 buffer_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    const std::uint8_t* bytes, std::size_t size)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes, static_cast<std::uint32_t>(size), 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "fixture creates provider buffer");
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 revision_event(
    const fly_session_op_token_v2& token, std::uint64_t revision)
{
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = revision;
    payload.generation = token.connection_generation;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 immutable_event(
    const fly_session_op_token_v2& token,
    fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash)
{
    static constexpr std::array<std::uint8_t, 1> reference{{0x21}};
    auto event = hash_event(token, resource, hash,
                            reference.data(), reference.size());
    event.payload_kind = FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
    return event;
}

fly_session_result_v2 complete_buffer(
    SessionSigningScheduler& scheduler, fly_session_port_event_v2 event,
    bool hash_payload)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    if (hash_payload)
    {
        fly_session_provider_hash_event_v2 payload{};
        std::memcpy(&payload, event.payload, sizeof(payload));
        buffer = payload.buffer;
    }
    else
    {
        fly_session_provider_buffer_event_v2 payload{};
        std::memcpy(&payload, event.payload, sizeof(payload));
        buffer = payload.buffer;
    }
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(buffer);
    return result;
}

void exact_provider_flow_persists_bound_key()
{
    const auto input = start();
    SessionSigningScheduler scheduler;
    check(scheduler.begin(input), "session signing scheduler begins");
    auto effect = scheduler.poll_effect();
    check(effect && effect->kind == SessionSigningEffectKind::GenerateKey &&
              effect->key_purpose == FLY_SESSION_KEY_SESSION_SIGNING_V2 &&
              effect->scope_binding.size() == 49 &&
              std::equal(input.pair_transcript_hash.begin(),
                         input.pair_transcript_hash.end(),
                         effect->scope_binding.begin()) &&
              std::equal(input.session_id.begin(), input.session_id.end(),
                         effect->scope_binding.begin() + 32) &&
              effect->scope_binding.back() ==
                  static_cast<std::uint8_t>(input.local_role) &&
              effect->token.operation_id == 40,
          "first effect generates a session-scoped signing key");
    if (!effect) return;

    static constexpr std::array<std::uint8_t, 3> durable_ref{{0x0d, 0x0e, 0x0f}};
    const auto public_hash = flynes::session::wire::sha256(
        session_point().data(), session_point().size());
    check(complete_buffer(scheduler,
              hash_event(effect->token, 500, public_hash,
                         durable_ref.data(), durable_ref.size()), true) ==
              FLY_SESSION_V2_OK,
          "generated handle retains its durable reference and public hash");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == SessionSigningEffectKind::ReadPublicKey &&
              effect->resource == 500 &&
              effect->public_key_encoding ==
                  FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2,
          "generated public key is read using exact X9.63 encoding");
    if (!effect) return;

    check(complete_buffer(scheduler,
              buffer_event(effect->token, FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
                           session_point().data(), session_point().size()),
              false) == FLY_SESSION_V2_OK,
          "matching provider public key advances to identity signature");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == SessionSigningEffectKind::SignBinding &&
              effect->resource == input.identity_key &&
              effect->key_purpose == FLY_SESSION_KEY_DEVICE_IDENTITY_V2 &&
              std::string(effect->domain.begin(), effect->domain.end()) ==
                  "flynes-session-signing-key-binding-v1" &&
              std::any_of(effect->digest.begin(), effect->digest.end(),
                          [](std::uint8_t byte) { return byte != 0; }),
          "identity key signs the frozen binding digest and domain");
    if (!effect) return;

    std::array<std::uint8_t, 64> signature{};
    std::fill(signature.begin(), signature.begin() + 32,
              std::uint8_t{0x77});
    std::fill(signature.begin() + 32, signature.end(),
              std::uint8_t{0x44});
    check(complete_buffer(scheduler,
              buffer_event(effect->token,
                           FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
                           signature.data(), signature.size()), false) ==
              FLY_SESSION_V2_OK,
          "canonical low-S provider signature finishes the binding");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == SessionSigningEffectKind::PersistBinding &&
              std::string(effect->name_space.begin(), effect->name_space.end()) ==
                  "flynes/session-signing/v1" &&
              effect->record_key.size() == 17 &&
              std::equal(input.session_id.begin(), input.session_id.end(),
                         effect->record_key.begin()) &&
              effect->record_key.back() ==
                  static_cast<std::uint8_t>(input.local_role) &&
              effect->expected_revision == 0 && effect->value.size() == 351 &&
              effect->value[0] == 0 && effect->value[1] == 0 &&
              effect->value[2] == 0 && effect->value[3] == 3 &&
              std::equal(durable_ref.begin(), durable_ref.end(),
                         effect->value.begin() + 4),
          "binding persistence uses the frozen namespace, key, CAS and envelope");
    if (!effect) return;

    const auto persisted = effect->value;
    check(scheduler.complete(revision_event(effect->token, 42)) ==
              FLY_SESSION_V2_OK && !scheduler.ready(),
          "secure key record alone cannot authorize LINK_HELLO");
    effect = scheduler.poll_effect();
    check(effect &&
              effect->kind == SessionSigningEffectKind::PersistBindingObject &&
              effect->object_kind ==
                  flynes::session::wire::kSessionSigningBindingObjectKindV1 &&
              effect->value.size() ==
                  flynes::session::wire::kSessionSigningBindingSizeV1 &&
              effect->expected_hash == scheduler.material().binding_hash,
          "exact binding is also persisted as registered immutable 0x0212");
    if (!effect) return;
    check(complete_buffer(scheduler,
              immutable_event(effect->token, 43, effect->expected_hash), true) ==
              FLY_SESSION_V2_OK && scheduler.ready() &&
              scheduler.material().key == 500 &&
              scheduler.material().record_revision == 42 &&
              scheduler.material().binding_object_ref == 43 &&
              scheduler.material().public_key == session_point() &&
              std::equal(scheduler.material().binding.begin(),
                         scheduler.material().binding.end(),
                         persisted.begin() + 7) &&
              std::equal(scheduler.material().binding_hash.begin(),
                         scheduler.material().binding_hash.end(),
                         persisted.end() - 32),
          "successful CAS exposes only the bound persisted signing material");
    flynes::session::wire::SessionSigningBindingV1 decoded{};
    check(flynes::session::wire::decode_session_signing_binding_v1(
              scheduler.material().binding.data(),
              scheduler.material().binding.size(), input.pair_transcript_hash,
              input.session_id, input.local_role, input.identity_public_key,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &decoded) == flynes::session::wire::Status::Ok &&
              decoded.session_signing_public_key == session_point(),
          "persisted binding decodes against the exact pairing transcript");
}

void mismatched_generated_public_hash_fails_closed()
{
    SessionSigningScheduler scheduler;
    check(scheduler.begin(start()), "negative scheduler begins");
    auto effect = scheduler.poll_effect();
    if (!effect) return;
    std::array<std::uint8_t, 32> wrong_hash{};
    wrong_hash.fill(std::uint8_t{0x55});
    static constexpr std::array<std::uint8_t, 1> durable_ref{{0x01}};
    check(complete_buffer(scheduler,
              hash_event(effect->token, 501, wrong_hash,
                         durable_ref.data(), durable_ref.size()), true) ==
              FLY_SESSION_V2_OK,
          "negative fixture accepts well-formed generated-key terminal");
    effect = scheduler.poll_effect();
    if (!effect) return;
    check(complete_buffer(scheduler,
              buffer_event(effect->token, FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
                           session_point().data(), session_point().size()),
              false) == FLY_SESSION_V2_AUTH_FAILED && scheduler.failed() &&
              !scheduler.ready() && !scheduler.poll_effect(),
          "public key not matching the generated-key hash fails closed");
}

} // namespace

int main()
{
    exact_provider_flow_persists_bound_key();
    mismatched_generated_public_hash_fails_closed();
    return failures == 0 ? 0 : 1;
}
