#include "app_stream_assembler.hpp"

#include <algorithm>
#include <cstring>

namespace flynes::session::wire {
namespace {

std::uint32_t read_be32(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           bytes[3];
}

} // namespace

AppStreamAssembler::AppStreamAssembler(QuicChannel channel) : channel_(channel)
{
    buffer_.reserve(std::min<std::size_t>(max_object_bytes(channel) + 6, 4096));
}

StreamAssemblyResult AppStreamAssembler::fail(StreamAssemblyResult result) noexcept
{
    buffer_.clear();
    closed_ = true;
    return result;
}

StreamAssemblyResult AppStreamAssembler::data(
    const std::uint8_t* bytes, std::size_t size,
    std::vector<OwnedAppFrame>* output)
{
    if (output == nullptr || (bytes == nullptr && size != 0))
        return StreamAssemblyResult::InvalidArgument;
    if (closed_)
        return StreamAssemblyResult::Closed;
    const std::size_t hard_limit = max_object_bytes(channel_) + 6;
    if (size > hard_limit || buffer_.size() > hard_limit - size)
        return fail(StreamAssemblyResult::Backpressure);
    try
    {
    if (size != 0)
        buffer_.insert(buffer_.end(), bytes, bytes + size);
    }
    catch (...)
    {
        return fail(StreamAssemblyResult::Backpressure);
    }

    bool produced = false;
    while (buffer_.size() >= 4)
    {
        const std::uint32_t frame_length = read_be32(buffer_.data());
        if (frame_length < 2)
            return fail(StreamAssemblyResult::ProtocolViolation);
        const std::size_t object_size = static_cast<std::size_t>(frame_length) - 2;
        if (object_size > max_object_bytes(channel_))
            return fail(StreamAssemblyResult::Backpressure);
        const std::size_t total = static_cast<std::size_t>(frame_length) + 4;
        if (buffer_.size() < total)
            break;

        AppFrame parsed{};
        if (parse_app_frame(channel_, buffer_.data(), total, &parsed) != Status::Ok)
            return fail(StreamAssemblyResult::ProtocolViolation);
        OwnedAppFrame owned;
        owned.frame_type_tag = parsed.frame_type_tag;
        owned.type_namespace = parsed.type_namespace;
        owned.type_name = parsed.type_name;
        owned.object_bytes.assign(parsed.object_bytes, parsed.object_bytes + parsed.object_size);
        std::copy(std::begin(parsed.object_hash), std::end(parsed.object_hash),
                  owned.object_hash.begin());
        std::copy(std::begin(parsed.app_frame_hash), std::end(parsed.app_frame_hash),
                  owned.app_frame_hash.begin());
        output->push_back(std::move(owned));
        buffer_.erase(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(total));
        produced = true;
    }
    return produced ? StreamAssemblyResult::Produced : StreamAssemblyResult::NeedMore;
}

StreamAssemblyResult AppStreamAssembler::finish(std::vector<OwnedAppFrame>* output)
{
    if (output == nullptr)
        return StreamAssemblyResult::InvalidArgument;
    if (closed_)
        return StreamAssemblyResult::Closed;
    if (!buffer_.empty())
        return fail(StreamAssemblyResult::ProtocolViolation);
    closed_ = true;
    return StreamAssemblyResult::CleanFin;
}

StreamAssemblyResult AppStreamAssembler::reset() noexcept
{
    buffer_.clear();
    closed_ = true;
    return StreamAssemblyResult::Closed;
}

} // namespace flynes::session::wire
