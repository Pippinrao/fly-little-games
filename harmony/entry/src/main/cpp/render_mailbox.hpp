#pragma once

#include <cstdint>
#include <mutex>
#include <vector>

namespace flynes::harmony {

struct RenderFrame final
{
    std::uint64_t surface_generation = 0;
    std::uint64_t frame_index = 0;
    std::uint32_t width = 0;
    std::uint32_t height = 0;
    std::vector<std::uint8_t> rgb565;
};

enum class RenderDecisionKind
{
    IDLE,
    UPLOAD_AND_PRESENT,
};

struct RenderDecision final
{
    RenderDecisionKind kind = RenderDecisionKind::IDLE;
    RenderFrame frame;
};

struct RenderMailboxStatus final
{
    std::uint64_t surface_generation = 0;
    std::uint64_t source_frames = 0;
    std::uint64_t uploaded_frames = 0;
    std::uint64_t presented_frames = 0;
    std::uint64_t present_failures = 0;
    std::uint32_t surface_width = 0;
    std::uint32_t surface_height = 0;
    bool surface_ready = false;
    bool paused = false;
};

// Thread-safe handoff between the emulation producer and the EGL render thread.
// Native presentation is source-driven: a VSync with no newer source frame is idle.
class RenderMailbox final
{
public:
    bool create_surface(std::uint64_t generation, std::uint32_t width, std::uint32_t height);
    bool resize_surface(std::uint64_t generation, std::uint32_t width, std::uint32_t height);
    bool destroy_surface(std::uint64_t generation);
    void set_paused(bool paused);

    bool submit_source_frame(std::uint64_t generation,
                             std::uint64_t frame_index,
                             std::uint32_t width,
                             std::uint32_t height,
                             std::vector<std::uint8_t> rgb565);
    RenderDecision next_decision(std::uint64_t generation) const;
    void complete(const RenderDecision& decision, bool success);
    void record_external_present(std::uint64_t generation, bool success);
    RenderMailboxStatus status() const;

private:
    mutable std::mutex mutex_;
    RenderMailboxStatus status_;
    RenderFrame latest_;
    bool has_latest_ = false;
    bool has_uploaded_ = false;
    std::uint64_t uploaded_frame_index_ = 0;
};

} // namespace flynes::harmony
