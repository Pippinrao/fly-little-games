#include "initial_plan_scheduler.hpp"

#include "../wire/gatt_fragment.hpp"

#include <algorithm>
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

bool same_tag(const std::vector<std::uint8_t>& actual,
              const std::array<std::uint8_t, 32>& expected) noexcept
{
    if (actual.size() != expected.size()) return false;
    std::uint8_t difference = 0;
    for (std::size_t i = 0; i < expected.size(); ++i)
        difference = static_cast<std::uint8_t>(
            difference | (actual[i] ^ expected[i]));
    return difference == 0;
}

} // namespace

fly_session_op_token_v2 InitialPlanScheduler::token(
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

std::uint8_t InitialPlanScheduler::logical_type(Message message) const noexcept
{
    switch (message)
    {
    case Message::Plan: return 24;
    case Message::Ack: return 25;
    case Message::Final: return 26;
    default: return 0;
    }
}

std::uint64_t InitialPlanScheduler::counter(Message message) const noexcept
{
    return message == Message::Final ? 7 : 6;
}

wire::PairRoleV1 InitialPlanScheduler::sender(Message message) const noexcept
{
    return message == Message::Ack ? wire::PairRoleV1::Responder
                                   : wire::PairRoleV1::Initiator;
}

std::vector<std::uint8_t>* InitialPlanScheduler::exact_for(
    Message message) noexcept
{
    switch (message)
    {
    case Message::Plan: return &plan_exact_;
    case Message::Ack: return &ack_exact_;
    case Message::Final: return &final_exact_;
    default: return nullptr;
    }
}

bool InitialPlanScheduler::begin(const InitialPlanStartV1& start)
{
    if (begun_ || failed_ || start.generation == 0 ||
        start.first_operation_id == 0 ||
        start.first_operation_id > (std::numeric_limits<std::uint64_t>::max)() - 16 ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        (start.local_role != wire::PairRoleV1::Initiator &&
         start.local_role != wire::PairRoleV1::Responder) ||
        public_role(start.local_role) != start.evidence.local_role ||
        start.evidence.generation != start.generation ||
        start.gatt_i2r_key == 0 || start.gatt_r2i_key == 0 ||
        start.control_i2r_key == 0 || start.control_r2i_key == 0)
        return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    if (!lock_.begin(start.evidence) || lock_.selected_plan() != start.selected_plan)
        return false;
    plan_evidence_.generation = start.generation;
    plan_evidence_.transcript = start.evidence.transcript;
    plan_evidence_.initiator_reveal = start.evidence.initiator_reveal;
    plan_evidence_.responder_reveal = start.evidence.responder_reveal;
    plan_evidence_.initiator_capability = start.evidence.initiator_capability;
    plan_evidence_.responder_capability = start.evidence.responder_capability;
    plan_evidence_.selected_plan = start.selected_plan;
    plan_evidence_.selected_plan_hash =
        wire::selected_bearer_plan_hash_v1(start.selected_plan);
    begun_ = true;
    if (start.local_role == wire::PairRoleV1::Initiator)
        return prepare_random() == FLY_SESSION_V2_OK;
    current_message_ = Message::Plan;
    stage_ = Stage::WaitPeer;
    return true;
}

fly_session_result_v2 InitialPlanScheduler::issue(
    InitialPlanEffect effect, std::uint32_t payload_kind, Stage stage)
{
    const auto expected = operations_.expect(effect.token, payload_kind);
    if (expected != FLY_SESSION_V2_OK) return reject(expected);
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialPlanScheduler::prepare_random()
{
    InitialPlanEffect effect{};
    effect.kind = InitialPlanEffectKind::Random;
    effect.token = token(next_operation_id_++);
    current_message_ = Message::Plan;
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2,
                 Stage::RandomPlan);
}

fly_session_result_v2 InitialPlanScheduler::prepare_local_hmac(Message message)
{
    try
    {
        std::array<std::uint8_t, 32> empty_tag{};
        wire::Status status = wire::Status::InvalidField;
        local_hmac_input_.clear();
        if (message == Message::Plan)
        {
            std::array<std::uint8_t, wire::kInitialBearerPlanInnerSizeV1> inner{};
            status = wire::encode_initial_bearer_plan_inner_v1(
                start_.evidence.transcript,
                start_.evidence.initiator_capability,
                start_.evidence.responder_capability,
                start_.selected_plan, plan_nonce_, empty_tag, &inner);
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_hmac_input_v1(
                    inner.data(), wire::kInitialBearerPlanPretagSizeV1,
                    &local_hmac_input_);
        }
        else if (message == Message::Ack)
        {
            std::array<std::uint8_t, wire::kInitialBearerPlanAckInnerSizeV1> inner{};
            status = wire::encode_initial_bearer_plan_ack_inner_v1(
                start_.evidence.transcript, plan_evidence_.plan_logical_hash,
                plan_evidence_.selected_plan_hash, empty_tag, &inner);
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_ack_hmac_input_v1(
                    inner.data(), wire::kInitialBearerPlanAckPretagSizeV1,
                    &local_hmac_input_);
        }
        else if (message == Message::Final)
        {
            std::array<std::uint8_t, wire::kInitialBearerPlanFinalInnerSizeV1> inner{};
            status = wire::encode_initial_bearer_plan_final_inner_v1(
                start_.evidence.transcript, plan_evidence_.plan_logical_hash,
                plan_evidence_.selected_plan_hash,
                plan_evidence_.ack_logical_hash, empty_tag, &inner);
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_final_hmac_input_v1(
                    inner.data(), wire::kInitialBearerPlanFinalPretagSizeV1,
                    &local_hmac_input_);
        }
        if (status != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        InitialPlanEffect effect{};
        effect.kind = InitialPlanEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.resource = sender(message) == wire::PairRoleV1::Initiator
            ? start_.gatt_i2r_key : start_.gatt_r2i_key;
        effect.input = local_hmac_input_;
        current_message_ = message;
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
                     Stage::HmacLocal);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialPlanScheduler::prepare_aead(
    bool seal, Message message)
{
    try
    {
        InitialPlanEffect effect{};
        effect.kind = seal ? InitialPlanEffectKind::AeadSeal
                           : InitialPlanEffectKind::AeadOpen;
        effect.token = token(next_operation_id_++);
        const auto message_sender = sender(message);
        effect.resource = message_sender == wire::PairRoleV1::Initiator
            ? start_.control_i2r_key : start_.control_r2i_key;
        std::array<std::uint8_t, wire::kPairSecureNonceSizeV1> nonce{};
        std::array<std::uint8_t, wire::kPairSecureAadSizeV1> aad{};
        if (wire::build_pair_secure_nonce_v1(
                message_sender, counter(message), &nonce) != wire::Status::Ok ||
            wire::build_pair_secure_aad_v1(
                logical_type(message), message_sender,
                start_.evidence.transcript, counter(message), &aad) !=
                wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        effect.nonce.assign(nonce.begin(), nonce.end());
        effect.aad.assign(aad.begin(), aad.end());
        if (seal)
        {
            if (message == Message::Plan)
            {
                std::array<std::uint8_t, wire::kInitialBearerPlanInnerSizeV1> inner{};
                if (wire::encode_initial_bearer_plan_inner_v1(
                        start_.evidence.transcript,
                        start_.evidence.initiator_capability,
                        start_.evidence.responder_capability,
                        start_.selected_plan, plan_nonce_, local_tag_, &inner) !=
                    wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
            else if (message == Message::Ack)
            {
                std::array<std::uint8_t, wire::kInitialBearerPlanAckInnerSizeV1> inner{};
                if (wire::encode_initial_bearer_plan_ack_inner_v1(
                        start_.evidence.transcript,
                        plan_evidence_.plan_logical_hash,
                        plan_evidence_.selected_plan_hash, local_tag_, &inner) !=
                    wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
            else
            {
                std::array<std::uint8_t, wire::kInitialBearerPlanFinalInnerSizeV1> inner{};
                if (wire::encode_initial_bearer_plan_final_inner_v1(
                        start_.evidence.transcript,
                        plan_evidence_.plan_logical_hash,
                        plan_evidence_.selected_plan_hash,
                        plan_evidence_.ack_logical_hash, local_tag_, &inner) !=
                    wire::Status::Ok)
                    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
                effect.input.assign(inner.begin(), inner.end());
            }
        }
        else
        {
            if (!peer_envelope_) return reject(FLY_SESSION_V2_INVALID_STATE);
            effect.input = peer_envelope_->ciphertext_and_tag;
        }
        current_message_ = message;
        return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
                     seal ? Stage::SealLocal : Stage::OpenPeer);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialPlanScheduler::prepare_peer_hmac()
{
    InitialPlanEffect effect{};
    effect.kind = InitialPlanEffectKind::Hmac;
    effect.token = token(next_operation_id_++);
    effect.resource = sender(current_message_) == wire::PairRoleV1::Initiator
        ? start_.gatt_i2r_key : start_.gatt_r2i_key;
    effect.input = peer_hmac_input_;
    return issue(std::move(effect), FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
                 Stage::HmacPeer);
}

fly_session_result_v2 InitialPlanScheduler::prepare_persist(
    const InitialPlanCommand& command)
{
    try
    {
        InitialPlanEffect effect{};
        effect.kind = InitialPlanEffectKind::Persist;
        effect.token = token(next_operation_id_++);
        effect.persist_kind = command.kind;
        pending_lock_command_id_ = command.id;
        if (command.kind == InitialPlanCommandKind::PersistMutualLock)
        {
            static constexpr char marker[] = "flynes-initial-plan-mutual-v1";
            effect.input.assign(marker, marker + sizeof(marker) - 1);
            effect.input.insert(effect.input.end(),
                                plan_evidence_.plan_logical_hash.begin(),
                                plan_evidence_.plan_logical_hash.end());
            effect.input.insert(effect.input.end(),
                                plan_evidence_.ack_logical_hash.begin(),
                                plan_evidence_.ack_logical_hash.end());
            effect.input.insert(effect.input.end(),
                                plan_evidence_.final_logical_hash.begin(),
                                plan_evidence_.final_logical_hash.end());
            effect.input.insert(effect.input.end(),
                                plan_evidence_.selected_plan_hash.begin(),
                                plan_evidence_.selected_plan_hash.end());
        }
        else
        {
            Message message = Message::None;
            if (command.kind == InitialPlanCommandKind::PersistPlan ||
                command.kind == InitialPlanCommandKind::PersistPlanAndLock)
                message = Message::Plan;
            else if (command.kind == InitialPlanCommandKind::PersistAck ||
                     command.kind == InitialPlanCommandKind::PersistAckAndLock)
                message = Message::Ack;
            else if (command.kind == InitialPlanCommandKind::PersistFinal)
                message = Message::Final;
            auto* exact = exact_for(message);
            if (!exact || exact->empty())
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            effect.input = *exact;
        }
        return issue(std::move(effect),
                     FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2,
                     Stage::Persist);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialPlanScheduler::finalize_local_envelope(
    const std::vector<std::uint8_t>& ciphertext_and_tag)
{
    try
    {
        std::vector<std::uint8_t> envelope;
        std::vector<std::uint8_t> logical;
        const auto type = logical_type(current_message_);
        if (wire::encode_pair_secure_envelope_v1(
                type, counter(current_message_), ciphertext_and_tag.data(),
                ciphertext_and_tag.size(), &envelope) != wire::Status::Ok ||
            wire::encode_gatt_logical_message(type, envelope.data(),
                                               envelope.size(), &logical) !=
                wire::GattFragmentResult::Accepted)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        auto* exact = exact_for(current_message_);
        if (!exact) return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        *exact = std::move(logical);
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy(exact->end() - 32, exact->end(), logical_hash.begin());
        plan_evidence_.sender = public_role(sender(current_message_));
        plan_evidence_.receiver = plan_evidence_.sender == PairRole::Initiator
            ? PairRole::Responder : PairRole::Initiator;
        bool accepted = false;
        if (current_message_ == Message::Plan)
        {
            plan_evidence_.plan_logical_hash = logical_hash;
            accepted = lock_.accept_plan(plan_evidence_);
        }
        else if (current_message_ == Message::Ack)
        {
            plan_evidence_.ack_logical_hash = logical_hash;
            accepted = lock_.accept_ack(plan_evidence_);
        }
        else
        {
            plan_evidence_.final_logical_hash = logical_hash;
            accepted = lock_.accept_final(plan_evidence_);
        }
        const auto command = lock_.poll();
        return accepted && command
            ? prepare_persist(*command)
            : reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    }
    catch (const std::bad_alloc&)
    {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialPlanScheduler::accept_peer_logical(
    const std::uint8_t* bytes, std::size_t size)
{
    if (!begun_ || failed_ || stage_ != Stage::WaitPeer || !bytes)
        return FLY_SESSION_V2_INVALID_STATE;
    wire::GattLogicalMessageView logical{};
    const auto type = logical_type(current_message_);
    if (wire::decode_gatt_logical_message(bytes, size, type, &logical) !=
            wire::GattFragmentResult::Complete)
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    wire::PairSecureEnvelopeV1 envelope{};
    if (wire::decode_pair_secure_envelope_v1(
            type, logical.body, logical.body_size, &envelope) != wire::Status::Ok ||
        envelope.message_counter != counter(current_message_))
        return reject(FLY_SESSION_V2_AUTH_FAILED);
    try { peer_exact_.assign(bytes, bytes + size); }
    catch (const std::bad_alloc&) { return reject(FLY_SESSION_V2_OUT_OF_MEMORY); }
    peer_envelope_ = std::move(envelope);
    return prepare_aead(false, current_message_) == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 InitialPlanScheduler::accept_verified_peer()
{
    if (peer_exact_.size() < 32)
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    std::array<std::uint8_t, 32> logical_hash{};
    std::copy(peer_exact_.end() - 32, peer_exact_.end(), logical_hash.begin());
    auto* exact = exact_for(current_message_);
    if (!exact) return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    *exact = peer_exact_;
    plan_evidence_.sender = public_role(sender(current_message_));
    plan_evidence_.receiver = plan_evidence_.sender == PairRole::Initiator
        ? PairRole::Responder : PairRole::Initiator;
    bool accepted = false;
    if (current_message_ == Message::Plan)
    {
        plan_evidence_.plan_logical_hash = logical_hash;
        accepted = lock_.accept_plan(plan_evidence_);
    }
    else if (current_message_ == Message::Ack)
    {
        plan_evidence_.ack_logical_hash = logical_hash;
        accepted = lock_.accept_ack(plan_evidence_);
    }
    else
    {
        plan_evidence_.final_logical_hash = logical_hash;
        accepted = lock_.accept_final(plan_evidence_);
    }
    const auto command = lock_.poll();
    return accepted && command
        ? prepare_persist(*command)
        : reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
}

fly_session_result_v2 InitialPlanScheduler::after_lock_transition()
{
    const auto command = lock_.poll();
    if (command)
    {
        if (command->kind == InitialPlanCommandKind::PersistMutualLock)
            return prepare_persist(*command);
        if (command->kind == InitialPlanCommandKind::SendPlan ||
            command->kind == InitialPlanCommandKind::SendAck ||
            command->kind == InitialPlanCommandKind::SendFinal)
        {
            Message message = command->kind == InitialPlanCommandKind::SendPlan
                ? Message::Plan : command->kind == InitialPlanCommandKind::SendAck
                    ? Message::Ack : Message::Final;
            auto* exact = exact_for(message);
            if (!exact || exact->empty())
                return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
            local_send_ = *exact;
            current_message_ = message;
            stage_ = Stage::WaitLocalSend;
            return FLY_SESSION_V2_OK;
        }
        if (lock_.mutually_locked())
        {
            stage_ = Stage::Ready;
            return FLY_SESSION_V2_OK;
        }
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    }
    if (lock_.mutually_locked())
    {
        stage_ = Stage::Ready;
        return FLY_SESSION_V2_OK;
    }
    if (start_.local_role == wire::PairRoleV1::Responder &&
        current_message_ == Message::Plan)
        return prepare_local_hmac(Message::Ack);
    if (start_.local_role == wire::PairRoleV1::Initiator &&
        current_message_ == Message::Ack)
        return prepare_local_hmac(Message::Final);
    return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
}

fly_session_result_v2 InitialPlanScheduler::mark_local_sent()
{
    if (!local_send_ || stage_ != Stage::WaitLocalSend || failed_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto command = lock_.poll();
    if (!command || !lock_.complete(command->id, command->generation, true))
        return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
    local_send_.reset();
    if (current_message_ == Message::Plan)
    {
        current_message_ = Message::Ack;
        stage_ = Stage::WaitPeer;
        return FLY_SESSION_V2_OK;
    }
    if (current_message_ == Message::Ack)
    {
        current_message_ = Message::Final;
        stage_ = Stage::WaitPeer;
        return FLY_SESSION_V2_OK;
    }
    return after_lock_transition();
}

fly_session_result_v2 InitialPlanScheduler::read_buffer(
    const ProviderOperationCompletion& completion,
    std::vector<std::uint8_t>& out) const
{
    if (!completion.payload.buffer || completion.payload.value0 == 0 ||
        completion.payload.value0 > 4096)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out.assign(static_cast<std::size_t>(completion.payload.value0), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out.data(), out.size()};
    return fly_session_buffer_read_v2(
               completion.payload.buffer, 0, destination, &written) ==
               FLY_SESSION_V2_OK && written == out.size()
        ? FLY_SESSION_V2_OK : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 InitialPlanScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed_stage = stage_;
    const auto persist_kind = pending_->persist_kind;
    ProviderOperationCompletion completion{};
    const auto accepted = operations_.accept(event, completion);
    if (accepted != FLY_SESSION_V2_OK) return accepted;
    if (completion.result != FLY_SESSION_V2_OK)
        return reject(completion.result);
    pending_.reset();
    if (completed_stage == Stage::Persist)
    {
        const auto command = lock_.poll();
        if (!command || command->id != pending_lock_command_id_ ||
            command->kind != persist_kind ||
            !lock_.complete(command->id, command->generation, true))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        pending_lock_command_id_ = 0;
        return after_lock_transition();
    }
    std::vector<std::uint8_t> bytes;
    const auto read = read_buffer(completion, bytes);
    if (read != FLY_SESSION_V2_OK) return reject(read);
    if (completed_stage == Stage::RandomPlan)
    {
        if (bytes.size() != plan_nonce_.size() ||
            !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), plan_nonce_.begin());
        return prepare_local_hmac(Message::Plan);
    }
    if (completed_stage == Stage::HmacLocal)
    {
        if (bytes.size() != local_tag_.size())
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), local_tag_.begin());
        return prepare_aead(true, current_message_);
    }
    if (completed_stage == Stage::SealLocal)
    {
        std::size_t inner_size = 0;
        if (wire::pair_secure_inner_size_v1(
                logical_type(current_message_), &inner_size) != wire::Status::Ok ||
            bytes.size() != inner_size + 16)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        return finalize_local_envelope(bytes);
    }
    if (completed_stage == Stage::OpenPeer)
    {
        wire::Status status = wire::Status::InvalidField;
        peer_hmac_input_.clear();
        if (current_message_ == Message::Plan)
        {
            wire::InitialBearerPlanInnerV1 inner{};
            status = wire::decode_initial_bearer_plan_inner_v1(
                bytes.data(), bytes.size(), start_.evidence.transcript,
                start_.evidence.initiator_capability,
                start_.evidence.responder_capability,
                start_.selected_plan, &inner);
            peer_tag_ = inner.tag;
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_hmac_input_v1(
                    bytes.data(), wire::kInitialBearerPlanPretagSizeV1,
                    &peer_hmac_input_);
        }
        else if (current_message_ == Message::Ack)
        {
            wire::InitialBearerPlanAckInnerV1 inner{};
            status = wire::decode_initial_bearer_plan_ack_inner_v1(
                bytes.data(), bytes.size(), start_.evidence.transcript,
                plan_evidence_.plan_logical_hash,
                plan_evidence_.selected_plan_hash, &inner);
            peer_tag_ = inner.tag;
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_ack_hmac_input_v1(
                    bytes.data(), wire::kInitialBearerPlanAckPretagSizeV1,
                    &peer_hmac_input_);
        }
        else
        {
            wire::InitialBearerPlanFinalInnerV1 inner{};
            status = wire::decode_initial_bearer_plan_final_inner_v1(
                bytes.data(), bytes.size(), start_.evidence.transcript,
                plan_evidence_.plan_logical_hash,
                plan_evidence_.selected_plan_hash,
                plan_evidence_.ack_logical_hash, &inner);
            peer_tag_ = inner.tag;
            if (status == wire::Status::Ok)
                status = wire::build_initial_bearer_plan_final_hmac_input_v1(
                    bytes.data(), wire::kInitialBearerPlanFinalPretagSizeV1,
                    &peer_hmac_input_);
        }
        if (status != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        return prepare_peer_hmac();
    }
    if (completed_stage == Stage::HmacPeer)
        return same_tag(bytes, peer_tag_)
            ? accept_verified_peer()
            : reject(FLY_SESSION_V2_AUTH_FAILED);
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 InitialPlanScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    const auto result = operations_.cancel(pending_->token);
    if (result == FLY_SESSION_V2_OK) reject(FLY_SESSION_V2_CANCELLED);
    return result;
}

fly_session_result_v2 InitialPlanScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    local_send_.reset();
    lock_.invalidate();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK
        ? FLY_SESSION_V2_CONTRACT_VIOLATION : result;
}

} // namespace flynes::session
