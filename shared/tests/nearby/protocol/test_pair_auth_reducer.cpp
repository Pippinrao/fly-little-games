#include "link/pair_pipeline.hpp"
#include "wire/sha256.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <vector>

using namespace flynes::session;

namespace {

int failures = 0;
void check(bool value, const char* message)
{
    if (!value) { std::cerr << "FAIL: " << message << '\n'; ++failures; }
}

PlanHash hash(std::uint8_t value)
{
    PlanHash result{}; result[0] = value; return result;
}

CapabilitySummary summary()
{
    CapabilitySummary value{};
    value[1] = 1; value[8] = 1; value[9] = 1; value[32] = 1;
    value[64] = 1; value[96] = 2; value[97] = 1; value[98] = 1;
    value[99] = 2; value[100] = 1; value[102] = 10;
    value[107] = 1; value[108] = 1;
    return value;
}

wire::PairContributionV1 contribution(wire::PairRoleV1 role,
                                      const CapabilitySummary& capability)
{
    wire::PairContributionV1 result{};
    result.role = role;
    result.bytes[12] = static_cast<std::uint8_t>(role);
    result.identity_public_key[0] = 4;
    result.identity_public_key[1] = static_cast<std::uint8_t>(role);
    result.capability_summary_hash = wire::domain_hash(
        "flynes-pair-capability-summary-v1", capability.data(), capability.size());
    result.commitment = wire::pair_commitment_v1(result.bytes.data(), result.bytes.size());
    return result;
}

PairAuthStartV1 start(bool known_path = false)
{
    PairAuthStartV1 value{};
    value.generation = 7; value.engine_instance_id[0] = 1; value.link_id[0] = 2;
    value.signature_operation_ids = {{1, 2}};
    value.key_confirm_operation_ids = {{3, 4}};
    value.local_role = PairRole::Initiator; value.entry_mode = 1;
    value.known_path = known_path; value.pair_context_hash = hash(9);
    value.initiator_summary = summary(); value.responder_summary = summary();
    value.initiator_contribution = contribution(
        wire::PairRoleV1::Initiator, value.initiator_summary);
    value.responder_contribution = contribution(
        wire::PairRoleV1::Responder, value.responder_summary);
    value.initiator_commit.sender = wire::PairRoleV1::Initiator;
    value.initiator_commit.receiver = wire::PairRoleV1::Responder;
    value.initiator_commit.pair_context_hash = value.pair_context_hash;
    value.initiator_commit.commitment = value.initiator_contribution.commitment;
    value.responder_commit.sender = wire::PairRoleV1::Responder;
    value.responder_commit.receiver = wire::PairRoleV1::Initiator;
    value.responder_commit.pair_context_hash = value.pair_context_hash;
    value.responder_commit.commitment = value.responder_contribution.commitment;
    value.initiator_reveal_logical_hash = hash(2);
    value.responder_reveal_logical_hash = hash(3);
    return value;
}

class ContractProvider final : public PairPipelineCryptoProvider
{
public:
    bool verify_signature(const fly_session_op_token_v2& token, PairRole role,
                          const std::array<std::uint8_t, 65>& public_key,
                          const PlanHash&,
                          const std::array<std::uint8_t, 64>& signature) override
    {
        operations.push_back(token.operation_id);
        tokens_exact = tokens_exact && token.struct_size == FLY_SESSION_OP_TOKEN_V2_SIZE &&
            token.scope.kind == FLY_SESSION_SCOPE_LINK_V2 &&
            token.engine_instance_id[0] == 1 && token.scope.link_id[0] == 2 &&
            token.connection_generation == 7;
        return public_key[0] == 4 && public_key[1] == static_cast<std::uint8_t>(role) &&
               signature[0] == static_cast<std::uint8_t>(role);
    }

    bool derive_sas(const PlanHash&, std::array<std::uint8_t, 6>& value) override
    {
        value = {{'1', '2', '3', '4', '5', '6'}};
        return true;
    }

    bool verify_key_confirmation(const fly_session_op_token_v2& token, PairRole role,
                                 const PlanHash&, const PlanHash& confirmation) override
    {
        operations.push_back(token.operation_id);
        return confirmation[0] == static_cast<std::uint8_t>(role);
    }

