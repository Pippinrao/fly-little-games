#include "link/pair_pipeline.hpp"
#include "wire/pair_crypto.hpp"
#include "wire/sha256.hpp"

#include <array>
#include <cwchar>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#endif

using namespace flynes::session;

namespace {

int failures = 0;
/* This suite drives real ECDSA through Windows CNG, so on non-Windows
 * toolchains the only consumer of these two helpers is compiled out. GCC then
 * flags them as unused where MSVC does not. */
[[maybe_unused]] void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

[[maybe_unused]] CapabilitySummary summary()
{
    CapabilitySummary value{};
    value[1] = 1; value[8] = 1; value[9] = 1; value[32] = 1;
    value[64] = 1; value[96] = 2; value[97] = 1; value[98] = 1;
    value[99] = 2; value[100] = 1; value[102] = 10;
    value[107] = 1; value[108] = 1;
    return value;
}

#ifdef _WIN32

class P256Key final
{
public:
    explicit P256Key(const wchar_t* algorithm)
        : is_ecdsa_(std::wcscmp(algorithm, BCRYPT_ECDSA_P256_ALGORITHM) == 0)
    {
        check(BCryptOpenAlgorithmProvider(&algorithm_, algorithm, nullptr, 0) == 0,
              "CNG algorithm opens");
        check(BCryptGenerateKeyPair(algorithm_, &key_, 256, 0) == 0 &&
                  BCryptFinalizeKeyPair(key_, 0) == 0,
              "CNG P-256 key generates");
    }
    ~P256Key()
    {
        if (key_) BCryptDestroyKey(key_);
        if (algorithm_) BCryptCloseAlgorithmProvider(algorithm_, 0);
    }
    P256Key(const P256Key&) = delete;
    P256Key& operator=(const P256Key&) = delete;

    std::array<std::uint8_t, 65> public_key() const
    {
        std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
        ULONG written = 0;
        check(BCryptExportKey(key_, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                             blob.data(), static_cast<ULONG>(blob.size()),
                             &written, 0) == 0 && written == blob.size(),
              "CNG public key exports");
        std::array<std::uint8_t, 65> point{};
        point[0] = 4;
        std::copy(blob.begin() + sizeof(BCRYPT_ECCKEY_BLOB), blob.end(),
                  point.begin() + 1);
        return point;
    }

    std::array<std::uint8_t, 64> sign(const PlanHash& digest) const
    {
        std::array<std::uint8_t, 64> signature{};
        ULONG written = 0;
        check(is_ecdsa_ &&
                  BCryptSignHash(key_, nullptr,
                                 const_cast<PUCHAR>(digest.data()),
                                 static_cast<ULONG>(digest.size()),
                                 signature.data(),
                                 static_cast<ULONG>(signature.size()), &written, 0) == 0 &&
                  written == static_cast<ULONG>(signature.size()),
              "CNG signs one precomputed SHA-256 digest");
        static constexpr std::array<std::uint8_t, 32> order{{
            0xff, 0xff, 0xff, 0xff, 0x00, 0x00, 0x00, 0x00,
            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xbc, 0xe6, 0xfa, 0xad, 0xa7, 0x17, 0x9e, 0x84,
            0xf3, 0xb9, 0xca, 0xc2, 0xfc, 0x63, 0x25, 0x51}};
        static constexpr std::array<std::uint8_t, 32> half_order{{
            0x7f, 0xff, 0xff, 0xff, 0x80, 0x00, 0x00, 0x00,
            0x7f, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
            0xde, 0x73, 0x7d, 0x56, 0xd3, 0x8b, 0xcf, 0x42,
            0x79, 0xdc, 0xe5, 0x61, 0x7e, 0x31, 0x92, 0xa8}};
        if (std::lexicographical_compare(
                half_order.begin(), half_order.end(),
                signature.begin() + 32, signature.end()))
        {
            unsigned borrow = 0;
            for (std::size_t offset = 0; offset < 32; ++offset)
            {
                const auto index = 31 - offset;
                const int difference = static_cast<int>(order[index]) -
                    static_cast<int>(signature[32 + index]) -
                    static_cast<int>(borrow);
                signature[32 + index] = static_cast<std::uint8_t>(
                    difference < 0 ? difference + 256 : difference);
                borrow = difference < 0 ? 1u : 0u;
            }
        }
        return signature;
    }

