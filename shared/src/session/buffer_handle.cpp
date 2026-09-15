#include "buffer_handle.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

namespace flynes::session {

fly_session_buffer_v2_t* make_buffer_v2(const std::uint8_t* bytes,
                                        std::size_t size) noexcept
{
    return make_buffer_v2(bytes, size, nullptr);
}

fly_session_buffer_v2_t* make_buffer_v2(
    const std::uint8_t* bytes, std::size_t size,
    std::atomic<std::uint32_t>* destruction_observer) noexcept
{
    if (bytes == nullptr && size != 0)
        return nullptr;
    try
    {
        std::vector<std::uint8_t> copy;
        if (size != 0)
            copy.assign(bytes, bytes + size);
        return new fly_session_buffer_v2_handle(
            std::move(copy), destruction_observer);
    }
    catch (const std::bad_alloc&)
    {
        return nullptr;
    }
    catch (...)
    {
        return nullptr;
    }
}

} // namespace flynes::session

extern "C" fly_session_result_v2 fly_session_buffer_create_copy_v2(
    fly_session_bytes_v2 source, fly_session_buffer_v2_t** out_buffer)
{
    if (out_buffer == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out_buffer = nullptr;
    if (source.reserved_zero != 0 ||
        (source.data == nullptr && source.size != 0))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out_buffer = flynes::session::make_buffer_v2(source.data, source.size);
    return *out_buffer != nullptr ? FLY_SESSION_V2_OK
                                  : FLY_SESSION_V2_OUT_OF_MEMORY;
}

extern "C" fly_session_result_v2 fly_session_buffer_size_v2(
    const fly_session_buffer_v2_t* buffer, std::uint64_t* out_size)
{
    if (buffer == nullptr || out_size == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out_size = static_cast<std::uint64_t>(buffer->bytes.size());
    return FLY_SESSION_V2_OK;
}

extern "C" fly_session_result_v2 fly_session_buffer_read_v2(
    const fly_session_buffer_v2_t* buffer, std::uint64_t offset,
    fly_session_write_bytes_v2 destination, std::uint64_t* out_written)
{
    if (out_written != nullptr)
        *out_written = 0;
    if (buffer == nullptr || out_written == nullptr ||
        (destination.data == nullptr && destination.capacity != 0) ||
        offset > static_cast<std::uint64_t>(buffer->bytes.size()))
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    const auto remaining = static_cast<std::uint64_t>(buffer->bytes.size()) - offset;
    const auto count = std::min(remaining, destination.capacity);
    if (count != 0)
    {
        std::memcpy(destination.data,
                    buffer->bytes.data() + static_cast<std::size_t>(offset),
                    static_cast<std::size_t>(count));
    }
    *out_written = count;
    return FLY_SESSION_V2_OK;
}

extern "C" void fly_session_buffer_retain_v2(fly_session_buffer_v2_t* buffer)
{
    if (buffer == nullptr)
        return;
    auto current = buffer->references.load(std::memory_order_relaxed);
    while (current != 0 && current != std::numeric_limits<std::uint32_t>::max() &&
           !buffer->references.compare_exchange_weak(
               current, current + 1, std::memory_order_relaxed,
               std::memory_order_relaxed))
    {
    }
}

extern "C" void fly_session_buffer_release_v2(fly_session_buffer_v2_t* buffer)
{
    if (buffer != nullptr &&
        buffer->references.fetch_sub(1, std::memory_order_acq_rel) == 1)
        delete buffer;
}
