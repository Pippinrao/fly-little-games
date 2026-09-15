#include "gatt_fragment.hpp"
#include "sha256.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session::wire {
namespace {

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

void write_be16(std::uint8_t* bytes, std::uint16_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value >> 8u);
    bytes[1] = static_cast<std::uint8_t>(value);
}

void write_be32(std::uint8_t* bytes, std::uint32_t value) noexcept
{
    bytes[0] = static_cast<std::uint8_t>(value >> 24u);
    bytes[1] = static_cast<std::uint8_t>(value >> 16u);
    bytes[2] = static_cast<std::uint8_t>(value >> 8u);
    bytes[3] = static_cast<std::uint8_t>(value);
}

bool ranges_overlap(std::uint16_t first_offset, std::size_t first_size,
                    std::uint16_t second_offset, std::size_t second_size) noexcept
{
    const std::size_t first_end = static_cast<std::size_t>(first_offset) + first_size;
    const std::size_t second_end = static_cast<std::size_t>(second_offset) + second_size;
    return first_offset < second_end && second_offset < first_end;
}

} // namespace

GattFragmentResult encode_gatt_logical_ack_v1(
    const GattLogicalAckV1& ack,
    std::array<std::uint8_t, kGattLogicalAckBodySize>* out) noexcept
{
    if (out == nullptr)
        return GattFragmentResult::InvalidArgument;
    out->fill(0);
    const auto role = static_cast<std::uint8_t>(ack.original_sender_physical_role);
    if ((role != static_cast<std::uint8_t>(GattPhysicalDirection::CentralToPeripheral) &&
         role != static_cast<std::uint8_t>(GattPhysicalDirection::PeripheralToCentral)) ||
        ack.acked_logical_type == 0 ||
        ack.acked_logical_type == static_cast<std::uint8_t>(GattLogicalType::PhysicalAck) ||
        ack.message_id == 0)
        return GattFragmentResult::ProtocolViolation;
    (*out)[0] = role;
    (*out)[1] = ack.acked_logical_type;
    write_be16(out->data() + 2, ack.message_id);
    std::copy(ack.logical_hash.begin(), ack.logical_hash.end(), out->begin() + 4);
    return GattFragmentResult::Accepted;
}

GattFragmentResult decode_gatt_logical_ack_v1(
    const std::uint8_t* bytes, std::size_t size,
    GattLogicalAckV1* out) noexcept
{
    if (bytes == nullptr || out == nullptr)
        return GattFragmentResult::InvalidArgument;
    *out = {};
    if (size != kGattLogicalAckBodySize)
        return GattFragmentResult::ProtocolViolation;
    GattLogicalAckV1 decoded{};
    decoded.original_sender_physical_role =
        static_cast<GattPhysicalDirection>(bytes[0]);
    decoded.acked_logical_type = bytes[1];
    decoded.message_id = read_be16(bytes + 2);
    std::copy_n(bytes + 4, decoded.logical_hash.size(), decoded.logical_hash.begin());
    std::array<std::uint8_t, kGattLogicalAckBodySize> canonical{};
    if (encode_gatt_logical_ack_v1(decoded, &canonical) !=
            GattFragmentResult::Accepted ||
        !std::equal(canonical.begin(), canonical.end(), bytes))
        return GattFragmentResult::ProtocolViolation;
    *out = decoded;
    return GattFragmentResult::Complete;
}

GattFragmentResult decode_gatt_logical_message(
    const std::uint8_t* bytes, std::size_t size,
    std::uint8_t expected_type, GattLogicalMessageView* out) noexcept
{
    if (bytes == nullptr || out == nullptr)
        return GattFragmentResult::InvalidArgument;
    *out = {};
    if (size < kGattLogicalMinSize || size > kGattLogicalMaxSize ||
        expected_type == 0 || bytes[1] != expected_type ||
        bytes[2] != 0 || bytes[3] != 0)
        return GattFragmentResult::ProtocolViolation;
    const std::size_t body_size = read_be32(bytes + 4);
    if (body_size > kGattLogicalMaxSize - 40 ||
        size != 8 + body_size + 32)
        return GattFragmentResult::ProtocolViolation;
    const char* domain = nullptr;
    if (bytes[0] == 1 && expected_type >= 1 && expected_type <= 26)
        domain = "flynes-gatt-logical-v1";
    else if (bytes[0] == 2 && (expected_type == 27 || expected_type == 28))
        domain = "flynes-gatt-logical-v2";
    else
        return GattFragmentResult::ProtocolViolation;
    const auto calculated = domain_hash(domain, bytes, 8 + body_size);
    const auto* logical_hash = bytes + 8 + body_size;
    if (!std::equal(calculated.begin(), calculated.end(), logical_hash))
        return GattFragmentResult::ProtocolViolation;
    out->version = bytes[0];
    out->logical_type = bytes[1];
    out->body = bytes + 8;
    out->body_size = body_size;
    out->logical_hash = logical_hash;
    return GattFragmentResult::Complete;
}