    bool verify(const std::array<std::uint8_t, 65>& point,
                const PlanHash& digest,
                const std::array<std::uint8_t, 64>& signature) const
    {
        BCRYPT_KEY_HANDLE imported = nullptr;
        std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
        auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
        header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
        header->cbKey = 32;
        std::copy(point.begin() + 1, point.end(),
                  blob.begin() + sizeof(BCRYPT_ECCKEY_BLOB));
        if (point[0] != 4 ||
            BCryptImportKeyPair(algorithm_, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                                &imported, blob.data(),
                                static_cast<ULONG>(blob.size()), 0) != 0)
            return false;
        const bool valid = BCryptVerifySignature(
            imported, nullptr, const_cast<PUCHAR>(digest.data()),
            static_cast<ULONG>(digest.size()),
            const_cast<PUCHAR>(signature.data()),
            static_cast<ULONG>(signature.size()), 0) == 0;
        BCryptDestroyKey(imported);
        return valid;
    }

    std::array<std::uint8_t, 32> agree(
        const std::array<std::uint8_t, 65>& peer) const
    {
        BCRYPT_KEY_HANDLE imported = nullptr;
        BCRYPT_SECRET_HANDLE secret = nullptr;
        std::array<std::uint8_t, sizeof(BCRYPT_ECCKEY_BLOB) + 64> blob{};
        auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(blob.data());
        header->dwMagic = BCRYPT_ECDH_PUBLIC_P256_MAGIC;
        header->cbKey = 32;
        std::copy(peer.begin() + 1, peer.end(),
                  blob.begin() + sizeof(BCRYPT_ECCKEY_BLOB));
        std::array<std::uint8_t, 32> result{};
        ULONG written = 0;
        check(!is_ecdsa_ && peer[0] == 4 &&
                  BCryptImportKeyPair(algorithm_, nullptr, BCRYPT_ECCPUBLIC_BLOB,
                                      &imported, blob.data(),
                                      static_cast<ULONG>(blob.size()), 0) == 0 &&
                  BCryptSecretAgreement(key_, imported, &secret, 0) == 0 &&
                  BCryptDeriveKey(secret, BCRYPT_KDF_RAW_SECRET, nullptr,
                                  result.data(), static_cast<ULONG>(result.size()),
                                  &written, 0) == 0 &&
                  written == static_cast<ULONG>(result.size()),
              "CNG ECDH derives a 256-bit shared secret");
        if (secret) BCryptDestroySecret(secret);
        if (imported) BCryptDestroyKey(imported);
        return result;
    }

private:
    bool is_ecdsa_ = false;
    BCRYPT_ALG_HANDLE algorithm_ = nullptr;
    BCRYPT_KEY_HANDLE key_ = nullptr;
};

class CngPairProvider final : public PairPipelineCryptoProvider
{
public:
    CngPairProvider(const P256Key& verifier, const P256Key& local_ecdh,
                    std::array<std::uint8_t, 65> peer_ecdh)
        : verifier_(verifier), secret_(local_ecdh.agree(peer_ecdh)) {}

    bool verify_signature(const fly_session_op_token_v2& token, PairRole,
                          const std::array<std::uint8_t, 65>& public_key,
                          const PlanHash& digest,
                          const std::array<std::uint8_t, 64>& signature) override
    {
        return token.operation_id != 0 &&
               verifier_.verify(public_key, digest, signature);
    }

    bool derive_sas(const PlanHash& transcript,
                    std::array<std::uint8_t, 6>& sas) override
    {
        const auto key = wire::hkdf_extract_sha256(
            transcript.data(), transcript.size(), secret_.data(), secret_.size());
        return wire::derive_pair_sas_v1(key, transcript, 16, &sas) ==
               wire::PairCryptoStatus::Ok;
    }

    bool verify_key_confirmation(
        const fly_session_op_token_v2& token, PairRole role,
        const PlanHash& transcript, const PlanHash& confirmation) override
    {
        std::array<std::uint8_t, 33> input{};
        input[0] = static_cast<std::uint8_t>(role);
        std::copy(transcript.begin(), transcript.end(), input.begin() + 1);
        return token.operation_id != 0 && confirmation == wire::hmac_sha256(
            secret_.data(), secret_.size(), input.data(), input.size());
    }

