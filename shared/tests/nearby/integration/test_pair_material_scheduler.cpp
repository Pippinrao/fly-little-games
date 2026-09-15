#include "link/pair_material_scheduler.hpp"
#include "link/pair_reveal_scheduler.hpp"
#include "link/pair_signature_scheduler.hpp"
#include "link/pair_sas_scheduler.hpp"
#include "buffer_handle.hpp"
#include "wire/sha256.hpp"
#include "wire/p256_point.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using flynes::session::PairMaterialEffectKind;
using flynes::session::PairMaterialScheduler;
using flynes::session::PairMaterialStartV1;
using flynes::session::PairRevealEffectKind;
using flynes::session::PairRevealScheduler;
using flynes::session::PairRevealStartV1;
using flynes::session::PairSignatureEffectKind;
using flynes::session::PairSignatureScheduler;
using flynes::session::PairSignatureStartV1;
using flynes::session::PairSasEffectKind;
using flynes::session::PairSasScheduler;
using flynes::session::PairSasStartV1;
using flynes::session::wire::PairContextV1;
using flynes::session::wire::PairRoleV1;

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::fprintf(stderr, "FAIL: %s\n", message); ++failures; }
}

void be16(std::uint8_t* out, std::uint16_t value)
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

void be32(std::uint8_t* out, std::uint32_t value)
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

std::array<std::uint8_t, 65> generator()
{
    const char* value =
        "046b17d1f2e12c4247f8bce6e563a440f277037d812deb33a0f4a13945d898c296"
        "4fe342e2fe1a7f9b8ee7eb4a7c0f9e162bce33576b315ececbb6406837bf51f5";
    std::array<std::uint8_t, 65> result{};
    const auto nibble = [](char ch) -> std::uint8_t {
        return static_cast<std::uint8_t>(
            ch >= '0' && ch <= '9' ? ch - '0' : ch - 'a' + 10);
    };
    for (std::size_t i = 0; i < result.size(); ++i)
        result[i] = static_cast<std::uint8_t>(
            (nibble(value[i * 2]) << 4u) | nibble(value[i * 2 + 1]));
    return result;
}

PairContextV1 context()
{
    std::array<std::uint8_t, 80> bytes{};
    be16(bytes.data(), 1);
    be16(bytes.data() + 8, 2);
    bytes[12] = 1;
    std::fill(bytes.begin() + 16, bytes.begin() + 32, std::uint8_t{0x11});
    std::fill(bytes.begin() + 32, bytes.begin() + 48, std::uint8_t{0x22});
    be32(bytes.data() + 48, 60000);
    std::fill(bytes.begin() + 64, bytes.end(), std::uint8_t{0x33});
    PairContextV1 parsed{};
    check(flynes::session::wire::decode_pair_context_v1(
              bytes.data(), bytes.size(), &parsed) ==
              flynes::session::wire::Status::Ok,
          "scheduler fixture has canonical pair context");
    return parsed;
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
          "scheduler fixture creates provider buffer");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 hash_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    fly_session_resource_handle_v2 resource,
    const std::array<std::uint8_t, 32>& hash)
{
    static constexpr std::array<std::uint8_t, 1> reference{{1}};
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{reference.data(), 1, 0};
    check(fly_session_buffer_create_copy_v2(source, &buffer) ==
              FLY_SESSION_V2_OK,
          "scheduler fixture creates provider reference");
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 resource_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    fly_session_resource_handle_v2 resource)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_result_v2 complete(PairMaterialScheduler& scheduler,
                               fly_session_port_event_v2 event)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    if (event.payload_kind == FLY_SESSION_PROVIDER_KEY_HANDLE_V2 ||
        event.payload_kind == FLY_SESSION_PROVIDER_TLS_MATERIAL_V2)
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

fly_session_result_v2 complete_reveal(PairRevealScheduler& scheduler,
                                      fly_session_port_event_v2 event)
{
    fly_session_provider_buffer_event_v2 payload{};
    std::memcpy(&payload, event.payload, sizeof(payload));
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(payload.buffer);
    return result;
}

