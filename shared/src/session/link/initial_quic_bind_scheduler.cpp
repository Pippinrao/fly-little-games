#include "initial_quic_bind_scheduler.hpp"

#include "../ports/provider_events.hpp"
#include "../wire/p256_point.hpp"
#include "../wire/quic_contract.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {
namespace {

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes != nullptr &&
           std::any_of(bytes, bytes + size,
                       [](std::uint8_t value) { return value != 0; });
}

bool valid_role(wire::PairRoleV1 role) noexcept
{
    return role == wire::PairRoleV1::Initiator ||
           role == wire::PairRoleV1::Responder;
}

void append_u32be(std::vector<std::uint8_t>* bytes, std::uint32_t value)
{
    bytes->push_back(static_cast<std::uint8_t>(value >> 24));
    bytes->push_back(static_cast<std::uint8_t>(value >> 16));
    bytes->push_back(static_cast<std::uint8_t>(value >> 8));
    bytes->push_back(static_cast<std::uint8_t>(value));
}

bool read_u32be(const std::uint8_t* bytes, std::uint32_t* value) noexcept
{
    if (bytes == nullptr || value == nullptr) return false;
    *value = (static_cast<std::uint32_t>(bytes[0]) << 24) |
             (static_cast<std::uint32_t>(bytes[1]) << 16) |
             (static_cast<std::uint32_t>(bytes[2]) << 8) |
             static_cast<std::uint32_t>(bytes[3]);
    return true;
}

} // namespace

fly_session_op_token_v2 InitialQuicBindScheduler::token(
    std::uint64_t id) const noexcept
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
    value.operation_id = id;
    return value;
}

