#ifndef FLYNES_SESSION_PORTS_PROVIDER_EVENTS_HPP
#define FLYNES_SESSION_PORTS_PROVIDER_EVENTS_HPP

#include <flynes/flynes_session.h>

#include <array>
#include <cstdint>

namespace flynes::session {

class ParsedProviderEvent final
{
public:
    ParsedProviderEvent() noexcept = default;
    ~ParsedProviderEvent();
    ParsedProviderEvent(const ParsedProviderEvent&) = delete;
    ParsedProviderEvent& operator=(const ParsedProviderEvent&) = delete;
    ParsedProviderEvent(ParsedProviderEvent&& other) noexcept;
    ParsedProviderEvent& operator=(ParsedProviderEvent&& other) noexcept;

    std::uint32_t payload_kind = 0;
    fly_session_resource_handle_v2 resource = 0;
    std::uint64_t generation = 0;
    std::uint64_t value0 = 0;
    std::uint64_t value1 = 0;
    std::array<std::uint8_t, 32> hash{};
    fly_session_buffer_v2_t* buffer = nullptr;

private:
    void release() noexcept;
};

fly_session_result_v2 parse_provider_event_v2(
    const fly_session_port_event_v2& source,
    const fly_session_op_token_v2& expected_token,
    std::uint32_t expected_payload_kind,
    ParsedProviderEvent& destination) noexcept;

} // namespace flynes::session

#endif