    std::vector<std::uint64_t> operations;
    bool tokens_exact = true;
};

std::array<std::uint8_t, 64> signature(PairRole role)
{
    std::array<std::uint8_t, 64> value{};
    value[0] = static_cast<std::uint8_t>(role);
    value[63] = 1;
    return value;
}

void complete_signatures(PairPipeline& pipeline)
{
    check(pipeline.submit_signature(PairRole::Initiator,
                                    signature(PairRole::Initiator)),
          "initiator provider verification");
    check(pipeline.submit_signature(PairRole::Responder,
                                    signature(PairRole::Responder)),
          "responder provider verification");
}

void test_complete_provider_chain_and_seal()
{
    VerifiedPairEvidence raw{};
    raw.generation = 7; raw.local_role = PairRole::Initiator; raw.transcript = hash(1);
    check(!raw.authenticated(), "syntax-only evidence is not authenticated");

    ContractProvider provider;
    PairPipeline pipeline(provider);
    check(pipeline.begin(start()), "pipeline begins from bound commit/reveal");
    complete_signatures(pipeline);
    const auto sas = pipeline.sas();
    check(sas && pipeline.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *sas),
          "derived SAS is approved");
    check(pipeline.submit_key_confirmation(PairRole::Initiator, hash(1)) &&
              pipeline.submit_key_confirmation(PairRole::Responder, hash(2)),
          "both provider key confirmations pass");
    check(pipeline.accept_capability(PairRole::Initiator, summary(), hash(4)) &&
              pipeline.accept_capability(PairRole::Responder, summary(), hash(5)),
          "both capability commitments open");
    auto evidence = pipeline.take_verified_evidence();
    check(evidence && evidence->authenticated(), "complete chain seals evidence");
    check(provider.tokens_exact &&
              provider.operations == std::vector<std::uint64_t>({1, 2, 3, 4}),
          "provider receives exact monotonic operation fences");
    auto tampered = *evidence; tampered.transcript[0] ^= 1;
    check(!tampered.authenticated(), "seal detects evidence mutation");
}

void test_provider_failure_and_wrong_approval_fail_closed()
{
    ContractProvider provider;
    PairPipeline bad_signature(provider);
    check(bad_signature.begin(start()), "signature negative begins");
    auto invalid = signature(PairRole::Initiator); invalid[0] = 9;
    check(!bad_signature.submit_signature(PairRole::Initiator, invalid) &&
              bad_signature.failed() && !bad_signature.take_verified_evidence(),
          "provider signature failure yields no evidence");

    PairPipeline wrong_approval(provider);
    check(wrong_approval.begin(start(true)), "known-path negative begins");
    complete_signatures(wrong_approval);
    check(!wrong_approval.approve_local(FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2) &&
              wrong_approval.failed(),
          "known-friend path cannot downgrade to SAS approval");
}

void test_order_replay_low_s_and_commitment_fail_closed()
{
    ContractProvider provider;
    PairPipeline reflected(provider);
    check(reflected.begin(start()), "reflection case begins");
    check(!reflected.submit_signature(PairRole::Responder,
                                      signature(PairRole::Responder)) &&
              reflected.failed(), "responder signature cannot be reflected first");

    PairPipeline replay(provider);
    check(replay.begin(start()) && replay.submit_signature(
              PairRole::Initiator, signature(PairRole::Initiator)),
          "replay case accepts first signature");
    check(!replay.submit_signature(PairRole::Initiator,
                                   signature(PairRole::Initiator)) && replay.failed(),
          "signature replay fails closed");

    PairPipeline high_s(provider);
    check(high_s.begin(start()), "high-S case begins");
    auto malleable = signature(PairRole::Initiator);
    std::fill(malleable.begin() + 32, malleable.end(), uint8_t{0xff});
    check(!high_s.submit_signature(PairRole::Initiator, malleable) &&
              high_s.failed(), "high-S signature is rejected before provider trust");

    auto invalid_start = start();
    invalid_start.initiator_commit.commitment[0] ^= 1;
    PairPipeline commitment(provider);
    check(!commitment.begin(invalid_start) && commitment.failed(),
          "commit/reveal mismatch fails before signatures");

    auto non_monotonic = start();
    non_monotonic.key_confirm_operation_ids[0] =
        non_monotonic.signature_operation_ids[1];
    PairPipeline operation_fence(provider);
    check(!operation_fence.begin(non_monotonic) && operation_fence.failed(),
          "non-monotonic provider operation ids fail before crypto dispatch");
}

