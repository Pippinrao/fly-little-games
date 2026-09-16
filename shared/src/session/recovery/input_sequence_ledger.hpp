#ifndef FLYNES_SESSION_RECOVERY_INPUT_SEQUENCE_LEDGER_HPP
#define FLYNES_SESSION_RECOVERY_INPUT_SEQUENCE_LEDGER_HPP

/*
 * Task 12 / REC-DUAL step 2: parent-link input sequence ledger.
 *
 * Seat numbers are monotonic for the life of the parent link. Changing game,
 * cancelling, crashing or recovering must not zero or reuse a burned interval.
 * A CAS conflict must re-read the full root before this object is rebuilt.
 */

#include "flynes/flynes_session.h"

#include <array>
#include <cstdint>

namespace flynes::session::recovery {

inline constexpr std::uint8_t kLedgerSeatCountV1 = 2;

enum class ReservationKindV1 : std::uint8_t
{
    Normal = 1,
    Prime = 2,
    Terminal = 3,
    AuthorityClear = 4
};

struct SeatLedgerV1 final
{
    std::uint64_t reserved_through = 0;
    std::uint64_t ledger_generation = 0;
    std::array<std::uint8_t, 32> last_reservation_hash{};
    std::uint64_t consumed_through = 0;
};

class InputSequenceLedgerV1 final
{
public:
    fly_session_result_v2 genesis() noexcept;
    fly_session_result_v2 grant(std::uint8_t seat, std::uint64_t from_inclusive,
                                std::uint64_t through_inclusive,
                                ReservationKindV1 kind) noexcept;
    fly_session_result_v2 consume(std::uint8_t seat,
                                  std::uint64_t sequence) noexcept;
    fly_session_result_v2 close_child() noexcept;
    fly_session_result_v2 abort_prestart() noexcept;
    fly_session_result_v2 handoff_successor() noexcept;
    fly_session_result_v2 release_source_writer() noexcept;
    fly_session_result_v2 reset_to_zero() noexcept;

    [[nodiscard]] const SeatLedgerV1& seat(std::uint8_t index) const noexcept
    {
        return seats_[index < kLedgerSeatCountV1 ? index : 0];
    }
    [[nodiscard]] bool closed() const noexcept { return closed_; }
    [[nodiscard]] bool aborted_prestart() const noexcept
    {
        return aborted_prestart_;
    }
    [[nodiscard]] bool source_writer_alive() const noexcept
    {
        return source_writer_alive_;
    }
    [[nodiscard]] bool repair_blocked() const noexcept
    {
        return repair_blocked_;
    }
    [[nodiscard]] bool may_start() const noexcept
    {
        return source_writer_alive_ && !closed_ && !repair_blocked_ &&
               !aborted_prestart_ && granted_;
    }

private:
    std::array<SeatLedgerV1, kLedgerSeatCountV1> seats_{};
    bool granted_ = false;
    bool closed_ = false;
    bool aborted_prestart_ = false;
    bool source_writer_alive_ = true;
    bool repair_blocked_ = false;
    bool ever_nonzero_ = false;
};

} // namespace flynes::session::recovery

#endif
