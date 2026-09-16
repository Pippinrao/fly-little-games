#include "input_sequence_ledger.hpp"

#include "../wire/sha256.hpp"

#include <cstring>

namespace flynes::session::recovery {
namespace {

void store_u64be(std::uint8_t* out, std::uint64_t value) noexcept
{
    for (int i = 7; i >= 0; --i)
    {
        out[i] = static_cast<std::uint8_t>(value & 0xffu);
        value >>= 8u;
    }
}

std::array<std::uint8_t, 32> reservation_hash(
    std::uint8_t seat, std::uint64_t from, std::uint64_t through,
    std::uint64_t generation,
    const std::array<std::uint8_t, 32>& previous) noexcept
{
    std::uint8_t packed[1 + 8 + 8 + 8 + 32] = {};
    packed[0] = seat;
    store_u64be(packed + 1, from);
    store_u64be(packed + 9, through);
    store_u64be(packed + 17, generation);
    std::memcpy(packed + 25, previous.data(), previous.size());
    return wire::domain_hash("flynes-ledger-reservation-v1", packed,
                             sizeof(packed));
}

} // namespace

fly_session_result_v2 InputSequenceLedgerV1::genesis() noexcept
{
    if (ever_nonzero_)
        return FLY_SESSION_V2_INVALID_STATE;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::grant(
    std::uint8_t seat, std::uint64_t from_inclusive,
    std::uint64_t through_inclusive, ReservationKindV1) noexcept
{
    if (seat >= kLedgerSeatCountV1 || from_inclusive == 0 ||
        through_inclusive < from_inclusive)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (repair_blocked_ || closed_ || !source_writer_alive_)
        return FLY_SESSION_V2_INVALID_STATE;
    auto& slot = seats_[seat];
    const auto expected_from =
        slot.reserved_through == 0 ? 1u : slot.reserved_through + 1u;
    if (from_inclusive != expected_from)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto next_generation = slot.ledger_generation + 1u;
    slot.last_reservation_hash = reservation_hash(
        seat, from_inclusive, through_inclusive, next_generation,
        slot.last_reservation_hash);
    slot.reserved_through = through_inclusive;
    slot.ledger_generation = next_generation;
    granted_ = true;
    ever_nonzero_ = true;
    aborted_prestart_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::consume(
    std::uint8_t seat, std::uint64_t sequence) noexcept
{
    if (seat >= kLedgerSeatCountV1 || sequence == 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (!granted_ || closed_ || repair_blocked_)
        return FLY_SESSION_V2_INVALID_STATE;
    auto& slot = seats_[seat];
    if (sequence <= slot.consumed_through || sequence > slot.reserved_through)
        return FLY_SESSION_V2_INVALID_STATE;
    slot.consumed_through = sequence;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::close_child() noexcept
{
    if (!granted_ || closed_)
        return FLY_SESSION_V2_INVALID_STATE;
    for (auto& slot : seats_)
        slot.consumed_through = slot.reserved_through;
    closed_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::abort_prestart() noexcept
{
    if (closed_ || repair_blocked_)
        return FLY_SESSION_V2_INVALID_STATE;
    for (const auto& slot : seats_)
    {
        if (slot.consumed_through != 0)
            return FLY_SESSION_V2_INVALID_STATE;
    }
    aborted_prestart_ = true;
    closed_ = true;
    granted_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::handoff_successor() noexcept
{
    if (!closed_)
        return FLY_SESSION_V2_INVALID_STATE;
    for (auto& slot : seats_)
        slot.ledger_generation += 1u;
    closed_ = false;
    granted_ = false;
    aborted_prestart_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::release_source_writer() noexcept
{
    source_writer_alive_ = false;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 InputSequenceLedgerV1::reset_to_zero() noexcept
{
    if (ever_nonzero_)
        return FLY_SESSION_V2_INVALID_STATE;
    return FLY_SESSION_V2_OK;
}

} // namespace flynes::session::recovery