fly_session_result_v2 complete_signature(PairSignatureScheduler& scheduler,
                                         fly_session_port_event_v2 event)
{
    fly_session_buffer_v2_t* buffer = nullptr;
    if (event.payload_kind == FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2 ||
        event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2)
    {
        fly_session_provider_buffer_event_v2 payload{};
        std::memcpy(&payload, event.payload, sizeof(payload));
        buffer = payload.buffer;
    }
    else if (event.payload_kind == FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2)
    {
        fly_session_provider_hash_event_v2 payload{};
        std::memcpy(&payload, event.payload, sizeof(payload));
        buffer = payload.buffer;
    }
    const auto result = scheduler.complete(event);
    fly_session_buffer_release_v2(buffer);
    return result;
}

fly_session_port_event_v2 verification_event(
    const fly_session_op_token_v2& token,
    fly_session_result_v2 result = FLY_SESSION_V2_OK)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

fly_session_port_event_v2 failure_event(
    const fly_session_op_token_v2& token, std::uint32_t kind,
    fly_session_result_v2 result)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

void test_exact_provider_sequence_builds_local_pair_material()
{
    PairMaterialStartV1 start{};
    start.engine_instance_id[0] = 1;
    start.link_id[0] = 2;
    start.generation = 7;
    start.first_operation_id = 40;
    start.context = context();
    start.role = PairRoleV1::Responder;
    std::fill(start.capability_summary_hash.begin(),
              start.capability_summary_hash.end(), std::uint8_t{0x66});
    PairMaterialScheduler scheduler;
    check(scheduler.begin(start), "pair material scheduler begins once");

    const std::array<std::uint32_t, 3> purposes{{
        FLY_SESSION_KEY_DEVICE_IDENTITY_V2,
        FLY_SESSION_KEY_PAIR_ECDH_V2,
        FLY_SESSION_KEY_TLS_V2}};
    const std::array<fly_session_resource_handle_v2, 3> handles{{11, 12, 13}};
    std::array<std::uint8_t, 32> handle_hash{};
    handle_hash[0] = 1;
    for (std::size_t index = 0; index < purposes.size(); ++index)
    {
        const auto effect = scheduler.poll_effect();
        check(effect && effect->kind == PairMaterialEffectKind::GenerateKey &&
                  effect->key_purpose == purposes[index] &&
                  effect->exact_bytes == std::vector<std::uint8_t>(
                      start.context.bytes.begin(), start.context.bytes.end()),
              "key generation effect has exact purpose and attempt binding");
        if (!effect) return;
        check(complete(scheduler, hash_event(
                  effect->token, FLY_SESSION_PROVIDER_KEY_HANDLE_V2,
                  handles[index], handle_hash)) == FLY_SESSION_V2_OK,
              "typed key handle advances material scheduler");
    }

    const auto point = generator();
    for (std::size_t index = 0; index < 2; ++index)
    {
        const auto effect = scheduler.poll_effect();
        check(effect && effect->kind == PairMaterialEffectKind::ReadPublicKey &&
                  effect->resource == handles[index] &&
                  effect->public_key_encoding ==
                      FLY_SESSION_PUBLIC_KEY_X963_UNCOMPRESSED_V2,
              "identity and ECDH public points use exact X9.63 encoding");
        if (!effect) return;
        check(complete(scheduler, buffer_event(
                  effect->token, FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
                  point.data(), point.size())) == FLY_SESSION_V2_OK,
              "typed public point advances material scheduler");
    }

    const std::array<std::uint8_t, 4> tls_spki{{0x30, 0x02, 0x01, 0x01}};
    auto effect = scheduler.poll_effect();
    check(effect && effect->kind == PairMaterialEffectKind::ReadPublicKey &&
              effect->resource == handles[2] &&
              effect->public_key_encoding == FLY_SESSION_PUBLIC_KEY_DER_SPKI_V2,
          "TLS public material uses DER-SPKI encoding");
    if (!effect) return;
    check(complete(scheduler, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_KEY_PUBLIC_V2,
              tls_spki.data(), tls_spki.size())) == FLY_SESSION_V2_OK,
          "TLS SPKI bytes advance material scheduler");

    const auto spki_hash = flynes::session::wire::sha256(
        tls_spki.data(), tls_spki.size());
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairMaterialEffectKind::CreateTlsMaterial &&
              effect->resource == handles[2],
          "TLS material is created from the exact TLS key handle");
    if (!effect) return;
    check(complete(scheduler, hash_event(
              effect->token, FLY_SESSION_PROVIDER_TLS_MATERIAL_V2,
              14, spki_hash)) == FLY_SESSION_V2_OK,
          "TLS material must return the exact DER-SPKI hash");

    std::array<std::uint8_t, 32> nonce{};
    std::fill(nonce.begin(), nonce.end(), std::uint8_t{0x55});
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairMaterialEffectKind::RandomBytes &&
              effect->byte_count == 32,
          "contribution nonce comes from an exact 32-byte random effect");
    if (!effect) return;
    check(complete(scheduler, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
              nonce.data(), nonce.size())) == FLY_SESSION_V2_OK &&
              scheduler.ready(),
          "all public provider terminals seal local pair material");
    const auto material = scheduler.material();
    check(material && material->identity_key == 11 &&
              material->ecdh_key == 12 && material->tls_key == 13 &&
              material->tls_material == 14 &&
              material->contribution.role == PairRoleV1::Responder &&
              material->contribution.tls_spki_hash == spki_hash,
          "sealed material binds handles, contribution, and SPKI pin");
}

