#ifndef FLYNES_SESSION_BUFFER_HANDLE_HPP
#define FLYNES_SESSION_BUFFER_HANDLE_HPP

#include <flynes/flynes_session.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

struct fly_session_buffer_v2_handle
{
    explicit fly_session_buffer_v2_handle(
        std::vector<std::uint8_t> source,
        std::atomic<std::uint32_t>* destruction_observer = nullptr)
        : bytes(std::move(source)), destruction_observer(destruction_observer)
    {
    }

    ~fly_session_buffer_v2_handle()
    {
        if (destruction_observer)
            destruction_observer->fetch_add(1, std::memory_order_relaxed);
    }

    std::atomic<std::uint32_t> references{1};
    const std::vector<std::uint8_t> bytes;
    std::atomic<std::uint32_t>* destruction_observer = nullptr;
};

namespace flynes::session {

fly_session_buffer_v2_t* make_buffer_v2(const std::uint8_t* bytes,
                                        std::size_t size) noexcept;
fly_session_buffer_v2_t* make_buffer_v2(
    const std::uint8_t* bytes, std::size_t size,
    std::atomic<std::uint32_t>* destruction_observer) noexcept;

} // namespace flynes::session

#endif
