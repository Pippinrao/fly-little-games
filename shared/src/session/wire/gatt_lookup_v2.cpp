#include "gatt_lookup_v2.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace flynes::session::wire {
namespace {

constexpr std::size_t kHeaderSize = 8;
constexpr std::size_t kHashSize = 32;

bool all_zero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    for (std::size_t i = 0; i < size; ++i)
    {
        if (bytes[i] != 0)
            return false;
    }
    return true;
}

bool any_nonzero(const std::uint8_t* bytes, std::size_t size) noexcept
{
    return !all_zero(bytes, size);
}

std::uint16_t read_be16(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>((static_cast<std::uint16_t>(bytes[0]) << 8u) |
                                      bytes[1]);
}

std::uint32_t read_be32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           bytes[3];
}

void write_be32(std::uint8_t* bytes, std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value >> 24u);
    bytes[1] = static_cast<std::uint8_t>(value >> 16u);
    bytes[2] = static_cast<std::uint8_t>(value >> 8u);
    bytes[3] = static_cast<std::uint8_t>(value);
}

bool valid_code(const std::array<std::uint8_t, 6>& code) noexcept
{
    return std::all_of(code.begin(), code.end(), [](std::uint8_t value) {
        return value >= static_cast<std::uint8_t>('0') &&
               value <= static_cast<std::uint8_t>('9');
    });
}

bool valid_pair_context(const std::uint8_t* context) noexcept
{
    return read_be16(context) == 1 &&
           all_zero(context + 2, 6) &&
           read_be16(context + 8) == 2 &&
           read_be16(context + 10) == 0 &&
           context[12] == 1 &&
           all_zero(context + 13, 3) &&
           any_nonzero(context + 16, 16) &&
           any_nonzero(context + 32, 16) &&
           read_be32(context + 48) == 60000 &&
           all_zero(context + 52, 12) &&
           any_nonzero(context + 64, 16);
}

void finish_logical(std::uint8_t* out, std::size_t body_size) noexcept
{
    const auto hash = domain_hash("flynes-gatt-logical-v2", out, kHeaderSize + body_size);
    std::copy(hash.begin(), hash.end(), out + kHeaderSize + body_size);
}

Status prepare(std::uint8_t type, std::size_t body_size, std::uint8_t* out,
               std::size_t capacity, std::size_t* written) noexcept
{
    if (out == nullptr || written == nullptr)
        return Status::InvalidField;
    *written = 0;
    const std::size_t total = kHeaderSize + body_size + kHashSize;
    if (capacity < total)
        return Status::Truncated;
    std::memset(out, 0, total);
    out[0] = 2;
    out[1] = type;
    write_be32(out + 4, static_cast<std::uint32_t>(body_size));
    out[9] = 2;
    *written = total;
    return Status::Ok;
}

} // namespace

bool gatt_lookup_type_allowed_v2(std::uint8_t type, std::uint8_t outer_version) noexcept
{
    return outer_version == 2 &&
           (type == static_cast<std::uint8_t>(GattLookupTypeV2::CodeLookupRequest) ||
            type == static_cast<std::uint8_t>(GattLookupTypeV2::CodeLookupMatch));
}

Status encode_gatt_lookup_request_v2(const std::array<std::uint8_t, 6>& code,
                                     const std::array<std::uint8_t, 16>& request_nonce,
                                     std::uint8_t* out, std::size_t capacity,
                                     std::size_t* written) noexcept
{
    if (!valid_code(code) || !any_nonzero(request_nonce.data(), request_nonce.size()))
        return Status::InvalidField;
    const Status status = prepare(static_cast<std::uint8_t>(GattLookupTypeV2::CodeLookupRequest),
                                  kGattLookupRequestBodySizeV2, out, capacity, written);
    if (status != Status::Ok)
        return status;
    std::copy(code.begin(), code.end(), out + 16);
    std::copy(request_nonce.begin(), request_nonce.end(), out + 24);
    finish_logical(out, kGattLookupRequestBodySizeV2);
    return Status::Ok;
}

