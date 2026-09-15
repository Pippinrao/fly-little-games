#include "session_engine.hpp"
#include "../ports/provider_events.hpp"
#include "../wire/gatt_lookup_v2.hpp"
#include "../wire/pair_capability.hpp"
#include "../wire/p256_point.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <new>

namespace {

bool same_token(const fly_session_op_token_v2& left,
                const fly_session_op_token_v2& right) noexcept
{
    return left.struct_size == right.struct_size &&
           left.abi_version == right.abi_version &&
           std::memcmp(left.engine_instance_id,
                       right.engine_instance_id,
                       sizeof(left.engine_instance_id)) == 0 &&
           left.scope.struct_size == right.scope.struct_size &&
           left.scope.abi_version == right.scope.abi_version &&
           left.scope.kind == right.scope.kind &&
           left.scope.reserved_zero == right.scope.reserved_zero &&
           std::memcmp(left.scope.link_id,
                       right.scope.link_id,
                       sizeof(left.scope.link_id)) == 0 &&
           std::memcmp(left.scope.branch_id,
                       right.scope.branch_id,
                       sizeof(left.scope.branch_id)) == 0 &&
           left.connection_generation == right.connection_generation &&
           left.config_revision == right.config_revision &&
           left.authority_term == right.authority_term &&
           left.writer_generation == right.writer_generation &&
           left.timeline_epoch == right.timeline_epoch &&
           left.seat_revision == right.seat_revision &&
           left.mode_generation == right.mode_generation &&
           left.media_generation == right.media_generation &&
           left.operation_id == right.operation_id &&
           std::memcmp(left.transition_id,
                       right.transition_id,
                       sizeof(left.transition_id)) == 0;
}

bool same_event(const fly_session_port_event_v2& left,
                const fly_session_port_event_v2& right) noexcept
{
    return same_token(left.token, right.token) &&
           left.event_sequence == right.event_sequence &&
           left.event_kind == right.event_kind &&
           left.terminal == right.terminal && left.result == right.result &&
           left.payload_kind == right.payload_kind &&
           left.payload_size == right.payload_size &&
           std::memcmp(left.payload, right.payload, left.payload_size) == 0;
}

std::uint32_t pair_material_payload_kind(
    flynes::session::PairMaterialEffectKind kind) noexcept
{
    using Kind = flynes::session::PairMaterialEffectKind;
    switch (kind)
    {
    case Kind::GenerateKey:
        return FLY_SESSION_PROVIDER_KEY_HANDLE_V2;
    case Kind::ReadPublicKey:
        return FLY_SESSION_PROVIDER_KEY_PUBLIC_V2;
    case Kind::CreateTlsMaterial:
        return FLY_SESSION_PROVIDER_TLS_MATERIAL_V2;
    case Kind::RandomBytes:
        return FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
    }
    return 0;
}

std::uint32_t pair_reveal_payload_kind(
    flynes::session::PairRevealEffectKind kind) noexcept
{
    using Kind = flynes::session::PairRevealEffectKind;
    switch (kind)
    {
    case Kind::AgreeKey:
        return FLY_SESSION_PROVIDER_KEY_AGREEMENT_V2;
    case Kind::DeriveKey:
        return FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
    case Kind::RandomBytes:
        return FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen:
        return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    }
    return 0;
}

std::uint32_t pair_signature_payload_kind(
    flynes::session::PairSignatureEffectKind kind) noexcept
{
    using Kind = flynes::session::PairSignatureEffectKind;
    switch (kind)
    {
    case Kind::DeriveKey:
        return FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
    case Kind::Sign:
        return FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
    case Kind::Verify:
        return FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen:
        return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    case Kind::PersistTranscript:
        return FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
    }
    return 0;
}

std::uint32_t pair_sas_payload_kind(
    flynes::session::PairSasEffectKind kind) noexcept
{
    return kind == flynes::session::PairSasEffectKind::DeriveKey
        ? FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2
        : FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
}

std::uint32_t pair_known_payload_kind(
    flynes::session::PairKnownEffectKind kind) noexcept
{
    using Kind = flynes::session::PairKnownEffectKind;
    switch (kind)
    {
    case Kind::Hmac: return FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
    case Kind::Sign: return FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
    case Kind::Verify: return FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen: return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    }
    return 0;
}

std::uint32_t pair_key_confirm_payload_kind(
    flynes::session::PairKeyConfirmEffectKind kind) noexcept
{
    return kind == flynes::session::PairKeyConfirmEffectKind::Hmac
        ? FLY_SESSION_PROVIDER_CRYPTO_MAC_V2
        : FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
}

std::uint32_t pair_capability_payload_kind(
    flynes::session::PairCapabilityEffectKind kind) noexcept
{
    return kind == flynes::session::PairCapabilityEffectKind::Hmac
        ? FLY_SESSION_PROVIDER_CRYPTO_MAC_V2
        : FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
}

std::uint32_t initial_plan_payload_kind(
    flynes::session::InitialPlanEffectKind kind) noexcept
{
    using Kind = flynes::session::InitialPlanEffectKind;
    switch (kind)
    {
    case Kind::Random: return FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
    case Kind::Hmac: return FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen: return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    case Kind::Persist: return FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    }
    return 0;
}

std::uint32_t initial_bearer_payload_kind(
    flynes::session::InitialBearerEffectKind kind) noexcept
{
    using Kind = flynes::session::InitialBearerEffectKind;
    switch (kind)
    {
    case Kind::DeriveKey: return FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
    case Kind::CreateBearer:
    case Kind::JoinBearer: return FLY_SESSION_PROVIDER_BEARER_PATH_V2;
    case Kind::PrepareCredential:
        return FLY_SESSION_PROVIDER_BEARER_CREDENTIAL_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen: return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    case Kind::Persist: return FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    }
    return 0;
}

std::uint32_t endpoint_offer_payload_kind(
    flynes::session::EndpointOfferEffectKind kind) noexcept
{
    using Kind = flynes::session::EndpointOfferEffectKind;
    switch (kind)
    {
    case Kind::DeriveKey: return FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
    case Kind::Random: return FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2;
    case Kind::ResolveEndpoint: return FLY_SESSION_PROVIDER_BEARER_ENDPOINT_V2;
    case Kind::AeadSeal:
    case Kind::AeadOpen: return FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2;
    case Kind::Persist: return FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    }
    return 0;
}

std::uint32_t initial_quic_bind_payload_kind(
    const flynes::session::InitialQuicBindEffect& effect) noexcept
{
    return effect.expected_payload_kind;
}

std::uint32_t session_signing_payload_kind(
    flynes::session::SessionSigningEffectKind kind) noexcept
{
    using Kind = flynes::session::SessionSigningEffectKind;
    switch (kind)
    {
    case Kind::GenerateKey: return FLY_SESSION_PROVIDER_KEY_HANDLE_V2;
    case Kind::ReadPublicKey: return FLY_SESSION_PROVIDER_KEY_PUBLIC_V2;
    case Kind::SignBinding: return FLY_SESSION_PROVIDER_KEY_SIGNATURE_V2;
    case Kind::PersistBinding:
        return FLY_SESSION_PROVIDER_SECURE_STORE_REVISION_V2;
    case Kind::PersistBindingObject:
        return FLY_SESSION_PROVIDER_OBJECT_IMMUTABLE_V2;
    }
    return 0;
}

} // namespace

namespace flynes::session {

void SessionEngine::publish_link_view_locked(std::uint32_t link_state)
{
    std::vector<SessionActionSpec> actions;
    if (link_state == FLY_SESSION_LINK_IDLE_V2)
    {
        if (ports_.has_discovery())
        {
            actions = {
                {2, FLY_SESSION_ACTION_CREATE_INVITE_V2, true,
                 "nearby.action.create_invite", ""},
                {3, FLY_SESSION_ACTION_JOIN_CODE_V2, true,
                 "nearby.action.join_code", ""},
                {4, FLY_SESSION_ACTION_START_DISCOVERY_V2, true,
                 "nearby.action.start_discovery", ""}};
        }
    }
    else if (link_state == FLY_SESSION_LINK_INVITING_V2)
    {
        actions = {{5, FLY_SESSION_ACTION_CANCEL_INVITE_V2, true,
                    "nearby.action.cancel_invite", ""}};
    }
    else if (link_state == FLY_SESSION_LINK_JOINING_V2)
    {
        actions = {{6, FLY_SESSION_ACTION_CANCEL_JOIN_V2, true,
                    "nearby.action.cancel_join", ""}};
    }
    else if (link_state == FLY_SESSION_LINK_DISCOVERING_V2)
    {
        actions = {{7, FLY_SESSION_ACTION_STOP_DISCOVERY_V2, true,
                    "nearby.action.stop_discovery", ""}};
        if (!candidates_.empty())
            actions.push_back(
                {13, FLY_SESSION_ACTION_JOIN_CANDIDATE_V2, true,
                 "nearby.action.join_candidate", ""});
    }
    else if (link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 &&
             pair_known_ && pair_known_->ready() && !pair_known_->known_path() &&
             pair_sas_ && pair_sas_->ready() && pair_signature_ &&
             !pair_signature_->local_approved())
    {
        actions = {
            {17, FLY_SESSION_ACTION_CONFIRM_SAS_V2, true,
             "nearby.action.confirm_sas", ""},
            {18, FLY_SESSION_ACTION_REJECT_SAS_V2, true,
             "nearby.action.reject_sas", ""}};
    }
    authorization_->generation.fetch_add(1, std::memory_order_acq_rel);
    auto* next = make_session_view(
        authorization_, current_view_->snapshot.view_revision + 1,
        FLY_SESSION_ENGINE_READY_V2, link_state,
        FLY_SESSION_GAME_NOT_STARTED_V2,
        link_state == FLY_SESSION_LINK_IDLE_V2 && !ports_.has_discovery()
            ? "nearby.reason.discovery.unavailable" : "",
        actions);
    if ((link_state == FLY_SESSION_LINK_AUTHENTICATING_V2 ||
         link_state == FLY_SESSION_LINK_PROVISIONING_V2) && pair_sas_ &&
        pair_sas_->ready() && pair_signature_)
    {
        const auto sas = pair_sas_->sas();
        if (sas)
        {
            next->has_pairing = true;
            next->pairing.struct_size = FLY_SESSION_PAIRING_V2_SIZE;
            next->pairing.abi_version = FLY_SESSION_ABI_VERSION_2;
            next->pairing.connection_generation = link_generation_;
            next->pairing.entry_mode = 1;
            next->pairing.local_role =
                static_cast<std::uint32_t>(local_pair_role_);
            next->pairing.local_confirmed =
                pair_signature_->local_approved() ? 1U : 0U;
            next->pairing.peer_confirmed = pair_key_confirm_ &&
                    pair_key_confirm_->ready()
                ? 1U : 0U;
            next->pairing.stage = next->pairing.peer_confirmed
                ? FLY_SESSION_PAIRING_CONFIRMED_V2
                : pair_signature_->local_approved()
                    ? FLY_SESSION_PAIRING_AWAITING_PEER_CONFIRM_V2
                    : FLY_SESSION_PAIRING_AWAITING_LOCAL_SAS_V2;
            std::copy(sas->begin(), sas->end(), next->pairing.sas);
            const auto transcript = pair_signature_->transcript_hash();
            std::copy(transcript.begin(), transcript.end(),
                      next->pairing.pair_transcript_hash);
        }
    }
    next->candidates = candidates_;
    next->snapshot.candidate_count =
        static_cast<std::uint32_t>(next->candidates.size());
    auto* old = current_view_;
    current_view_ = next;
    fly_session_view_release_v2(old);
}

fly_session_op_token_v2 SessionEngine::make_link_operation_token_locked()
{
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              value.engine_instance_id);
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    value.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2;
    value.scope.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    value.connection_generation = link_generation_;
    value.operation_id = next_operation_id_++;
    return value;
}

void SessionEngine::cancel_pair_material_locked() noexcept
{
    cancel_session_signing_locked();
    cancel_initial_quic_bind_locked();
    cancel_endpoint_offer_locked();
    cancel_initial_bearer_locked();
    cancel_initial_plan_locked();
    cancel_pair_capability_locked();
    cancel_pair_key_confirm_locked();
    cancel_pair_sas_locked();
    release_pair_sas_locked();
    cancel_pair_known_locked();
    cancel_pair_signature_locked();
    release_pair_signature_locked();
    cancel_pair_reveal_locked();
    release_pair_reveal_locked();
    if (!pair_material_ || (!pair_material_active_ &&
                            !pair_material_dispatch_pending_))
        return;
    const auto effect = pair_material_->poll_effect();
    if (!effect)
        return;
    fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
    if (!pair_material_active_)
    {
        result = FLY_SESSION_V2_OK;
    }
    else
    {
        switch (effect->kind)
        {
        case PairMaterialEffectKind::GenerateKey:
        case PairMaterialEffectKind::ReadPublicKey:
            result = ports_.cancel_key(&effect->token);
            break;
        case PairMaterialEffectKind::CreateTlsMaterial:
            result = ports_.cancel_tls_material(&effect->token);
            break;
        case PairMaterialEffectKind::RandomBytes:
            result = ports_.cancel_crypto(&effect->token);
            break;
        }
    }
    pair_material_dispatch_pending_ = false;
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_material_->cancel_pending();
        pair_material_active_ = false;
    }
}

void SessionEngine::cancel_session_signing_locked() noexcept
{
    if (!session_signing_) return;
    const auto effect = session_signing_->poll_effect();
    if (effect && session_signing_active_)
    {
        fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
        if (effect->kind == SessionSigningEffectKind::PersistBinding)
            result = ports_.cancel_secure_store(&effect->token);
        else if (effect->kind == SessionSigningEffectKind::PersistBindingObject)
            result = ports_.cancel_object_store(&effect->token);
        else
            result = ports_.cancel_key(&effect->token);
        if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
            result == FLY_SESSION_V2_DUPLICATE)
            session_signing_->cancel_pending();
    }
    else if (effect)
    {
        session_signing_->cancel_pending();
    }
    const auto key = session_signing_->material().key;
    if (key != 0) ports_.release_key(key);
    session_signing_active_ = false;
    session_signing_dispatch_pending_ = false;
    session_signing_.reset();
}

void SessionEngine::cancel_pair_known_locked() noexcept
{
    if (!pair_known_ || (!pair_known_active_ && !pair_known_dispatch_pending_))
        return;
    const auto effect = pair_known_->poll_effect();
    if (!effect)
    {
        pair_known_active_ = false;
        pair_known_dispatch_pending_ = false;
        return;
    }
    if (!pair_known_active_)
    {
        pair_known_->cancel_pending();
        pair_known_dispatch_pending_ = false;
        return;
    }
    const auto result = effect->kind == PairKnownEffectKind::Sign
        ? ports_.cancel_key(&effect->token) : ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_known_->cancel_pending();
        pair_known_active_ = false;
        pair_known_dispatch_pending_ = false;
    }
}

void SessionEngine::cancel_initial_quic_bind_locked() noexcept
{
    if (!initial_quic_bind_) return;
    const auto effect = initial_quic_bind_->poll_effect();
    if (effect && initial_quic_bind_active_)
    {
        fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
        if (effect->kind == InitialQuicBindEffectKind::DeriveKey ||
            effect->kind == InitialQuicBindEffectKind::Hmac)
            result = ports_.cancel_crypto(&effect->token);
        else
            result = ports_.cancel_quic(&effect->token);
        if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
            result == FLY_SESSION_V2_DUPLICATE)
            initial_quic_bind_->cancel_pending();
    }
    else if (effect) initial_quic_bind_->cancel_pending();
    const auto resources = initial_quic_bind_->owned_resources();
    if (resources.i2r_key != 0) ports_.release_secret(resources.i2r_key);
    if (resources.r2i_key != 0) ports_.release_secret(resources.r2i_key);
    initial_quic_bind_active_ = false;
    initial_quic_bind_dispatch_pending_ = false;
    initial_quic_bind_.reset();
}

void SessionEngine::cancel_endpoint_offer_locked() noexcept
{
    if (!endpoint_offer_) return;
    const auto effect = endpoint_offer_->poll_effect();
    if (effect && endpoint_offer_active_)
    {
        fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
        if (effect->kind == EndpointOfferEffectKind::ResolveEndpoint)
            result = ports_.cancel_bearer(&effect->token);
        else if (effect->kind == EndpointOfferEffectKind::Persist)
            result = ports_.cancel_secure_store(&effect->token);
        else
            result = ports_.cancel_crypto(&effect->token);
        if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
            result == FLY_SESSION_V2_DUPLICATE)
            endpoint_offer_->cancel_pending();
    }
    else if (effect) endpoint_offer_->cancel_pending();
    if (endpoint_offer_->i2r_key() != 0)
        ports_.release_secret(endpoint_offer_->i2r_key());
    if (endpoint_offer_->r2i_key() != 0)
        ports_.release_secret(endpoint_offer_->r2i_key());
    endpoint_offer_active_ = false;
    endpoint_offer_dispatch_pending_ = false;
    endpoint_offer_.reset();
}

void SessionEngine::cancel_initial_bearer_locked() noexcept
{
    if (!initial_bearer_)
        return;
    const auto resources = initial_bearer_->owned_resources();
    const auto effect = initial_bearer_->poll_effect();
    if (effect && initial_bearer_active_)
    {
        fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
        if (effect->kind == InitialBearerEffectKind::DeriveKey ||
            effect->kind == InitialBearerEffectKind::AeadSeal ||
            effect->kind == InitialBearerEffectKind::AeadOpen)
            result = ports_.cancel_crypto(&effect->token);
        else if (effect->kind == InitialBearerEffectKind::Persist)
            result = ports_.cancel_secure_store(&effect->token);
        else
            result = ports_.cancel_bearer(&effect->token);
        if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
            result == FLY_SESSION_V2_DUPLICATE)
            initial_bearer_->cancel_pending();
    }
    else if (effect)
    {
        initial_bearer_->cancel_pending();
    }
    if (resources.credential_i2r_key != 0)
        ports_.release_secret(resources.credential_i2r_key);
    if (resources.credential_r2i_key != 0)
        ports_.release_secret(resources.credential_r2i_key);
    if (resources.credential != 0)
        ports_.release_bearer_credential(resources.credential);
    initial_bearer_active_ = false;
    initial_bearer_dispatch_pending_ = false;
    initial_bearer_.reset();
}

void SessionEngine::cancel_initial_plan_locked() noexcept
{
    if (!initial_plan_ || (!initial_plan_active_ &&
                           !initial_plan_dispatch_pending_))
    {
        initial_plan_.reset();
        return;
    }
    const auto effect = initial_plan_->poll_effect();
    if (!effect)
    {
        initial_plan_active_ = false;
        initial_plan_dispatch_pending_ = false;
        initial_plan_.reset();
        return;
    }
    if (!initial_plan_active_)
    {
        initial_plan_->cancel_pending();
        initial_plan_dispatch_pending_ = false;
        initial_plan_.reset();
        return;
    }
    const auto result = effect->kind == InitialPlanEffectKind::Persist
        ? ports_.cancel_secure_store(&effect->token)
        : ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        initial_plan_->cancel_pending();
        initial_plan_active_ = false;
        initial_plan_dispatch_pending_ = false;
        initial_plan_.reset();
    }
}

void SessionEngine::cancel_pair_capability_locked() noexcept
{
    if (!pair_capability_ || (!pair_capability_active_ &&
                              !pair_capability_dispatch_pending_))
    {
        pair_capability_.reset();
        return;
    }
    const auto effect = pair_capability_->poll_effect();
    if (!effect)
    {
        pair_capability_active_ = false;
        pair_capability_dispatch_pending_ = false;
        pair_capability_.reset();
        return;
    }
    if (!pair_capability_active_)
    {
        pair_capability_->cancel_pending();
        pair_capability_dispatch_pending_ = false;
        pair_capability_.reset();
        return;
    }
    const auto result = ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_capability_->cancel_pending();
        pair_capability_active_ = false;
        pair_capability_dispatch_pending_ = false;
        pair_capability_.reset();
    }
}

void SessionEngine::cancel_pair_key_confirm_locked() noexcept
{
    pending_peer_key_confirm_.clear();
    pending_peer_key_confirm_hash_.fill(0);
    if (!pair_key_confirm_ || (!pair_key_confirm_active_ &&
                               !pair_key_confirm_dispatch_pending_))
    {
        pair_key_confirm_.reset();
        return;
    }
    const auto effect = pair_key_confirm_->poll_effect();
    if (!effect)
    {
        pair_key_confirm_active_ = false;
        pair_key_confirm_dispatch_pending_ = false;
        pair_key_confirm_.reset();
        return;
    }
    if (!pair_key_confirm_active_)
    {
        pair_key_confirm_->cancel_pending();
        pair_key_confirm_dispatch_pending_ = false;
        pair_key_confirm_.reset();
        return;
    }
    const auto result = ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_key_confirm_->cancel_pending();
        pair_key_confirm_active_ = false;
        pair_key_confirm_dispatch_pending_ = false;
        pair_key_confirm_.reset();
    }
}