void test_confirmation_capability_and_qr_fail_closed()
{
    ContractProvider provider;
    PairPipeline bad_confirmation(provider);
    check(bad_confirmation.begin(start()), "confirmation negative begins");
    complete_signatures(bad_confirmation);
    const auto sas = bad_confirmation.sas();
    check(sas && bad_confirmation.approve_local(
              FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *sas),
          "confirmation negative reaches approved state");
    check(!bad_confirmation.submit_key_confirmation(
              PairRole::Initiator, hash(9)) && bad_confirmation.failed() &&
              !bad_confirmation.take_verified_evidence(),
          "wrong key confirmation fails closed without evidence");

    PairPipeline mutated_capability(provider);
    check(mutated_capability.begin(start()), "capability negative begins");
    complete_signatures(mutated_capability);
    const auto capability_sas = mutated_capability.sas();
    check(capability_sas && mutated_capability.approve_local(
              FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *capability_sas) &&
              mutated_capability.submit_key_confirmation(
                  PairRole::Initiator, hash(1)) &&
              mutated_capability.submit_key_confirmation(
                  PairRole::Responder, hash(2)),
          "capability negative reaches reveal state");
    auto changed = summary();
    changed[8] ^= 1;
    check(!mutated_capability.accept_capability(
              PairRole::Initiator, changed, hash(4)) &&
              mutated_capability.failed() &&
              !mutated_capability.take_verified_evidence(),
          "mutated capability summary cannot open committed transcript");

    auto qr_start = start();
    qr_start.entry_mode = 2;
    qr_start.entry_context_hash = hash(7);
    PairPipeline qr(provider);
    check(qr.begin(qr_start), "QR pipeline binds nonzero entry context");
    complete_signatures(qr);
    check(!qr.sas() && qr.approve_local(
              FLY_SESSION_APPROVAL_QR_INVITER_ACCEPT_V2),
          "QR inviter uses proof-bound approval without BLE SAS");

    PairPipeline qr_downgrade(provider);
    check(qr_downgrade.begin(qr_start), "QR downgrade case begins");
    complete_signatures(qr_downgrade);
    check(!qr_downgrade.approve_local(
              FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2) &&
              qr_downgrade.failed(),
          "QR path cannot downgrade to BLE SAS approval");
}

void test_hidden_capability_bytes_are_opened_only_after_authentication()
{
    ContractProvider provider;
    auto hidden = start();
    hidden.initiator_summary.fill(0);
    hidden.responder_summary.fill(0);
    PairPipeline pipeline(provider);
    check(pipeline.begin(hidden),
          "pair authentication begins from committed capability hashes only");
    complete_signatures(pipeline);
    const auto sas = pipeline.sas();
    check(sas && pipeline.approve_local(
              FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2, *sas) &&
              pipeline.submit_key_confirmation(PairRole::Initiator, hash(1)) &&
              pipeline.submit_key_confirmation(PairRole::Responder, hash(2)) &&
              pipeline.accept_capability(PairRole::Initiator, summary(), hash(4)) &&
              pipeline.accept_capability(PairRole::Responder, summary(), hash(5)) &&
              pipeline.take_verified_evidence().has_value(),
          "committed capability bytes are revealed only after signatures and approval");
}

} // namespace

int main()
{
    test_complete_provider_chain_and_seal();
    test_provider_failure_and_wrong_approval_fail_closed();
    test_order_replay_low_s_and_commitment_fail_closed();
    test_confirmation_capability_and_qr_fail_closed();
    test_hidden_capability_bytes_are_opened_only_after_authentication();
    return failures == 0 ? 0 : 1;
}