    PlanHash confirmation(PairRole role, const PlanHash& transcript) const
    {
        std::array<std::uint8_t, 33> input{};
        input[0] = static_cast<std::uint8_t>(role);
        std::copy(transcript.begin(), transcript.end(), input.begin() + 1);
        return wire::hmac_sha256(secret_.data(), secret_.size(),
                                 input.data(), input.size());
    }

private:
    const P256Key& verifier_;
    PlanHash secret_{};
};

wire::PairContributionV1 contribution(
    wire::PairRoleV1 role, const CapabilitySummary& capability,
    const std::array<std::uint8_t, 65>& identity,
    const std::array<std::uint8_t, 65>& ephemeral)
{
    wire::PairContributionV1 value{};
    value.role = role;
    value.bytes[12] = static_cast<std::uint8_t>(role);
    std::copy(identity.begin(), identity.end(), value.bytes.begin() + 88);
    std::copy(ephemeral.begin(), ephemeral.end(), value.bytes.begin() + 153);
    value.bytes[218] = 1;
    value.capability_summary_hash = wire::domain_hash(
        "flynes-pair-capability-summary-v1", capability.data(), capability.size());
    std::copy(value.capability_summary_hash.begin(),
              value.capability_summary_hash.end(), value.bytes.begin() + 282);
    value.identity_public_key = identity;
    value.ephemeral_public_key = ephemeral;
    value.tls_spki_hash[0] = 1;
    value.commitment = wire::pair_commitment_v1(value.bytes.data(), value.bytes.size());
    return value;
}

PairAuthStartV1 start(PairRole local_role,
                      const wire::PairContributionV1& initiator,
                      const wire::PairContributionV1& responder)
{
    PairAuthStartV1 value{};
    value.generation = 7; value.engine_instance_id[0] = 1; value.link_id[0] = 2;
    value.signature_operation_ids = {{1, 2}};
    value.key_confirm_operation_ids = {{3, 4}};
    value.local_role = local_role; value.entry_mode = 1;
    value.pair_context_hash[0] = 9;
    value.initiator_summary = summary(); value.responder_summary = summary();
    value.initiator_contribution = initiator;
    value.responder_contribution = responder;
    value.initiator_commit.sender = wire::PairRoleV1::Initiator;
    value.initiator_commit.receiver = wire::PairRoleV1::Responder;
    value.initiator_commit.pair_context_hash = value.pair_context_hash;
    value.initiator_commit.commitment = initiator.commitment;
    value.initiator_commit.ephemeral_public_key = initiator.ephemeral_public_key;
    value.responder_commit.sender = wire::PairRoleV1::Responder;
    value.responder_commit.receiver = wire::PairRoleV1::Initiator;
    value.responder_commit.pair_context_hash = value.pair_context_hash;
    value.responder_commit.commitment = responder.commitment;
    value.responder_commit.ephemeral_public_key = responder.ephemeral_public_key;
    value.initiator_reveal_logical_hash[0] = 2;
    value.responder_reveal_logical_hash[0] = 3;
    return value;
}

void test_two_party_real_crypto_pipeline()
{
    P256Key initiator_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key responder_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key initiator_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    P256Key responder_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    const auto initiator = contribution(wire::PairRoleV1::Initiator, summary(),
        initiator_identity.public_key(), initiator_ecdh.public_key());
    const auto responder = contribution(wire::PairRoleV1::Responder, summary(),
        responder_identity.public_key(), responder_ecdh.public_key());
    CngPairProvider initiator_provider(initiator_identity, initiator_ecdh,
                                       responder_ecdh.public_key());
    CngPairProvider responder_provider(responder_identity, responder_ecdh,
                                       initiator_ecdh.public_key());
    PairPipeline left(initiator_provider), right(responder_provider);
    check(left.begin(start(PairRole::Initiator, initiator, responder)) &&
              right.begin(start(PairRole::Responder, initiator, responder)),
          "both pair pipelines bind the same transcript");
    const auto transcript = left.transcript_hash();
    check(transcript == right.transcript_hash(), "transcript agrees");
    const auto initiator_signature = initiator_identity.sign(transcript);
    const auto responder_signature = responder_identity.sign(transcript);
    for (auto* pipeline : {&left, &right}) {
        check(pipeline->submit_signature(PairRole::Initiator, initiator_signature),
              "initiator real signature verifies");
        check(pipeline->submit_signature(PairRole::Responder, responder_signature),
              "responder real signature verifies");
    }
    const auto sas = left.sas();
    check(sas.has_value() && sas == right.sas(), "ECDH/HKDF SAS agrees");
    check(left.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *sas) &&
              right.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *sas),
          "both users approve the same derived SAS");
    for (auto* pipeline : {&left, &right}) {
        check(pipeline->submit_key_confirmation(
                  PairRole::Initiator,
                  initiator_provider.confirmation(PairRole::Initiator, transcript)),
              "initiator key confirmation verifies");
        check(pipeline->submit_key_confirmation(
                  PairRole::Responder,
                  responder_provider.confirmation(PairRole::Responder, transcript)),
              "responder key confirmation verifies");
        check(pipeline->accept_capability(PairRole::Initiator, summary(), {{4}}) &&
                  pipeline->accept_capability(PairRole::Responder, summary(), {{5}}),
              "capabilities open after cryptographic gates");
    }
    const auto left_evidence = left.take_verified_evidence();
    const auto right_evidence = right.take_verified_evidence();
    check(left_evidence && right_evidence && left_evidence->authenticated() &&
              right_evidence->authenticated() &&
              left_evidence->transcript == right_evidence->transcript,
          "only complete real-crypto flows seal identical evidence");
}

