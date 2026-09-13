#ifndef FLYNES_SESSION_WIRE_PAIR_CAPABILITY_HPP
#define FLYNES_SESSION_WIRE_PAIR_CAPABILITY_HPP

#include "session_codec.hpp"

#include <array>

namespace flynes::session::wire {

using BearerPlanBytes = std::array<std::uint8_t, 48>;

enum class PairSelectionStatus
{
    Ok,
    InvalidInitiator,
    InvalidResponder,
    NoCommonPlan
};

// Pure wire validation and deterministic selection. These functions do not
// authenticate or certify peers, authorize radio use, or change authority/seats.
// Caller certification/authentication handshake and durable plan-lock remain
// separate gates before executing a selected plan.
Status validate_pair_capability(const std::uint8_t* bytes, std::size_t size) noexcept;

// Both summaries must be valid; matches require all 48 global-plan bytes.
// Every failure clears selected. If both inputs are invalid, initiator wins.
PairSelectionStatus select_pair_plan(const std::uint8_t* initiator,
                                     std::size_t initiator_size,
                                     const std::uint8_t* responder,
                                     std::size_t responder_size,
                                     BearerPlanBytes& selected) noexcept;

} // namespace flynes::session::wire

#endif
