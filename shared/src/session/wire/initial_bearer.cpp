#include "initial_bearer.hpp"

#include <algorithm>

namespace flynes::session::wire {
namespace {

constexpr std::array<std::uint8_t, 16> kServiceUuid{{
    0xe7,0x38,0xdc,0xda,0xa2,0x1a,0x58,0x2b,
    0x9c,0xed,0xa9,0x7f,0x6f,0xeb,0xee,0xdd}};

bool nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return bytes && std::any_of(bytes, bytes + size,
        [](std::uint8_t value) { return value != 0; });
}

bool zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !nonzero(bytes, size);
}

bool valid_role(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator || role == PairRoleV1::Responder;
}

PairRoleV1 opposite(PairRoleV1 role) noexcept
{
    return role == PairRoleV1::Initiator ? PairRoleV1::Responder
                                         : PairRoleV1::Initiator;
}

void put_be16(std::uint8_t* out, std::uint16_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 8u);
    out[1] = static_cast<std::uint8_t>(value);
}

void put_be32(std::uint8_t* out, std::uint32_t value) noexcept
{
    out[0] = static_cast<std::uint8_t>(value >> 24u);
    out[1] = static_cast<std::uint8_t>(value >> 16u);
    out[2] = static_cast<std::uint8_t>(value >> 8u);
    out[3] = static_cast<std::uint8_t>(value);
}

void put_be64(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (std::size_t i = 0; i < 8; ++i)
        out[i] = static_cast<std::uint8_t>(value >> (56u - i * 8u));
}

std::uint16_t read_be16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>((bytes[0] << 8u) | bytes[1]);
}

std::uint32_t read_be32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) | bytes[3];
}

std::uint64_t read_be64(const std::uint8_t* bytes) noexcept
{
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < 8; ++i) value = (value << 8u) | bytes[i];
    return value;
}

bool printable(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return std::all_of(bytes, bytes + size, [](std::uint8_t value) {
        return value >= 0x21 && value <= 0x7e;
    });
}

} // namespace

Status validate_bearer_credential_v1(std::uint8_t codec,
                                     const std::uint8_t* bytes,
                                     std::size_t size) noexcept
{
    if (!bytes) return Status::InvalidField;
    if (size < kBearerCredentialBytesSizeV1) return Status::Truncated;
    if (size > kBearerCredentialBytesSizeV1) return Status::Trailing;
    if (bytes[0] != 1) return Status::InvalidField;
    const auto first_length = bytes[2];
    const auto second_length = bytes[3];
    if (codec == 1)
    {
        if (bytes[1] != 1 || first_length < 1 || first_length > 32 ||
            second_length < 8 || second_length > 63 ||
            !printable(bytes + 4, first_length) ||
            !printable(bytes + 36, second_length) ||
            !zero(bytes + 4 + first_length, 32 - first_length) ||
            !zero(bytes + 36 + second_length, 64 - second_length))
            return Status::InvalidField;
        return Status::Ok;
    }
    if (codec == 2)
    {
        if ((bytes[1] != 1 && bytes[1] != 2) ||
            first_length < 16 || first_length > 32 ||
            (bytes[1] == 1 ? second_length != 0 : second_length != 32) ||
            !nonzero(bytes + 4, first_length) ||
            !zero(bytes + 4 + first_length, 32 - first_length) ||
            !zero(bytes + 36 + second_length, 64 - second_length))
            return Status::InvalidField;
        return Status::Ok;
    }
    return Status::UnknownEnum;
}