void test_stale_and_failed_key_terminals_never_seal_material()
{
    PairMaterialStartV1 start{};
    start.engine_instance_id[0] = 1;
    start.link_id[0] = 2;
    start.generation = 8;
    start.first_operation_id = 80;
    start.context = context();
    start.role = PairRoleV1::Initiator;
    start.capability_summary_hash[0] = 1;
    PairMaterialScheduler scheduler;
    check(scheduler.begin(start), "negative scheduler begins");
    const auto effect = scheduler.poll_effect();
    check(effect.has_value(), "negative scheduler exposes first effect");
    if (!effect) return;
    auto stale = failure_event(effect->token,
                               FLY_SESSION_PROVIDER_KEY_HANDLE_V2,
                               FLY_SESSION_V2_AUTH_FAILED);
    ++stale.token.connection_generation;
    check(scheduler.complete(stale) == FLY_SESSION_V2_STALE &&
              !scheduler.failed() && !scheduler.material(),
          "stale provider failure cannot poison or populate the current attempt");
    check(scheduler.complete(failure_event(
              effect->token, FLY_SESSION_PROVIDER_KEY_HANDLE_V2,
              FLY_SESSION_V2_AUTH_FAILED)) == FLY_SESSION_V2_AUTH_FAILED &&
              scheduler.failed() && !scheduler.ready() && !scheduler.material(),
          "exact typed key failure terminates without synthetic material");
}

