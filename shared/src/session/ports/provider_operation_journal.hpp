#ifndef FLYNES_SESSION_PORTS_PROVIDER_OPERATION_JOURNAL_HPP
#define FLYNES_SESSION_PORTS_PROVIDER_OPERATION_JOURNAL_HPP

#include "provider_events.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace flynes::session {

struct ProviderOperationCompletion final
{
    std::uint32_t payload_kind = 0;
    fly_session_result_v2 result = FLY_SESSION_V2_OK;
    bool terminal = false;
    ParsedProviderEvent payload{};
};

// Single-owner journal for asynchronous provider operations. An operation must be
// registered before dispatch. Only the exact immutable token and expected payload
// kind can complete it, and every accepted operation has at most one terminal.
class ProviderOperationJournal final
{
public:
    explicit ProviderOperationJournal(std::size_t active_capacity);

    fly_session_result_v2 expect(const fly_session_op_token_v2& token,
                                 std::uint32_t payload_kind);
    fly_session_result_v2 accept(const fly_session_port_event_v2& event,
                                 ProviderOperationCompletion& completion);
    fly_session_result_v2 cancel(const fly_session_op_token_v2& token) noexcept;
    [[nodiscard]] bool has_pending() const noexcept;

private:
    enum class State : std::uint8_t { Pending, Terminal, Cancelled };
    struct Record final
    {
        fly_session_op_token_v2 token{};
        std::uint32_t payload_kind = 0;
        State state = State::Pending;
        fly_session_port_event_v2 terminal_event{};
    };

    std::vector<Record> records_;
    std::size_t active_capacity_ = 0;
    std::uint64_t highest_operation_id_ = 0;
};

} // namespace flynes::session

#endif