bool InitialQuicBindScheduler::begin(const InitialQuicBindStartV1& start)
{
    if (begun_ || failed_ || endpoint_ == nullptr || !endpoint_->ready() ||
        start.generation == 0 || start.first_operation_id == 0 ||
        start.first_operation_id >
            (std::numeric_limits<std::uint64_t>::max)() - 32 ||
        !valid_role(start.local_role) ||
        !nonzero(start.engine_instance_id.data(), start.engine_instance_id.size()) ||
        !nonzero(start.link_id.data(), start.link_id.size()) ||
        !nonzero(start.transcript_hash.data(), start.transcript_hash.size()) ||
        !nonzero(start.session_id.data(), start.session_id.size()) ||
        !nonzero(start.listener_spki_hash.data(), start.listener_spki_hash.size()) ||
        start.ecdh_secret == 0 || start.bearer_path == 0 ||
        start.selected_plan[2] < 1 || start.selected_plan[2] > 2 ||
        endpoint_->listener() !=
            (start.selected_plan[2] == static_cast<std::uint8_t>(start.local_role)) ||
        !wire::validate_p256_uncompressed_point(start.local_identity_public.data()) ||
        !wire::validate_p256_uncompressed_point(start.peer_identity_public.data())) {
        return false;
    }
    local_listener_ = start.selected_plan[2] ==
        static_cast<std::uint8_t>(start.local_role);
    if (local_listener_ && start.tls_material == 0) return false;
    start_ = start;
    next_operation_id_ = start.first_operation_id;
    begun_ = true;
    return derive(true) == FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialQuicBindScheduler::issue(
    InitialQuicBindEffect effect, Stage stage)
{
    if (pending_ || effect.token.operation_id == 0 ||
        effect.expected_payload_kind == 0) {
        return reject(FLY_SESSION_V2_INVALID_STATE);
    }
    pending_ = std::move(effect);
    stage_ = stage;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InitialQuicBindScheduler::derive(bool i2r)
{
    try {
        static constexpr char kSaltDomain[] = "flynes-pair-v1";
        static constexpr char kI2r[] = "flynes-pair-quic-bind-i2r-v1";
        static constexpr char kR2i[] = "flynes-pair-quic-bind-r2i-v1";
        std::array<std::uint8_t, sizeof(kSaltDomain) - 1 + 32> salt_input{};
        std::copy_n(reinterpret_cast<const std::uint8_t*>(kSaltDomain),
                    sizeof(kSaltDomain) - 1, salt_input.begin());
        std::copy(start_.transcript_hash.begin(), start_.transcript_hash.end(),
                  salt_input.begin() + sizeof(kSaltDomain) - 1);
        const auto salt = wire::sha256(salt_input.data(), salt_input.size());
        const auto* label = i2r ? kI2r : kR2i;
        const auto label_size = i2r ? sizeof(kI2r) - 1 : sizeof(kR2i) - 1;
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::DeriveKey;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2;
        effect.resource = start_.ecdh_secret;
        effect.salt.assign(salt.begin(), salt.end());
        effect.info.assign(reinterpret_cast<const std::uint8_t*>(label),
                           reinterpret_cast<const std::uint8_t*>(label) +
                               label_size);
        return issue(std::move(effect), i2r ? Stage::DeriveI2r
                                            : Stage::DeriveR2i);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::start_connection()
{
    try {
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::StartConnection;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2;
        effect.resource = start_.bearer_path;
        effect.tls_material = local_listener_ ? start_.tls_material : 0;
        effect.listener = local_listener_;
        effect.endpoint = endpoint_->endpoint();
        effect.policy.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
        effect.policy.abi_version = FLY_SESSION_ABI_VERSION_2;
        effect.policy.require_full_tls13 = 1;
        effect.policy.forbid_resumption = 1;
        effect.policy.forbid_zero_rtt = 1;
        static constexpr char kAlpn[] = "flynes-nearby/2";
        std::copy_n(kAlpn, sizeof(kAlpn), effect.policy.alpn);
        std::copy(start_.listener_spki_hash.begin(),
                  start_.listener_spki_hash.end(),
                  effect.policy.expected_der_spki_hash);
        return issue(std::move(effect), Stage::StartConnection);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::inspect_handshake()
{
    InitialQuicBindEffect effect{};
    effect.kind = InitialQuicBindEffectKind::InspectHandshake;
    effect.token = token(next_operation_id_++);
    effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2;
    effect.resource = resources_.connection;
    return issue(std::move(effect), Stage::InspectHandshake);
}

fly_session_result_v2 InitialQuicBindScheduler::request_exporter()
{
    try {
        const auto request = wire::make_channel_exporter_request_v1(
            start_.transcript_hash, start_.session_id, {});
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::Exporter;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2;
        effect.resource = resources_.connection;
        effect.info.assign(request.label.begin(), request.label.end());
        effect.exporter_context.assign(request.context.begin(),
                                       request.context.end());
        return issue(std::move(effect), Stage::Exporter);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::open_or_accept_stream()
{
    InitialQuicBindEffect effect{};
    effect.kind = local_listener_ ? InitialQuicBindEffectKind::AcceptBindStream
                                  : InitialQuicBindEffectKind::OpenBindStream;
    effect.token = token(next_operation_id_++);
    effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_STREAM_V2;
    effect.resource = resources_.connection;
    effect.accept = local_listener_;
    effect.opener_role = local_listener_
        ? static_cast<std::uint32_t>(start_.local_role == wire::PairRoleV1::Initiator
                                         ? wire::PairRoleV1::Responder
                                         : wire::PairRoleV1::Initiator)
        : static_cast<std::uint32_t>(start_.local_role);
    return issue(std::move(effect), local_listener_ ? Stage::AcceptStream
                                                    : Stage::OpenStream);
}

fly_session_result_v2 InitialQuicBindScheduler::make_local_proof()
{
    try {
        std::array<std::uint8_t, wire::kChannelBindProofBodySizeV1> body{};
        if (wire::encode_channel_bind_proof_body_v1(
                bind_, start_.local_role, start_.local_identity_public,
                start_.peer_identity_public, &body) != wire::Status::Ok ||
            wire::build_channel_bind_proof_hmac_input_v1(
                body.data(), body.size(), exporter_, &pending_hmac_body_) !=
                wire::Status::Ok) {
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        }
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        effect.resource = start_.local_role == wire::PairRoleV1::Initiator
                              ? resources_.i2r_key
                              : resources_.r2i_key;
        effect.input = pending_hmac_body_;
        return issue(std::move(effect), Stage::MakeLocalProof);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::verify_peer_proof()
{
    try {
        const auto peer_role = start_.local_role == wire::PairRoleV1::Initiator
                                   ? wire::PairRoleV1::Responder
                                   : wire::PairRoleV1::Initiator;
        const auto& proof = local_listener_ ? connector_proof_ : listener_proof_;
        if (wire::decode_channel_bind_proof_v1(
                proof.data(), proof.size(), bind_, peer_role,
                start_.peer_identity_public, start_.local_identity_public,
                &expected_peer_tag_) != wire::Status::Ok ||
            wire::build_channel_bind_proof_hmac_input_v1(
                proof.data(), wire::kChannelBindProofBodySizeV1, exporter_,
                &pending_hmac_body_) != wire::Status::Ok) {
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        }
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        effect.resource = peer_role == wire::PairRoleV1::Initiator
                              ? resources_.i2r_key
                              : resources_.r2i_key;
        effect.input = pending_hmac_body_;
        return issue(std::move(effect), local_listener_
                                           ? Stage::VerifyConnectorProof
                                           : Stage::VerifyListenerProof);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::make_ack()
{
    try {
        std::array<std::uint8_t, wire::kChannelBindAckBodySizeV1> body{};
        if (wire::encode_channel_bind_ack_body_v1(
                channel_id_, wire::channel_bind_proof_hash_v1(connector_proof_),
                wire::channel_bind_proof_hash_v1(listener_proof_),
                start_.local_role, &body) != wire::Status::Ok ||
            wire::build_channel_bind_ack_hmac_input_v1(
                body.data(), body.size(), exporter_, &pending_hmac_body_) !=
                wire::Status::Ok) {
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        }
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        effect.resource = start_.local_role == wire::PairRoleV1::Initiator
                              ? resources_.i2r_key
                              : resources_.r2i_key;
        effect.input = pending_hmac_body_;
        return issue(std::move(effect), Stage::MakeAck);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::verify_ack()
{
    try {
        const auto connector_role = start_.local_role == wire::PairRoleV1::Initiator
                                        ? wire::PairRoleV1::Responder
                                        : wire::PairRoleV1::Initiator;
        if (wire::decode_channel_bind_ack_v1(
                ack_.data(), ack_.size(), channel_id_,
                wire::channel_bind_proof_hash_v1(connector_proof_),
                wire::channel_bind_proof_hash_v1(listener_proof_),
                connector_role, &expected_peer_tag_) != wire::Status::Ok ||
            wire::build_channel_bind_ack_hmac_input_v1(
                ack_.data(), wire::kChannelBindAckBodySizeV1, exporter_,
                &pending_hmac_body_) != wire::Status::Ok) {
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        }
        InitialQuicBindEffect effect{};
        effect.kind = InitialQuicBindEffectKind::Hmac;
        effect.token = token(next_operation_id_++);
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CRYPTO_MAC_V2;
        effect.resource = connector_role == wire::PairRoleV1::Initiator
                              ? resources_.i2r_key
                              : resources_.r2i_key;
        effect.input = pending_hmac_body_;
        return issue(std::move(effect), Stage::VerifyAck);
    } catch (const std::bad_alloc&) {
        return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
    }
}

fly_session_result_v2 InitialQuicBindScheduler::write_bytes(
    std::vector<std::uint8_t> bytes, bool finish, Stage stage)
{
    InitialQuicBindEffect effect{};
    effect.kind = InitialQuicBindEffectKind::Write;
    effect.token = token(next_operation_id_++);
    effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_END_V2;
    effect.resource = resources_.send_stream;
    effect.finish = finish;
    effect.input = std::move(bytes);
    return issue(std::move(effect), stage);
}

fly_session_result_v2 InitialQuicBindScheduler::read_bytes(
    std::uint64_t maximum, Stage stage)
{
    InitialQuicBindEffect effect{};
    effect.kind = InitialQuicBindEffectKind::Read;
    effect.token = token(next_operation_id_++);
    effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
    effect.resource = resources_.receive_stream;
    effect.read_credit = maximum;
    return issue(std::move(effect), stage);
}

fly_session_result_v2 InitialQuicBindScheduler::read_buffer(
    fly_session_buffer_v2_t* buffer, std::vector<std::uint8_t>* out) const
{
    if (buffer == nullptr || out == nullptr) return FLY_SESSION_V2_CONTRACT_VIOLATION;
    std::uint64_t size = 0;
    if (fly_session_buffer_size_v2(buffer, &size) != FLY_SESSION_V2_OK ||
        size > 4096) return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try { out->assign(static_cast<std::size_t>(size), 0); }
    catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    std::uint64_t written = 0;
    const fly_session_write_bytes_v2 destination{out->data(), out->size()};
    return fly_session_buffer_read_v2(buffer, 0, destination, &written) ==
                       FLY_SESSION_V2_OK &&
                   written == out->size()
               ? FLY_SESSION_V2_OK
               : FLY_SESSION_V2_CONTRACT_VIOLATION;
}

fly_session_result_v2 InitialQuicBindScheduler::complete(
    const fly_session_port_event_v2& event)
{
    if (!pending_ || failed_) return FLY_SESSION_V2_INVALID_STATE;
    const auto completed = stage_;
    ParsedProviderEvent parsed;
    const auto parse = parse_provider_event_v2(
        event, pending_->token, pending_->expected_payload_kind, parsed);
    if (parse != FLY_SESSION_V2_OK) return parse;
    if (event.result != FLY_SESSION_V2_OK) return reject(event.result);
    pending_.reset();

    if (completed == Stage::DeriveI2r || completed == Stage::DeriveR2i) {
        if (parsed.resource == 0) return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (completed == Stage::DeriveI2r) {
            resources_.i2r_key = parsed.resource;
            return derive(false);
        }
        resources_.r2i_key = parsed.resource;
        return start_connection();
    }
    if (completed == Stage::StartConnection) {
        resources_.connection = parsed.resource;
        return inspect_handshake();
    }
    if (completed == Stage::InspectHandshake) {
        std::vector<std::uint8_t> bytes;
        const auto read = read_buffer(parsed.buffer, &bytes);
        if (read != FLY_SESSION_V2_OK) return reject(read);
        if (bytes.size() != wire::kQuicHandshakeFactsWireSizeV2 ||
            wire::sha256(bytes.data(), bytes.size()) != parsed.hash) {
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        }
        wire::QuicHandshakeFactsV2 facts{};
        if (wire::decode_quic_handshake_facts_v2(
                bytes.data(), bytes.size(), &facts) != wire::Status::Ok) {
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        }
        fly_session_quic_connect_policy_v2 policy{};
        policy.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
        policy.abi_version = FLY_SESSION_ABI_VERSION_2;
        policy.require_full_tls13 = 1;
        policy.forbid_resumption = 1;
        policy.forbid_zero_rtt = 1;
        static constexpr char kAlpn[] = "flynes-nearby/2";
        std::copy_n(kAlpn, sizeof(kAlpn), policy.alpn);
        std::copy(start_.listener_spki_hash.begin(),
                  start_.listener_spki_hash.end(),
                  policy.expected_der_spki_hash);
        const auto verified = local_listener_
                                  ? wire::verify_quic_listener_handshake_v2(facts)
                                  : wire::verify_quic_handshake_v2(policy, facts);
        if (verified != wire::Status::Ok)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        return request_exporter();
    }

    std::vector<std::uint8_t> bytes;
    if (parsed.buffer != nullptr) {
        const auto read = read_buffer(parsed.buffer, &bytes);
        if (read != FLY_SESSION_V2_OK) return reject(read);
    }
    if (completed == Stage::Exporter) {
        if (bytes.size() != exporter_.size() ||
            !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::copy(bytes.begin(), bytes.end(), exporter_.begin());
        channel_id_ = wire::derive_channel_id_v1(
            start_.transcript_hash, start_.session_id, {}, exporter_);
        if (wire::encode_initial_channel_bind_v1(
                start_.transcript_hash, start_.session_id, channel_id_,
                &bind_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        return open_or_accept_stream();
    }
    if (completed == Stage::OpenStream || completed == Stage::AcceptStream) {
        if (parsed.resource == 0 || parsed.value0 == 0)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        resources_.send_stream = parsed.resource;
        resources_.receive_stream = parsed.value0;
        if (completed == Stage::OpenStream) return make_local_proof();
        read_accumulator_.clear();
        expected_read_size_ = 8 + 4 + wire::kChannelBindProofSizeV1;
        return read_bytes(expected_read_size_, Stage::ReadConnectorProof);
    }
    if (completed == Stage::MakeLocalProof) {
        if (bytes.size() != 32 || !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, 32> tag{};
        std::copy(bytes.begin(), bytes.end(), tag.begin());
        std::array<std::uint8_t, wire::kChannelBindProofBodySizeV1> body{};
        std::copy_n(pending_hmac_body_.data() + 32,
                    body.size(), body.begin());
        auto& proof = local_listener_ ? listener_proof_ : connector_proof_;
        if (wire::encode_channel_bind_proof_v1(body, tag, &proof) !=
            wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::vector<std::uint8_t> record;
        if (!local_listener_) {
            const auto preamble = wire::bind_stream_preamble_v1();
            record.insert(record.end(), preamble.begin(), preamble.end());
        }
        append_u32be(&record, wire::kChannelBindProofSizeV1);
        record.insert(record.end(), proof.begin(), proof.end());
        return write_bytes(std::move(record), false,
                           local_listener_ ? Stage::WriteListenerProof
                                           : Stage::WriteConnectorProof);
    }
    if (completed == Stage::WriteConnectorProof) {
        read_accumulator_.clear();
        expected_read_size_ = 4 + wire::kChannelBindProofSizeV1;
        return read_bytes(expected_read_size_, Stage::ReadListenerProof);
    }
    if (completed == Stage::WriteListenerProof) {
        read_accumulator_.clear();
        expected_read_size_ = 4 + wire::kChannelBindAckSizeV1;
        return read_bytes(expected_read_size_, Stage::ReadAck);
    }
    if (completed == Stage::ReadConnectorProof ||
        completed == Stage::ReadListenerProof || completed == Stage::ReadAck) {
        if (bytes.empty() || read_accumulator_.size() + bytes.size() >
                                 expected_read_size_)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        try { read_accumulator_.insert(read_accumulator_.end(),
                                       bytes.begin(), bytes.end()); }
        catch (const std::bad_alloc&) {
            return reject(FLY_SESSION_V2_OUT_OF_MEMORY);
        }
        if (read_accumulator_.size() < expected_read_size_)
            return read_bytes(expected_read_size_ - read_accumulator_.size(),
                              completed);
        std::size_t offset = 0;
        if (completed == Stage::ReadConnectorProof) {
            const auto preamble = wire::bind_stream_preamble_v1();
            if (!std::equal(preamble.begin(), preamble.end(),
                            read_accumulator_.begin()))
                return reject(FLY_SESSION_V2_AUTH_FAILED);
            offset = preamble.size();
        }
        std::uint32_t record_size = 0;
        if (!read_u32be(read_accumulator_.data() + offset, &record_size))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        offset += 4;
        if (completed == Stage::ReadAck) {
            if (record_size != wire::kChannelBindAckSizeV1)
                return reject(FLY_SESSION_V2_AUTH_FAILED);
            std::copy_n(read_accumulator_.begin() + offset, ack_.size(),
                        ack_.begin());
            return verify_ack();
        }
        if (record_size != wire::kChannelBindProofSizeV1)
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        auto& proof = completed == Stage::ReadConnectorProof
                          ? connector_proof_ : listener_proof_;
        std::copy_n(read_accumulator_.begin() + offset, proof.size(),
                    proof.begin());
        return verify_peer_proof();
    }
    if (completed == Stage::VerifyConnectorProof ||
        completed == Stage::VerifyListenerProof || completed == Stage::VerifyAck) {
        if (bytes.size() != expected_peer_tag_.size() ||
            !std::equal(bytes.begin(), bytes.end(), expected_peer_tag_.begin()))
            return reject(FLY_SESSION_V2_AUTH_FAILED);
        if (completed == Stage::VerifyConnectorProof) return make_local_proof();
        if (completed == Stage::VerifyListenerProof) return make_ack();
        read_accumulator_.clear();
        expected_read_size_ = 0;
        return read_bytes(1, Stage::ReadConnectorFin);
    }
    if (completed == Stage::MakeAck) {
        if (bytes.size() != 32 || !nonzero(bytes.data(), bytes.size()))
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::array<std::uint8_t, 32> tag{};
        std::copy(bytes.begin(), bytes.end(), tag.begin());
        std::array<std::uint8_t, wire::kChannelBindAckBodySizeV1> body{};
        std::copy_n(pending_hmac_body_.data() + 30, body.size(), body.begin());
        if (wire::encode_channel_bind_ack_v1(body, tag, &ack_) != wire::Status::Ok)
            return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        std::vector<std::uint8_t> record;
        append_u32be(&record, wire::kChannelBindAckSizeV1);
        record.insert(record.end(), ack_.begin(), ack_.end());
        return write_bytes(std::move(record), true, Stage::WriteAckAndFin);
    }
    if (completed == Stage::WriteAckAndFin) {
        read_accumulator_.clear();
        expected_read_size_ = 0;
        return read_bytes(1, Stage::ReadListenerFin);
    }
    if (completed == Stage::ReadConnectorFin ||
        completed == Stage::ReadListenerFin) {
        if (!bytes.empty()) return reject(FLY_SESSION_V2_CONTRACT_VIOLATION);
        if (completed == Stage::ReadConnectorFin)
            return write_bytes({}, true, Stage::WriteListenerFin);
        channel_bound_ = true;
        stage_ = Stage::Bound;
        return FLY_SESSION_V2_OK;
    }
    if (completed == Stage::WriteListenerFin) {
        channel_bound_ = true;
        stage_ = Stage::Bound;
        return FLY_SESSION_V2_OK;
    }
    return reject(FLY_SESSION_V2_INVALID_STATE);
}

fly_session_result_v2 InitialQuicBindScheduler::cancel_pending() noexcept
{
    if (!pending_) return FLY_SESSION_V2_INVALID_STATE;
    pending_.reset();
    return reject(FLY_SESSION_V2_CANCELLED);
}

fly_session_result_v2 InitialQuicBindScheduler::reject(
    fly_session_result_v2 result) noexcept
{
    pending_.reset();
    failed_ = true;
    stage_ = Stage::Failed;
    return result == FLY_SESSION_V2_OK ? FLY_SESSION_V2_CONTRACT_VIOLATION
                                       : result;
}

} // namespace flynes::session