GattFragmentResult encode_gatt_logical_message(
    std::uint8_t logical_type, const std::uint8_t* body,
    std::size_t body_size, std::vector<std::uint8_t>* out) noexcept
{
    if (out == nullptr || (body == nullptr && body_size != 0))
        return GattFragmentResult::InvalidArgument;
    out->clear();
    const bool v1 = logical_type >= 1 && logical_type <= 26;
    const bool v2 = logical_type == 27 || logical_type == 28;
    if (!v1 && !v2)
        return GattFragmentResult::NotSupported;
    if (body_size > kGattLogicalMaxSize - kGattLogicalMinSize)
        return GattFragmentResult::Backpressure;
    try
    {
        std::vector<std::uint8_t> encoded(8 + body_size + 32, 0);
        encoded[0] = v1 ? 1 : 2;
        encoded[1] = logical_type;
        write_be32(encoded.data() + 4,
                   static_cast<std::uint32_t>(body_size));
        if (body_size != 0)
            std::copy_n(body, body_size, encoded.begin() + 8);
        const auto hash = domain_hash(
            v1 ? "flynes-gatt-logical-v1" : "flynes-gatt-logical-v2",
            encoded.data(), 8 + body_size);
        std::copy(hash.begin(), hash.end(), encoded.end() - 32);
        *out = std::move(encoded);
        return GattFragmentResult::Accepted;
    }
    catch (const std::bad_alloc&)
    {
        return GattFragmentResult::Backpressure;
    }
}

namespace {

bool exact_logical_message(const std::uint8_t* bytes, std::size_t size,
                           std::uint8_t expected_type) noexcept
{
    GattLogicalMessageView view{};
    return decode_gatt_logical_message(bytes, size, expected_type, &view) ==
           GattFragmentResult::Complete;
}

} // namespace

GattFragmentResult fragment_gatt_logical(
    const std::uint8_t* logical, std::size_t logical_size,
    std::uint8_t logical_type, std::uint16_t message_id,
    std::size_t att_value_cap,
    std::vector<std::vector<std::uint8_t>>* out) noexcept
{
    if (logical == nullptr || out == nullptr)
        return GattFragmentResult::InvalidArgument;
    out->clear();
    if (att_value_cap < 20)
        return GattFragmentResult::NotSupported;
    if (logical_size < kGattLogicalMinSize || logical_size > kGattLogicalMaxSize)
        return GattFragmentResult::Backpressure;
    if (message_id == 0 || logical_type == 0 ||
        !exact_logical_message(logical, logical_size, logical_type))
        return GattFragmentResult::ProtocolViolation;
    const std::size_t data_cap = att_value_cap - kGattPhysicalHeaderSize;
    const std::size_t count_size = (logical_size + data_cap - 1) / data_cap;
    if (count_size == 0 || count_size > 683 ||
        count_size > std::numeric_limits<std::uint16_t>::max())
        return GattFragmentResult::Backpressure;
    const auto count = static_cast<std::uint16_t>(count_size);
    const auto total = static_cast<std::uint16_t>(logical_size);
    try
    {
        out->reserve(count_size);
        for (std::uint16_t index = 0; index < count; ++index)
        {
            const std::size_t offset = static_cast<std::size_t>(index) * data_cap;
            const std::size_t data_size = std::min(data_cap, logical_size - offset);
            std::vector<std::uint8_t> value(kGattPhysicalHeaderSize + data_size, 0);
            value[0] = 1;
            value[1] = logical_type;
            value[2] = static_cast<std::uint8_t>((index == 0 ? 1u : 0u) |
                                                  (index + 1 == count ? 2u : 0u));
            write_be16(value.data() + 4, message_id);
            write_be16(value.data() + 6, index);
            write_be16(value.data() + 8, count);
            write_be16(value.data() + 10, static_cast<std::uint16_t>(offset));
            write_be16(value.data() + 12, total);
            std::copy(logical + offset, logical + offset + data_size,
                      value.begin() + static_cast<std::ptrdiff_t>(kGattPhysicalHeaderSize));
            out->push_back(std::move(value));
        }
    }
    catch (...)
    {
        out->clear();
        return GattFragmentResult::Backpressure;
    }
    return GattFragmentResult::Accepted;
}

