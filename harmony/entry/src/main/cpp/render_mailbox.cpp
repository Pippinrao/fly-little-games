#include "render_mailbox.hpp"

#include <utility>

namespace flynes::harmony {

bool RenderMailbox::create_surface(std::uint64_t generation,
                                   std::uint32_t width,
                                   std::uint32_t height)
{
    if (generation == 0 || width == 0 || height == 0)
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (generation <= status_.surface_generation)
    {
        return false;
    }
    status_.surface_generation = generation;
    status_.surface_width = width;
    status_.surface_height = height;
    status_.surface_ready = true;
    has_latest_ = false;
    has_uploaded_ = false;
    latest_ = {};
    return true;
}

bool RenderMailbox::destroy_surface(std::uint64_t generation)
{
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.surface_generation)
    {
        return false;
    }
    status_.surface_ready = false;
    status_.surface_width = 0;
    status_.surface_height = 0;
    has_latest_ = false;
    has_uploaded_ = false;
    latest_ = {};
    return true;
}

bool RenderMailbox::resize_surface(std::uint64_t generation,
                                   std::uint32_t width,
                                   std::uint32_t height)
{
    if (width == 0 || height == 0)
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.surface_generation)
    {
        return false;
    }
    status_.surface_width = width;
    status_.surface_height = height;
    return true;
}

void RenderMailbox::set_paused(bool paused)
{
    std::lock_guard lock(mutex_);
    status_.paused = paused;
}

bool RenderMailbox::submit_source_frame(std::uint64_t generation,
                                        std::uint64_t frame_index,
                                        std::uint32_t width,
                                        std::uint32_t height,
                                        std::vector<std::uint8_t> rgb565)
{
    if (width == 0 || height == 0 || rgb565.size() != width * height * 2ULL)
    {
        return false;
    }
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.surface_generation)
    {
        return false;
    }
    if (has_latest_ && frame_index <= latest_.frame_index)
    {
        return false;
    }
    latest_.surface_generation = generation;
    latest_.frame_index = frame_index;
    latest_.width = width;
    latest_.height = height;
    latest_.rgb565 = std::move(rgb565);
    has_latest_ = true;
    ++status_.source_frames;
    return true;
}

RenderDecision RenderMailbox::next_decision(std::uint64_t generation) const
{
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || status_.paused || generation != status_.surface_generation ||
        !has_latest_ || (has_uploaded_ && latest_.frame_index <= uploaded_frame_index_))
    {
        return {};
    }
    return {RenderDecisionKind::UPLOAD_AND_PRESENT, latest_};
}

void RenderMailbox::complete(const RenderDecision& decision, bool success)
{
    if (decision.kind != RenderDecisionKind::UPLOAD_AND_PRESENT)
    {
        return;
    }
    std::lock_guard lock(mutex_);
    if (decision.frame.surface_generation != status_.surface_generation)
    {
        return;
    }
    if (!success)
    {
        ++status_.present_failures;
        return;
    }
    if (!has_uploaded_ || decision.frame.frame_index > uploaded_frame_index_)
    {
        uploaded_frame_index_ = decision.frame.frame_index;
        has_uploaded_ = true;
        ++status_.uploaded_frames;
        ++status_.presented_frames;
    }
}

void RenderMailbox::record_external_present(std::uint64_t generation, bool success)
{
    std::lock_guard lock(mutex_);
    if (!status_.surface_ready || generation != status_.surface_generation) return;
    if (!success)
    {
        ++status_.present_failures;
        return;
    }
    ++status_.uploaded_frames;
    ++status_.presented_frames;
}

RenderMailboxStatus RenderMailbox::status() const
{
    std::lock_guard lock(mutex_);
    return status_;
}

} // namespace flynes::harmony