Status encode_gatt_lookup_match_v2(const std::array<std::uint8_t, 16>& request_nonce,
                                   const std::array<std::uint8_t, 80>& pair_context,
                                   std::uint8_t* out, std::size_t capacity,
                                   std::size_t* written) noexcept
{
    if (!any_nonzero(request_nonce.data(), request_nonce.size()) ||
        !valid_pair_context(pair_context.data()))
        return Status::InvalidField;
    const Status status = prepare(static_cast<std::uint8_t>(GattLookupTypeV2::CodeLookupMatch),
                                  kGattLookupMatchBodySizeV2, out, capacity, written);
    if (status != Status::Ok)
        return status;
    std::copy(request_nonce.begin(), request_nonce.end(), out + 16);
    std::copy(pair_context.begin(), pair_context.end(), out + 32);
    const auto context_hash = domain_hash("flynes-pair-context-v1",
                                          pair_context.data(), pair_context.size());
    std::copy(context_hash.begin(), context_hash.end(), out + 112);
    finish_logical(out, kGattLookupMatchBodySizeV2);
    return Status::Ok;
}

Status decode_gatt_lookup_v2(GattLookupDirection direction, const std::uint8_t* bytes,
                             std::size_t size, GattLookupMessageV2* out) noexcept
{
    if (out == nullptr || (bytes == nullptr && size != 0))
        return Status::InvalidField;
    *out = {};
    if (size < kHeaderSize)
        return Status::Truncated;
    if (!gatt_lookup_type_allowed_v2(bytes[1], bytes[0]))
        return Status::UnknownKind;
    if (!all_zero(bytes + 2, 2))
        return Status::NonzeroReserved;

    const bool request = bytes[1] == static_cast<std::uint8_t>(GattLookupTypeV2::CodeLookupRequest);
    const std::size_t body_size = request ? kGattLookupRequestBodySizeV2
                                          : kGattLookupMatchBodySizeV2;
    const std::size_t expected = kHeaderSize + body_size + kHashSize;
    if (size < expected)
        return Status::Truncated;
    if (size > expected)
        return Status::Trailing;
    if (read_be32(bytes + 4) != body_size)
        return Status::BadLength;
    if (read_be16(bytes + 8) != 2)
        return Status::InvalidField;
    if (!all_zero(bytes + 10, 6))
        return Status::NonzeroReserved;
    if ((request && direction != GattLookupDirection::ResponderToInitiator) ||
        (!request && direction != GattLookupDirection::InitiatorToResponder))
        return Status::InvalidField;

    if (request)
    {
        for (std::size_t i = 16; i < 22; ++i)
        {
            if (bytes[i] < static_cast<std::uint8_t>('0') ||
                bytes[i] > static_cast<std::uint8_t>('9'))
                return Status::InvalidField;
        }
        if (!all_zero(bytes + 22, 2) || !any_nonzero(bytes + 24, 16))
            return Status::InvalidField;
    }
    else
    {
        if (!any_nonzero(bytes + 16, 16) || !valid_pair_context(bytes + 32))
            return Status::InvalidField;
        const auto context_hash = domain_hash("flynes-pair-context-v1", bytes + 32, 80);
        if (!std::equal(context_hash.begin(), context_hash.end(), bytes + 112))
            return Status::InvalidField;
    }

    const auto logical_hash = domain_hash("flynes-gatt-logical-v2", bytes,
                                          kHeaderSize + body_size);
    if (!std::equal(logical_hash.begin(), logical_hash.end(), bytes + kHeaderSize + body_size))
        return Status::InvalidField;

    out->type = static_cast<GattLookupTypeV2>(bytes[1]);
    out->body = bytes + kHeaderSize;
    out->body_size = body_size;
    out->logical_hash = logical_hash;
    return Status::Ok;
}

bool lookup_match_binds_request_v2(const GattLookupMessageV2& match,
                                   const std::array<std::uint8_t, 16>& request_nonce,
                                   const std::array<std::uint8_t, 80>& pair_context) noexcept
{
    if (match.type != GattLookupTypeV2::CodeLookupMatch ||
        match.body == nullptr || match.body_size != kGattLookupMatchBodySizeV2)
        return false;
    if (!std::equal(request_nonce.begin(), request_nonce.end(), match.body + 8) ||
        !std::equal(pair_context.begin(), pair_context.end(), match.body + 24))
        return false;
    const auto context_hash = domain_hash("flynes-pair-context-v1",
                                          pair_context.data(), pair_context.size());
    return std::equal(context_hash.begin(), context_hash.end(), match.body + 104);
}

} // namespace flynes::session::wire