void SessionEngine::cancel_pair_sas_locked() noexcept
{
    if (!pair_sas_ || (!pair_sas_active_ && !pair_sas_dispatch_pending_))
        return;
    const auto effect = pair_sas_->poll_effect();
    if (!effect)
    {
        pair_sas_active_ = false;
        pair_sas_dispatch_pending_ = false;
        return;
    }
    if (!pair_sas_active_)
    {
        pair_sas_->cancel_pending();
        pair_sas_dispatch_pending_ = false;
        return;
    }
    const auto result = ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_sas_->cancel_pending();
        pair_sas_active_ = false;
        pair_sas_dispatch_pending_ = false;
    }
}

void SessionEngine::release_pair_sas_locked() noexcept
{
    if (!pair_sas_ || pair_sas_active_)
        return;
    const auto owned = pair_sas_->owned_secrets();
    if (owned.sas_key != 0)
        ports_.release_secret(owned.sas_key);
    if (owned.gatt_r2i_key != 0)
        ports_.release_secret(owned.gatt_r2i_key);
    if (owned.gatt_i2r_key != 0)
        ports_.release_secret(owned.gatt_i2r_key);
    pair_sas_.reset();
    pair_sas_dispatch_pending_ = false;
    pair_sas_expected_kind_ = 0;
}

void SessionEngine::cancel_pair_signature_locked() noexcept
{
    if (!pair_signature_ || (!pair_signature_active_ &&
                             !pair_signature_dispatch_pending_))
        return;
    const auto effect = pair_signature_->poll_effect();
    if (!effect)
    {
        pair_signature_active_ = false;
        pair_signature_dispatch_pending_ = false;
        return;
    }
    if (!pair_signature_active_)
    {
        pair_signature_->cancel_pending();
        pair_signature_dispatch_pending_ = false;
        return;
    }
    const auto result = effect->kind == PairSignatureEffectKind::Sign
        ? ports_.cancel_key(&effect->token)
        : effect->kind == PairSignatureEffectKind::PersistTranscript
            ? ports_.cancel_object_store(&effect->token)
            : ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_signature_->cancel_pending();
        pair_signature_active_ = false;
        pair_signature_dispatch_pending_ = false;
    }
}

void SessionEngine::release_pair_signature_locked() noexcept
{
    if (!pair_signature_ || pair_signature_active_)
        return;
    const auto owned = pair_signature_->owned_secrets();
    if (owned.control_r2i_key != 0)
        ports_.release_secret(owned.control_r2i_key);
    if (owned.control_i2r_key != 0)
        ports_.release_secret(owned.control_i2r_key);
    pair_signature_.reset();
    pair_signature_dispatch_pending_ = false;
    pair_signature_expected_kind_ = 0;
}

void SessionEngine::release_pair_material_locked() noexcept
{
    if (!pair_material_ || pair_material_active_)
        return;
    const auto owned = pair_material_->owned_resources();
    if (owned.tls_material != 0)
        ports_.release_tls_material(owned.tls_material);
    if (owned.tls_key != 0)
        ports_.release_key(owned.tls_key);
    if (owned.ecdh_key != 0)
        ports_.release_key(owned.ecdh_key);
    if (owned.identity_key != 0)
        ports_.release_key(owned.identity_key);
    pair_material_.reset();
    pair_material_dispatch_pending_ = false;
    pair_material_expected_kind_ = 0;
}

void SessionEngine::cancel_pair_reveal_locked() noexcept
{
    if (!pair_reveal_ || (!pair_reveal_active_ &&
                          !pair_reveal_dispatch_pending_))
        return;
    const auto effect = pair_reveal_->poll_effect();
    if (!effect)
    {
        pair_reveal_active_ = false;
        pair_reveal_dispatch_pending_ = false;
        return;
    }
    if (!pair_reveal_active_)
    {
        pair_reveal_->cancel_pending();
        pair_reveal_dispatch_pending_ = false;
        return;
    }
    const auto result = effect->kind == PairRevealEffectKind::AgreeKey
        ? ports_.cancel_key(&effect->token)
        : ports_.cancel_crypto(&effect->token);
    if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
        result == FLY_SESSION_V2_DUPLICATE)
    {
        pair_reveal_->cancel_pending();
        pair_reveal_active_ = false;
        pair_reveal_dispatch_pending_ = false;
    }
}

void SessionEngine::release_pair_reveal_locked() noexcept
{
    if (!pair_reveal_ || pair_reveal_active_)
        return;
    const auto owned = pair_reveal_->owned_secrets();
    if (owned.r2i_key != 0)
        ports_.release_secret(owned.r2i_key);
    if (owned.i2r_key != 0)
        ports_.release_secret(owned.i2r_key);
    if (owned.ecdh_secret != 0)
        ports_.release_secret(owned.ecdh_secret);
    pair_reveal_.reset();
    pair_reveal_dispatch_pending_ = false;
    pair_reveal_expected_kind_ = 0;
}

bool SessionEngine::start_pair_reveal_locked() noexcept
{
    if (!pair_material_ || !pair_material_->ready() || !pair_exchange_ ||
        !pair_context_ || pair_exchange_->failed() || pair_reveal_ ||
        (local_pair_role_ != wire::PairRoleV1::Initiator &&
         local_pair_role_ != wire::PairRoleV1::Responder) ||
        next_operation_id_ >
            (std::numeric_limits<std::uint64_t>::max)() - 8)
        return false;
    const auto material = pair_material_->material();
    if (!material)
        return false;
    PairRevealStartV1 reveal_start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              reveal_start.engine_instance_id.begin());
    reveal_start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    reveal_start.generation = link_generation_;
    reveal_start.first_operation_id = next_operation_id_;
    reveal_start.local_role = local_pair_role_;
    reveal_start.context = *pair_context_;
    reveal_start.initiator_commit = pair_exchange_->initiator_commit();
    reveal_start.responder_commit = pair_exchange_->responder_commit();
    reveal_start.local_material = *material;
    try
    {
        auto reveal = std::make_unique<PairRevealScheduler>();
        if (!reveal->begin(reveal_start))
            return false;
        pair_reveal_ = std::move(reveal);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    next_operation_id_ += 8;
    pair_reveal_dispatch_pending_ = true;
    return true;
}

bool SessionEngine::start_pair_signature_locked() noexcept
{
    if (!pair_material_ || !pair_reveal_ || !pair_exchange_ ||
        !pair_context_ || pair_signature_ || !pair_material_->ready() ||
        !pair_reveal_->ready() || !pair_exchange_->ready() ||
        !ports_.has_object_store() ||
        (local_pair_role_ != wire::PairRoleV1::Initiator &&
         local_pair_role_ != wire::PairRoleV1::Responder) ||
        next_operation_id_ >
            (std::numeric_limits<std::uint64_t>::max)() - 10)
        return false;
    const auto material = pair_material_->material();
    const auto reveal_secrets = pair_reveal_->owned_secrets();
    if (!material || material->identity_key == 0 ||
        reveal_secrets.ecdh_secret == 0)
        return false;
    PairSignatureStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.local_identity_key = material->identity_key;
    start.ecdh_secret = reveal_secrets.ecdh_secret;
    start.context = *pair_context_;
    start.initiator_commit = pair_exchange_->initiator_commit();
    start.responder_commit = pair_exchange_->responder_commit();
    start.initiator_contribution = pair_exchange_->initiator_contribution();
    start.responder_contribution = pair_exchange_->responder_contribution();
    start.initiator_reveal_logical_hash =
        pair_exchange_->initiator_reveal_hash();
    start.responder_reveal_logical_hash =
        pair_exchange_->responder_reveal_hash();
    try
    {
        auto signature = std::make_unique<PairSignatureScheduler>();
        if (!signature->begin(start))
            return false;
        pair_signature_ = std::move(signature);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    next_operation_id_ += 10;
    pair_signature_dispatch_pending_ = true;
    return true;
}

bool SessionEngine::start_pair_sas_locked() noexcept
{
    if (!pair_signature_ || !pair_reveal_ || pair_sas_ ||
        !pair_signature_->ready() || !pair_reveal_->ready())
        return false;
    const auto reveal_secrets = pair_reveal_->owned_secrets();
    if (reveal_secrets.ecdh_secret == 0)
        return false;
    PairSasStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.ecdh_secret = reveal_secrets.ecdh_secret;
    start.transcript_hash = pair_signature_->transcript_hash();
    try
    {
        auto sas = std::make_unique<PairSasScheduler>();
        if (!sas->begin(start))
            return false;
        pair_sas_ = std::move(sas);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    pair_sas_dispatch_pending_ = true;
    return true;
}

bool SessionEngine::start_pair_known_locked() noexcept
{
    if (!pair_signature_ || !pair_sas_ || !pair_exchange_ || !pair_material_ ||
        pair_known_ ||
        !pair_signature_->ready() || !pair_sas_->ready() ||
        next_operation_id_ > (std::numeric_limits<std::uint64_t>::max)() - 12)
        return false;
    const auto signature = pair_signature_->owned_secrets();
    const auto sas = pair_sas_->owned_secrets();
    const auto material = pair_material_->material();
    if (!material || material->identity_key == 0 || sas.gatt_i2r_key == 0 ||
        sas.gatt_r2i_key == 0 || signature.control_i2r_key == 0 ||
        signature.control_r2i_key == 0)
        return false;
    PairKnownStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = pair_signature_->transcript_hash();
    start.initiator_public_key =
        pair_exchange_->initiator_contribution().identity_public_key;
    start.responder_public_key =
        pair_exchange_->responder_contribution().identity_public_key;
    start.local_identity_key = material->identity_key;
    start.gatt_i2r_key = sas.gatt_i2r_key;
    start.gatt_r2i_key = sas.gatt_r2i_key;
    start.control_i2r_key = signature.control_i2r_key;
    start.control_r2i_key = signature.control_r2i_key;
    // Anonymous candidate joins have no authenticated contact record. The
    // JOIN_FRIEND path will supply true only after its SecureStore lookup has
    // returned the exact active identity record.
    start.local_known = false;
    try
    {
        auto known = std::make_unique<PairKnownScheduler>();
        if (!known->begin(start)) return false;
        pair_known_ = std::move(known);
    }
    catch (const std::bad_alloc&) { return false; }
    next_operation_id_ += 12;
    pair_known_dispatch_pending_ = pair_known_->poll_effect().has_value();
    return true;
}

bool SessionEngine::start_pair_key_confirm_locked() noexcept
{
    if (!pair_signature_ || !pair_sas_ || pair_key_confirm_ ||
        !pair_signature_->ready() || !pair_signature_->local_approved() ||
        !pair_sas_->ready() || !pair_exchange_)
        return false;
    const auto sas_secrets = pair_sas_->owned_secrets();
    const auto signature_secrets = pair_signature_->owned_secrets();
    PairKeyConfirmStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = pair_signature_->transcript_hash();
    start.initiator_contribution = pair_exchange_->initiator_contribution();
    start.responder_contribution = pair_exchange_->responder_contribution();
    start.gatt_i2r_key = sas_secrets.gatt_i2r_key;
    start.gatt_r2i_key = sas_secrets.gatt_r2i_key;
    start.control_i2r_key = signature_secrets.control_i2r_key;
    start.control_r2i_key = signature_secrets.control_r2i_key;
    try
    {
        auto confirmation =
            std::make_unique<PairKeyConfirmScheduler>(*pair_signature_);
        if (!confirmation->begin(start))
            return false;
        if (!pending_peer_key_confirm_.empty())
        {
            const auto accepted = confirmation->accept_peer_envelope(
                pending_peer_key_confirm_.data(),
                pending_peer_key_confirm_.size(),
                pending_peer_key_confirm_hash_);
            if (accepted != FLY_SESSION_V2_ACCEPTED)
                return false;
            pending_peer_key_confirm_.clear();
            pending_peer_key_confirm_hash_.fill(0);
        }
        pair_key_confirm_ = std::move(confirmation);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    pair_key_confirm_dispatch_pending_ =
        pair_key_confirm_->poll_effect().has_value();
    return true;
}

bool SessionEngine::start_pair_capability_locked() noexcept
{
    if (!pair_signature_ || !pair_sas_ || !pair_key_confirm_ ||
        pair_capability_ || !local_pair_capability_ ||
        !pair_key_confirm_->ready())
        return false;
    const auto sas_secrets = pair_sas_->owned_secrets();
    const auto signature_secrets = pair_signature_->owned_secrets();
    PairCapabilityStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = pair_signature_->transcript_hash();
    start.local_summary = *local_pair_capability_;
    start.gatt_i2r_key = sas_secrets.gatt_i2r_key;
    start.gatt_r2i_key = sas_secrets.gatt_r2i_key;
    start.control_i2r_key = signature_secrets.control_i2r_key;
    start.control_r2i_key = signature_secrets.control_r2i_key;
    try
    {
        auto capability =
            std::make_unique<PairCapabilityScheduler>(*pair_signature_);
        if (!capability->begin(start))
            return false;
        pair_capability_ = std::move(capability);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    pair_capability_dispatch_pending_ =
        pair_capability_->poll_effect().has_value();
    return true;
}

bool SessionEngine::queue_local_pair_commit_locked() noexcept
{
    if (!pair_material_ || !pair_material_->ready() || !pair_exchange_ ||
        pair_exchange_->failed() || gatt_write_pending_ || gatt_write_active_ ||
        (local_pair_role_ == wire::PairRoleV1::Responder &&
         !pair_exchange_->initiator_commit_received()) ||
        (local_pair_role_ != wire::PairRoleV1::Initiator &&
         local_pair_role_ != wire::PairRoleV1::Responder))
        return false;
    const auto material = pair_material_->material();
    if (!material ||
        pair_exchange_->accept_commit(
            local_pair_role_, material->commit_bytes.data(),
            material->commit_bytes.size()) !=
            wire::PairExchangeResultV1::Accepted)
        return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            static_cast<std::uint8_t>(wire::GattLogicalType::PairCommit),
            material->commit_bytes.data(), material->commit_bytes.size(),
            &logical) != wire::GattFragmentResult::Accepted)
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(),
            static_cast<std::uint8_t>(wire::GattLogicalType::PairCommit),
            next_gatt_message_id_, discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    if (local_pair_role_ == wire::PairRoleV1::Responder &&
        !start_pair_reveal_locked())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0)
        next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = static_cast<std::uint8_t>(
        wire::GattLogicalType::PairCommit);
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::queue_local_pair_reveal_locked() noexcept
{
    if (!pair_reveal_ || !pair_exchange_ || gatt_write_pending_ ||
        gatt_write_active_ || !pair_reveal_->local_reveal_ready() ||
        pair_reveal_->local_reveal_sent())
        return false;
    const auto envelope = pair_reveal_->local_envelope();
    const auto material = pair_material_ ? pair_material_->material()
                                         : std::nullopt;
    if (!envelope || !material)
        return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            static_cast<std::uint8_t>(wire::GattLogicalType::PairReveal),
            envelope->data(), envelope->size(), &logical) !=
        wire::GattFragmentResult::Accepted)
        return false;
    std::array<std::uint8_t, 32> logical_hash{};
    std::copy(logical.end() - 32, logical.end(), logical_hash.begin());
    const auto exchange_result = pair_exchange_->accept_reveal(
            local_pair_role_,
            material->contribution.bytes.data(),
            material->contribution.bytes.size(), logical_hash);
    const auto expected_result =
        local_pair_role_ == wire::PairRoleV1::Initiator
            ? wire::PairExchangeResultV1::Accepted
            : wire::PairExchangeResultV1::Ready;
    if (exchange_result != expected_result)
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(),
            static_cast<std::uint8_t>(wire::GattLogicalType::PairReveal),
            next_gatt_message_id_, discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0)
        next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = static_cast<std::uint8_t>(
        wire::GattLogicalType::PairReveal);
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::queue_local_pair_signature_locked() noexcept
{
    if (!pair_signature_ || gatt_write_pending_ || gatt_write_active_ ||
        !pair_signature_->local_envelope_ready() ||
        pair_signature_->local_signature_sent())
        return false;
    const auto envelope = pair_signature_->local_envelope();
    if (!envelope)
        return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            static_cast<std::uint8_t>(wire::GattLogicalType::PairSignature),
            envelope->data(), envelope->size(), &logical) !=
        wire::GattFragmentResult::Accepted)
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(),
            static_cast<std::uint8_t>(wire::GattLogicalType::PairSignature),
            next_gatt_message_id_, discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0)
        next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = static_cast<std::uint8_t>(
        wire::GattLogicalType::PairSignature);
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::queue_local_pair_known_locked() noexcept
{
    if (!pair_known_ || gatt_write_pending_ || gatt_write_active_ ||
        !pair_known_->local_envelope_ready())
        return false;
    const auto envelope = pair_known_->local_envelope();
    const auto type = pair_known_->local_message_type();
    if (!envelope || (type != 21 && type != 17)) return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            type, envelope->data(), envelope->size(), &logical) !=
        wire::GattFragmentResult::Accepted)
        return false;
    pending_local_known_hash_ = wire::sha256(logical.data(), logical.size());
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(), type, next_gatt_message_id_,
            discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0) next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = type;
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::queue_local_pair_key_confirm_locked() noexcept
{
    if (!pair_key_confirm_ || gatt_write_pending_ || gatt_write_active_ ||
        !pair_key_confirm_->local_envelope_ready() ||
        pair_key_confirm_->local_sent())
        return false;
    const auto envelope = pair_key_confirm_->local_envelope();
    if (!envelope)
        return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            static_cast<std::uint8_t>(wire::GattLogicalType::KeyConfirm),
            envelope->data(), envelope->size(), &logical) !=
        wire::GattFragmentResult::Accepted)
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(),
            static_cast<std::uint8_t>(wire::GattLogicalType::KeyConfirm),
            next_gatt_message_id_, discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0)
        next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = static_cast<std::uint8_t>(
        wire::GattLogicalType::KeyConfirm);
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::queue_local_pair_capability_locked() noexcept
{
    if (!pair_capability_ || gatt_write_pending_ || gatt_write_active_ ||
        !pair_capability_->local_envelope_ready() ||
        pair_capability_->local_sent())
        return false;
    const auto envelope = pair_capability_->local_envelope();
    if (!envelope)
        return false;
    std::vector<std::uint8_t> logical;
    if (wire::encode_gatt_logical_message(
            static_cast<std::uint8_t>(
                wire::GattLogicalType::PairCapabilityReveal),
            envelope->data(), envelope->size(), &logical) !=
        wire::GattFragmentResult::Accepted)
        return false;
    std::array<std::uint8_t, 32> logical_hash{};
    std::copy(logical.end() - 32, logical.end(), logical_hash.begin());
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical.data(), logical.size(),
            static_cast<std::uint8_t>(
                wire::GattLogicalType::PairCapabilityReveal),
            next_gatt_message_id_, discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0)
        next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = static_cast<std::uint8_t>(
        wire::GattLogicalType::PairCapabilityReveal);
    pending_local_capability_hash_ = logical_hash;
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::finish_pair_capability_locked() noexcept
{
    if (!pair_capability_ || !pair_capability_->ready() ||
        !pair_signature_ || !local_pair_capability_)
        return false;
    const auto peer = pair_capability_->peer_summary();
    auto evidence = pair_signature_->take_verified_evidence();
    if (!peer || !evidence || !evidence->authenticated())
        return false;
    wire::BearerPlanBytes selected{};
    const auto& initiator = local_pair_role_ == wire::PairRoleV1::Initiator
        ? *local_pair_capability_ : *peer;
    const auto& responder = local_pair_role_ == wire::PairRoleV1::Responder
        ? *local_pair_capability_ : *peer;
    if (wire::select_pair_plan(
            initiator.data(), initiator.size(), responder.data(),
            responder.size(), selected) != wire::PairSelectionStatus::Ok)
        return false;
    verified_pair_evidence_ = std::move(evidence);
    selected_pair_plan_ = selected;
    return start_initial_plan_locked();
}