void test_responder_reveal_requires_verified_initiator_reveal()
{
    PairRevealStartV1 start{};
    start.engine_instance_id[0] = 1;
    start.link_id[0] = 2;
    start.generation = 9;
    start.first_operation_id = 100;
    start.local_role = PairRoleV1::Responder;
    start.context = context();
    const auto point = generator();
    std::array<std::uint8_t, 32> tls_hash{};
    std::array<std::uint8_t, 32> nonce{};
    std::array<std::uint8_t, 32> capability_hash{};
    tls_hash[0] = 1; nonce[0] = 2; capability_hash[0] = 3;
    std::array<std::uint8_t, 320> initiator_bytes{};
    std::array<std::uint8_t, 320> responder_bytes{};
    check(flynes::session::wire::encode_pair_contribution_v1(
              start.context, PairRoleV1::Initiator, point, point, tls_hash,
              nonce, capability_hash,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator_bytes) == flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_pair_contribution_v1(
                  start.context, PairRoleV1::Responder, point, point, tls_hash,
                  nonce, capability_hash,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &responder_bytes) == flynes::session::wire::Status::Ok,
          "reveal scheduler fixture contributions encode");
    flynes::session::wire::PairContributionV1 initiator{};
    check(flynes::session::wire::decode_pair_contribution_v1(
              initiator_bytes.data(), initiator_bytes.size(), start.context,
              PairRoleV1::Initiator,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator) == flynes::session::wire::Status::Ok &&
              flynes::session::wire::decode_pair_contribution_v1(
                  responder_bytes.data(), responder_bytes.size(), start.context,
                  PairRoleV1::Responder,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.local_material.contribution) ==
                  flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_pair_commit_v1(
                  start.context, initiator,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.local_material.commit_bytes) ==
                  flynes::session::wire::Status::Ok,
          "reveal scheduler fixture contributions decode");
    check(flynes::session::wire::decode_pair_commit_v1(
              start.local_material.commit_bytes.data(),
              start.local_material.commit_bytes.size(), start.context,
              PairRoleV1::Initiator,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &start.initiator_commit) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_pair_commit_v1(
                  start.context, start.local_material.contribution,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.local_material.commit_bytes) ==
                  flynes::session::wire::Status::Ok &&
              flynes::session::wire::decode_pair_commit_v1(
                  start.local_material.commit_bytes.data(),
                  start.local_material.commit_bytes.size(), start.context,
                  PairRoleV1::Responder,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.responder_commit) ==
                  flynes::session::wire::Status::Ok,
          "reveal scheduler fixture commits decode");
    start.local_material.ecdh_key = 12;

    PairRevealScheduler scheduler;
    check(scheduler.begin(start), "responder reveal scheduler begins");
    auto effect = scheduler.poll_effect();
    check(effect && effect->kind == PairRevealEffectKind::AgreeKey &&
              effect->resource == 12 &&
              effect->peer_public_key == std::vector<std::uint8_t>(
                  point.begin(), point.end()),
          "ECDH effect uses only local PAIR_ECDH and peer commit point");
    check(scheduler.complete(resource_event(
              effect->token, FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2, 21)) ==
              FLY_SESSION_V2_OK,
          "ECDH terminal advances to direction key derivation");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairRevealEffectKind::DeriveKey &&
              effect->resource == 21 &&
              std::string(effect->info.begin(), effect->info.end()) ==
                  "flynes-pair-reveal-i2r-v1",
          "first HKDF derives only the i2r reveal key");
    std::array<std::uint8_t, 12> peer_nonce{};
    peer_nonce[0] = 7;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        peer_ciphertext{};
    peer_ciphertext[0] = 8;
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealBodySizeV1> peer_body{};
    check(flynes::session::wire::encode_pair_reveal_envelope_v1(
              peer_nonce, peer_ciphertext, &peer_body) ==
              flynes::session::wire::Status::Ok,
          "peer reveal fixture envelope encodes");
    std::array<std::uint8_t, 32> peer_logical_hash{};
    peer_logical_hash[0] = 9;
    check(scheduler.accept_peer_envelope(
              peer_body.data(), peer_body.size(), peer_logical_hash) ==
              FLY_SESSION_V2_ACCEPTED && scheduler.poll_effect() &&
              scheduler.poll_effect()->kind == PairRevealEffectKind::DeriveKey,
          "valid peer reveal waits behind already-fenced key derivation");
    check(scheduler.complete(resource_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 22)) ==
              FLY_SESSION_V2_OK,
          "i2r secret advances to r2i derivation");
    effect = scheduler.poll_effect();
    check(effect && std::string(effect->info.begin(), effect->info.end()) ==
                  "flynes-pair-reveal-r2i-v1" &&
              scheduler.complete(resource_event(
                  effect->token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, 23)) ==
                  FLY_SESSION_V2_OK && scheduler.poll_effect() &&
              scheduler.poll_effect()->kind == PairRevealEffectKind::AeadOpen &&
              !scheduler.local_reveal_ready(),
          "queued peer reveal opens only after both direction keys exist");

    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairRevealEffectKind::AeadOpen &&
              effect->resource == 22 && effect->nonce.size() == 12 &&
              effect->aad.size() == 124 && effect->input.size() == 336,
          "peer reveal open binds exact i2r key, nonce, AAD, and ciphertext");
    check(complete_reveal(scheduler, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              initiator_bytes.data(), initiator_bytes.size())) ==
              FLY_SESSION_V2_OK && scheduler.peer_reveal_verified(),
          "only provider plaintext bound to the initiator commit verifies");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairRevealEffectKind::RandomBytes &&
              effect->byte_count == 12,
          "verified initiator reveal unlocks responder nonce generation");
    std::array<std::uint8_t, 12> local_reveal_nonce{};
    local_reveal_nonce[0] = 10;
    check(complete_reveal(scheduler, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
              local_reveal_nonce.data(), local_reveal_nonce.size())) ==
              FLY_SESSION_V2_OK,
          "typed 12-byte nonce advances to responder reveal sealing");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairRevealEffectKind::AeadSeal &&
              effect->resource == 23 && effect->nonce ==
                  std::vector<std::uint8_t>(local_reveal_nonce.begin(),
                                            local_reveal_nonce.end()) &&
              effect->aad.size() == 124 &&
              effect->input == std::vector<std::uint8_t>(
                  responder_bytes.begin(), responder_bytes.end()),
          "responder reveal seal binds r2i key and exact contribution");
    std::array<std::uint8_t,
               flynes::session::wire::kPairRevealCiphertextAndTagSizeV1>
        local_ciphertext{};
    local_ciphertext[0] = 11;
    check(complete_reveal(scheduler, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              local_ciphertext.data(), local_ciphertext.size())) ==
              FLY_SESSION_V2_OK && scheduler.local_reveal_ready() &&
              !scheduler.ready(),
          "sealed responder reveal waits for physical delivery completion");
    const auto local_envelope = scheduler.local_envelope();
    flynes::session::wire::PairRevealEnvelopeV1 decoded{};
    check(local_envelope &&
              flynes::session::wire::decode_pair_reveal_envelope_v1(
                  local_envelope->data(), local_envelope->size(), &decoded) ==
                  flynes::session::wire::Status::Ok &&
              decoded.public_nonce == local_reveal_nonce &&
              decoded.ciphertext_and_tag == local_ciphertext &&
              scheduler.mark_local_reveal_sent() == FLY_SESSION_V2_OK &&
              scheduler.ready() &&
              scheduler.peer_logical_hash() == peer_logical_hash,
          "responder seals only after peer verification and finishes only after send");
}

