#ifndef FLYNES_SESSION_WIRE_APP_STREAM_ASSEMBLER_HPP
#define FLYNES_SESSION_WIRE_APP_STREAM_ASSEMBLER_HPP

#include "app_frame.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace flynes::session::wire {

enum class StreamAssemblyResult : std::int32_t
{
    NeedMore = 1,
    Produced = 2,
    CleanFin = 3,
    Closed = -1,
    Backpressure = -2,
    ProtocolViolation = -3,
    InvalidArgument = -4
};

struct OwnedAppFrame
{
    std::uint16_t frame_type_tag = 0;
    FrameTypeNamespace type_namespace = FrameTypeNamespace::ObjectKind;
    std::string type_name;
    std::vector<std::uint8_t> object_bytes;
    std::array<std::uint8_t, 32> object_hash{};
    std::array<std::uint8_t, 32> app_frame_hash{};
};

class AppStreamAssembler
{
public:
    explicit AppStreamAssembler(QuicChannel channel);

    StreamAssemblyResult data(const std::uint8_t* bytes,
                              std::size_t size,
                              std::vector<OwnedAppFrame>* output);
    StreamAssemblyResult finish(std::vector<OwnedAppFrame>* output);
    StreamAssemblyResult reset() noexcept;

    [[nodiscard]] std::size_t buffered_bytes() const noexcept { return buffer_.size(); }
    [[nodiscard]] bool closed() const noexcept { return closed_; }

private:
    StreamAssemblyResult fail(StreamAssemblyResult result) noexcept;

    QuicChannel channel_{};
    std::vector<std::uint8_t> buffer_;
    bool closed_ = false;
};

} // namespace flynes::session::wire

#endif