bool SessionEngine::start_initial_plan_locked() noexcept
{
    if (initial_plan_ || !verified_pair_evidence_ || !selected_pair_plan_ ||
        !pair_sas_ || !pair_signature_ || !ports_.has_secure_store())
        return false;
    const auto sas_secrets = pair_sas_->owned_secrets();
    const auto signature_secrets = pair_signature_->owned_secrets();
    InitialPlanStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.evidence = *verified_pair_evidence_;
    start.selected_plan = *selected_pair_plan_;
    start.gatt_i2r_key = sas_secrets.gatt_i2r_key;
    start.gatt_r2i_key = sas_secrets.gatt_r2i_key;
    start.control_i2r_key = signature_secrets.control_i2r_key;
    start.control_r2i_key = signature_secrets.control_r2i_key;
    try
    {
        auto plan = std::make_unique<InitialPlanScheduler>();
        if (!plan->begin(start)) return false;
        initial_plan_ = std::move(plan);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    initial_plan_dispatch_pending_ = initial_plan_->poll_effect().has_value();
    return true;
}

bool SessionEngine::queue_local_initial_plan_locked() noexcept
{
    if (!initial_plan_ || gatt_write_pending_ || gatt_write_active_)
        return false;
    const auto logical = initial_plan_->local_logical();
    if (!logical || logical->size() < wire::kGattLogicalMinSize)
        return false;
    const auto type = (*logical)[1];
    if (type < static_cast<std::uint8_t>(wire::GattLogicalType::InitialBearerPlan) ||
        type > static_cast<std::uint8_t>(wire::GattLogicalType::InitialBearerPlanFinal))
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical->data(), logical->size(), type, next_gatt_message_id_,
            discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0) next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = type;
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::start_initial_bearer_locked() noexcept
{
    if (initial_bearer_ || !initial_plan_ ||
        !initial_plan_->mutually_locked() || !selected_pair_plan_ ||
        !pair_context_ || !pair_reveal_ || !ports_.has_bearer() ||
        !ports_.has_crypto() || !ports_.has_secure_store())
        return false;
    const auto reveal_secrets = pair_reveal_->owned_secrets();
    if (reveal_secrets.ecdh_secret == 0)
        return false;
    InitialBearerStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = initial_plan_->verified_plan().transcript;
    std::copy_n(pair_context_->bytes.begin() + 32, start.session_id.size(),
                start.session_id.begin());
    start.selected_plan = *selected_pair_plan_;
    start.ecdh_secret = reveal_secrets.ecdh_secret;
    try
    {
        auto bearer = std::make_unique<InitialBearerScheduler>(*initial_plan_);
        if (!bearer->begin(start)) return false;
        initial_bearer_ = std::move(bearer);
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
    initial_bearer_dispatch_pending_ =
        initial_bearer_->poll_effect().has_value();
    return true;
}

bool SessionEngine::queue_local_initial_bearer_locked() noexcept
{
    if (!initial_bearer_ || gatt_write_pending_ || gatt_write_active_)
        return false;
    const auto logical = initial_bearer_->local_logical();
    if (!logical || logical->size() != 668 ||
        (*logical)[1] != static_cast<std::uint8_t>(
            wire::GattLogicalType::InitialBearerCredential))
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    const auto type = static_cast<std::uint8_t>(
        wire::GattLogicalType::InitialBearerCredential);
    if (wire::fragment_gatt_logical(
            logical->data(), logical->size(), type, next_gatt_message_id_,
            discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0) next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = type;
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::start_endpoint_offer_locked() noexcept
{
    if (endpoint_offer_ || !initial_bearer_ || !initial_bearer_->ready() ||
        !selected_pair_plan_ || !pair_context_ || !pair_reveal_ ||
        !pair_material_ || !ports_.has_bearer() || !ports_.has_crypto() ||
        !ports_.has_secure_store())
        return false;
    const auto local_material = pair_material_->material();
    const auto peer = pair_reveal_->peer_contribution();
    if (!local_material || !peer) return false;
    const auto listener = (*selected_pair_plan_)[2];
    const auto local_role = static_cast<std::uint8_t>(local_pair_role_);
    const auto listener_spki = listener == local_role
        ? local_material->contribution.tls_spki_hash : peer->tls_spki_hash;
    const auto reveal = pair_reveal_->owned_secrets();
    EndpointOfferStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = initial_plan_->verified_plan().transcript;
    std::copy_n(pair_context_->bytes.begin() + 32, start.session_id.size(),
                start.session_id.begin());
    start.selected_plan = *selected_pair_plan_;
    start.listener_spki_hash = listener_spki;
    start.ecdh_secret = reveal.ecdh_secret;
    start.bearer_path = initial_bearer_->owned_resources().bearer;
    try
    {
        auto offer = std::make_unique<EndpointOfferScheduler>(*initial_bearer_);
        if (!offer->begin(start)) return false;
        endpoint_offer_ = std::move(offer);
    }
    catch (const std::bad_alloc&) { return false; }
    endpoint_offer_dispatch_pending_ = endpoint_offer_->poll_effect().has_value();
    return true;
}

bool SessionEngine::queue_local_endpoint_offer_locked() noexcept
{
    if (!endpoint_offer_ || gatt_write_pending_ || gatt_write_active_)
        return false;
    const auto logical = endpoint_offer_->local_logical();
    if (!logical || logical->size() != 256 || (*logical)[1] != 22)
        return false;
    std::vector<std::vector<std::uint8_t>> fragments;
    if (wire::fragment_gatt_logical(
            logical->data(), logical->size(), 22, next_gatt_message_id_,
            discovery_att_value_cap_, &fragments) !=
            wire::GattFragmentResult::Accepted || fragments.empty())
        return false;
    ++next_gatt_message_id_;
    if (next_gatt_message_id_ == 0) next_gatt_message_id_ = 1;
    gatt_write_fragments_ = std::move(fragments);
    gatt_write_index_ = 0;
    gatt_write_logical_type_ = 22;
    gatt_write_pending_ = true;
    return true;
}

bool SessionEngine::start_initial_quic_bind_locked() noexcept
{
    if (initial_quic_bind_ || !endpoint_offer_ || !endpoint_offer_->ready() ||
        !selected_pair_plan_ || !pair_context_ || !pair_reveal_ ||
        !pair_material_ || !pair_material_->ready() ||
        !ports_.has_quic() || !ports_.has_crypto())
        return false;
    const auto local_material = pair_material_->material();
    const auto peer = pair_reveal_->peer_contribution();
    if (!local_material || !peer) return false;
    const auto reveal = pair_reveal_->owned_secrets();
    if (reveal.ecdh_secret == 0 || local_material->tls_material == 0)
        return false;

    InitialQuicBindStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.local_role = local_pair_role_;
    start.transcript_hash = initial_plan_->verified_plan().transcript;
    std::copy_n(pair_context_->bytes.begin() + 32, start.session_id.size(),
                start.session_id.begin());
    start.selected_plan = *selected_pair_plan_;
    const auto listener = (*selected_pair_plan_)[2];
    const auto local_role = static_cast<std::uint8_t>(local_pair_role_);
    start.listener_spki_hash = listener == local_role
        ? local_material->contribution.tls_spki_hash : peer->tls_spki_hash;
    start.local_identity_public = local_material->contribution.identity_public_key;
    start.peer_identity_public = peer->identity_public_key;
    start.ecdh_secret = reveal.ecdh_secret;
    start.bearer_path = initial_bearer_->owned_resources().bearer;
    start.tls_material = local_material->tls_material;
    try
    {
        auto bind = std::make_unique<InitialQuicBindScheduler>(*endpoint_offer_);
        if (!bind->begin(start)) return false;
        initial_quic_bind_ = std::move(bind);
    }
    catch (const std::bad_alloc&) { return false; }
    initial_quic_bind_dispatch_pending_ =
        initial_quic_bind_->poll_effect().has_value();
    return true;
}

bool SessionEngine::start_session_signing_locked() noexcept
{
    if (session_signing_ || !initial_quic_bind_ ||
        !initial_quic_bind_->channel_bound() || !pair_context_ ||
        !pair_signature_ || !pair_material_ || !pair_material_->ready() ||
        !ports_.has_key() || !ports_.has_secure_store() ||
        !ports_.has_object_store())
        return false;
    const auto material = pair_material_->material();
    if (!material || material->identity_key == 0) return false;
    SessionSigningStartV1 start{};
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              start.engine_instance_id.begin());
    start.link_id[0] = static_cast<std::uint8_t>(link_generation_);
    start.generation = link_generation_;
    start.first_operation_id = next_operation_id_;
    start.pair_transcript_hash = pair_signature_->transcript_hash();
    std::copy_n(pair_context_->bytes.begin() + 32, start.session_id.size(),
                start.session_id.begin());
    start.local_role = local_pair_role_;
    start.identity_key = material->identity_key;
    start.identity_public_key = material->contribution.identity_public_key;
    try
    {
        auto signing = std::make_unique<SessionSigningScheduler>();
        if (!signing->begin(start)) return false;
        session_signing_ = std::move(signing);
    }
    catch (const std::bad_alloc&) { return false; }
    session_signing_dispatch_pending_ =
        session_signing_->poll_effect().has_value();
    return true;
}

bool SessionEngine::accept_completed_gatt_locked(
    const std::vector<std::uint8_t>& logical) noexcept
{
    if (logical.size() < wire::kGattLogicalMinSize)
        return false;
    wire::GattLogicalMessageView message{};
    const auto type = logical[1];
    if (wire::decode_gatt_logical_message(
            logical.data(), logical.size(), type, &message) !=
        wire::GattFragmentResult::Complete)
        return false;
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::PhysicalAck))
    {
        wire::GattLogicalAckV1 ack{};
        const auto expected_role = discovery_physical_role_ ==
                FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2
            ? wire::GattPhysicalDirection::CentralToPeripheral
            : wire::GattPhysicalDirection::PeripheralToCentral;
        return wire::decode_gatt_logical_ack_v1(
                   message.body, message.body_size, &ack) ==
                   wire::GattFragmentResult::Complete &&
               ack.original_sender_physical_role == expected_role;
    }
    if (type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InviteCodeLookupRequest) ||
        type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InviteCodeLookupResponse))
    {
        wire::GattLookupMessageV2 lookup{};
        const auto direction = type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::InviteCodeLookupRequest)
            ? wire::GattLookupDirection::ResponderToInitiator
            : wire::GattLookupDirection::InitiatorToResponder;
        return wire::decode_gatt_lookup_v2(
                   direction, logical.data(), logical.size(), &lookup) ==
               wire::Status::Ok;
    }
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::PairContext))
    {
        if (local_pair_role_ != wire::PairRoleV1::Responder || pair_context_ ||
            pair_exchange_)
            return false;
        wire::PairContextV1 context{};
        if (wire::decode_pair_context_v1(message.body, message.body_size,
                                         &context) != wire::Status::Ok)
            return false;
        try
        {
            auto exchange = std::make_unique<wire::PairExchangeV1>(
                wire::validate_p256_uncompressed_point_callback, nullptr);
            if (exchange->begin(message.body, message.body_size) !=
                wire::PairExchangeResultV1::Accepted)
                return false;
            pair_exchange_ = std::move(exchange);
        }
        catch (const std::bad_alloc&)
        {
            return false;
        }
        pair_context_ = context;
        bearer_probe_pending_ = true;
        return true;
    }
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::PairCommit))
    {
        const auto peer_role = local_pair_role_ == wire::PairRoleV1::Initiator
            ? wire::PairRoleV1::Responder
            : wire::PairRoleV1::Initiator;
        if (!pair_exchange_ || !pair_context_ || pair_exchange_->failed() ||
            (local_pair_role_ != wire::PairRoleV1::Initiator &&
             local_pair_role_ != wire::PairRoleV1::Responder) ||
            pair_exchange_->accept_commit(
                peer_role, message.body,
                message.body_size) != wire::PairExchangeResultV1::Accepted)
            return false;
        if (local_pair_role_ == wire::PairRoleV1::Initiator)
            return start_pair_reveal_locked();
        return !pair_material_ || !pair_material_->ready() ||
               queue_local_pair_commit_locked();
    }
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::PairReveal))
    {
        if (!pair_reveal_ || !pair_exchange_ || pair_reveal_->failed())
            return false;
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy_n(message.logical_hash, logical_hash.size(),
                    logical_hash.begin());
        const auto accepted = pair_reveal_->accept_peer_envelope(
            message.body, message.body_size, logical_hash);
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            pair_reveal_->poll_effect())
            pair_reveal_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::PairSignature))
    {
        if (!pair_signature_ || pair_signature_->failed())
            return false;
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy_n(message.logical_hash, logical_hash.size(),
                    logical_hash.begin());
        const auto accepted = pair_signature_->accept_peer_envelope(
            message.body, message.body_size, logical_hash);
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            pair_signature_->poll_effect())
            pair_signature_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == 21 || type == 17)
    {
        if (!pair_known_ || pair_known_->failed()) return false;
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy_n(message.logical_hash, logical_hash.size(),
                    logical_hash.begin());
        const auto accepted = pair_known_->accept_peer_envelope(
            type, message.body, message.body_size, logical_hash);
        if (accepted == FLY_SESSION_V2_ACCEPTED && pair_known_->poll_effect())
            pair_known_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == static_cast<std::uint8_t>(wire::GattLogicalType::KeyConfirm))
    {
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy_n(message.logical_hash, logical_hash.size(),
                    logical_hash.begin());
        if (pair_key_confirm_)
        {
            const auto accepted = pair_key_confirm_->accept_peer_envelope(
                message.body, message.body_size, logical_hash);
            if (accepted == FLY_SESSION_V2_ACCEPTED &&
                pair_key_confirm_->poll_effect())
                pair_key_confirm_dispatch_pending_ = true;
            return accepted == FLY_SESSION_V2_ACCEPTED;
        }
        if (!pair_sas_ || !pair_sas_->ready() || !pair_signature_ ||
            pair_signature_->local_approved() ||
            !pending_peer_key_confirm_.empty())
            return false;
        try
        {
            pending_peer_key_confirm_.assign(
                message.body, message.body + message.body_size);
            pending_peer_key_confirm_hash_ = logical_hash;
            return true;
        }
        catch (const std::bad_alloc&)
        {
            pending_peer_key_confirm_.clear();
            pending_peer_key_confirm_hash_.fill(0);
            return false;
        }
    }
    if (type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::PairCapabilityReveal))
    {
        if (!pair_capability_ || pair_capability_->failed())
            return false;
        std::array<std::uint8_t, 32> logical_hash{};
        std::copy_n(message.logical_hash, logical_hash.size(),
                    logical_hash.begin());
        const auto accepted = pair_capability_->accept_peer_envelope(
            message.body, message.body_size, logical_hash);
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            pair_capability_->poll_effect())
            pair_capability_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InitialBearerPlan) ||
        type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InitialBearerPlanAck) ||
        type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InitialBearerPlanFinal))
    {
        if (!initial_plan_ || initial_plan_->failed()) return false;
        const auto accepted = initial_plan_->accept_peer_logical(
            logical.data(), logical.size());
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            initial_plan_->poll_effect())
            initial_plan_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::InitialBearerCredential))
    {
        if (!initial_bearer_ || initial_bearer_->failed()) return false;
        const auto accepted = initial_bearer_->accept_peer_logical(
            logical.data(), logical.size());
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            initial_bearer_->poll_effect())
            initial_bearer_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    if (type == static_cast<std::uint8_t>(
                    wire::GattLogicalType::BearerEndpointOffer))
    {
        if (!endpoint_offer_ || endpoint_offer_->failed()) return false;
        const auto accepted = endpoint_offer_->accept_peer_logical(
            logical.data(), logical.size());
        if (accepted == FLY_SESSION_V2_ACCEPTED &&
            endpoint_offer_->poll_effect())
            endpoint_offer_dispatch_pending_ = true;
        return accepted == FLY_SESSION_V2_ACCEPTED;
    }
    // Later phases are admitted only after their provider-driven reducer state
    // exists. Silently dropping a valid-hash KEY_CONFIRM or PLAN message would
    // allow an out-of-order peer to keep an unauthenticated resource alive.
    return false;
}

bool SessionEngine::queue_gatt_ack_locked(
    const wire::GattCompletedMetadata& completed) noexcept
{
    if (completed.message_id == 0 || completed.logical_type == 0 ||
        completed.logical_type == static_cast<std::uint8_t>(
            wire::GattLogicalType::PhysicalAck) ||
        pending_gatt_acks_.size() >= request_record_capacity_)
        return false;
    try
    {
        pending_gatt_acks_.push_back(completed);
        return true;
    }
    catch (const std::bad_alloc&)
    {
        return false;
    }
}

SessionEngine::SessionEngine(const fly_session_config_v2& config,
                             const fly_session_ports_v2& ports)
    : ports_(ports),
      authorization_(make_authorization_state()),
      action_capacity_(config.action_queue_capacity),
      notice_capacity_(config.notice_queue_capacity),
      request_record_capacity_((std::max)(UINT32_C(64),
                                          config.action_queue_capacity * 4U))
{
    pending_actions_.reserve(action_capacity_);
    notices_.reserve(notice_capacity_);
    request_records_.reserve(request_record_capacity_);
    pending_events_.reserve(action_capacity_);
    completion_records_.reserve(UINT32_C(64));
    current_view_ = make_session_view(
        authorization_, 1, FLY_SESSION_ENGINE_LOADING_V2, true);

    platform_watch_token_.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    platform_watch_token_.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(authorization_->engine_instance_id.begin(),
              authorization_->engine_instance_id.end(),
              platform_watch_token_.engine_instance_id);
    platform_watch_token_.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE;
    platform_watch_token_.scope.abi_version = FLY_SESSION_ABI_VERSION_2;
    platform_watch_token_.scope.kind = FLY_SESSION_SCOPE_ENGINE_V2;
    platform_watch_token_.operation_id = 1;
}

SessionEngine::~SessionEngine()
{
    authorization_->generation.fetch_add(1, std::memory_order_acq_rel);
    if (inbox_)
    {
        inbox_->closed.store(true, std::memory_order_release);
        fly_session_inbox_release_v2(inbox_);
    }
    for (const auto& action : pending_actions_)
    {
        fly_session_approval_token_release_v2(action.token);
    }
    for (const auto& record : request_records_)
    {
        fly_session_approval_token_release_v2(record.token);
    }
    fly_session_view_release_v2(current_view_);
    fly_session_view_release_v2(shutdown_complete_view_);
}

fly_session_result_v2 SessionEngine::start()
{
    auto inbox = std::make_unique<fly_session_inbox_v2_t>();
    inbox->engine = shared_from_this();
    inbox_ = inbox.release();
    const auto result = ports_.watch_platform_state(&platform_watch_token_, inbox_);
    if (result == FLY_SESSION_V2_ACCEPTED || result == FLY_SESSION_V2_OK)
    {
        return FLY_SESSION_V2_OK;
    }
    inbox_->closed.store(true, std::memory_order_release);
    fly_session_inbox_release_v2(inbox_);
    inbox_ = nullptr;
    return result;
}

fly_session_result_v2 SessionEngine::submit_action(
    const fly_session_action_v2& action)
{
    std::unique_ptr<fly_session_task_v2_t> task;
    try
    {
        task = std::make_unique<fly_session_task_v2_t>();
        task->engine = shared_from_this();
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }

    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!approval_token_matches(action.approval_token, authorization_))
        {
            return FLY_SESSION_V2_STALE;
        }
        if (shutdown_complete_ || handle_detached_)
        {
            return FLY_SESSION_V2_CLOSED;
        }
        const auto existing = std::find_if(
            request_records_.begin(),
            request_records_.end(),
            [&action](const RequestRecord& record) {
                return record.request_id == action.request_id;
            });
        if (existing != request_records_.end())
        {
            const bool same_choice =
                existing->choice_size == action.choice_size &&
                (action.choice_size == 0 ||
                 std::memcmp(&existing->choice,
                             &action.choice,
                             sizeof(action.choice)) == 0);
            return existing->token == action.approval_token && same_choice
                       ? FLY_SESSION_V2_DUPLICATE
                       : FLY_SESSION_V2_INVALID_ARGUMENT;
        }
        if (pending_actions_.size() >= action_capacity_ ||
            reserved_results_ >= notice_capacity_)
        {
            return FLY_SESSION_V2_BACKPRESSURE;
        }

        PendingAction pending{};
        pending.request_id = action.request_id;
        pending.expected_view_revision = action.expected_view_revision;
        pending.token = action.approval_token;
        pending.choice_size = action.choice_size;
        pending.choice = action.choice;
        fly_session_approval_token_retain_v2(pending.token);
        pending_actions_.push_back(pending);
        ++reserved_results_;

        if (request_records_.size() == request_record_capacity_)
        {
            fly_session_approval_token_release_v2(request_records_.front().token);
            request_records_.erase(request_records_.begin());
        }
        RequestRecord record{};
        record.request_id = action.request_id;
        record.token = action.approval_token;
        record.choice_size = action.choice_size;
        record.choice = action.choice;
        fly_session_approval_token_retain_v2(record.token);
        request_records_.push_back(record);
        if (!worker_scheduled_)
        {
            worker_scheduled_ = true;
            schedule = true;
        }
    }

    if (!schedule)
    {
        return FLY_SESSION_V2_ACCEPTED;
    }

    const auto post_result = ports_.post(task.get());
    if (post_result == FLY_SESSION_V2_ACCEPTED || post_result == FLY_SESSION_V2_OK)
    {
        task.release();
        return FLY_SESSION_V2_ACCEPTED;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    const auto pending = std::find_if(
        pending_actions_.begin(),
        pending_actions_.end(),
        [&action](const PendingAction& item) {
            return item.request_id == action.request_id &&
                   item.token == action.approval_token;
        });
    if (pending != pending_actions_.end())
    {
        fly_session_approval_token_release_v2(pending->token);
        pending_actions_.erase(pending);
        --reserved_results_;
    }
    const auto record = std::find_if(
        request_records_.begin(),
        request_records_.end(),
        [&action](const RequestRecord& item) {
            return item.request_id == action.request_id &&
                   item.token == action.approval_token;
        });
    if (record != request_records_.end())
    {
        fly_session_approval_token_release_v2(record->token);
        request_records_.erase(record);
    }
    worker_scheduled_ = false;
    return post_result;
}

