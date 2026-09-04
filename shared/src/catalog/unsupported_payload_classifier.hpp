#ifndef FLYNES_CATALOG_UNSUPPORTED_PAYLOAD_CLASSIFIER_HPP
#define FLYNES_CATALOG_UNSUPPORTED_PAYLOAD_CLASSIFIER_HPP

#include "catalog/rom_payload_parser.hpp"

#include <cstdint>

namespace flynes::catalog {

/** Internal catalog value: its numbers are stable for parity tests, not an ABI or wire format. */
enum class UnsupportedPayloadReason : std::uint8_t
{
    NESTED_ARCHIVE = 0,
    EXECUTABLE = 1,
    GAME_BOY = 2,
    SIDECAR = 3,
    UNKNOWN_FORMAT = 4,
};

/**
 * Classifies a borrowed payload after ROM parsing failed. A null empty view represents an empty
 * sidecar; a null non-empty view is invalid and is reported safely as UNKNOWN_FORMAT.
 */
UnsupportedPayloadReason classify_unsupported_payload(ByteView payload) noexcept;

} // namespace flynes::catalog

#endif