PairSignatureStartV1 signature_start(PairRoleV1 local_role,
                                     std::uint64_t first_operation_id)
{
    PairSignatureStartV1 start{};
    start.engine_instance_id[0] = local_role == PairRoleV1::Initiator ? 1 : 2;
    start.link_id[0] = 3;
    start.generation = 10;
    start.first_operation_id = first_operation_id;
    start.local_role = local_role;
    start.local_identity_key = local_role == PairRoleV1::Initiator ? 41 : 42;
    start.ecdh_secret = local_role == PairRoleV1::Initiator ? 51 : 52;
    start.context = context();

    const auto point = generator();
    std::array<std::uint8_t, 32> tls_hash{};
    std::array<std::uint8_t, 32> capability_hash{};
    std::array<std::uint8_t, 32> initiator_nonce{};
    std::array<std::uint8_t, 32> responder_nonce{};
    tls_hash[0] = 4;
    capability_hash[0] = 5;
    initiator_nonce[0] = 6;
    responder_nonce[0] = 7;
    std::array<std::uint8_t, 320> initiator_bytes{};
    std::array<std::uint8_t, 320> responder_bytes{};
    check(flynes::session::wire::encode_pair_contribution_v1(
              start.context, PairRoleV1::Initiator, point, point, tls_hash,
              initiator_nonce, capability_hash,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &initiator_bytes) == flynes::session::wire::Status::Ok &&
              flynes::session::wire::encode_pair_contribution_v1(
                  start.context, PairRoleV1::Responder, point, point, tls_hash,
                  responder_nonce, capability_hash,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &responder_bytes) == flynes::session::wire::Status::Ok,
          "signature scheduler fixture contributions encode");
    check(flynes::session::wire::decode_pair_contribution_v1(
              initiator_bytes.data(), initiator_bytes.size(), start.context,
              PairRoleV1::Initiator,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &start.initiator_contribution) ==
              flynes::session::wire::Status::Ok &&
              flynes::session::wire::decode_pair_contribution_v1(
                  responder_bytes.data(), responder_bytes.size(), start.context,
                  PairRoleV1::Responder,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.responder_contribution) ==
                  flynes::session::wire::Status::Ok,
          "signature scheduler fixture contributions decode");
    std::array<std::uint8_t, 152> commit_bytes{};
    check(flynes::session::wire::encode_pair_commit_v1(
              start.context, start.initiator_contribution,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &commit_bytes) == flynes::session::wire::Status::Ok &&
              flynes::session::wire::decode_pair_commit_v1(
                  commit_bytes.data(), commit_bytes.size(), start.context,
                  PairRoleV1::Initiator,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.initiator_commit) ==
                  flynes::session::wire::Status::Ok,
          "signature scheduler fixture initiator commit binds");
    check(flynes::session::wire::encode_pair_commit_v1(
              start.context, start.responder_contribution,
              flynes::session::wire::validate_p256_uncompressed_point_callback,
              nullptr, &commit_bytes) == flynes::session::wire::Status::Ok &&
              flynes::session::wire::decode_pair_commit_v1(
                  commit_bytes.data(), commit_bytes.size(), start.context,
                  PairRoleV1::Responder,
                  flynes::session::wire::validate_p256_uncompressed_point_callback,
                  nullptr, &start.responder_commit) ==
                  flynes::session::wire::Status::Ok,
          "signature scheduler fixture responder commit binds");
    start.initiator_reveal_logical_hash = flynes::session::wire::sha256(
        initiator_bytes.data(), initiator_bytes.size());
    start.responder_reveal_logical_hash = flynes::session::wire::sha256(
        responder_bytes.data(), responder_bytes.size());
    return start;
}