fly_session_result_v2 SessionEngine::submit_input(
    const fly_session_input_v2& input)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (shutdown_requested_ || shutdown_complete_ || handle_detached_)
        return FLY_SESSION_V2_CLOSED;
    if (current_view_->snapshot.engine_state != FLY_SESSION_ENGINE_READY_V2 ||
        current_view_->snapshot.link_state !=
            FLY_SESSION_LINK_CONNECTED_LOBBY_V2 ||
        current_view_->snapshot.game_state != FLY_SESSION_GAME_RUNNING_V2 ||
        current_view_->snapshot.scope.kind != FLY_SESSION_SCOPE_GAME_V2 ||
        std::memcmp(current_view_->snapshot.scope.link_id,
                    input.scope.link_id, sizeof(input.scope.link_id)) != 0 ||
        std::memcmp(current_view_->snapshot.scope.branch_id,
                    input.scope.branch_id, sizeof(input.scope.branch_id)) != 0)
        return FLY_SESSION_V2_INVALID_STATE;

    // The public runtime/input scheduler is not yet installed. Fail closed rather
    // than accepting and dropping an edge that the UI believes was committed.
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 SessionEngine::deliver(
    const fly_session_port_event_v2& event)
{
    std::unique_ptr<fly_session_task_v2_t> task;
    try
    {
        task = std::make_unique<fly_session_task_v2_t>();
        task->engine = shared_from_this();
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }

    bool schedule = false;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (handle_detached_ || shutdown_complete_)
        {
            return FLY_SESSION_V2_CLOSED;
        }
        const auto existing = std::find_if(
            completion_records_.begin(),
            completion_records_.end(),
            [&event](const CompletionRecord& record) {
                return record.event.token.operation_id == event.token.operation_id &&
                       record.event.event_sequence == event.event_sequence;
            });
        if (existing != completion_records_.end())
        {
            return same_event(existing->event, event)
                       ? FLY_SESSION_V2_DUPLICATE
                       : FLY_SESSION_V2_CONTRACT_VIOLATION;
        }
        const bool platform_event = same_token(event.token, platform_watch_token_);
        const bool discovery_event =
            discovery_active_ && same_token(event.token, discovery_token_);
        const bool gatt_event = gatt_subscription_active_ &&
            same_token(event.token, gatt_subscription_token_);
        const bool disconnect_event = discovery_disconnect_active_ &&
            same_token(event.token, discovery_disconnect_token_);
        const bool pair_context_random_event = pair_context_random_active_ &&
            same_token(event.token, pair_context_random_token_);
        const bool bearer_probe_event = bearer_probe_active_ &&
            same_token(event.token, bearer_probe_token_);
        const bool pair_material_event = pair_material_active_ &&
            same_token(event.token, pair_material_token_);
        const bool pair_reveal_event = pair_reveal_active_ &&
            same_token(event.token, pair_reveal_token_);
        const bool pair_signature_event = pair_signature_active_ &&
            same_token(event.token, pair_signature_token_);
        const bool pair_known_event = pair_known_active_ &&
            same_token(event.token, pair_known_token_);
        const bool pair_sas_event = pair_sas_active_ &&
            same_token(event.token, pair_sas_token_);
        const bool pair_key_confirm_event = pair_key_confirm_active_ &&
            same_token(event.token, pair_key_confirm_token_);
        const bool pair_capability_event = pair_capability_active_ &&
            same_token(event.token, pair_capability_token_);
        const bool initial_plan_event = initial_plan_active_ &&
            same_token(event.token, initial_plan_token_);
        const bool initial_bearer_event = initial_bearer_active_ &&
            same_token(event.token, initial_bearer_token_);
        const bool endpoint_offer_event = endpoint_offer_active_ &&
            same_token(event.token, endpoint_offer_token_);
        const bool initial_quic_bind_event = initial_quic_bind_active_ &&
            same_token(event.token, initial_quic_bind_token_);
        const bool session_signing_event = session_signing_active_ &&
            same_token(event.token, session_signing_token_);
        const bool gatt_write_event = gatt_write_active_ &&
            same_token(event.token, gatt_write_token_);
        if (!platform_event && !discovery_event && !gatt_event &&
            !disconnect_event && !pair_context_random_event &&
            !bearer_probe_event && !pair_material_event &&
            !pair_reveal_event && !pair_signature_event && !pair_known_event &&
            !pair_sas_event &&
            !pair_key_confirm_event && !pair_capability_event &&
            !initial_plan_event && !initial_bearer_event &&
            !endpoint_offer_event && !initial_quic_bind_event &&
            !session_signing_event &&
            !gatt_write_event)
        {
            return FLY_SESSION_V2_STALE;
        }
        ParsedProviderEvent parsed;
        if (discovery_event || gatt_event || disconnect_event ||
            pair_context_random_event || bearer_probe_event ||
            pair_material_event || pair_reveal_event || pair_signature_event ||
            pair_known_event || pair_sas_event || pair_key_confirm_event ||
            pair_capability_event || initial_plan_event ||
            initial_bearer_event || endpoint_offer_event ||
            initial_quic_bind_event || session_signing_event ||
            gatt_write_event)
        {
            if (gatt_event &&
                event.payload_kind != FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2 &&
                event.payload_kind != FLY_SESSION_PROVIDER_DISCOVERY_END_V2)
                return FLY_SESSION_V2_CONTRACT_VIOLATION;
            if (disconnect_event &&
                event.payload_kind != FLY_SESSION_PROVIDER_DISCOVERY_END_V2)
                return FLY_SESSION_V2_CONTRACT_VIOLATION;
            const auto& expected_token = discovery_event
                ? discovery_token_
                : gatt_event ? gatt_subscription_token_
                : disconnect_event ? discovery_disconnect_token_
                : pair_context_random_event ? pair_context_random_token_
                : bearer_probe_event ? bearer_probe_token_
                : pair_material_event ? pair_material_token_
                : pair_reveal_event ? pair_reveal_token_
                : pair_signature_event ? pair_signature_token_
                : pair_known_event ? pair_known_token_
                : pair_sas_event ? pair_sas_token_
                : pair_key_confirm_event ? pair_key_confirm_token_
                : pair_capability_event ? pair_capability_token_
                : initial_plan_event ? initial_plan_token_
                : initial_bearer_event ? initial_bearer_token_
                : endpoint_offer_event ? endpoint_offer_token_
                : initial_quic_bind_event ? initial_quic_bind_token_
                : session_signing_event ? session_signing_token_
                                       : gatt_write_token_;
            const auto expected_kind = pair_context_random_event
                ? static_cast<std::uint32_t>(
                      FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2)
                : bearer_probe_event
                ? static_cast<std::uint32_t>(
                      FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2)
                : pair_material_event ? pair_material_expected_kind_
                : pair_reveal_event ? pair_reveal_expected_kind_
                : pair_signature_event ? pair_signature_expected_kind_
                : pair_known_event ? pair_known_expected_kind_
                : pair_sas_event ? pair_sas_expected_kind_
                : pair_key_confirm_event ? pair_key_confirm_expected_kind_
                : pair_capability_event ? pair_capability_expected_kind_
                : initial_plan_event ? initial_plan_expected_kind_
                : initial_bearer_event ? initial_bearer_expected_kind_
                : endpoint_offer_event ? endpoint_offer_expected_kind_
                : initial_quic_bind_event ? initial_quic_bind_expected_kind_
                : session_signing_event ? session_signing_expected_kind_
                : gatt_write_event
                    ? static_cast<std::uint32_t>(
                          FLY_SESSION_PROVIDER_DISCOVERY_END_V2)
                    : event.payload_kind;
            const auto parse_result = parse_provider_event_v2(
                event, expected_token, expected_kind, parsed);
            if (parse_result != FLY_SESSION_V2_OK)
                return parse_result;
            if ((discovery_event || gatt_event || disconnect_event) &&
                event.payload_kind != FLY_SESSION_PROVIDER_DISCOVERY_END_V2 &&
                parsed.generation != event.token.connection_generation)
                return FLY_SESSION_V2_STALE;
        }
        if (pending_events_.size() >= action_capacity_)
        {
            return FLY_SESSION_V2_BACKPRESSURE;
        }
        if (completion_records_.size() == completion_records_.capacity())
        {
            completion_records_.erase(completion_records_.begin());
        }
        completion_records_.push_back(CompletionRecord{event});
        pending_events_.emplace_back(event, parsed.buffer);
        if (!worker_scheduled_)
        {
            worker_scheduled_ = true;
            schedule = true;
        }
    }

    if (!schedule)
    {
        return FLY_SESSION_V2_ACCEPTED;
    }
    const auto post_result = ports_.post(task.get());
    if (post_result == FLY_SESSION_V2_ACCEPTED || post_result == FLY_SESSION_V2_OK)
    {
        task.release();
        return FLY_SESSION_V2_ACCEPTED;
    }
    std::lock_guard<std::mutex> lock(mutex_);
    worker_scheduled_ = false;
    const auto pending = std::find_if(
        pending_events_.begin(), pending_events_.end(),
        [&event](const PendingPortEvent& item) {
            return item.event.token.operation_id == event.token.operation_id &&
                   item.event.event_sequence == event.event_sequence;
        });
    if (pending != pending_events_.end())
    {
        pending_events_.erase(pending);
    }
    const auto record = std::find_if(
        completion_records_.begin(),
        completion_records_.end(),
        [&event](const CompletionRecord& item) {
            return item.event.token.operation_id == event.token.operation_id &&
                   item.event.event_sequence == event.event_sequence;
        });
    if (record != completion_records_.end())
    {
        completion_records_.erase(record);
    }
    return post_result;
}