Status encode_bearer_join_params_v1(
    const BearerPlanBytes& selected_plan, std::uint32_t valid_for_ms,
    const std::array<std::uint8_t, kBearerCredentialBytesSizeV1>& credential,
    std::array<std::uint8_t, kBearerJoinParamsSizeV1>* out) noexcept
{
    const auto platform = selected_plan[4] == 3 ? std::uint8_t{3} : std::uint8_t{1};
    if (!out || valid_for_ms == 0 || valid_for_ms > 60000 ||
        validate_bearer_plan_v1(selected_plan, platform) != Status::Ok ||
        validate_bearer_credential_v1(selected_plan[3], credential.data(),
                                      credential.size()) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy_n(selected_plan.data(), 4, out->begin() + 8);
    put_be32(out->data() + 16, valid_for_ms);
    put_be16(out->data() + 20,
             static_cast<std::uint16_t>(credential.size()));
    std::copy(credential.begin(), credential.end(), out->begin() + 24);
    return Status::Ok;
}

Status decode_bearer_join_params_v1(
    const std::uint8_t* bytes, std::size_t size,
    const BearerPlanBytes& expected_plan, BearerJoinParamsV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kBearerJoinParamsSizeV1) return Status::Truncated;
    if (size > kBearerJoinParamsSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (!zero(bytes + 2, 6) || !zero(bytes + 12, 4) ||
        !zero(bytes + 22, 2) || read_be16(bytes + 20) != 100 ||
        !zero(bytes + 124, 412))
        return Status::NonzeroReserved;
    if (!std::equal(expected_plan.begin(), expected_plan.begin() + 4, bytes + 8))
        return Status::InvalidField;
    const auto valid_for = read_be32(bytes + 16);
    if (valid_for == 0 || valid_for > 60000 ||
        validate_bearer_credential_v1(bytes[11], bytes + 24, 100) != Status::Ok)
        return Status::InvalidField;
    out->bearer_kind = bytes[8];
    out->bearer_creator = static_cast<PairRoleV1>(bytes[9]);
    out->quic_listener = static_cast<PairRoleV1>(bytes[10]);
    out->credential_codec = bytes[11];
    out->valid_for_ms = valid_for;
    std::copy_n(bytes + 24, 100, out->credential.begin());
    return Status::Ok;
}

Status encode_initial_bearer_credential_plaintext_v1(
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    PairRoleV1 sender_role, const BearerPlanBytes& selected_plan,
    const std::array<std::uint8_t, kBearerJoinParamsSizeV1>& join_params,
    std::array<std::uint8_t, kInitialBearerCredentialPlaintextSizeV1>* out) noexcept
{
    BearerJoinParamsV1 decoded{};
    if (!out || !valid_role(sender_role) ||
        sender_role != static_cast<PairRoleV1>(selected_plan[1]) ||
        !nonzero(pair_transcript_hash.data(), 32) || !nonzero(session_id.data(), 16) ||
        decode_bearer_join_params_v1(join_params.data(), join_params.size(),
                                     selected_plan, &decoded) != Status::Ok)
        return Status::InvalidField;
    out->fill(0);
    (*out)[1] = 1;
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 8);
    std::copy(session_id.begin(), session_id.end(), out->begin() + 40);
    (*out)[56] = static_cast<std::uint8_t>(sender_role);
    (*out)[57] = static_cast<std::uint8_t>(opposite(sender_role));
    std::copy(join_params.begin(), join_params.end(), out->begin() + 64);
    return Status::Ok;
}

Status decode_initial_bearer_credential_plaintext_v1(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_transcript_hash,
    const std::array<std::uint8_t, 16>& expected_session_id,
    PairRoleV1 expected_sender, const BearerPlanBytes& expected_plan,
    InitialBearerCredentialPlaintextV1* out) noexcept
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kInitialBearerCredentialPlaintextSizeV1) return Status::Truncated;
    if (size > kInitialBearerCredentialPlaintextSizeV1) return Status::Trailing;
    if (bytes[0] != 0 || bytes[1] != 1) return Status::InvalidField;
    if (!zero(bytes + 2, 6) || !zero(bytes + 58, 6))
        return Status::NonzeroReserved;
    if (!valid_role(expected_sender) ||
        expected_sender != static_cast<PairRoleV1>(expected_plan[1]) ||
        bytes[56] != static_cast<std::uint8_t>(expected_sender) ||
        bytes[57] != static_cast<std::uint8_t>(opposite(expected_sender)) ||
        !std::equal(expected_transcript_hash.begin(),
                    expected_transcript_hash.end(), bytes + 8) ||
        !std::equal(expected_session_id.begin(), expected_session_id.end(),
                    bytes + 40) ||
        decode_bearer_join_params_v1(bytes + 64, 536, expected_plan,
                                     &out->join) != Status::Ok)
        return Status::InvalidField;
    out->pair_transcript_hash = expected_transcript_hash;
    out->session_id = expected_session_id;
    out->sender = expected_sender;
    out->receiver = opposite(expected_sender);
    return Status::Ok;
}

