#include "initial_bearer_scheduler.hpp"

#include "../wire/gatt_fragment.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {
namespace {

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

PairRole public_role(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator
        ? PairRole::Initiator : PairRole::Responder;
}

wire::PairRoleV1 wire_role(std::uint8_t value) noexcept
{
    return value == static_cast<std::uint8_t>(PairRole::Initiator)
        ? wire::PairRoleV1::Initiator : wire::PairRoleV1::Responder;
}

wire::PairRoleV1 other(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator
        ? wire::PairRoleV1::Responder : wire::PairRoleV1::Initiator;
}

} // namespace

fly_session_op_token_v2 InitialBearerScheduler::token(
    std::uint64_t operation_id) const noexcept
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(start_.engine_instance_id.begin(), start_.engine_instance_id.end(),
              value.engine_instance_id);
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    std::copy(start_.link_id.begin(), start_.link_id.end(), value.scope.link_id);
    value.connection_generation = start_.generation;
    value.operation_id = operation_id;
    return value;
}

bool InitialBearerScheduler::begin(const InitialBearerStartV1& start)
{
    if (begun_ || failed_ || !plan_ || !plan_->mutually_locked() ||
        start.generation == 0 || start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 16 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        !nonzero(start.session_id.data(), start.session_id.size()) ||
        start.ecdh_secret == 0 ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        plan_->verified_plan().generation != start.generation ||
        plan_->verified_plan().transcript != start.transcript_hash ||
        plan_->verified_plan().selected_plan != start.selected_plan ||
        (start.selected_plan[1] != static_cast<std::uint8_t>(PairRole::Initiator) &&
         start.selected_plan[1] != static_cast<std::uint8_t>(PairRole::Responder)))
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    local_creator_ = start.selected_plan[1] ==
        static_cast<std::uint8_t>(public_role(start.local_role));
    begun_ = true;
    return derive(true) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialBearerScheduler::issue(
    InitialBearerEffect effect, std::uint32_t payload_kind, Stage stage)
{
    const auto expected = operations_.expect(effect.token, payload_kind);
    if (expected != FLY_SESSION_V2_OK) return reject(expected);
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialBearerScheduler::derive(bool i2r)
{
    try
    {
        static constexpr char salt_label[] = "flynes-pair-v1";
        static constexpr char i2r_label[] =
            "flynes-initial-bearer-credential-i2r-v1";
        static constexpr char r2i_label[] =
            "flynes-initial-bearer-credential-r2i-v1";
        std::array<std::uint8_t, sizeof(salt_label) - 1 + 32> salt_input{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(salt_label),
                    sizeof(salt_label) - 1, salt_input.begin());
        std::copy(start_.transcript_hash.begin(), start_.transcript_hash.end(),
                  salt_input.begin() + sizeof(salt_label) - 1);
        const auto salt = wire::sha256(salt_input.data(), salt_input.size());
        const auto* label = i2r ? i2r_label : r2i_label;
        const auto label_size = i2r ? sizeof(i2r_label) - 1
                                    : sizeof(r2i_label) - 1;
        InitialBearerEffect effect{};
        effect.kind = InitialBearerEffectKind::DeriveKey;
        effect.token = token(next_operation_id_++);
        effect.resource = start_.ecdh_secret;
        effect.salt.assign(salt.begin(), salt.end());
        effect.info.assign(reinterpret_cast<const std::uint8_t*>(label),
                           reinterpret_cast<const std::uint8_t*>(label) + label_size);
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2,
                     i2r ? Stage::DeriveI2r : Stage::DeriveR2i);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialBearerScheduler::process_plan_command()
{
    const auto command = plan_->pending_command();
    if (!command)
    {
        stage_ = local_creator_ ? Stage::PrepareCreator : Stage::WaitPeer;
        return local_creator_ ? prepare_credential(true) : FLY_SESSION_V2_OK;
    }
    pending_plan_command_id_ = command->id;
    if (command->kind == InitialPlanCommandKind::PersistPromptConsumed)
        return persist(true);
    if (command->kind == InitialPlanCommandKind::PersistCredentials)
        return persist(false);
    if (command->kind == InitialPlanCommandKind::CreateBearer ||
        command->kind == InitialPlanCommandKind::JoinBearer)
    {
        InitialBearerEffect effect{};
        const bool create = command->kind == InitialPlanCommandKind::CreateBearer;
        effect.kind = create ? InitialBearerEffectKind::CreateBearer
                             : InitialBearerEffectKind::JoinBearer;
        effect.token = token(next_operation_id_++);
        effect.resource = create ? 0 : resources_.credential;
        effect.plan_hash = wire::selected_bearer_plan_hash_v1(start_.selected_plan);
        effect.selected_plan = start_.selected_plan;
        effect.confirmation_budget = command->may_prompt ? 1u : 0u;
        return issue(std::move(effect), FLY_SESSION_PROVIDER_BEARER_PATH_V2,
                     create ? Stage::Create : Stage::Join);
    }
    if (command->kind == InitialPlanCommandKind::PublishCredentials)
    {
        if (!local_creator_ || local_send_ || peer_exact_.empty())
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        local_send_ = peer_exact_;
        stage_ = Stage::WaitLocalSend;
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
}

fly_session_result_v2 InitialBearerScheduler::prepare_credential(bool creator)
{
    try
    {
        if (creator != local_creator_ || (creator && resources_.bearer == 0) ||
            (!creator && peer_exact_.empty()))
            return reject(FLY_SESSION_V2_INVALID_STATE);
        InitialBearerEffect effect{};
        effect.kind = InitialBearerEffectKind::PrepareCredential;
        effect.token = token(next_operation_id_++);
        effect.resource = creator ? resources_.bearer : 0;
        effect.plan_hash = wire::selected_bearer_plan_hash_v1(start_.selected_plan);
        effect.selected_plan = start_.selected_plan;
        effect.creator = creator;
        if (!creator)
            effect.input.assign(join_params_.begin(), join_params_.end());
        return issue(std::move(effect), FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2,
                     creator ? Stage::PrepareCreator : Stage::PrepareReceiver);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialBearerScheduler::seal_local_credential()
{
    try
    {
        std::array<std::uint8_t,
                   wire::kInitialBearerCredentialPlaintextSizeV1> plaintext{};
        const auto creator = wire_role(start_.selected_plan[1]);
        if (wire::encode_initial_bearer_credential_plaintext_v1(
                start_.transcript_hash, start_.session_id, creator,
                start_.selected_plan, join_params_, &plaintext) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, wire::kInitialBearerCredentialNonceSizeV1> nonce{};
        std::array<std::uint8_t, wire::kInitialBearerCredentialAadSizeV1> aad{};
        if (wire::build_initial_bearer_credential_nonce_v1(creator, 1, &nonce) !=
                wire::Status::Ok ||
            wire::build_initial_bearer_credential_aad_v1(
                creator, start_.transcript_hash, start_.session_id, 1, &aad) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        InitialBearerEffect effect{};
        effect.kind = InitialBearerEffectKind::AeadSeal;
        effect.token = token(next_operation_id_++);
        effect.resource = creator == wire::PairRoleV1::Initiator
            ? resources_.credential_i2r_key : resources_.credential_r2i_key;
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        effect.input.assign(plaintext.begin(), plaintext.end());
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                     Stage::SealLocal);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialBearerScheduler::persist(bool prompt)
{
    try
    {
        InitialBearerEffect effect{};
        effect.kind = InitialBearerEffectKind::Persist;
        effect.token = token(next_operation_id_++);
        if (prompt)
        {
            effect.persist_kind = InitialPlanCommandKind::PersistPromptConsumed;
            static constexpr char marker[] = "flynes-initial-prompt-consumed-v1";
            effect.input.assign(marker, marker + sizeof(marker) - 1);
            effect.input.insert(effect.input.end(), start_.transcript_hash.begin(),
                                start_.transcript_hash.end());
        }
        else
        {
            effect.persist_kind = InitialPlanCommandKind::PersistCredentials;
            if (peer_exact_.empty())
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            effect.input = peer_exact_;
        }
        return issue(std::move(effect),
                     FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                     prompt ? Stage::PersistPrompt : Stage::PersistCredential);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialBearerScheduler::join_bearer()
{
    return process_plan_command();
}

fly_session_result_v2 InitialBearerScheduler::read_buffer(
    const ProviderOperationCompletion& completion,
    std::vector<std::uint8_t>& out) const
{
    if (!completion.payload.buffer)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    std::uint64_t size = 0;
    if (fly_session_buffer_size_v2(completion.payload.buffer, &size) !=
            FLY_SESSION_V2_OK || size == 0 || size > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(size), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    return fly_session_buffer_read_v2(
               completion.payload.buffer, 0, destination, &written) ==
               FLY_SESSION_V2_OK && written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 InitialBearerScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed_stage = stage_;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);
    pending_.reset();

    if (completed_stage == Stage::DeriveI2r ||
        completed_stage == Stage::DeriveR2i)
    {
        if (completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (completed_stage == Stage::DeriveI2r)
        {
            resources_.credential_i2r_key = completion.payload.resource;
            return derive(false);
        }
        resources_.credential_r2i_key = completion.payload.resource;
        return process_plan_command();
    }
    if (completed_stage == Stage::Create)
    {
        resources_.bearer = completion.payload.resource;
        if (resources_.bearer == 0 ||
            !plan_->complete_command(pending_plan_command_id_, true))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_plan_command_id_ = 0;
        return prepare_credential(true);
    }
    if (completed_stage == Stage::Join)
    {
        resources_.bearer = completion.payload.resource;
        if (resources_.bearer == 0 ||
            !plan_->complete_command(pending_plan_command_id_, true))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_plan_command_id_ = 0;
        ready_ = true;
        stage_ = Stage::Ready;
        return FLY_SESSION_V2_OK;
    }
    if (completed_stage == Stage::PersistPrompt ||
        completed_stage == Stage::PersistCredential)
    {
        if (!plan_->complete_command(pending_plan_command_id_, true))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_plan_command_id_ = 0;
        return process_plan_command();
    }

    std::vector<std::uint8_t> bytes;
    const auto read = read_buffer(completion, bytes);
    if (read != FLY_SESSION_V2_OK) return reject(read);
    if (completed_stage == Stage::PrepareCreator ||
        completed_stage == Stage::PrepareReceiver)
    {
        if (bytes.size() != wire::kBearerJoinParamsSizeV1 ||
            completion.payload.resource == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        const auto digest = wire::sha256(bytes.data(), bytes.size());
        if (digest != completion.payload.hash)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        wire::BearerJoinParamsV1 decoded{};
        if (wire::decode_bearer_join_params_v1(
                bytes.data(), bytes.size(), start_.selected_plan, &decoded) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        if (completed_stage == Stage::PrepareReceiver &&
            !std::equal(bytes.begin(), bytes.end(), join_params_.begin()))
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::copy(bytes.begin(), bytes.end(), join_params_.begin());
        resources_.credential = completion.payload.resource;
        if (completed_stage == Stage::PrepareCreator)
            return seal_local_credential();
        VerifiedCredentialEvidence evidence{};
        evidence.binding = plan_->verified_plan();
        evidence.binding.sender = public_role(wire_role(start_.selected_plan[1]));
        evidence.binding.receiver = evidence.binding.sender == PairRole::Initiator
            ? PairRole::Responder : PairRole::Initiator;
        evidence.credential_logical_hash = credential_logical_hash_;
        if (!plan_->accept_credentials(evidence))
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        return process_plan_command();
    }
    if (completed_stage == Stage::SealLocal)
    {
        if (bytes.size() !=
                wire::kInitialBearerCredentialPlaintextSizeV1 + 16)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t,
                   wire::kInitialBearerCredentialEnvelopeSizeV1> envelope{};
        std::vector<std::uint8_t> logical;
        if (wire::encode_initial_bearer_credential_envelope_v1(
                1, bytes.data(), bytes.size(), &envelope) != wire::Status::Ok ||
            wire::encode_gatt_logical_message(
                8, envelope.data(), envelope.size(), &logical) !=
                wire::GattFragmentResult::Accepted)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        peer_exact_ = std::move(logical);
        std::copy(peer_exact_.end() - 32, peer_exact_.end(),
                  credential_logical_hash_.begin());
        VerifiedCredentialEvidence evidence{};
        evidence.binding = plan_->verified_plan();
        evidence.binding.sender = public_role(wire_role(start_.selected_plan[1]));
        evidence.binding.receiver = evidence.binding.sender == PairRole::Initiator
            ? PairRole::Responder : PairRole::Initiator;
        evidence.credential_logical_hash = credential_logical_hash_;
        if (!plan_->accept_credentials(evidence))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        return process_plan_command();
    }
    if (completed_stage == Stage::OpenPeer)
    {
        if (bytes.size() != wire::kInitialBearerCredentialPlaintextSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        wire::InitialBearerCredentialPlaintextV1 decoded{};
        const auto creator = wire_role(start_.selected_plan[1]);
        if (wire::decode_initial_bearer_credential_plaintext_v1(
                bytes.data(), bytes.size(), start_.transcript_hash,
                start_.session_id, creator, start_.selected_plan, &decoded) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        std::copy_n(bytes.begin() + 64, join_params_.size(), join_params_.begin());
        return prepare_credential(false);
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 InitialBearerScheduler::accept_peer_logical(
    const std::uint8_t* bytes, std::size_t size)
{
    if (!begun_ || failed_ || local_creator_ || stage_ != Stage::WaitPeer ||
        !bytes)
        return FLY_SESSION_V2_INVALID_STATE;
    wire::GattLogicalMessageView logical{};
    if (wire::decode_gatt_logical_message(bytes, size, 8, &logical) !=
            wire::GattFragmentResult::Complete)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    wire::InitialBearerCredentialEnvelopeV1 envelope{};
    if (wire::decode_initial_bearer_credential_envelope_v1(
            logical.body, logical.body_size, &envelope) != wire::Status::Ok ||
        envelope.message_counter != 1)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    try
    {
        peer_exact_.assign(bytes, bytes + size);
        peer_ciphertext_ = std::move(envelope.ciphertext_and_tag);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
    std::copy(peer_exact_.end() - 32, peer_exact_.end(),
              credential_logical_hash_.begin());
    const auto creator = wire_role(start_.selected_plan[1]);
    std::array<std::uint8_t, wire::kInitialBearerCredentialNonceSizeV1> nonce{};
    std::array<std::uint8_t, wire::kInitialBearerCredentialAadSizeV1> aad{};
    if (wire::build_initial_bearer_credential_nonce_v1(creator, 1, &nonce) !=
            wire::Status::Ok ||
        wire::build_initial_bearer_credential_aad_v1(
            creator, start_.transcript_hash, start_.session_id, 1, &aad) !=
            wire::Status::Ok)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    try
    {
        InitialBearerEffect effect{};
        effect.kind = InitialBearerEffectKind::AeadOpen;
        effect.token = token(next_operation_id_++);
        effect.resource = creator == wire::PairRoleV1::Initiator
            ? resources_.credential_i2r_key : resources_.credential_r2i_key;
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        effect.input = peer_ciphertext_;
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                     Stage::OpenPeer) == FLY_SESSION_V2_OK
            ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialBearerScheduler::mark_local_sent()
{
    if (!local_creator_ || !local_send_ || stage_ != Stage::WaitLocalSend ||
        failed_ || !plan_->complete_command(pending_plan_command_id_, true))
        return reject(FLY_SESSION_V2_INVALID_STATE);
    pending_plan_command_id_ = 0;
    local_send_.reset();
    ready_ = true;
    stage_ = Stage::Ready;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialBearerScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK)
        reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 InitialBearerScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    if (pending_plan_command_id_ != 0)
        (void)plan_->complete_command(pending_plan_command_id_, false);
    pending_plan_command_id_ = 0;
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