void SessionEngine::run_work() noexcept
{
    for (;;)
    {
        bool dispatch_disconnect = false;
        fly_session_op_token_v2 deferred_disconnect_token{};
        fly_session_resource_handle_v2 deferred_disconnect_connection = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (discovery_disconnect_pending_)
            {
                discovery_disconnect_pending_ = false;
                discovery_disconnect_token_ = make_link_operation_token_locked();
                deferred_disconnect_token = discovery_disconnect_token_;
                deferred_disconnect_connection = discovery_connection_;
                discovery_disconnect_active_ = true;
                dispatch_disconnect = true;
            }
        }
        if (dispatch_disconnect)
        {
            const auto result = ports_.disconnect_discovery(
                &deferred_disconnect_token, deferred_disconnect_connection,
                link_generation_, inbox_);
            if (result == FLY_SESSION_V2_OK ||
                result == FLY_SESSION_V2_CANCELLED ||
                result == FLY_SESSION_V2_DUPLICATE)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                discovery_disconnect_active_ = false;
                gatt_subscription_active_ = false;
                gatt_reassembler_.reset();
                pair_context_.reset();
                discovery_connection_ = 0;
                complete_shutdown_locked();
            }
            // An asynchronous disconnect is completed by its fenced END event.
            // Other synchronous failures intentionally leave the resource
            // outstanding so destroy cannot free an inbox still owned by the OS.
            continue;
        }

        bool dispatch_gatt_subscribe = false;
        fly_session_op_token_v2 subscribe_token{};
        fly_session_resource_handle_v2 subscribe_connection = 0;
        std::uint32_t subscribe_characteristic = 0;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (gatt_subscribe_pending_ && !shutdown_requested_)
            {
                gatt_subscribe_pending_ = false;
                gatt_subscription_token_ = make_link_operation_token_locked();
                subscribe_token = gatt_subscription_token_;
                subscribe_connection = discovery_connection_;
                subscribe_characteristic = discovery_physical_role_ ==
                        FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2
                    ? FLY_SESSION_DISCOVERY_INDICATE_V2
                    : FLY_SESSION_DISCOVERY_WRITE_V2;
                gatt_subscription_active_ = true;
                dispatch_gatt_subscribe = true;
            }
        }
        if (dispatch_gatt_subscribe)
        {
            const auto result = ports_.subscribe_discovery(
                &subscribe_token, subscribe_connection,
                subscribe_characteristic, inbox_);
            if (result != FLY_SESSION_V2_ACCEPTED && result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                gatt_subscription_active_ = false;
                gatt_reassembler_.reset();
                pair_context_.reset();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            else if (result == FLY_SESSION_V2_OK)
            {
                // A subscription that completes synchronously cannot deliver a
                // byte stream and is therefore not a usable GATT channel.
                std::lock_guard<std::mutex> lock(mutex_);
                gatt_subscription_active_ = false;
                gatt_reassembler_.reset();
                pair_context_.reset();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_context_random = false;
        fly_session_op_token_v2 local_pair_context_random_token{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_context_random_pending_ && !shutdown_requested_)
            {
                pair_context_random_pending_ = false;
                pair_context_random_token_ = make_link_operation_token_locked();
                local_pair_context_random_token = pair_context_random_token_;
                pair_context_random_active_ = true;
                dispatch_pair_context_random = true;
            }
        }
        if (dispatch_pair_context_random)
        {
            static constexpr std::array<std::uint8_t, 29> binding{{
                'f','l','y','n','e','s','-','p','a','i','r','-','c','o','n',
                't','e','x','t','-','r','a','n','d','o','m','-','v','1'}};
            const fly_session_bytes_v2 exact_binding{
                binding.data(), static_cast<std::uint32_t>(binding.size()), 0};
            const auto result = ports_.random(
                &local_pair_context_random_token, 48, exact_binding, inbox_);
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_context_random_active_ = false;
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_gatt_write = false;
        fly_session_op_token_v2 local_gatt_write_token{};
        fly_session_resource_handle_v2 local_gatt_connection = 0;
        std::uint32_t local_gatt_characteristic = 0;
        std::vector<std::uint8_t> local_gatt_fragment;
        bool gatt_write_copy_failed = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!gatt_write_pending_ && !gatt_write_active_ &&
                !pending_gatt_acks_.empty() && !shutdown_requested_)
            {
                const auto completed = pending_gatt_acks_.front();
                wire::GattLogicalAckV1 ack{};
                ack.original_sender_physical_role = discovery_physical_role_ ==
                        FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2
                    ? wire::GattPhysicalDirection::PeripheralToCentral
                    : wire::GattPhysicalDirection::CentralToPeripheral;
                ack.acked_logical_type = completed.logical_type;
                ack.message_id = completed.message_id;
                ack.logical_hash = completed.logical_hash;
                std::array<std::uint8_t, wire::kGattLogicalAckBodySize> body{};
                std::vector<std::uint8_t> logical;
                std::vector<std::vector<std::uint8_t>> fragments;
                const bool encoded = wire::encode_gatt_logical_ack_v1(
                                         ack, &body) ==
                                         wire::GattFragmentResult::Accepted &&
                    wire::encode_gatt_logical_message(
                        static_cast<std::uint8_t>(wire::GattLogicalType::PhysicalAck),
                        body.data(), body.size(), &logical) ==
                        wire::GattFragmentResult::Accepted &&
                    wire::fragment_gatt_logical(
                        logical.data(), logical.size(),
                        static_cast<std::uint8_t>(wire::GattLogicalType::PhysicalAck),
                        next_gatt_message_id_, discovery_att_value_cap_,
                        &fragments) == wire::GattFragmentResult::Accepted;
                if (encoded)
                {
                    ++next_gatt_message_id_;
                    if (next_gatt_message_id_ == 0) next_gatt_message_id_ = 1;
                    gatt_write_fragments_ = std::move(fragments);
                    gatt_write_index_ = 0;
                    gatt_write_logical_type_ = static_cast<std::uint8_t>(
                        wire::GattLogicalType::PhysicalAck);
                    gatt_write_pending_ = true;
                    pending_gatt_acks_.pop_front();
                }
                else
                {
                    pending_gatt_acks_.clear();
                    cancel_pair_material_locked();
                    release_pair_material_locked();
                    discovery_disconnect_pending_ = discovery_connection_ != 0;
                    publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    gatt_write_copy_failed = true;
                }
            }
            if (!gatt_write_pending_ && !gatt_write_active_ &&
                pending_gatt_acks_.empty() &&
                !deferred_gatt_write_fragments_.empty() && !shutdown_requested_)
            {
                gatt_write_fragments_ =
                    std::move(deferred_gatt_write_fragments_);
                gatt_write_index_ = 0;
                gatt_write_logical_type_ = deferred_gatt_write_logical_type_;
                deferred_gatt_write_logical_type_ = 0;
                gatt_write_pending_ = true;
            }
            if (gatt_write_pending_ && !gatt_write_active_ &&
                gatt_write_index_ < gatt_write_fragments_.size() &&
                !shutdown_requested_)
            {
                try
                {
                    local_gatt_fragment =
                        gatt_write_fragments_[gatt_write_index_];
                    gatt_write_token_ = make_link_operation_token_locked();
                    local_gatt_write_token = gatt_write_token_;
                    local_gatt_connection = discovery_connection_;
                    local_gatt_characteristic = discovery_physical_role_ ==
                            FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2
                        ? FLY_SESSION_DISCOVERY_WRITE_V2
                        : FLY_SESSION_DISCOVERY_INDICATE_V2;
                    gatt_write_active_ = true;
                    dispatch_gatt_write = true;
                }
                catch (const std::bad_alloc&)
                {
                    gatt_write_pending_ = false;
                    gatt_write_fragments_.clear();
                    cancel_pair_material_locked();
                    release_pair_material_locked();
                    discovery_disconnect_pending_ =
                        discovery_connection_ != 0;
                    publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    gatt_write_copy_failed = true;
                }
            }
        }
        if (gatt_write_copy_failed)
            continue;
        if (dispatch_gatt_write)
        {
            fly_session_buffer_v2_t* buffer = nullptr;
            const fly_session_bytes_v2 source{
                local_gatt_fragment.data(),
                static_cast<std::uint32_t>(local_gatt_fragment.size()), 0};
            auto result = fly_session_buffer_create_copy_v2(source, &buffer);
            if (result == FLY_SESSION_V2_OK)
            {
                result = ports_.write_discovery(
                    local_gatt_characteristic ==
                        FLY_SESSION_DISCOVERY_INDICATE_V2,
                    &local_gatt_write_token, local_gatt_connection,
                    local_gatt_characteristic, buffer, inbox_);
            }
            fly_session_buffer_release_v2(buffer);
            if (result == FLY_SESSION_V2_OK)
            {
                fly_session_port_event_v2 completed{};
                completed.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
                completed.abi_version = FLY_SESSION_ABI_VERSION_2;
                completed.token = local_gatt_write_token;
                completed.event_sequence = 1;
                completed.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
                completed.terminal = 1;
                completed.result = FLY_SESSION_V2_OK;
                completed.payload_kind = FLY_SESSION_PROVIDER_DISCOVERY_END_V2;
                fly_session_provider_end_event_v2 payload{};
                payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
                payload.abi_version = FLY_SESSION_ABI_VERSION_2;
                completed.payload_size = sizeof(payload);
                std::memcpy(completed.payload, &payload, sizeof(payload));
                result = deliver(completed);
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK &&
                result != FLY_SESSION_V2_DUPLICATE)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                gatt_write_active_ = false;
                gatt_write_pending_ = false;
                gatt_write_fragments_.clear();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_reveal = false;
        PairRevealEffect reveal_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_reveal_dispatch_pending_ && pair_reveal_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_reveal_->poll_effect();
                if (effect)
                {
                    pair_reveal_dispatch_pending_ = false;
                    reveal_effect = *effect;
                    pair_reveal_token_ = effect->token;
                    pair_reveal_expected_kind_ =
                        pair_reveal_payload_kind(effect->kind);
                    pair_reveal_active_ = true;
                    dispatch_pair_reveal = true;
                }
            }
        }
        if (dispatch_pair_reveal)
        {
            const fly_session_bytes_v2 peer{
                reveal_effect.peer_public_key.data(),
                static_cast<std::uint32_t>(reveal_effect.peer_public_key.size()), 0};
            const fly_session_bytes_v2 salt{
                reveal_effect.salt.data(),
                static_cast<std::uint32_t>(reveal_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                reveal_effect.info.data(),
                static_cast<std::uint32_t>(reveal_effect.info.size()), 0};
            const fly_session_bytes_v2 nonce{
                reveal_effect.nonce.data(),
                static_cast<std::uint32_t>(reveal_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                reveal_effect.aad.data(),
                static_cast<std::uint32_t>(reveal_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                reveal_effect.input.data(),
                static_cast<std::uint32_t>(reveal_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (reveal_effect.kind)
            {
            case PairRevealEffectKind::AgreeKey:
                result = ports_.agree_key(
                    &reveal_effect.token, reveal_effect.resource, peer, input,
                    inbox_);
                break;
            case PairRevealEffectKind::DeriveKey:
                result = ports_.hkdf(
                    &reveal_effect.token, reveal_effect.resource, salt, info,
                    reveal_effect.byte_count, inbox_);
                break;
            case PairRevealEffectKind::RandomBytes:
                result = ports_.random(
                    &reveal_effect.token, reveal_effect.byte_count, info, inbox_);
                break;
            case PairRevealEffectKind::AeadSeal:
            case PairRevealEffectKind::AeadOpen:
                result = ports_.aead(
                    reveal_effect.kind == PairRevealEffectKind::AeadSeal,
                    &reveal_effect.token, reveal_effect.resource, nonce, aad,
                    input, inbox_);
                break;
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_reveal_active_ = false;
                pair_reveal_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_signature = false;
        PairSignatureEffect signature_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_signature_dispatch_pending_ && pair_signature_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_signature_->poll_effect();
                if (effect)
                {
                    pair_signature_dispatch_pending_ = false;
                    signature_effect = *effect;
                    pair_signature_token_ = effect->token;
                    pair_signature_expected_kind_ =
                        pair_signature_payload_kind(effect->kind);
                    pair_signature_active_ = true;
                    dispatch_pair_signature = true;
                }
            }
        }
        if (dispatch_pair_signature)
        {
            const fly_session_bytes_v2 salt{
                signature_effect.salt.data(),
                static_cast<std::uint32_t>(signature_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                signature_effect.info.data(),
                static_cast<std::uint32_t>(signature_effect.info.size()), 0};
            const fly_session_bytes_v2 domain{
                signature_effect.domain.data(),
                static_cast<std::uint32_t>(signature_effect.domain.size()), 0};
            const fly_session_bytes_v2 public_key{
                signature_effect.public_key.data(),
                static_cast<std::uint32_t>(signature_effect.public_key.size()), 0};
            const fly_session_bytes_v2 signature{
                signature_effect.signature.data(),
                static_cast<std::uint32_t>(signature_effect.signature.size()), 0};
            const fly_session_bytes_v2 nonce{
                signature_effect.nonce.data(),
                static_cast<std::uint32_t>(signature_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                signature_effect.aad.data(),
                static_cast<std::uint32_t>(signature_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                signature_effect.input.data(),
                static_cast<std::uint32_t>(signature_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (signature_effect.kind)
            {
            case PairSignatureEffectKind::DeriveKey:
                result = ports_.hkdf(
                    &signature_effect.token, signature_effect.resource, salt,
                    info, signature_effect.byte_count, inbox_);
                break;
            case PairSignatureEffectKind::Sign:
                result = ports_.sign_prehashed(
                    &signature_effect.token, signature_effect.resource,
                    signature_effect.key_purpose, domain,
                    signature_effect.digest.data(), inbox_);
                break;
            case PairSignatureEffectKind::Verify:
                result = ports_.verify_prehashed(
                    &signature_effect.token, public_key, domain,
                    signature_effect.digest.data(), signature, inbox_);
                break;
            case PairSignatureEffectKind::AeadSeal:
            case PairSignatureEffectKind::AeadOpen:
                result = ports_.aead(
                    signature_effect.kind == PairSignatureEffectKind::AeadSeal,
                    &signature_effect.token, signature_effect.resource, nonce,
                    aad, input, inbox_);
                break;
            case PairSignatureEffectKind::PersistTranscript:
            {
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(input, &buffer) ==
                    FLY_SESSION_V2_OK)
                    result = ports_.put_immutable_object(
                        &signature_effect.token, signature_effect.object_kind,
                        signature_effect.expected_hash.data(), buffer, inbox_);
                else
                    result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
                break;
            }
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_signature_active_ = false;
                pair_signature_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_sas = false;
        PairSasEffect sas_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_sas_dispatch_pending_ && pair_sas_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_sas_->poll_effect();
                if (effect)
                {
                    pair_sas_dispatch_pending_ = false;
                    sas_effect = *effect;
                    pair_sas_token_ = effect->token;
                    pair_sas_expected_kind_ =
                        pair_sas_payload_kind(effect->kind);
                    pair_sas_active_ = true;
                    dispatch_pair_sas = true;
                }
            }
        }
        if (dispatch_pair_sas)
        {
            const fly_session_bytes_v2 salt{
                sas_effect.salt.data(),
                static_cast<std::uint32_t>(sas_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                sas_effect.info.data(),
                static_cast<std::uint32_t>(sas_effect.info.size()), 0};
            const fly_session_bytes_v2 input{
                sas_effect.input.data(),
                static_cast<std::uint32_t>(sas_effect.input.size()), 0};
            const auto result = sas_effect.kind == PairSasEffectKind::DeriveKey
                ? ports_.hkdf(&sas_effect.token, sas_effect.resource, salt,
                              info, sas_effect.byte_count, inbox_)
                : ports_.hmac_sha256(&sas_effect.token, sas_effect.resource,
                                     input, inbox_);
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_sas_active_ = false;
                pair_sas_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_known = false;
        PairKnownEffect known_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_known_dispatch_pending_ && pair_known_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_known_->poll_effect();
                if (effect)
                {
                    pair_known_dispatch_pending_ = false;
                    known_effect = *effect;
                    pair_known_token_ = effect->token;
                    pair_known_expected_kind_ =
                        pair_known_payload_kind(effect->kind);
                    pair_known_active_ = true;
                    dispatch_pair_known = true;
                }
            }
        }
        if (dispatch_pair_known)
        {
            const fly_session_bytes_v2 domain{
                known_effect.domain.data(),
                static_cast<std::uint32_t>(known_effect.domain.size()), 0};
            const fly_session_bytes_v2 public_key{
                known_effect.public_key.data(),
                static_cast<std::uint32_t>(known_effect.public_key.size()), 0};
            const fly_session_bytes_v2 signature{
                known_effect.signature.data(),
                static_cast<std::uint32_t>(known_effect.signature.size()), 0};
            const fly_session_bytes_v2 nonce{
                known_effect.nonce.data(),
                static_cast<std::uint32_t>(known_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                known_effect.aad.data(),
                static_cast<std::uint32_t>(known_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                known_effect.input.data(),
                static_cast<std::uint32_t>(known_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (known_effect.kind)
            {
            case PairKnownEffectKind::Hmac:
                result = ports_.hmac_sha256(
                    &known_effect.token, known_effect.resource, input, inbox_);
                break;
            case PairKnownEffectKind::Sign:
                result = ports_.sign_prehashed(
                    &known_effect.token, known_effect.resource,
                    known_effect.key_purpose, domain,
                    known_effect.digest.data(), inbox_);
                break;
            case PairKnownEffectKind::Verify:
                result = ports_.verify_prehashed(
                    &known_effect.token, public_key, domain,
                    known_effect.digest.data(), signature, inbox_);
                break;
            case PairKnownEffectKind::AeadSeal:
            case PairKnownEffectKind::AeadOpen:
                result = ports_.aead(
                    known_effect.kind == PairKnownEffectKind::AeadSeal,
                    &known_effect.token, known_effect.resource, nonce, aad,
                    input, inbox_);
                break;
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_known_active_ = false;
                pair_known_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_key_confirm = false;
        PairKeyConfirmEffect key_confirm_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_key_confirm_dispatch_pending_ && pair_key_confirm_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_key_confirm_->poll_effect();
                if (effect)
                {
                    pair_key_confirm_dispatch_pending_ = false;
                    key_confirm_effect = *effect;
                    pair_key_confirm_token_ = effect->token;
                    pair_key_confirm_expected_kind_ =
                        pair_key_confirm_payload_kind(effect->kind);
                    pair_key_confirm_active_ = true;
                    dispatch_pair_key_confirm = true;
                }
            }
        }
        if (dispatch_pair_key_confirm)
        {
            const fly_session_bytes_v2 nonce{
                key_confirm_effect.nonce.data(),
                static_cast<std::uint32_t>(key_confirm_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                key_confirm_effect.aad.data(),
                static_cast<std::uint32_t>(key_confirm_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                key_confirm_effect.input.data(),
                static_cast<std::uint32_t>(key_confirm_effect.input.size()), 0};
            const auto result =
                key_confirm_effect.kind == PairKeyConfirmEffectKind::Hmac
                ? ports_.hmac_sha256(
                      &key_confirm_effect.token, key_confirm_effect.resource,
                      input, inbox_)
                : ports_.aead(
                      key_confirm_effect.kind ==
                          PairKeyConfirmEffectKind::AeadSeal,
                      &key_confirm_effect.token, key_confirm_effect.resource,
                      nonce, aad, input, inbox_);
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_key_confirm_active_ = false;
                pair_key_confirm_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_capability = false;
        PairCapabilityEffect capability_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_capability_dispatch_pending_ && pair_capability_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_capability_->poll_effect();
                if (effect)
                {
                    pair_capability_dispatch_pending_ = false;
                    capability_effect = *effect;
                    pair_capability_token_ = effect->token;
                    pair_capability_expected_kind_ =
                        pair_capability_payload_kind(effect->kind);
                    pair_capability_active_ = true;
                    dispatch_pair_capability = true;
                }
            }
        }
        if (dispatch_pair_capability)
        {
            const fly_session_bytes_v2 nonce{
                capability_effect.nonce.data(),
                static_cast<std::uint32_t>(capability_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                capability_effect.aad.data(),
                static_cast<std::uint32_t>(capability_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                capability_effect.input.data(),
                static_cast<std::uint32_t>(capability_effect.input.size()), 0};
            const auto result =
                capability_effect.kind == PairCapabilityEffectKind::Hmac
                ? ports_.hmac_sha256(
                      &capability_effect.token, capability_effect.resource,
                      input, inbox_)
                : ports_.aead(
                      capability_effect.kind ==
                          PairCapabilityEffectKind::AeadSeal,
                      &capability_effect.token, capability_effect.resource,
                      nonce, aad, input, inbox_);
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_capability_active_ = false;
                pair_capability_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_initial_plan = false;
        InitialPlanEffect initial_plan_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initial_plan_dispatch_pending_ && initial_plan_ &&
                !shutdown_requested_)
            {
                const auto effect = initial_plan_->poll_effect();
                if (effect)
                {
                    initial_plan_dispatch_pending_ = false;
                    initial_plan_effect = *effect;
                    initial_plan_token_ = effect->token;
                    initial_plan_expected_kind_ =
                        initial_plan_payload_kind(effect->kind);
                    initial_plan_active_ = true;
                    dispatch_initial_plan = true;
                }
            }
        }
        if (dispatch_initial_plan)
        {
            const fly_session_bytes_v2 nonce{
                initial_plan_effect.nonce.data(),
                static_cast<std::uint32_t>(initial_plan_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                initial_plan_effect.aad.data(),
                static_cast<std::uint32_t>(initial_plan_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                initial_plan_effect.input.data(),
                static_cast<std::uint32_t>(initial_plan_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            if (initial_plan_effect.kind == InitialPlanEffectKind::Random)
            {
                static constexpr std::array<std::uint8_t, 28> purpose{{
                    'f','l','y','n','e','s','-','i','n','i','t','i','a','l','-',
                    'p','l','a','n','-','n','o','n','c','e','-','v','1'}};
                const fly_session_bytes_v2 purpose_bytes{
                    purpose.data(), static_cast<std::uint32_t>(purpose.size()), 0};
                result = ports_.random(&initial_plan_effect.token, 16,
                                       purpose_bytes, inbox_);
            }
            else if (initial_plan_effect.kind == InitialPlanEffectKind::Hmac)
            {
                result = ports_.hmac_sha256(
                    &initial_plan_effect.token, initial_plan_effect.resource,
                    input, inbox_);
            }
            else if (initial_plan_effect.kind == InitialPlanEffectKind::AeadSeal ||
                     initial_plan_effect.kind == InitialPlanEffectKind::AeadOpen)
            {
                result = ports_.aead(
                    initial_plan_effect.kind == InitialPlanEffectKind::AeadSeal,
                    &initial_plan_effect.token, initial_plan_effect.resource,
                    nonce, aad, input, inbox_);
            }
            else
            {
                static constexpr std::array<std::uint8_t, 16> name_space{{
                    'f','l','y','n','e','s','-','n','e','a','r','b','y','-','v','2'}};
                std::array<std::uint8_t, 33> record_key{};
                if (verified_pair_evidence_)
                    std::copy(verified_pair_evidence_->transcript.begin(),
                              verified_pair_evidence_->transcript.end(),
                              record_key.begin());
                record_key[32] = static_cast<std::uint8_t>(
                    initial_plan_effect.persist_kind);
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(input, &buffer) ==
                    FLY_SESSION_V2_OK)
                {
                    const fly_session_bytes_v2 ns{
                        name_space.data(),
                        static_cast<std::uint32_t>(name_space.size()), 0};
                    const fly_session_bytes_v2 key{
                        record_key.data(),
                        static_cast<std::uint32_t>(record_key.size()), 0};
                    result = ports_.compare_replace_secure_store(
                        &initial_plan_effect.token, ns, key, 0, buffer, inbox_);
                }
                else
                {
                    result = FLY_SESSION_V2_OUT_OF_MEMORY;
                }
                fly_session_buffer_release_v2(buffer);
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initial_plan_active_ = false;
                initial_plan_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_initial_bearer = false;
        InitialBearerEffect initial_bearer_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initial_bearer_dispatch_pending_ && initial_bearer_ &&
                !shutdown_requested_)
            {
                const auto effect = initial_bearer_->poll_effect();
                if (effect)
                {
                    initial_bearer_dispatch_pending_ = false;
                    initial_bearer_effect = *effect;
                    initial_bearer_token_ = effect->token;
                    initial_bearer_expected_kind_ =
                        initial_bearer_payload_kind(effect->kind);
                    initial_bearer_active_ = true;
                    dispatch_initial_bearer = true;
                }
            }
        }
        if (dispatch_initial_bearer)
        {
            const fly_session_bytes_v2 salt{
                initial_bearer_effect.salt.data(),
                static_cast<std::uint32_t>(initial_bearer_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                initial_bearer_effect.info.data(),
                static_cast<std::uint32_t>(initial_bearer_effect.info.size()), 0};
            const fly_session_bytes_v2 nonce{
                initial_bearer_effect.nonce.data(),
                static_cast<std::uint32_t>(initial_bearer_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                initial_bearer_effect.aad.data(),
                static_cast<std::uint32_t>(initial_bearer_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                initial_bearer_effect.input.data(),
                static_cast<std::uint32_t>(initial_bearer_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            if (initial_bearer_effect.kind == InitialBearerEffectKind::DeriveKey)
            {
                result = ports_.hkdf(
                    &initial_bearer_effect.token, initial_bearer_effect.resource,
                    salt, info, 32, inbox_);
            }
            else if (initial_bearer_effect.kind ==
                         InitialBearerEffectKind::CreateBearer ||
                     initial_bearer_effect.kind ==
                         InitialBearerEffectKind::JoinBearer)
            {
                result = ports_.start_bearer(
                    initial_bearer_effect.kind ==
                        InitialBearerEffectKind::CreateBearer,
                    &initial_bearer_effect.token,
                    initial_bearer_effect.plan_hash.data(),
                    initial_bearer_effect.resource,
                    initial_bearer_effect.confirmation_budget, inbox_);
            }
            else if (initial_bearer_effect.kind ==
                     InitialBearerEffectKind::PrepareCredential)
            {
                const fly_session_bytes_v2 selected{
                    initial_bearer_effect.selected_plan.data(),
                    static_cast<std::uint32_t>(
                        initial_bearer_effect.selected_plan.size()), 0};
                result = ports_.prepare_bearer_credential(
                    &initial_bearer_effect.token,
                    initial_bearer_effect.plan_hash.data(), selected,
                    initial_bearer_effect.resource,
                    initial_bearer_effect.creator, input, inbox_);
            }
            else if (initial_bearer_effect.kind ==
                         InitialBearerEffectKind::AeadSeal ||
                     initial_bearer_effect.kind ==
                         InitialBearerEffectKind::AeadOpen)
            {
                result = ports_.aead(
                    initial_bearer_effect.kind ==
                        InitialBearerEffectKind::AeadSeal,
                    &initial_bearer_effect.token,
                    initial_bearer_effect.resource, nonce, aad, input, inbox_);
            }
            else
            {
                static constexpr std::array<std::uint8_t, 16> name_space{{
                    'f','l','y','n','e','s','-','n','e','a','r','b','y','-','v','2'}};
                std::array<std::uint8_t, 33> record_key{};
                if (verified_pair_evidence_)
                    std::copy(verified_pair_evidence_->transcript.begin(),
                              verified_pair_evidence_->transcript.end(),
                              record_key.begin());
                record_key[32] = static_cast<std::uint8_t>(
                    initial_bearer_effect.persist_kind);
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(input, &buffer) ==
                    FLY_SESSION_V2_OK)
                {
                    const fly_session_bytes_v2 ns{
                        name_space.data(),
                        static_cast<std::uint32_t>(name_space.size()), 0};
                    const fly_session_bytes_v2 key{
                        record_key.data(),
                        static_cast<std::uint32_t>(record_key.size()), 0};
                    result = ports_.compare_replace_secure_store(
                        &initial_bearer_effect.token, ns, key, 0, buffer, inbox_);
                }
                else result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initial_bearer_active_ = false;
                initial_bearer_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_endpoint_offer = false;
        EndpointOfferEffect endpoint_offer_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (endpoint_offer_dispatch_pending_ && endpoint_offer_ &&
                !shutdown_requested_)
            {
                const auto effect = endpoint_offer_->poll_effect();
                if (effect)
                {
                    endpoint_offer_dispatch_pending_ = false;
                    endpoint_offer_effect = *effect;
                    endpoint_offer_token_ = effect->token;
                    endpoint_offer_expected_kind_ =
                        endpoint_offer_payload_kind(effect->kind);
                    endpoint_offer_active_ = true;
                    dispatch_endpoint_offer = true;
                }
            }
        }
        if (dispatch_endpoint_offer)
        {
            const fly_session_bytes_v2 salt{
                endpoint_offer_effect.salt.data(),
                static_cast<std::uint32_t>(endpoint_offer_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                endpoint_offer_effect.info.data(),
                static_cast<std::uint32_t>(endpoint_offer_effect.info.size()), 0};
            const fly_session_bytes_v2 nonce{
                endpoint_offer_effect.nonce.data(),
                static_cast<std::uint32_t>(endpoint_offer_effect.nonce.size()), 0};
            const fly_session_bytes_v2 aad{
                endpoint_offer_effect.aad.data(),
                static_cast<std::uint32_t>(endpoint_offer_effect.aad.size()), 0};
            const fly_session_bytes_v2 input{
                endpoint_offer_effect.input.data(),
                static_cast<std::uint32_t>(endpoint_offer_effect.input.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            if (endpoint_offer_effect.kind == EndpointOfferEffectKind::DeriveKey)
                result = ports_.hkdf(
                    &endpoint_offer_effect.token, endpoint_offer_effect.resource,
                    salt, info, 32, inbox_);
            else if (endpoint_offer_effect.kind == EndpointOfferEffectKind::Random)
            {
                static constexpr std::array<std::uint8_t, 24> purpose{{
                    'f','l','y','n','e','s','-','e','n','d','p','o','i','n','t','-',
                    'n','o','n','c','e','-','v','1'}};
                const fly_session_bytes_v2 purpose_bytes{
                    purpose.data(), static_cast<std::uint32_t>(purpose.size()), 0};
                result = ports_.random(&endpoint_offer_effect.token,
                    endpoint_offer_effect.byte_count, purpose_bytes, inbox_);
            }
            else if (endpoint_offer_effect.kind ==
                     EndpointOfferEffectKind::ResolveEndpoint)
            {
                const fly_session_bytes_v2 listener_token{
                    endpoint_offer_effect.listener_token.data(),
                    static_cast<std::uint32_t>(
                        endpoint_offer_effect.listener_token.size()), 0};
                result = ports_.resolve_bearer_endpoint(
                    &endpoint_offer_effect.token, endpoint_offer_effect.resource,
                    listener_token, inbox_);
            }
            else if (endpoint_offer_effect.kind == EndpointOfferEffectKind::AeadSeal ||
                     endpoint_offer_effect.kind == EndpointOfferEffectKind::AeadOpen)
                result = ports_.aead(
                    endpoint_offer_effect.kind == EndpointOfferEffectKind::AeadSeal,
                    &endpoint_offer_effect.token, endpoint_offer_effect.resource,
                    nonce, aad, input, inbox_);
            else
            {
                static constexpr std::array<std::uint8_t, 16> name_space{{
                    'f','l','y','n','e','s','-','n','e','a','r','b','y','-','v','2'}};
                std::array<std::uint8_t, 33> record_key{};
                if (verified_pair_evidence_)
                    std::copy(verified_pair_evidence_->transcript.begin(),
                              verified_pair_evidence_->transcript.end(),
                              record_key.begin());
                record_key[32] = 0xfe;
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(input, &buffer) ==
                    FLY_SESSION_V2_OK)
                {
                    const fly_session_bytes_v2 ns{name_space.data(),
                        static_cast<std::uint32_t>(name_space.size()), 0};
                    const fly_session_bytes_v2 key{record_key.data(),
                        static_cast<std::uint32_t>(record_key.size()), 0};
                    result = ports_.compare_replace_secure_store(
                        &endpoint_offer_effect.token, ns, key, 0, buffer, inbox_);
                }
                else result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
            }
            if (result != FLY_SESSION_V2_ACCEPTED && result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                endpoint_offer_active_ = false;
                endpoint_offer_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_initial_quic_bind = false;
        InitialQuicBindEffect initial_quic_bind_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (initial_quic_bind_dispatch_pending_ && initial_quic_bind_ &&
                !shutdown_requested_)
            {
                const auto effect = initial_quic_bind_->poll_effect();
                if (effect)
                {
                    initial_quic_bind_dispatch_pending_ = false;
                    initial_quic_bind_effect = *effect;
                    initial_quic_bind_token_ = effect->token;
                    initial_quic_bind_expected_kind_ =
                        initial_quic_bind_payload_kind(*effect);
                    initial_quic_bind_active_ = true;
                    dispatch_initial_quic_bind = true;
                }
            }
        }
        if (dispatch_initial_quic_bind)
        {
            const fly_session_bytes_v2 endpoint{
                initial_quic_bind_effect.endpoint.data(),
                static_cast<std::uint32_t>(
                    initial_quic_bind_effect.endpoint.size()), 0};
            const fly_session_bytes_v2 salt{
                initial_quic_bind_effect.salt.data(),
                static_cast<std::uint32_t>(initial_quic_bind_effect.salt.size()), 0};
            const fly_session_bytes_v2 info{
                initial_quic_bind_effect.info.data(),
                static_cast<std::uint32_t>(initial_quic_bind_effect.info.size()), 0};
            const fly_session_bytes_v2 input{
                initial_quic_bind_effect.input.data(),
                static_cast<std::uint32_t>(initial_quic_bind_effect.input.size()), 0};
            const fly_session_bytes_v2 exporter_context{
                initial_quic_bind_effect.exporter_context.data(),
                static_cast<std::uint32_t>(
                    initial_quic_bind_effect.exporter_context.size()), 0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (initial_quic_bind_effect.kind)
            {
            case InitialQuicBindEffectKind::DeriveKey:
                result = ports_.hkdf(
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource, salt, info, 32, inbox_);
                break;
            case InitialQuicBindEffectKind::StartConnection:
                result = ports_.start_quic(
                    initial_quic_bind_effect.listener,
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource, endpoint,
                    initial_quic_bind_effect.tls_material,
                    &initial_quic_bind_effect.policy, inbox_);
                break;
            case InitialQuicBindEffectKind::InspectHandshake:
                result = ports_.inspect_quic(
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource, inbox_);
                break;
            case InitialQuicBindEffectKind::Exporter:
                result = ports_.quic_exporter(
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource, info, exporter_context,
                    32, inbox_);
                break;
            case InitialQuicBindEffectKind::OpenBindStream:
            case InitialQuicBindEffectKind::AcceptBindStream:
                result = ports_.open_quic_stream(
                    true, initial_quic_bind_effect.accept,
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource,
                    initial_quic_bind_effect.opener_role, 1, inbox_);
                break;
            case InitialQuicBindEffectKind::Hmac:
                result = ports_.hmac_sha256(
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource, input, inbox_);
                break;
            case InitialQuicBindEffectKind::Write:
            {
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(input, &buffer) ==
                    FLY_SESSION_V2_OK)
                    result = ports_.write_quic(
                        &initial_quic_bind_effect.token,
                        initial_quic_bind_effect.resource, buffer,
                        initial_quic_bind_effect.finish, inbox_);
                else
                    result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
                break;
            }
            case InitialQuicBindEffectKind::Read:
                result = ports_.grant_quic_read(
                    &initial_quic_bind_effect.token,
                    initial_quic_bind_effect.resource,
                    initial_quic_bind_effect.read_credit, inbox_);
                break;
            }
            if (result != FLY_SESSION_V2_ACCEPTED && result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                initial_quic_bind_active_ = false;
                initial_quic_bind_->cancel_pending();
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_session_signing = false;
        SessionSigningEffect session_signing_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (session_signing_dispatch_pending_ && session_signing_ &&
                !shutdown_requested_)
            {
                const auto effect = session_signing_->poll_effect();
                if (effect)
                {
                    session_signing_dispatch_pending_ = false;
                    session_signing_effect = *effect;
                    session_signing_token_ = effect->token;
                    session_signing_expected_kind_ =
                        session_signing_payload_kind(effect->kind);
                    session_signing_active_ = true;
                    dispatch_session_signing = true;
                }
            }
        }
        if (dispatch_session_signing)
        {
            const fly_session_bytes_v2 scope_binding{
                session_signing_effect.scope_binding.data(),
                static_cast<std::uint32_t>(
                    session_signing_effect.scope_binding.size()), 0};
            const fly_session_bytes_v2 domain{
                session_signing_effect.domain.data(),
                static_cast<std::uint32_t>(
                    session_signing_effect.domain.size()), 0};
            const fly_session_bytes_v2 name_space{
                session_signing_effect.name_space.data(),
                static_cast<std::uint32_t>(
                    session_signing_effect.name_space.size()), 0};
            const fly_session_bytes_v2 record_key{
                session_signing_effect.record_key.data(),
                static_cast<std::uint32_t>(
                    session_signing_effect.record_key.size()), 0};
            const fly_session_bytes_v2 value{
                session_signing_effect.value.data(),
                static_cast<std::uint32_t>(session_signing_effect.value.size()),
                0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (session_signing_effect.kind)
            {
            case SessionSigningEffectKind::GenerateKey:
                result = ports_.generate_key(
                    &session_signing_effect.token,
                    session_signing_effect.key_purpose, scope_binding, inbox_);
                break;
            case SessionSigningEffectKind::ReadPublicKey:
                result = ports_.public_key(
                    &session_signing_effect.token,
                    session_signing_effect.resource,
                    session_signing_effect.public_key_encoding, inbox_);
                break;
            case SessionSigningEffectKind::SignBinding:
                result = ports_.sign_prehashed(
                    &session_signing_effect.token,
                    session_signing_effect.resource,
                    session_signing_effect.key_purpose, domain,
                    session_signing_effect.digest.data(), inbox_);
                break;
            case SessionSigningEffectKind::PersistBinding:
            {
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(value, &buffer) ==
                    FLY_SESSION_V2_OK)
                    result = ports_.compare_replace_secure_store(
                        &session_signing_effect.token, name_space, record_key,
                        session_signing_effect.expected_revision, buffer,
                        inbox_);
                else result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
                break;
            }
            case SessionSigningEffectKind::PersistBindingObject:
            {
                fly_session_buffer_v2_t* buffer = nullptr;
                if (fly_session_buffer_create_copy_v2(value, &buffer) ==
                    FLY_SESSION_V2_OK)
                    result = ports_.put_immutable_object(
                        &session_signing_effect.token,
                        session_signing_effect.object_kind,
                        session_signing_effect.expected_hash.data(), buffer,
                        inbox_);
                else result = FLY_SESSION_V2_OUT_OF_MEMORY;
                fly_session_buffer_release_v2(buffer);
                break;
            }
            }
            if (result != FLY_SESSION_V2_ACCEPTED && result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                session_signing_active_ = false;
                cancel_pair_material_locked();
                release_pair_material_locked();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_bearer_probe = false;
        fly_session_op_token_v2 local_bearer_probe_token{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (bearer_probe_pending_ && !shutdown_requested_)
            {
                bearer_probe_pending_ = false;
                bearer_probe_token_ = make_link_operation_token_locked();
                local_bearer_probe_token = bearer_probe_token_;
                bearer_probe_active_ = true;
                dispatch_bearer_probe = true;
            }
        }
        if (dispatch_bearer_probe)
        {
            static constexpr std::array<std::uint8_t, 26> policy{{
                'f','l','y','n','e','s','-','b','e','a','r','e','r','-',
                'c','a','p','a','b','i','l','i','t','y','-','2'}};
            fly_session_clock_sample_v2 clock{};
            clock.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
            clock.abi_version = FLY_SESSION_ABI_VERSION_2;
            const fly_session_bytes_v2 policy_bytes{
                policy.data(), static_cast<std::uint32_t>(policy.size()), 0};
            const auto clock_result = ports_.read_clock(&clock);
            const auto result = clock_result == FLY_SESSION_V2_OK
                ? ports_.probe_bearer(
                      &local_bearer_probe_token, policy_bytes,
                      clock.continuous_ns + UINT64_C(5000000000), inbox_)
                : clock_result;
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                bearer_probe_active_ = false;
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        bool dispatch_pair_material = false;
        PairMaterialEffect material_effect{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pair_material_dispatch_pending_ && pair_material_ &&
                !shutdown_requested_)
            {
                const auto effect = pair_material_->poll_effect();
                if (effect)
                {
                    pair_material_dispatch_pending_ = false;
                    material_effect = *effect;
                    pair_material_token_ = effect->token;
                    pair_material_expected_kind_ =
                        pair_material_payload_kind(effect->kind);
                    pair_material_active_ = true;
                    dispatch_pair_material = true;
                }
            }
        }
        if (dispatch_pair_material)
        {
            const fly_session_bytes_v2 exact_bytes{
                material_effect.exact_bytes.data(),
                static_cast<std::uint32_t>(material_effect.exact_bytes.size()),
                0};
            fly_session_result_v2 result = FLY_SESSION_V2_INVALID_STATE;
            switch (material_effect.kind)
            {
            case PairMaterialEffectKind::GenerateKey:
                result = ports_.generate_key(
                    &material_effect.token, material_effect.key_purpose,
                    exact_bytes, inbox_);
                break;
            case PairMaterialEffectKind::ReadPublicKey:
                result = ports_.public_key(
                    &material_effect.token, material_effect.resource,
                    material_effect.public_key_encoding, inbox_);
                break;
            case PairMaterialEffectKind::CreateTlsMaterial:
                result = ports_.create_tls_material(
                    &material_effect.token, material_effect.resource,
                    exact_bytes, inbox_);
                break;
            case PairMaterialEffectKind::RandomBytes:
                result = ports_.random(
                    &material_effect.token, material_effect.byte_count,
                    exact_bytes, inbox_);
                break;
            }
            if (result != FLY_SESSION_V2_ACCEPTED &&
                result != FLY_SESSION_V2_OK)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pair_material_active_ = false;
                pair_material_->cancel_pending();
                discovery_disconnect_pending_ = discovery_connection_ != 0;
                publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            continue;
        }

        PendingAction pending{};
        bool applied = false;
        bool dispatch_discovery = false;
        bool dispatch_discovery_connect = false;
        bool dispatch_discovery_stop = false;
        bool advertise = false;
        fly_session_resource_handle_v2 selected_candidate = 0;
        std::uint64_t selected_candidate_generation = 0;
        std::uint32_t target_link_state = FLY_SESSION_LINK_IDLE_V2;
        fly_session_op_token_v2 operation_token{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!pending_events_.empty())
            {
                auto pending_event = std::move(pending_events_.front());
                pending_events_.erase(pending_events_.begin());
                const auto& event = pending_event.event;
                if (same_token(event.token, platform_watch_token_))
                {
                    if (event.terminal != 0)
                    {
                        platform_watch_terminal_ = true;
                        if (shutdown_requested_)
                            complete_shutdown_locked();
                    }
                    else if (event.event_kind ==
                                 FLY_SESSION_PORT_EVENT_PLATFORM_STATE_V2 &&
                             event.result == FLY_SESSION_V2_OK &&
                             event.payload_kind ==
                                 FLY_SESSION_PLATFORM_STATE_SNAPSHOT_V2 &&
                             event.payload_size ==
                                 FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE &&
                             current_view_->snapshot.engine_state ==
                                 FLY_SESSION_ENGINE_LOADING_V2)
                    {
                        fly_session_platform_state_event_v2 payload{};
                        std::memcpy(&payload, event.payload, sizeof(payload));
                        if (payload.struct_size ==
                                FLY_SESSION_PLATFORM_STATE_EVENT_V2_SIZE &&
                            payload.abi_version == FLY_SESSION_ABI_VERSION_2 &&
                            payload.state_revision != 0 &&
                            payload.foreground <= 1 &&
                            payload.network_ready <= 1 &&
                            payload.reserved_zero == 0 &&
                            payload.reserved_zero2 == 0)
                        {
                            publish_link_view_locked(FLY_SESSION_LINK_IDLE_V2);
                        }
                    }
                }
                else if (pair_context_random_active_ &&
                         same_token(event.token, pair_context_random_token_))
                {
                    pair_context_random_active_ = false;
                    if (shutdown_requested_)
                    {
                        complete_shutdown_locked();
                        continue;
                    }
                    ParsedProviderEvent parsed;
                    const auto parsed_result = parse_provider_event_v2(
                        event, pair_context_random_token_,
                        FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, parsed);
                    std::array<std::uint8_t, 48> random{};
                    std::uint64_t written = 0;
                    bool accepted = parsed_result == FLY_SESSION_V2_OK &&
                        event.result == FLY_SESSION_V2_OK &&
                        parsed.buffer != nullptr && parsed.value0 == random.size();
                    if (accepted)
                    {
                        const fly_session_write_bytes_v2 destination{
                            random.data(), random.size()};
                        accepted = fly_session_buffer_read_v2(
                            parsed.buffer, 0, destination, &written) ==
                                FLY_SESSION_V2_OK &&
                            written == random.size();
                    }
                    try
                    {
                        std::array<std::uint8_t, 80> body{};
                        if (accepted)
                        {
                            body[1] = 1;
                            body[9] = 2;
                            body[12] = 1;
                            std::copy_n(random.begin(), 16, body.begin() + 16);
                            std::copy_n(random.begin() + 16, 16,
                                        body.begin() + 32);
                            body[50] = 0xea;
                            body[51] = 0x60;
                            std::copy_n(random.begin() + 32, 16,
                                        body.begin() + 64);
                            wire::PairContextV1 context{};
                            accepted = wire::decode_pair_context_v1(
                                body.data(), body.size(), &context) ==
                                wire::Status::Ok;
                            if (accepted)
                            {
                                auto exchange =
                                    std::make_unique<wire::PairExchangeV1>(
                                        wire::validate_p256_uncompressed_point_callback,
                                        nullptr);
                                accepted = exchange->begin(
                                    body.data(), body.size()) ==
                                    wire::PairExchangeResultV1::Accepted;
                                if (accepted)
                                {
                                    std::vector<std::uint8_t> logical;
                                    accepted = wire::encode_gatt_logical_message(
                                        static_cast<std::uint8_t>(
                                            wire::GattLogicalType::PairContext),
                                        body.data(), body.size(), &logical) ==
                                        wire::GattFragmentResult::Accepted;
                                    std::vector<std::vector<std::uint8_t>> fragments;
                                    accepted = accepted &&
                                        wire::fragment_gatt_logical(
                                            logical.data(), logical.size(),
                                            static_cast<std::uint8_t>(
                                                wire::GattLogicalType::PairContext),
                                            next_gatt_message_id_,
                                            discovery_att_value_cap_,
                                            &fragments) ==
                                            wire::GattFragmentResult::Accepted &&
                                        !fragments.empty();
                                    if (accepted)
                                    {
                                        pair_context_ = context;
                                        pair_exchange_ = std::move(exchange);
                                        ++next_gatt_message_id_;
                                        if (next_gatt_message_id_ == 0)
                                            next_gatt_message_id_ = 1;
                                        gatt_write_fragments_ =
                                            std::move(fragments);
                                        gatt_write_index_ = 0;
                                        gatt_write_logical_type_ =
                                            static_cast<std::uint8_t>(
                                                wire::GattLogicalType::PairContext);
                                        gatt_write_pending_ = true;
                                        bearer_probe_pending_ = true;
                                    }
                                }
                            }
                        }
                    }
                    catch (const std::bad_alloc&)
                    {
                        accepted = false;
                    }
                    if (!accepted)
                    {
                        pair_context_.reset();
                        pair_exchange_.reset();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (bearer_probe_active_ &&
                         same_token(event.token, bearer_probe_token_))
                {
                    bearer_probe_active_ = false;
                    if (shutdown_requested_)
                    {
                        complete_shutdown_locked();
                        continue;
                    }
                    ParsedProviderEvent parsed;
                    const auto parsed_result = parse_provider_event_v2(
                        event, bearer_probe_token_,
                        FLY_SESSION_PROVIDER_BEARER_CAPABILITIES_V2, parsed);
                    std::array<std::uint8_t, 512> capability{};
                    std::uint64_t written = 0;
                    bool accepted = parsed_result == FLY_SESSION_V2_OK &&
                        event.result == FLY_SESSION_V2_OK &&
                        parsed.buffer != nullptr && parsed.value0 == capability.size();
                    if (accepted)
                    {
                        const fly_session_write_bytes_v2 destination{
                            capability.data(), capability.size()};
                        accepted = fly_session_buffer_read_v2(
                            parsed.buffer, 0, destination, &written) ==
                                FLY_SESSION_V2_OK &&
                            written == capability.size() &&
                            wire::validate_pair_capability(
                                capability.data(), capability.size()) ==
                                wire::Status::Ok;
                    }
                    if (accepted && pair_context_ &&
                        next_operation_id_ <=
                            (std::numeric_limits<std::uint64_t>::max)() - 8)
                    {
                        local_pair_capability_ = capability;
                        PairMaterialStartV1 start{};
                        std::copy(authorization_->engine_instance_id.begin(),
                                  authorization_->engine_instance_id.end(),
                                  start.engine_instance_id.begin());
                        start.link_id[0] =
                            static_cast<std::uint8_t>(link_generation_);
                        start.generation = link_generation_;
                        start.first_operation_id = next_operation_id_;
                        next_operation_id_ += 8;
                        start.context = *pair_context_;
                        start.role = local_pair_role_;
                        start.capability_summary_hash = wire::domain_hash(
                            "flynes-pair-capability-summary-v1",
                            capability.data(), capability.size());
                        pair_material_ =
                            std::make_unique<PairMaterialScheduler>();
                        accepted = pair_material_->begin(start);
                        pair_material_dispatch_pending_ = accepted;
                    }
                    else
                    {
                        accepted = false;
                    }
                    if (!accepted)
                    {
                        local_pair_capability_.reset();
                        pair_material_.reset();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (gatt_write_active_ &&
                         same_token(event.token, gatt_write_token_))
                {
                    gatt_write_active_ = false;
                    if (shutdown_requested_)
                    {
                        gatt_write_pending_ = false;
                        gatt_write_fragments_.clear();
                        gatt_write_index_ = 0;
                        gatt_write_logical_type_ = 0;
                        complete_shutdown_locked();
                        continue;
                    }
                    if (event.result == FLY_SESSION_V2_OK &&
                        event.payload_kind ==
                            FLY_SESSION_PROVIDER_DISCOVERY_END_V2)
                    {
                        ++gatt_write_index_;
                        if (gatt_write_index_ >= gatt_write_fragments_.size())
                        {
                            const auto completed_type =
                                gatt_write_logical_type_;
                            gatt_write_pending_ = false;
                            gatt_write_index_ = 0;
                            gatt_write_logical_type_ = 0;
                            gatt_write_fragments_.clear();
                            if (completed_type == static_cast<std::uint8_t>(
                                    wire::GattLogicalType::PairReveal) &&
                                (!pair_reveal_ ||
                                 pair_reveal_->mark_local_reveal_sent() !=
                                     FLY_SESSION_V2_OK ||
                                 (pair_reveal_->ready() &&
                                  !start_pair_signature_locked())))
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PairSignature) &&
                                     (!pair_signature_ ||
                                      pair_signature_->mark_local_signature_sent() !=
                                          FLY_SESSION_V2_OK ||
                                      (pair_signature_->ready() &&
                                       !start_pair_sas_locked())))
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if ((completed_type == static_cast<std::uint8_t>(
                                          wire::GattLogicalType::PairKnownStatus) ||
                                      completed_type == static_cast<std::uint8_t>(
                                          wire::GattLogicalType::PairKnownBranch)) &&
                                     (!pair_known_ ||
                                      pair_known_->mark_local_sent(
                                          pending_local_known_hash_) !=
                                          FLY_SESSION_V2_OK))
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if ((completed_type == static_cast<std::uint8_t>(
                                          wire::GattLogicalType::PairKnownStatus) ||
                                      completed_type == static_cast<std::uint8_t>(
                                          wire::GattLogicalType::PairKnownBranch)) &&
                                     pair_known_ && pair_known_->ready())
                            {
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_AUTHENTICATING_V2);
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::KeyConfirm) &&
                                     (!pair_key_confirm_ ||
                                      pair_key_confirm_->mark_local_sent() !=
                                          FLY_SESSION_V2_OK))
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PairCapabilityReveal) &&
                                     (!pair_capability_ ||
                                      pair_capability_->mark_local_sent(
                                          pending_local_capability_hash_) !=
                                          FLY_SESSION_V2_OK))
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PairCapabilityReveal) &&
                                     pair_capability_ &&
                                     pair_capability_->ready() &&
                                     !finish_pair_capability_locked())
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if (completed_type >= static_cast<std::uint8_t>(
                                         wire::GattLogicalType::InitialBearerPlan) &&
                                     completed_type <= static_cast<std::uint8_t>(
                                         wire::GattLogicalType::InitialBearerPlanFinal))
                            {
                                bool accepted = initial_plan_ &&
                                    initial_plan_->mark_local_sent() ==
                                        FLY_SESSION_V2_OK;
                                if (accepted)
                                {
                                    next_operation_id_ = (std::max)(
                                        next_operation_id_,
                                        initial_plan_->next_operation_id());
                                    initial_plan_dispatch_pending_ =
                                        initial_plan_->poll_effect().has_value();
                                    if (initial_plan_->mutually_locked() &&
                                        !initial_bearer_)
                                        accepted = start_initial_bearer_locked();
                                }
                                if (!accepted)
                                {
                                    cancel_pair_material_locked();
                                    release_pair_material_locked();
                                    discovery_disconnect_pending_ =
                                        discovery_connection_ != 0;
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_FAILED_V2);
                                }
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::InitialBearerCredential))
                            {
                                const bool accepted = initial_bearer_ &&
                                    initial_bearer_->mark_local_sent() ==
                                        FLY_SESSION_V2_OK;
                                if (accepted)
                                {
                                    next_operation_id_ = (std::max)(
                                        next_operation_id_,
                                        initial_bearer_->next_operation_id());
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_CONNECTING_V2);
                                    if (!start_endpoint_offer_locked())
                                    {
                                        cancel_pair_material_locked();
                                        release_pair_material_locked();
                                        discovery_disconnect_pending_ =
                                            discovery_connection_ != 0;
                                        publish_link_view_locked(
                                            FLY_SESSION_LINK_FAILED_V2);
                                    }
                                }
                                else
                                {
                                    cancel_pair_material_locked();
                                    release_pair_material_locked();
                                    discovery_disconnect_pending_ =
                                        discovery_connection_ != 0;
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_FAILED_V2);
                                }
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::BearerEndpointOffer))
                            {
                                bool accepted = endpoint_offer_ &&
                                    endpoint_offer_->mark_local_sent() ==
                                        FLY_SESSION_V2_OK;
                                if (accepted)
                                {
                                    next_operation_id_ = (std::max)(
                                        next_operation_id_,
                                        endpoint_offer_->next_operation_id());
                                    accepted = start_initial_quic_bind_locked();
                                }
                                if (!accepted)
                                {
                                    cancel_pair_material_locked();
                                    release_pair_material_locked();
                                    discovery_disconnect_pending_ =
                                        discovery_connection_ != 0;
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_FAILED_V2);
                                }
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::KeyConfirm) &&
                                     pair_key_confirm_ &&
                                     pair_key_confirm_->ready())
                            {
                                next_operation_id_ = (std::max)(
                                    next_operation_id_,
                                    pair_key_confirm_->next_operation_id());
                                if (start_pair_capability_locked())
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_PROVISIONING_V2);
                                else
                                {
                                    cancel_pair_material_locked();
                                    release_pair_material_locked();
                                    discovery_disconnect_pending_ =
                                        discovery_connection_ != 0;
                                    publish_link_view_locked(
                                        FLY_SESSION_LINK_FAILED_V2);
                                }
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PairCommit) &&
                                     pair_reveal_ &&
                                     pair_reveal_->local_reveal_ready() &&
                                     !queue_local_pair_reveal_locked())
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else if (completed_type == static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PairContext) &&
                                     local_pair_role_ ==
                                         wire::PairRoleV1::Initiator &&
                                     pair_material_ && pair_material_->ready() &&
                                     !queue_local_pair_commit_locked())
                            {
                                cancel_pair_material_locked();
                                release_pair_material_locked();
                                discovery_disconnect_pending_ =
                                    discovery_connection_ != 0;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                        }
                    }
                    else
                    {
                        gatt_write_pending_ = false;
                        gatt_write_fragments_.clear();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_reveal_active_ && pair_reveal_ &&
                         same_token(event.token, pair_reveal_token_))
                {
                    const bool peer_was_verified =
                        pair_reveal_->peer_reveal_verified();
                    pair_reveal_active_ = false;
                    const auto result = pair_reveal_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_reveal_dispatch_pending_ = false;
                        release_pair_reveal_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted && !peer_was_verified &&
                        pair_reveal_->peer_reveal_verified())
                    {
                        const auto peer = pair_reveal_->peer_contribution();
                        const auto peer_role =
                            local_pair_role_ == wire::PairRoleV1::Initiator
                                ? wire::PairRoleV1::Responder
                                : wire::PairRoleV1::Initiator;
                        const auto expected_exchange_result =
                            local_pair_role_ == wire::PairRoleV1::Initiator
                                ? wire::PairExchangeResultV1::Ready
                                : wire::PairExchangeResultV1::Accepted;
                        accepted = peer && pair_exchange_ &&
                            pair_exchange_->accept_reveal(
                                peer_role, peer->bytes.data(),
                                peer->bytes.size(),
                                pair_reveal_->peer_logical_hash()) ==
                                expected_exchange_result;
                    }
                    if (accepted && pair_reveal_->poll_effect())
                        pair_reveal_dispatch_pending_ = true;
                    if (accepted && pair_reveal_->local_reveal_ready() &&
                        !pair_reveal_->local_reveal_sent() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_pair_reveal_locked();
                    if (accepted && pair_reveal_->ready() &&
                        !pair_signature_)
                        accepted = start_pair_signature_locked();
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_signature_active_ && pair_signature_ &&
                         same_token(event.token, pair_signature_token_))
                {
                    pair_signature_active_ = false;
                    const auto result = pair_signature_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_signature_dispatch_pending_ = false;
                        release_pair_signature_locked();
                        cancel_pair_reveal_locked();
                        release_pair_reveal_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted && pair_signature_->poll_effect())
                        pair_signature_dispatch_pending_ = true;
                    if (accepted && pair_signature_->local_envelope_ready() &&
                        !pair_signature_->local_signature_sent() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_pair_signature_locked();
                    if (accepted && pair_signature_->ready() && !pair_sas_)
                        accepted = start_pair_sas_locked();
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_sas_active_ && pair_sas_ &&
                         same_token(event.token, pair_sas_token_))
                {
                    pair_sas_active_ = false;
                    const auto result = pair_sas_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_sas_dispatch_pending_ = false;
                        release_pair_sas_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    if (result == FLY_SESSION_V2_OK && !pair_sas_->ready())
                    {
                        pair_sas_dispatch_pending_ = true;
                    }
                    else if (result == FLY_SESSION_V2_OK)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_, pair_sas_->next_operation_id());
                        if (!start_pair_known_locked())
                        {
                            release_pair_sas_locked();
                            cancel_pair_material_locked();
                            release_pair_material_locked();
                            discovery_disconnect_pending_ =
                                discovery_connection_ != 0;
                            publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                        }
                    }
                    else
                    {
                        release_pair_sas_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_known_active_ && pair_known_ &&
                         same_token(event.token, pair_known_token_))
                {
                    pair_known_active_ = false;
                    const auto result = pair_known_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_known_dispatch_pending_ = false;
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                        next_operation_id_ = (std::max)(
                            next_operation_id_, pair_known_->next_operation_id());
                    if (accepted && pair_known_->poll_effect())
                        pair_known_dispatch_pending_ = true;
                    if (accepted && pair_known_->local_envelope_ready() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_pair_known_locked();
                    if (accepted && pair_known_->ready())
                        publish_link_view_locked(
                            FLY_SESSION_LINK_AUTHENTICATING_V2);
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_key_confirm_active_ && pair_key_confirm_ &&
                         same_token(event.token, pair_key_confirm_token_))
                {
                    pair_key_confirm_active_ = false;
                    const auto result = pair_key_confirm_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_key_confirm_dispatch_pending_ = false;
                        cancel_pair_key_confirm_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            pair_key_confirm_->next_operation_id());
                    if (accepted && pair_key_confirm_->poll_effect())
                        pair_key_confirm_dispatch_pending_ = true;
                    if (accepted &&
                        pair_key_confirm_->local_envelope_ready() &&
                        !pair_key_confirm_->local_sent() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_pair_key_confirm_locked();
                    if (accepted && pair_key_confirm_->ready())
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            pair_key_confirm_->next_operation_id());
                        accepted = start_pair_capability_locked();
                        if (accepted)
                            publish_link_view_locked(
                                FLY_SESSION_LINK_PROVISIONING_V2);
                    }
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_capability_active_ && pair_capability_ &&
                         same_token(event.token, pair_capability_token_))
                {
                    pair_capability_active_ = false;
                    const auto result = pair_capability_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_capability_dispatch_pending_ = false;
                        cancel_pair_capability_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            pair_capability_->next_operation_id());
                    if (accepted && pair_capability_->poll_effect())
                        pair_capability_dispatch_pending_ = true;
                    if (accepted && pair_capability_->local_envelope_ready() &&
                        !pair_capability_->local_sent() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_pair_capability_locked();
                    if (accepted && pair_capability_->ready())
                        accepted = finish_pair_capability_locked();
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (initial_plan_active_ && initial_plan_ &&
                         same_token(event.token, initial_plan_token_))
                {
                    initial_plan_active_ = false;
                    const auto result = initial_plan_->complete(event);
                    if (shutdown_requested_)
                    {
                        initial_plan_dispatch_pending_ = false;
                        cancel_initial_plan_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            initial_plan_->next_operation_id());
                        initial_plan_dispatch_pending_ =
                            initial_plan_->poll_effect().has_value();
                    }
                    if (accepted && initial_plan_->local_logical() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_initial_plan_locked();
                    if (accepted && initial_plan_->mutually_locked() &&
                        !initial_bearer_)
                        accepted = start_initial_bearer_locked();
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (initial_bearer_active_ && initial_bearer_ &&
                         same_token(event.token, initial_bearer_token_))
                {
                    initial_bearer_active_ = false;
                    const auto result = initial_bearer_->complete(event);
                    if (shutdown_requested_)
                    {
                        initial_bearer_dispatch_pending_ = false;
                        cancel_initial_bearer_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            initial_bearer_->next_operation_id());
                        initial_bearer_dispatch_pending_ =
                            initial_bearer_->poll_effect().has_value();
                    }
                    if (accepted && initial_bearer_->local_logical() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_initial_bearer_locked();
                    if (accepted && initial_bearer_->ready())
                    {
                        publish_link_view_locked(FLY_SESSION_LINK_CONNECTING_V2);
                        accepted = start_endpoint_offer_locked();
                    }
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (endpoint_offer_active_ && endpoint_offer_ &&
                         same_token(event.token, endpoint_offer_token_))
                {
                    endpoint_offer_active_ = false;
                    const auto result = endpoint_offer_->complete(event);
                    if (shutdown_requested_)
                    {
                        endpoint_offer_dispatch_pending_ = false;
                        cancel_endpoint_offer_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            endpoint_offer_->next_operation_id());
                        endpoint_offer_dispatch_pending_ =
                            endpoint_offer_->poll_effect().has_value();
                    }
                    if (accepted && endpoint_offer_->local_logical() &&
                        !gatt_write_active_ && !gatt_write_pending_)
                        accepted = queue_local_endpoint_offer_locked();
                    if (accepted && endpoint_offer_->ready() &&
                        !initial_quic_bind_)
                        accepted = start_initial_quic_bind_locked();
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ = discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (initial_quic_bind_active_ && initial_quic_bind_ &&
                         same_token(event.token, initial_quic_bind_token_))
                {
                    initial_quic_bind_active_ = false;
                    const auto result = initial_quic_bind_->complete(event);
                    if (shutdown_requested_)
                    {
                        initial_quic_bind_dispatch_pending_ = false;
                        cancel_initial_quic_bind_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            initial_quic_bind_->next_operation_id());
                        initial_quic_bind_dispatch_pending_ =
                            initial_quic_bind_->poll_effect().has_value();
                    }
                    if (accepted && initial_quic_bind_->channel_bound())
                    {
                        // ChannelBind releases the BLE/Bonjour bootstrap, but
                        // CONNECTED_LOBBY remains gated on the subsequent
                        // bidirectional HELLO/LINK_READY exchange.
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_CONNECTING_V2);
                        if (!session_signing_)
                            accepted = start_session_signing_locked();
                    }
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ = discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (session_signing_active_ && session_signing_ &&
                         same_token(event.token, session_signing_token_))
                {
                    session_signing_active_ = false;
                    const auto result = session_signing_->complete(event);
                    if (shutdown_requested_)
                    {
                        session_signing_dispatch_pending_ = false;
                        cancel_session_signing_locked();
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    bool accepted = result == FLY_SESSION_V2_OK;
                    if (accepted)
                    {
                        next_operation_id_ = (std::max)(
                            next_operation_id_,
                            session_signing_->next_operation_id());
                        session_signing_dispatch_pending_ =
                            session_signing_->poll_effect().has_value();
                    }
                    if (accepted && session_signing_->ready())
                    {
                        // The exact local binding is durable. LINK_HELLO is the
                        // next protocol gate and will carry these same bytes.
                        publish_link_view_locked(FLY_SESSION_LINK_CONNECTING_V2);
                    }
                    if (!accepted)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ = discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (pair_material_active_ && pair_material_ &&
                         same_token(event.token, pair_material_token_))
                {
                    pair_material_active_ = false;
                    const auto result = pair_material_->complete(event);
                    if (shutdown_requested_)
                    {
                        pair_material_dispatch_pending_ = false;
                        release_pair_material_locked();
                        complete_shutdown_locked();
                        continue;
                    }
                    if (result == FLY_SESSION_V2_OK &&
                        !pair_material_->ready())
                    {
                        pair_material_dispatch_pending_ = true;
                    }
                    else if (result == FLY_SESSION_V2_OK &&
                             pair_exchange_ &&
                             ((local_pair_role_ ==
                                   wire::PairRoleV1::Responder &&
                               pair_exchange_->initiator_commit_received()) ||
                              local_pair_role_ ==
                                  wire::PairRoleV1::Initiator) &&
                             !gatt_write_active_ && !gatt_write_pending_ &&
                             !queue_local_pair_commit_locked())
                    {
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                    else if (result != FLY_SESSION_V2_OK)
                    {
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (discovery_active_ &&
                         same_token(event.token, discovery_token_))
                {
                    if (event.payload_kind ==
                            FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2)
                    {
                        ParsedProviderEvent parsed;
                        const auto result = parse_provider_event_v2(
                            event, discovery_token_,
                            FLY_SESSION_PROVIDER_DISCOVERY_CANDIDATE_V2,
                            parsed);
                        if (!shutdown_requested_ && result == FLY_SESSION_V2_OK &&
                            parsed.generation == link_generation_)
                        {
                            const auto existing = std::find_if(
                                candidates_.begin(), candidates_.end(),
                                [&parsed](const fly_session_candidate_v2& item) {
                                    return item.candidate == parsed.resource;
                                });
                            fly_session_candidate_v2 item{};
                            item.struct_size = FLY_SESSION_CANDIDATE_V2_SIZE;
                            item.abi_version = FLY_SESSION_ABI_VERSION_2;
                            item.candidate = parsed.resource;
                            item.discovery_generation = parsed.generation;
                            item.last_observed_continuous_ns = parsed.value1;
                            item.rssi_bucket = static_cast<std::int32_t>(parsed.value0);
                            item.available_action_mask =
                                UINT64_C(1) << FLY_SESSION_ACTION_JOIN_CANDIDATE_V2;
                            if (existing == candidates_.end())
                                candidates_.push_back(item);
                            else
                                *existing = item;
                            publish_link_view_locked(
                                current_view_->snapshot.link_state);
                        }
                    }
                    else if (event.payload_kind ==
                             FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2)
                    {
                        ParsedProviderEvent parsed;
                        const auto result = parse_provider_event_v2(
                            event, discovery_token_,
                            FLY_SESSION_PROVIDER_DISCOVERY_CONNECTION_V2,
                            parsed);
                        discovery_active_ = false;
                        candidates_.clear();
                        const bool valid_connection =
                            result == FLY_SESSION_V2_OK &&
                            parsed.generation == link_generation_ &&
                            (parsed.value0 ==
                                 FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2 ||
                             parsed.value0 ==
                                 FLY_SESSION_DISCOVERY_PHYSICAL_PERIPHERAL_V2) &&
                            parsed.value1 >= 23 && parsed.value1 <= 517;
                        if (shutdown_requested_ && valid_connection)
                        {
                            discovery_connection_ = parsed.resource;
                            discovery_physical_role_ =
                                static_cast<std::uint32_t>(parsed.value0);
                            discovery_disconnect_pending_ = true;
                        }
                        else if (shutdown_requested_)
                            complete_shutdown_locked();
                        else if (valid_connection)
                        {
                            discovery_connection_ = parsed.resource;
                            pair_context_.reset();
                            discovery_physical_role_ =
                                static_cast<std::uint32_t>(parsed.value0);
                            discovery_att_value_cap_ =
                                static_cast<std::size_t>(parsed.value1 - 3);
                            if (!ports_.has_pairing_stack())
                            {
                                discovery_disconnect_pending_ = true;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_FAILED_V2);
                            }
                            else
                            {
                                const auto direction = discovery_physical_role_ ==
                                        FLY_SESSION_DISCOVERY_PHYSICAL_CENTRAL_V2
                                    ? wire::GattPhysicalDirection::PeripheralToCentral
                                    : wire::GattPhysicalDirection::CentralToPeripheral;
                                gatt_reassembler_ =
                                    std::make_unique<wire::GattReassembler>(
                                        link_generation_, direction);
                                gatt_subscribe_pending_ = true;
                                pair_context_random_pending_ =
                                    local_pair_role_ ==
                                    wire::PairRoleV1::Initiator;
                                publish_link_view_locked(
                                    FLY_SESSION_LINK_AUTHENTICATING_V2);
                            }
                        }
                        else
                            publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                    else if (event.payload_kind ==
                                 FLY_SESSION_PROVIDER_DISCOVERY_END_V2)
                    {
                        discovery_active_ = false;
                        candidates_.clear();
                        if (shutdown_requested_)
                            complete_shutdown_locked();
                        else if (event.result == FLY_SESSION_V2_OK ||
                                 event.result == FLY_SESSION_V2_CANCELLED)
                            publish_link_view_locked(FLY_SESSION_LINK_IDLE_V2);
                        else
                            publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (gatt_subscription_active_ &&
                         same_token(event.token, gatt_subscription_token_))
                {
                    if (event.payload_kind ==
                            FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2)
                    {
                        ParsedProviderEvent parsed;
                        const auto result = parse_provider_event_v2(
                            event, gatt_subscription_token_,
                            FLY_SESSION_PROVIDER_DISCOVERY_BYTES_V2, parsed);
                        bool accepted = result == FLY_SESSION_V2_OK &&
                            parsed.generation == link_generation_ &&
                            parsed.value0 > wire::kGattPhysicalHeaderSize &&
                            parsed.value0 <= 244 && gatt_reassembler_;
                        std::array<std::uint8_t, 244> bytes{};
                        std::uint64_t written = 0;
                        if (accepted)
                        {
                            fly_session_write_bytes_v2 destination{
                                bytes.data(), parsed.value0};
                            accepted = fly_session_buffer_read_v2(
                                parsed.buffer, 0, destination, &written) ==
                                    FLY_SESSION_V2_OK &&
                                written == parsed.value0;
                        }
                        fly_session_clock_sample_v2 clock{};
                        clock.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
                        clock.abi_version = FLY_SESSION_ABI_VERSION_2;
                        if (accepted)
                            accepted = ports_.read_clock(&clock) ==
                                FLY_SESSION_V2_OK;
                        std::vector<std::uint8_t> logical;
                        wire::GattCompletedMetadata completed{};
                        if (accepted)
                        {
                            const auto fragment_result = gatt_reassembler_->accept(
                                link_generation_, clock.continuous_ns,
                                bytes.data(), static_cast<std::size_t>(written),
                                &logical, &completed);
                            accepted = fragment_result ==
                                           wire::GattFragmentResult::Accepted ||
                                       fragment_result ==
                                           wire::GattFragmentResult::Duplicate ||
                                       fragment_result ==
                                           wire::GattFragmentResult::Complete;
                            if (accepted && fragment_result ==
                                                wire::GattFragmentResult::Complete)
                            {
                                accepted = accept_completed_gatt_locked(logical);
                                if (accepted && completed.logical_type !=
                                        static_cast<std::uint8_t>(
                                            wire::GattLogicalType::PhysicalAck))
                                {
                                    if (gatt_write_pending_ && !gatt_write_active_ &&
                                        gatt_write_logical_type_ !=
                                            static_cast<std::uint8_t>(
                                                wire::GattLogicalType::PhysicalAck) &&
                                        deferred_gatt_write_fragments_.empty())
                                    {
                                        deferred_gatt_write_fragments_ =
                                            std::move(gatt_write_fragments_);
                                        deferred_gatt_write_logical_type_ =
                                            gatt_write_logical_type_;
                                        gatt_write_pending_ = false;
                                        gatt_write_index_ = 0;
                                        gatt_write_logical_type_ = 0;
                                    }
                                    accepted = queue_gatt_ack_locked(completed);
                                }
                            }
                            else if (accepted && fragment_result ==
                                                     wire::GattFragmentResult::Duplicate &&
                                     completed.message_id != 0 &&
                                     completed.logical_type != static_cast<std::uint8_t>(
                                         wire::GattLogicalType::PhysicalAck))
                                accepted = queue_gatt_ack_locked(completed);
                        }
                        if (!accepted)
                        {
                            cancel_pair_material_locked();
                            release_pair_material_locked();
                            discovery_disconnect_pending_ =
                                discovery_connection_ != 0;
                            publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                        }
                    }
                    else
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        gatt_subscription_active_ = false;
                        gatt_reassembler_.reset();
                        pair_context_.reset();
                        if (shutdown_requested_)
                            discovery_disconnect_pending_ =
                                discovery_connection_ != 0;
                        else
                        {
                            discovery_disconnect_pending_ =
                                discovery_connection_ != 0;
                            publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                        }
                    }
                }
                else if (discovery_disconnect_active_ &&
                         same_token(event.token, discovery_disconnect_token_))
                {
                    discovery_disconnect_active_ = false;
                    gatt_subscription_active_ = false;
                    gatt_reassembler_.reset();
                    pair_context_.reset();
                    discovery_connection_ = 0;
                    complete_shutdown_locked();
                }
                continue;
            }
            if (pending_actions_.empty())
            {
                worker_scheduled_ = false;
                return;
            }
            pending = pending_actions_.front();
            pending_actions_.erase(pending_actions_.begin());

            const auto action_kind = pending.token->action_kind;
            applied = approval_token_matches(pending.token, authorization_);
            if (applied && action_kind == FLY_SESSION_ACTION_CREATE_INVITE_V2)
            {
                applied = pending.choice_size == 0 &&
                          current_view_->snapshot.link_state ==
                              FLY_SESSION_LINK_IDLE_V2 &&
                          ports_.has_discovery();
                if (applied)
                {
                    local_pair_role_ = wire::PairRoleV1::Initiator;
                    dispatch_discovery = true;
                    advertise = true;
                    target_link_state = FLY_SESSION_LINK_INVITING_V2;
                }
            }
            else if (applied && action_kind == FLY_SESSION_ACTION_JOIN_CODE_V2)
            {
                applied = pending.choice_size == FLY_SESSION_ACTION_CHOICE_V2_SIZE &&
                          pending.choice.struct_size ==
                              FLY_SESSION_ACTION_CHOICE_V2_SIZE &&
                          pending.choice.abi_version == FLY_SESSION_ABI_VERSION_2 &&
                          pending.choice.choice_kind ==
                              FLY_SESSION_CHOICE_INVITE_CODE_V2 &&
                          pending.choice.reserved_zero == 0 &&
                          current_view_->snapshot.link_state ==
                              FLY_SESSION_LINK_IDLE_V2 &&
                          ports_.has_discovery();
                for (std::size_t index = 0; applied && index < 6; ++index)
                {
                    const auto byte = pending.choice.choice_id[index];
                    applied = byte >= static_cast<std::uint8_t>('0') &&
                              byte <= static_cast<std::uint8_t>('9');
                }
                for (std::size_t index = 6; applied && index < 16; ++index)
                    applied = pending.choice.choice_id[index] == 0;
                if (applied)
                {
                    local_pair_role_ = wire::PairRoleV1::Responder;
                    dispatch_discovery = true;
                    advertise = false;
                    target_link_state = FLY_SESSION_LINK_JOINING_V2;
                }
            }
            else if (applied &&
                     action_kind == FLY_SESSION_ACTION_START_DISCOVERY_V2)
            {
                applied = pending.choice_size == 0 &&
                          current_view_->snapshot.link_state ==
                              FLY_SESSION_LINK_IDLE_V2 &&
                          ports_.has_discovery();
                if (applied)
                {
                    candidates_.clear();
                    dispatch_discovery = true;
                    advertise = false;
                    target_link_state = FLY_SESSION_LINK_DISCOVERING_V2;
                }
            }
            else if (applied &&
                     action_kind == FLY_SESSION_ACTION_JOIN_CANDIDATE_V2)
            {
                applied = pending.choice_size ==
                              FLY_SESSION_ACTION_CHOICE_V2_SIZE &&
                          pending.choice.struct_size ==
                              FLY_SESSION_ACTION_CHOICE_V2_SIZE &&
                          pending.choice.abi_version ==
                              FLY_SESSION_ABI_VERSION_2 &&
                          pending.choice.choice_kind ==
                              FLY_SESSION_CHOICE_REFERENCE_V2 &&
                          pending.choice.reserved_zero == 0 &&
                          current_view_->snapshot.link_state ==
                              FLY_SESSION_LINK_DISCOVERING_V2 &&
                          discovery_active_ && ports_.has_discovery();
                for (const auto byte : pending.choice.choice_id)
                    applied = applied && byte == 0;
                const auto selected = std::find_if(
                    candidates_.begin(), candidates_.end(),
                    [&pending](const fly_session_candidate_v2& item) {
                        return item.candidate == pending.choice.value;
                    });
                applied = applied && selected != candidates_.end() &&
                          selected->discovery_generation == link_generation_;
                if (applied)
                {
                    selected_candidate = selected->candidate;
                    selected_candidate_generation =
                        selected->discovery_generation;
                    candidates_.clear();
                    local_pair_role_ = wire::PairRoleV1::Responder;
                    dispatch_discovery_connect = true;
                    target_link_state = FLY_SESSION_LINK_JOINING_V2;
                }
            }
            else if (applied &&
                     (action_kind == FLY_SESSION_ACTION_CONFIRM_SAS_V2 ||
                      action_kind == FLY_SESSION_ACTION_REJECT_SAS_V2))
            {
                applied = pending.choice_size == 0 && pair_sas_ &&
                          pair_sas_->ready() && pair_signature_ &&
                          pair_known_ && pair_known_->ready() &&
                          !pair_known_->known_path() &&
                          pair_signature_->ready() &&
                          !pair_signature_->local_approved() &&
                          current_view_->snapshot.link_state ==
                              FLY_SESSION_LINK_AUTHENTICATING_V2;
                if (applied &&
                    action_kind == FLY_SESSION_ACTION_CONFIRM_SAS_V2)
                {
                    const bool approved = pair_signature_->approve_local(
                        FLY_SESSION_APPROVAL_BLE_SAS_MATCH_V2);
                    applied = approved && start_pair_key_confirm_locked();
                    if (applied)
                        publish_link_view_locked(
                            FLY_SESSION_LINK_AUTHENTICATING_V2);
                    else if (approved)
                    {
                        cancel_pair_material_locked();
                        release_pair_material_locked();
                        discovery_disconnect_pending_ =
                            discovery_connection_ != 0;
                        publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                    }
                }
                else if (applied)
                {
                    cancel_pair_material_locked();
                    release_pair_material_locked();
                    discovery_disconnect_pending_ =
                        discovery_connection_ != 0;
                    publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
                }
            }
            else if (applied &&
                     (action_kind == FLY_SESSION_ACTION_CANCEL_INVITE_V2 ||
                      action_kind == FLY_SESSION_ACTION_CANCEL_JOIN_V2 ||
                      action_kind == FLY_SESSION_ACTION_STOP_DISCOVERY_V2))
            {
                const std::uint32_t expected_state =
                    action_kind == FLY_SESSION_ACTION_CANCEL_INVITE_V2
                        ? static_cast<std::uint32_t>(FLY_SESSION_LINK_INVITING_V2)
                        : action_kind == FLY_SESSION_ACTION_CANCEL_JOIN_V2
                              ? static_cast<std::uint32_t>(FLY_SESSION_LINK_JOINING_V2)
                              : static_cast<std::uint32_t>(FLY_SESSION_LINK_DISCOVERING_V2);
                applied = pending.choice_size == 0 && discovery_active_ &&
                          current_view_->snapshot.link_state == expected_state;
                if (applied)
                {
                    candidates_.clear();
                    dispatch_discovery_stop = true;
                    operation_token = discovery_token_;
                }
            }
            else if (applied &&
                     action_kind != FLY_SESSION_ACTION_CANCEL_LOADING_V2)
            {
                applied = false;
            }
            if (applied && (dispatch_discovery || dispatch_discovery_connect))
            {
                operation_token = make_link_operation_token_locked();
                discovery_token_ = operation_token;
                discovery_active_ = true;
            }
        }

        if (applied && dispatch_discovery)
        {
            fly_session_clock_sample_v2 sample{};
            sample.struct_size = FLY_SESSION_CLOCK_SAMPLE_V2_SIZE;
            sample.abi_version = FLY_SESSION_ABI_VERSION_2;
            static constexpr std::array<std::uint8_t, 16> policy{{
                0x66, 0x6c, 0x79, 0x6e, 0x65, 0x73, 0x2d, 0x6e,
                0x65, 0x61, 0x72, 0x62, 0x79, 0x2d, 0x76, 0x32}};
            const fly_session_bytes_v2 policy_bytes{
                policy.data(), static_cast<std::uint32_t>(policy.size()), 0};
            const auto clock_result = ports_.read_clock(&sample);
            const auto start_result = clock_result == FLY_SESSION_V2_OK
                ? ports_.start_discovery(
                      advertise, &operation_token, policy_bytes,
                      sample.continuous_ns + UINT64_C(60000000000), inbox_)
                : clock_result;
            applied = start_result == FLY_SESSION_V2_ACCEPTED ||
                      start_result == FLY_SESSION_V2_OK;
        }
        else if (applied && dispatch_discovery_connect)
        {
            const auto connect_result = ports_.connect_discovery(
                &operation_token, selected_candidate,
                selected_candidate_generation, inbox_);
            applied = connect_result == FLY_SESSION_V2_ACCEPTED ||
                      connect_result == FLY_SESSION_V2_OK;
        }
        else if (applied && dispatch_discovery_stop)
        {
            const auto stop_result = ports_.stop_discovery(&operation_token);
            applied = stop_result == FLY_SESSION_V2_ACCEPTED ||
                      stop_result == FLY_SESSION_V2_OK ||
                      stop_result == FLY_SESSION_V2_CANCELLED ||
                      stop_result == FLY_SESSION_V2_DUPLICATE;
            if (applied && stop_result != FLY_SESSION_V2_ACCEPTED)
            {
                std::lock_guard<std::mutex> lock(mutex_);
                discovery_active_ = false;
            }
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (dispatch_discovery || dispatch_discovery_connect)
            {
                if (applied)
                    publish_link_view_locked(target_link_state);
                else if (dispatch_discovery)
                    discovery_active_ = false;
                else
                    publish_link_view_locked(FLY_SESSION_LINK_FAILED_V2);
            }
            else if (dispatch_discovery_stop && applied && !discovery_active_)
            {
                publish_link_view_locked(FLY_SESSION_LINK_IDLE_V2);
            }
            fly_session_notice_v2 notice{};
            notice.struct_size = FLY_SESSION_NOTICE_V2_SIZE;
            notice.abi_version = FLY_SESSION_ABI_VERSION_2;
            notice.notice_sequence = next_notice_sequence_++;
            notice.request_id = pending.request_id;
            notice.kind = FLY_SESSION_NOTICE_ACTION_RESULT_V2;
            notice.outcome = applied ? FLY_SESSION_ACTION_APPLIED_V2
                                     : FLY_SESSION_ACTION_REJECTED_V2;
            notice.result = applied ? FLY_SESSION_V2_OK
                                    : FLY_SESSION_V2_INVALID_STATE;
            notice.view_revision = current_view_->snapshot.view_revision;
            notices_.push_back(notice);
        }
        fly_session_approval_token_release_v2(pending.token);
    }
}

fly_session_result_v2 SessionEngine::read_notice(
    fly_session_notice_v2& out_notice)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (notices_.empty())
    {
        return FLY_SESSION_V2_EMPTY;
    }
    out_notice = notices_.front();
    notices_.erase(notices_.begin());
    --reserved_results_;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionEngine::acquire_view(
    fly_session_view_v2_t** out_view)
{
    std::lock_guard<std::mutex> lock(mutex_);
    session_view_retain(current_view_);
    *out_view = current_view_;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 SessionEngine::begin_shutdown(std::uint64_t request_id)
{
    if (request_id == 0)
    {
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }

    fly_session_view_v2_t* old_view = nullptr;
    bool stop_required = false;
    bool stop_discovery_required = false;
    bool disconnect_required = false;
    fly_session_op_token_v2 discovery_token{};
    fly_session_op_token_v2 disconnect_token{};
    fly_session_resource_handle_v2 disconnect_connection = 0;
    try
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (shutdown_requested_ || shutdown_complete_)
        {
            return FLY_SESSION_V2_DUPLICATE;
        }
        auto* next_view = make_session_view(
            authorization_,
            current_view_->snapshot.view_revision + 1,
            FLY_SESSION_ENGINE_SHUTTING_DOWN_V2,
            false);
        try
        {
            shutdown_complete_view_ = make_session_view(
                authorization_,
                current_view_->snapshot.view_revision + 2,
                FLY_SESSION_ENGINE_SHUTDOWN_COMPLETE_V2,
                false);
        }
        catch (...)
        {
            fly_session_view_release_v2(next_view);
            throw;
        }
        authorization_->generation.fetch_add(1, std::memory_order_acq_rel);
        old_view = current_view_;
        current_view_ = next_view;
        shutdown_requested_ = true;
        pair_context_random_pending_ = false;
        if (pair_context_random_active_)
        {
            const auto cancel_result =
                ports_.cancel_crypto(&pair_context_random_token_);
            if (cancel_result == FLY_SESSION_V2_OK ||
                cancel_result == FLY_SESSION_V2_CANCELLED ||
                cancel_result == FLY_SESSION_V2_DUPLICATE)
                pair_context_random_active_ = false;
        }
        bearer_probe_pending_ = false;
        if (bearer_probe_active_)
        {
            const auto cancel_result =
                ports_.cancel_bearer(&bearer_probe_token_);
            if (cancel_result == FLY_SESSION_V2_OK ||
                cancel_result == FLY_SESSION_V2_CANCELLED ||
                cancel_result == FLY_SESSION_V2_DUPLICATE)
                bearer_probe_active_ = false;
        }
        cancel_pair_material_locked();
        release_pair_material_locked();
        gatt_write_pending_ = false;
        gatt_write_fragments_.clear();
        gatt_write_index_ = 0;
        gatt_write_logical_type_ = 0;
        pending_gatt_acks_.clear();
        deferred_gatt_write_fragments_.clear();
        deferred_gatt_write_logical_type_ = 0;
        stop_required = !platform_watch_terminal_;
        stop_discovery_required = discovery_active_;
        discovery_token = discovery_token_;
        disconnect_required = discovery_connection_ != 0 &&
            (gatt_subscribe_pending_ || gatt_subscription_active_);
        if (disconnect_required)
        {
            gatt_subscribe_pending_ = false;
            discovery_disconnect_token_ = make_link_operation_token_locked();
            disconnect_token = discovery_disconnect_token_;
            disconnect_connection = discovery_connection_;
            discovery_disconnect_active_ = true;
        }
        if (!stop_required && !stop_discovery_required && !disconnect_required)
        {
            complete_shutdown_locked();
        }
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    fly_session_view_release_v2(old_view);
    if (stop_required)
    {
        const auto stop_result = ports_.stop_platform_state(&platform_watch_token_);
        if (stop_result == FLY_SESSION_V2_OK ||
            stop_result == FLY_SESSION_V2_CANCELLED ||
            stop_result == FLY_SESSION_V2_DUPLICATE)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            platform_watch_terminal_ = true;
            complete_shutdown_locked();
        }
    }
    if (stop_discovery_required)
    {
        const auto stop_result = ports_.stop_discovery(&discovery_token);
        if (stop_result == FLY_SESSION_V2_OK ||
            stop_result == FLY_SESSION_V2_CANCELLED ||
            stop_result == FLY_SESSION_V2_DUPLICATE)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            discovery_active_ = false;
            complete_shutdown_locked();
        }
    }
    if (disconnect_required)
    {
        const auto result = ports_.disconnect_discovery(
            &disconnect_token, disconnect_connection, link_generation_, inbox_);
        if (result == FLY_SESSION_V2_OK || result == FLY_SESSION_V2_CANCELLED ||
            result == FLY_SESSION_V2_DUPLICATE)
        {
            std::lock_guard<std::mutex> lock(mutex_);
            discovery_disconnect_active_ = false;
            gatt_subscription_active_ = false;
            gatt_reassembler_.reset();
            pair_context_.reset();
            discovery_connection_ = 0;
            complete_shutdown_locked();
        }
    }
    return FLY_SESSION_V2_ACCEPTED;
}

void SessionEngine::complete_shutdown_locked() noexcept
{
    if (shutdown_complete_ || !shutdown_complete_view_ ||
        !platform_watch_terminal_ || discovery_active_ ||
        gatt_subscribe_pending_ || gatt_subscription_active_ ||
        discovery_disconnect_pending_ || discovery_disconnect_active_ ||
        pair_context_random_pending_ || pair_context_random_active_ ||
        bearer_probe_pending_ || bearer_probe_active_ ||
        pair_material_dispatch_pending_ || pair_material_active_ ||
        pair_reveal_dispatch_pending_ || pair_reveal_active_ ||
        pair_signature_dispatch_pending_ || pair_signature_active_ ||
        pair_known_dispatch_pending_ || pair_known_active_ ||
        pair_sas_dispatch_pending_ || pair_sas_active_ ||
        pair_key_confirm_dispatch_pending_ || pair_key_confirm_active_ ||
        pair_capability_dispatch_pending_ || pair_capability_active_ ||
        initial_plan_dispatch_pending_ || initial_plan_active_ ||
        initial_bearer_dispatch_pending_ || initial_bearer_active_ ||
        endpoint_offer_dispatch_pending_ || endpoint_offer_active_ ||
        initial_quic_bind_dispatch_pending_ || initial_quic_bind_active_ ||
        session_signing_dispatch_pending_ || session_signing_active_ ||
        gatt_write_pending_ || gatt_write_active_)
    {
        return;
    }
    auto* old_view = current_view_;
    current_view_ = shutdown_complete_view_;
    shutdown_complete_view_ = nullptr;
    shutdown_complete_ = true;
    if (inbox_)
    {
        inbox_->closed.store(true, std::memory_order_release);
    }
    fly_session_view_release_v2(old_view);
}

bool SessionEngine::can_destroy() const noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    return shutdown_complete_ && !worker_scheduled_ &&
           pending_actions_.empty() && pending_events_.empty();
}

void SessionEngine::detach_handle() noexcept
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (!handle_detached_)
    {
        handle_detached_ = true;
        authorization_->generation.fetch_add(1, std::memory_order_acq_rel);
        if (inbox_)
        {
            inbox_->closed.store(true, std::memory_order_release);
        }
    }
}

} // namespace flynes::session

extern "C" void fly_session_task_run_v2(fly_session_task_v2_t* task)
{
    if (!task)
    {
        return;
    }
    auto engine = std::move(task->engine);
    delete task;
    if (engine)
    {
        engine->run_work();
    }
}

extern "C" void fly_session_task_release_v2(fly_session_task_v2_t* task)
{
    delete task;
}

extern "C" void fly_session_inbox_retain_v2(fly_session_inbox_v2_t* inbox)
{
    if (inbox)
    {
        inbox->references.fetch_add(1, std::memory_order_relaxed);
    }
}

extern "C" void fly_session_inbox_release_v2(fly_session_inbox_v2_t* inbox)
{
    if (inbox && inbox->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
    {
        delete inbox;
    }
}