void test_signature_scheduler_completes_bidirectional_provider_flow()
{
    PairSignatureScheduler initiator;
    PairSignatureScheduler responder;
    check(initiator.begin(signature_start(PairRoleV1::Initiator, 200)) &&
              responder.begin(signature_start(PairRoleV1::Responder, 300)),
          "both signature schedulers begin from the same revealed transcript");
    check(initiator.transcript_hash() == responder.transcript_hash(),
          "both roles derive exactly the same pair transcript hash");

    auto advance_derive = [](PairSignatureScheduler& scheduler,
                             fly_session_resource_handle_v2 i2r,
                             fly_session_resource_handle_v2 r2i) {
        auto effect = scheduler.poll_effect();
        check(effect && effect->kind == PairSignatureEffectKind::DeriveKey &&
                  std::string(effect->info.begin(), effect->info.end()) ==
                      "flynes-pair-control-i2r-v1",
              "signature flow derives the i2r control key first");
        if (!effect) return;
        check(scheduler.complete(resource_event(
                  effect->token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, i2r)) ==
                  FLY_SESSION_V2_OK,
              "typed i2r control key terminal advances");
        effect = scheduler.poll_effect();
        check(effect && effect->kind == PairSignatureEffectKind::DeriveKey &&
                  std::string(effect->info.begin(), effect->info.end()) ==
                      "flynes-pair-control-r2i-v1",
              "signature flow derives the r2i control key second");
        if (!effect) return;
        check(scheduler.complete(resource_event(
                  effect->token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, r2i)) ==
                  FLY_SESSION_V2_OK,
              "typed r2i control key terminal advances");
    };

    advance_derive(initiator, 501, 502);
    auto effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Sign &&
              effect->resource == 41 && effect->digest == initiator.transcript_hash() &&
              std::string(effect->domain.begin(), effect->domain.end()) ==
                  "flynes-pair-signature-v1",
          "initiator signs the exact transcript with its identity handle");
    if (!effect) return;
    std::array<std::uint8_t, 64> initiator_signature{};
    initiator_signature[0] = 1;
    initiator_signature[32] = 1;
    check(complete_signature(initiator, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
              initiator_signature.data(), initiator_signature.size())) ==
              FLY_SESSION_V2_OK,
          "initiator signature bytes require public-provider verification");
    effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Verify &&
              effect->signature == std::vector<std::uint8_t>(
                  initiator_signature.begin(), initiator_signature.end()),
          "locally produced initiator signature crosses the public verify boundary");
    if (!effect) return;
    check(complete_signature(initiator, verification_event(effect->token)) ==
              FLY_SESSION_V2_OK,
          "verified initiator signature advances to sealing");
    effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::AeadSeal &&
              effect->resource == 501 && effect->input.size() == 176,
          "initiator signature inner is sealed with i2r control key");
    if (!effect) return;
    const auto initiator_inner = effect->input;
    std::array<std::uint8_t, 192> initiator_ciphertext{};
    initiator_ciphertext[0] = 8;
    check(complete_signature(initiator, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              initiator_ciphertext.data(), initiator_ciphertext.size())) ==
              FLY_SESSION_V2_OK && initiator.local_envelope_ready(),
          "initiator publishes only provider-produced ciphertext");
    const auto initiator_envelope = initiator.local_envelope();
    check(initiator_envelope &&
              initiator.mark_local_signature_sent() == FLY_SESSION_V2_OK,
          "initiator waits for physical signature delivery");
    if (!initiator_envelope) return;

    std::array<std::uint8_t, 32> initiator_logical_hash{};
    initiator_logical_hash[0] = 9;
    check(responder.accept_peer_envelope(
              initiator_envelope->data(), initiator_envelope->size(),
              initiator_logical_hash) == FLY_SESSION_V2_ACCEPTED,
          "responder may queue an early authenticated envelope behind derivation");
    advance_derive(responder, 601, 602);
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::AeadOpen &&
              effect->resource == 601 && effect->input ==
                  std::vector<std::uint8_t>(initiator_ciphertext.begin(),
                                            initiator_ciphertext.end()),
          "responder opens the initiator ciphertext with the i2r key");
    if (!effect) return;
    check(complete_signature(responder, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              initiator_inner.data(), initiator_inner.size())) ==
              FLY_SESSION_V2_OK,
          "decoded initiator inner advances only to signature verification");
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Verify,
          "responder verifies initiator identity before signing");
    if (!effect) return;
    check(complete_signature(responder, verification_event(effect->token)) ==
              FLY_SESSION_V2_OK,
          "verified initiator unlocks responder signing");
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Sign &&
              effect->resource == 42,
          "responder signs with its own identity handle");
    if (!effect) return;
    std::array<std::uint8_t, 64> responder_signature{};
    responder_signature[0] = 2;
    responder_signature[32] = 2;
    check(complete_signature(responder, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2,
              responder_signature.data(), responder_signature.size())) ==
              FLY_SESSION_V2_OK,
          "responder signature enters public verification");
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Verify,
          "responder also verifies its locally produced signature");
    if (!effect) return;
    check(complete_signature(responder, verification_event(effect->token)) ==
              FLY_SESSION_V2_OK,
          "verified responder signature advances to sealing");
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::AeadSeal &&
              effect->resource == 602 && effect->input.size() == 176,
          "responder signature inner is sealed with r2i control key");
    if (!effect) return;
    const auto responder_inner = effect->input;
    std::array<std::uint8_t, 192> responder_ciphertext{};
    responder_ciphertext[0] = 10;
    check(complete_signature(responder, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              responder_ciphertext.data(), responder_ciphertext.size())) ==
              FLY_SESSION_V2_OK,
          "responder emits its provider-produced ciphertext");
    const auto responder_envelope = responder.local_envelope();
    check(responder_envelope &&
              responder.mark_local_signature_sent() == FLY_SESSION_V2_OK &&
              !responder.ready(),
          "responder waits for durable transcript storage after delivery");
    effect = responder.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::PersistTranscript &&
              effect->object_kind ==
                  flynes::session::wire::kPairTranscriptObjectKindV1 &&
              effect->input.size() ==
                  flynes::session::wire::kPairTranscriptSizeV1,
          "responder persists exact registered PairTranscriptV1 before approval");
    if (!effect) return;
    check(complete_signature(responder, hash_event(
              effect->token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 701,
              effect->expected_hash)) == FLY_SESSION_V2_OK &&
              responder.ready(),
          "durable object-store completion unlocks responder approval");
    if (!responder_envelope) return;

    std::array<std::uint8_t, 32> responder_logical_hash{};
    responder_logical_hash[0] = 11;
    check(initiator.accept_peer_envelope(
              responder_envelope->data(), responder_envelope->size(),
              responder_logical_hash) == FLY_SESSION_V2_ACCEPTED,
          "initiator accepts exactly one responder signature envelope");
    effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::AeadOpen &&
              effect->resource == 502,
          "initiator opens responder ciphertext with r2i control key");
    if (!effect) return;
    check(complete_signature(initiator, buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
              responder_inner.data(), responder_inner.size())) ==
              FLY_SESSION_V2_OK,
          "initiator decodes the responder signature inner");
    effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::Verify,
          "initiator verifies responder identity last");
    if (!effect) return;
    check(complete_signature(initiator, verification_event(effect->token)) ==
              FLY_SESSION_V2_OK && !initiator.ready(),
          "initiator also waits for durable transcript storage");
    effect = initiator.poll_effect();
    check(effect && effect->kind == PairSignatureEffectKind::PersistTranscript &&
              effect->input.size() ==
                  flynes::session::wire::kPairTranscriptSizeV1,
          "initiator persists the same exact registered PairTranscriptV1");
    if (!effect) return;
    check(complete_signature(initiator, hash_event(
              effect->token, FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2, 702,
              effect->expected_hash)) == FLY_SESSION_V2_OK &&
              initiator.ready(),
          "both roles are signature-ready only after durable object completion");
}