Status build_initial_bearer_credential_nonce_v1(
    PairRoleV1 sender_role, std::uint64_t message_counter,
    std::array<std::uint8_t, kInitialBearerCredentialNonceSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender_role) || message_counter == 0)
        return Status::InvalidField;
    out->fill(0);
    put_be32(out->data(), sender_role == PairRoleV1::Initiator
        ? UINT32_C(0x49325201) : UINT32_C(0x52324901));
    put_be64(out->data() + 4, message_counter);
    return Status::Ok;
}

Status build_initial_bearer_credential_aad_v1(
    PairRoleV1 sender_role,
    const std::array<std::uint8_t, 32>& pair_transcript_hash,
    const std::array<std::uint8_t, 16>& session_id,
    std::uint64_t message_counter,
    std::array<std::uint8_t, kInitialBearerCredentialAadSizeV1>* out) noexcept
{
    if (!out || !valid_role(sender_role) || message_counter == 0 ||
        !nonzero(pair_transcript_hash.data(), 32) || !nonzero(session_id.data(), 16))
        return Status::InvalidField;
    out->fill(0);
    std::copy(kServiceUuid.begin(), kServiceUuid.end(), out->begin());
    (*out)[17] = 2;
    (*out)[20] = 8;
    (*out)[21] = static_cast<std::uint8_t>(sender_role);
    (*out)[22] = static_cast<std::uint8_t>(opposite(sender_role));
    std::copy(pair_transcript_hash.begin(), pair_transcript_hash.end(),
              out->begin() + 24);
    std::copy(session_id.begin(), session_id.end(), out->begin() + 56);
    put_be64(out->data() + 72, message_counter);
    put_be32(out->data() + 80, 600);
    return Status::Ok;
}

Status encode_initial_bearer_credential_envelope_v1(
    std::uint64_t message_counter, const std::uint8_t* ciphertext_and_tag,
    std::size_t size, std::array<std::uint8_t,
        kInitialBearerCredentialEnvelopeSizeV1>* out) noexcept
{
    if (!out || !ciphertext_and_tag || message_counter == 0 || size != 616)
        return Status::InvalidField;
    out->fill(0);
    (*out)[0] = 1;
    put_be64(out->data() + 4, message_counter);
    std::copy_n(ciphertext_and_tag, size, out->begin() + 12);
    return Status::Ok;
}

Status decode_initial_bearer_credential_envelope_v1(
    const std::uint8_t* bytes, std::size_t size,
    InitialBearerCredentialEnvelopeV1* out)
{
    if (!bytes || !out) return Status::InvalidField;
    *out = {};
    if (size < kInitialBearerCredentialEnvelopeSizeV1) return Status::Truncated;
    if (size > kInitialBearerCredentialEnvelopeSizeV1) return Status::Trailing;
    if (bytes[0] != 1 || !zero(bytes + 1, 3))
        return bytes[0] != 1 ? Status::InvalidField : Status::NonzeroReserved;
    const auto counter_value = read_be64(bytes + 4);
    if (counter_value == 0) return Status::InvalidField;
    out->message_counter = counter_value;
    out->ciphertext_and_tag.assign(bytes + 12, bytes + size);
    return Status::Ok;
}

} // namespace flynes::session::wire