void test_wrong_signature_and_sas_never_reach_key_confirmation()
{
    P256Key first_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key second_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key first_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    P256Key second_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    const auto first = contribution(wire::PairRoleV1::Initiator, summary(),
        first_identity.public_key(), first_ecdh.public_key());
    const auto second = contribution(wire::PairRoleV1::Responder, summary(),
        second_identity.public_key(), second_ecdh.public_key());
    CngPairProvider provider(first_identity, first_ecdh, second_ecdh.public_key());
    PairPipeline pipeline(provider);
    check(pipeline.begin(start(PairRole::Initiator, first, second)),
          "negative pipeline begins");
    auto signature = first_identity.sign(pipeline.transcript_hash());
    signature[0] ^= 1;
    check(!pipeline.submit_signature(PairRole::Initiator, signature) &&
              pipeline.failed() && !pipeline.take_verified_evidence(),
          "tampered prehashed signature fails closed without evidence");

    PairPipeline sas_pipeline(provider);
    check(sas_pipeline.begin(start(PairRole::Initiator, first, second)),
          "SAS negative pipeline begins");
    check(sas_pipeline.submit_signature(
              PairRole::Initiator, first_identity.sign(sas_pipeline.transcript_hash())) &&
              sas_pipeline.submit_signature(
                  PairRole::Responder, second_identity.sign(sas_pipeline.transcript_hash())),
          "SAS negative signatures pass");
    auto wrong = *sas_pipeline.sas(); wrong[0] ^= 1;
    check(!sas_pipeline.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, wrong) &&
              sas_pipeline.failed(), "wrong SAS fails before key confirmation");
}

