#ifndef FLYNES_SESSION_WIRE_CODEC_HPP
#define FLYNES_SESSION_WIRE_CODEC_HPP

#include <cstddef>
#include <cstdint>

namespace flynes::session::wire {

enum class Status : std::int32_t
{
    Ok = 0,
    Truncated = 1,
    Trailing = 2,
    UnknownEnum = 3,
    NonzeroReserved = 4,
    BadLength = 5,
    UnknownKind = 6,
    UnknownCriticalTag = 7,
    InvalidField = 8
};

enum class QuicChannel : std::uint8_t
{
    Control = 1,
    Input = 2,
    StateCommit = 3,
    Bulk = 4,
    Rom = 5,
    Video = 6,
    Audio = 7
};

Status check(const char* type_name,
             const std::uint8_t* bytes,
             std::size_t size,
             std::uint8_t hash_out[32]);

Status encode_frame_cursor(std::uint8_t kind,
                           std::uint64_t index,
                           std::uint8_t out[16],
                           std::size_t* written);

} // namespace flynes::session::wire

#endif