GattReassembler::GattReassembler(std::uint64_t connection_generation,
                                 GattPhysicalDirection direction)
    : generation_(connection_generation), direction_(direction)
{
    assemblies_.reserve(kGattMaxIncompleteMessages);
    completed_.reserve(kGattMaxIncompleteMessages);
}

void GattReassembler::erase(std::size_t index) noexcept
{
    buffered_bytes_ -= assemblies_[index].total_length;
    assemblies_.erase(assemblies_.begin() + static_cast<std::ptrdiff_t>(index));
}

GattFragmentResult GattReassembler::accept(
    std::uint64_t connection_generation, std::uint64_t now_ns,
    const std::uint8_t* value, std::size_t size,
    std::vector<std::uint8_t>* completed,
    GattCompletedMetadata* metadata)
{
    if (value == nullptr || completed == nullptr)
        return GattFragmentResult::InvalidArgument;
    completed->clear();
    if (metadata != nullptr)
        *metadata = {};
    if (connection_generation != generation_)
        return GattFragmentResult::Stale;
    if (direction_ != GattPhysicalDirection::CentralToPeripheral &&
        direction_ != GattPhysicalDirection::PeripheralToCentral)
        return GattFragmentResult::InvalidArgument;
    if (size <= kGattPhysicalHeaderSize || size > 244 || value[0] != 1 ||
        value[1] == 0 || (value[2] & ~std::uint8_t{3}) != 0 || value[3] != 0)
        return GattFragmentResult::ProtocolViolation;

    const std::uint8_t logical_type = value[1];
    const std::uint8_t flags = value[2];
    const std::uint16_t message_id = read_be16(value + 4);
    const std::uint16_t index = read_be16(value + 6);
    const std::uint16_t count = read_be16(value + 8);
    const std::uint16_t offset = read_be16(value + 10);
    const std::uint16_t total = read_be16(value + 12);
    const std::size_t data_size = size - kGattPhysicalHeaderSize;
    if (message_id == 0 || count == 0 || count > 683 || index >= count ||
        total < kGattLogicalMinSize || total > kGattLogicalMaxSize ||
        static_cast<std::size_t>(offset) + data_size > total ||
        ((flags & 1u) != 0u) != (index == 0) ||
        ((flags & 2u) != 0u) != (index + 1 == count))
        return GattFragmentResult::ProtocolViolation;

    for (const Completed& prior : completed_)
    {
        if (prior.message_id != message_id)
            continue;
        const bool same = prior.logical_type == logical_type &&
            prior.fragment_count == count && prior.total_length == total &&
            static_cast<std::size_t>(offset) + data_size <= prior.logical.size() &&
            std::equal(value + kGattPhysicalHeaderSize, value + size,
                       prior.logical.begin() + static_cast<std::ptrdiff_t>(offset));
        if (!same)
            return GattFragmentResult::ProtocolViolation;
        if (metadata != nullptr)
        {
            metadata->logical_type = prior.logical_type;
            metadata->message_id = prior.message_id;
            std::copy_n(prior.logical.end() - 32, 32,
                        metadata->logical_hash.begin());
        }
        return GattFragmentResult::Duplicate;
    }

    std::size_t assembly_index = assemblies_.size();
    for (std::size_t i = 0; i < assemblies_.size(); ++i)
    {
        if (assemblies_[i].message_id == message_id &&
            assemblies_[i].logical_type != logical_type)
        {
            erase(i);
            return GattFragmentResult::ProtocolViolation;
        }
        if (assemblies_[i].message_id == message_id &&
            assemblies_[i].logical_type == logical_type)
        {
            assembly_index = i;
            break;
        }
    }
    if (assembly_index == assemblies_.size())
    {
        if (assemblies_.size() >= kGattMaxIncompleteMessages ||
            buffered_bytes_ + total > kGattMaxBufferedBytes)
            return GattFragmentResult::Backpressure;
        Assembly fresh;
        fresh.logical_type = logical_type;
        fresh.message_id = message_id;
        fresh.fragment_count = count;
        fresh.total_length = total;
        fresh.last_progress_ns = now_ns;
        fresh.pieces.reserve(count);
        assemblies_.push_back(std::move(fresh));
        buffered_bytes_ += total;
        assembly_index = assemblies_.size() - 1;
    }

    Assembly& assembly = assemblies_[assembly_index];
    if (assembly.fragment_count != count || assembly.total_length != total)
    {
        erase(assembly_index);
        return GattFragmentResult::ProtocolViolation;
    }
    for (const Piece& piece : assembly.pieces)
    {
        if (piece.index == index)
        {
            const bool same = piece.offset == offset && piece.bytes.size() == data_size &&
                std::equal(piece.bytes.begin(), piece.bytes.end(),
                           value + kGattPhysicalHeaderSize);
            if (same)
                return GattFragmentResult::Duplicate;
            erase(assembly_index);
            return GattFragmentResult::ProtocolViolation;
        }
        if (ranges_overlap(piece.offset, piece.bytes.size(), offset, data_size))
        {
            erase(assembly_index);
            return GattFragmentResult::ProtocolViolation;
        }
    }

    Piece piece;
    piece.index = index;
    piece.offset = offset;
    piece.bytes.assign(value + kGattPhysicalHeaderSize, value + size);
    assembly.pieces.push_back(std::move(piece));
    assembly.last_progress_ns = now_ns;
    if (assembly.pieces.size() != assembly.fragment_count)
        return GattFragmentResult::Accepted;

    std::sort(assembly.pieces.begin(), assembly.pieces.end(),
              [](const Piece& left, const Piece& right) { return left.offset < right.offset; });
    std::size_t cursor = 0;
    completed->assign(assembly.total_length, 0);
    for (const Piece& current : assembly.pieces)
    {
        if (current.offset != cursor)
        {
            completed->clear();
            erase(assembly_index);
            return GattFragmentResult::ProtocolViolation;
        }
        std::copy(current.bytes.begin(), current.bytes.end(), completed->begin() +
                  static_cast<std::ptrdiff_t>(cursor));
        cursor += current.bytes.size();
    }
    if (cursor != assembly.total_length ||
        !exact_logical_message(completed->data(), completed->size(), assembly.logical_type))
    {
        completed->clear();
        erase(assembly_index);
        return GattFragmentResult::ProtocolViolation;
    }
    Completed prior;
    prior.logical_type = assembly.logical_type;
    prior.message_id = assembly.message_id;
    prior.fragment_count = assembly.fragment_count;
    prior.total_length = assembly.total_length;
    prior.logical = *completed;
    if (completed_.size() == kGattMaxIncompleteMessages)
        completed_.erase(completed_.begin());
    completed_.push_back(std::move(prior));
    if (metadata != nullptr)
    {
        metadata->logical_type = assembly.logical_type;
        metadata->message_id = assembly.message_id;
        std::copy_n(completed->end() - 32, 32, metadata->logical_hash.begin());
    }
    erase(assembly_index);
    return GattFragmentResult::Complete;
}

std::size_t GattReassembler::expire(std::uint64_t now_ns) noexcept
{
    std::size_t expired = 0;
    for (std::size_t i = assemblies_.size(); i > 0; --i)
    {
        const auto last = assemblies_[i - 1].last_progress_ns;
        if (now_ns >= last && now_ns - last >= kGattNoProgressTimeoutNs)
        {
            erase(i - 1);
            ++expired;
        }
    }
    return expired;
}

std::size_t GattReassembler::incomplete_count() const noexcept
{
    return assemblies_.size();
}

std::size_t GattReassembler::buffered_bytes() const noexcept
{
    return buffered_bytes_;
}

} // namespace flynes::session::wire