void test_sas_scheduler_derives_direction_keys_and_unbiased_code()
{
    PairSasStartV1 start{};
    start.engine_instance_id[0] = 1;
    start.link_id[0] = 2;
    start.generation = 11;
    start.first_operation_id = 400;
    start.ecdh_secret = 71;
    start.transcript_hash[0] = 9;
    PairSasScheduler scheduler;
    check(scheduler.begin(start), "SAS scheduler begins from verified signatures");
    const std::array<const char*, 3> labels{{
        "flynes-pair-gatt-i2r-v1", "flynes-pair-gatt-r2i-v1",
        "flynes-pair-sas-v1"}};
    const std::array<fly_session_resource_handle_v2, 3> handles{{72, 73, 74}};
    for (std::size_t index = 0; index < labels.size(); ++index)
    {
        const auto effect = scheduler.poll_effect();
        check(effect && effect->kind == PairSasEffectKind::DeriveKey &&
                  effect->resource == 71 && effect->byte_count == 32 &&
                  std::string(effect->info.begin(), effect->info.end()) ==
                      labels[index],
              "SAS scheduler derives exact purpose keys in fixed order");
        if (!effect) return;
        check(scheduler.complete(resource_event(
                  effect->token, FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2,
                  handles[index])) == FLY_SESSION_V2_OK,
              "typed SAS key derivation terminal advances");
    }
    auto effect = scheduler.poll_effect();
    check(effect && effect->kind == PairSasEffectKind::Hmac &&
              effect->resource == 74 && effect->input ==
                  std::vector<std::uint8_t>(start.transcript_hash.begin(),
                                            start.transcript_hash.end()),
          "SAS block zero HMACs the exact transcript hash");
    if (!effect) return;
    std::array<std::uint8_t, 32> rejected{};
    rejected.fill(0xff);
    check(scheduler.complete(buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
              rejected.data(), rejected.size())) == FLY_SESSION_V2_OK,
          "an all-rejected SAS block requests the canonical retry block");
    effect = scheduler.poll_effect();
    check(effect && effect->kind == PairSasEffectKind::Hmac &&
              effect->input.size() == 55 &&
              std::string(effect->input.begin(), effect->input.begin() + 19) ==
                  "flynes-sas-retry-v1" && effect->input.back() == 1,
          "SAS retry input appends transcript and u32be retry index");
    if (!effect) return;
    std::array<std::uint8_t, 32> accepted{};
    accepted[3] = 42;
    check(scheduler.complete(buffer_event(
              effect->token, FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
              accepted.data(), accepted.size())) == FLY_SESSION_V2_OK &&
              scheduler.ready() && scheduler.sas() ==
                  std::optional<std::array<std::uint8_t, 6>>(
                      std::array<std::uint8_t, 6>{{'0','0','0','0','4','2'}}),
          "first unbiased candidate becomes a six-digit ASCII SAS");
}

} // namespace

int main()
{
    test_exact_provider_sequence_builds_local_pair_material();
    test_stale_and_failed_key_terminals_never_seal_material();
    test_responder_reveal_requires_verified_initiator_reveal();
    test_signature_scheduler_completes_bidirectional_provider_flow();
    test_sas_scheduler_derives_direction_keys_and_unbiased_code();
    return failures == 0 ? 0 : 1;
}