void test_prehashed_digest_is_verified_exactly_once()
{
    P256Key signer(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key other(BCRYPT_ECDSA_P256_ALGORITHM);
    PlanHash digest{};
    for (std::size_t index = 0; index < digest.size(); ++index)
        digest[index] = static_cast<std::uint8_t>(index + 1);
    const auto signature = signer.sign(digest);
    check(signer.verify(signer.public_key(), digest, signature),
          "independent verifier accepts the original prehashed digest");
    const auto double_hashed = wire::sha256(digest.data(), digest.size());
    check(!signer.verify(signer.public_key(), double_hashed, signature),
          "independent verifier rejects a provider-side double hash");
    check(!signer.verify(other.public_key(), digest, signature),
          "signature cannot cross an identity purpose handle");
}

fly_session_port_event_v2 verification_event(
    const fly_session_op_token_v2& token, fly_session_result_v2 result,
    std::uint32_t payload_kind = FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2)
{
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = payload_kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    return event;
}

void test_public_verification_completion_is_the_only_signature_receipt()
{
    P256Key initiator_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key responder_identity(BCRYPT_ECDSA_P256_ALGORITHM);
    P256Key initiator_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    P256Key responder_ecdh(BCRYPT_ECDH_P256_ALGORITHM);
    const auto initiator = contribution(
        wire::PairRoleV1::Initiator, summary(), initiator_identity.public_key(),
        initiator_ecdh.public_key());
    const auto responder = contribution(
        wire::PairRoleV1::Responder, summary(), responder_identity.public_key(),
        responder_ecdh.public_key());
    PairVerificationScheduler scheduler;
    const auto auth_start = start(PairRole::Initiator, initiator, responder);
    check(scheduler.begin(auth_start), "public verification scheduler begins");
    const auto initiator_signature = initiator_identity.sign(
        scheduler.transcript_hash());
    check(scheduler.queue_signature(PairRole::Initiator, initiator_signature) ==
              FLY_SESSION_V2_ACCEPTED,
          "canonical signature queues one public verification effect");
    const auto first = scheduler.poll_signature_effect();
    check(first && first->token.operation_id ==
                       auth_start.signature_operation_ids[0] &&
              first->role == PairRole::Initiator &&
              first->public_key == initiator.identity_public_key &&
              first->digest == scheduler.transcript_hash() &&
              first->signature == initiator_signature,
          "effect binds the exact provider token, key, digest, and signature");
    if (!first) return;

    auto stale = verification_event(first->token, FLY_SESSION_V2_OK);
    ++stale.token.operation_id;
    check(scheduler.complete_signature(stale) == FLY_SESSION_V2_STALE &&
              !scheduler.signature_verified(PairRole::Initiator),
          "stale provider completion cannot advance authentication");
    auto forged = verification_event(
        first->token, FLY_SESSION_V2_OK,
        FLY_SESSION_PAIR_SIGNATURE_VERIFIED_V2);
    check(scheduler.complete_signature(forged) ==
              FLY_SESSION_V2_CONTRACT_VIOLATION &&
              !scheduler.signature_verified(PairRole::Initiator),
          "an internal sealed receipt cannot be injected as provider output");
    check(initiator_identity.verify(first->public_key, first->digest,
                                    first->signature) &&
              scheduler.complete_signature(
                  verification_event(first->token, FLY_SESSION_V2_OK)) ==
                  FLY_SESSION_V2_OK &&
              scheduler.signature_verified(PairRole::Initiator),
          "independently verified public completion seals initiator receipt");

    const auto responder_signature = responder_identity.sign(
        scheduler.transcript_hash());
    check(scheduler.queue_signature(PairRole::Responder, responder_signature) ==
              FLY_SESSION_V2_ACCEPTED,
          "responder signature queues only after initiator terminal");
    const auto second = scheduler.poll_signature_effect();
    check(second && responder_identity.verify(
                        second->public_key, second->digest, second->signature) &&
              scheduler.complete_signature(
                  verification_event(second->token, FLY_SESSION_V2_OK)) ==
                  FLY_SESSION_V2_OK &&
              scheduler.signatures_verified(),
          "both exact public provider terminals open the next auth gate");
    check(scheduler.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2),
          "local approval is bound after both public signature terminals");

    const std::array<std::uint8_t, 32> mac_key{{
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32}};
    for (const auto role : {PairRole::Initiator, PairRole::Responder})
    {
        std::vector<std::uint8_t> exact_input{
            'f', 'l', 'y', 'n', 'e', 's', '-', 'k', 'e', 'y', '-',
            'c', 'o', 'n', 'f', 'i', 'r', 'm', '-', 'v', '1',
            static_cast<std::uint8_t>(role)};
        const auto expected_mac = wire::hmac_sha256(
            mac_key.data(), mac_key.size(), exact_input.data(), exact_input.size());
        check(scheduler.queue_key_confirmation(
                  role, role == PairRole::Initiator ? 71 : 72,
                  exact_input, expected_mac) == FLY_SESSION_V2_ACCEPTED,
              "key confirmation queues one exact HMAC provider effect");
        const auto effect = scheduler.poll_key_confirmation_effect();
        check(effect && effect->role == role &&
                  effect->key == (role == PairRole::Initiator ? 71 : 72) &&
                  effect->exact_input == exact_input,
              "HMAC effect preserves the opaque purpose key and exact preimage");
        if (!effect) return;
        fly_session_buffer_v2_t* buffer = nullptr;
        const fly_session_bytes_v2 source{
            expected_mac.data(),
            static_cast<std::uint32_t>(expected_mac.size()), 0};
        check(fly_session_buffer_create_copy_v2(source, &buffer) ==
                  FLY_SESSION_V2_OK,
              "provider MAC result owns immutable bytes");
        fly_session_provider_buffer_event_v2 payload{};
        payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
        payload.abi_version = FLY_SESSION_ABI_VERSION_2;
        payload.buffer = buffer;
        payload.logical_size = expected_mac.size();
        auto event = verification_event(
            effect->token, FLY_SESSION_V2_OK,
            FLY_SESSION_PROVIDER_CRYPTO_MAC_V2);
        event.payload_size = sizeof(payload);
        std::memcpy(event.payload, &payload, sizeof(payload));
        check(scheduler.complete_key_confirmation(event) == FLY_SESSION_V2_OK,
              "only the exact public HMAC completion seals key confirmation");
        fly_session_buffer_release_v2(buffer);
    }
    check(scheduler.key_confirmations_verified() &&
              scheduler.accept_capability(PairRole::Initiator, summary(), {{4}}) &&
              scheduler.accept_capability(PairRole::Responder, summary(), {{5}}) &&
              scheduler.take_verified_evidence().has_value(),
          "public signature and HMAC receipts can seal pair evidence");
}

#endif

} // namespace

int main()
{
#ifdef _WIN32
    test_two_party_real_crypto_pipeline();
    test_wrong_signature_and_sas_never_reach_key_confirmation();
    test_prehashed_digest_is_verified_exactly_once();
    test_public_verification_completion_is_the_only_signature_receipt();
#endif
    return failures == 0 ? 0 : 1;
}
