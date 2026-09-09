#include "render_mailbox.hpp"

#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <vector>

namespace {

void require(bool condition, const char* message)
{
    if (!condition)
    {
        std::cerr << message << '\n';
        std::exit(1);
    }
}

std::vector<std::uint8_t> frame(std::uint8_t value)
{
    return std::vector<std::uint8_t>(256U * 240U * 2U, value);
}

} // namespace

int main()
{
    using flynes::harmony::RenderDecisionKind;
    using flynes::harmony::RenderMailbox;

    RenderMailbox mailbox;
    require(mailbox.create_surface(1, 1680, 1260), "first surface must be accepted");
    require(!mailbox.create_surface(1, 1680, 1260), "duplicate surface generation must be rejected");

    require(mailbox.submit_source_frame(1, 40, 256, 240, frame(0x11)),
            "current-generation source frame must be accepted");
    auto decision = mailbox.next_decision(1);
    require(decision.kind == RenderDecisionKind::UPLOAD_AND_PRESENT,
            "new source frame must upload and present");
    require(decision.frame.frame_index == 40, "decision must expose the source frame");
    mailbox.complete(decision, true);

    decision = mailbox.next_decision(1);
    require(decision.kind == RenderDecisionKind::IDLE,
            "native mode must not upload or present without a new source frame");

    require(!mailbox.submit_source_frame(1, 40, 256, 240, frame(0x22)),
            "duplicate source frame must be rejected");
    require(!mailbox.submit_source_frame(0, 41, 256, 240, frame(0x22)),
            "stale surface generation must be rejected");

    mailbox.set_paused(true);
    require(mailbox.submit_source_frame(1, 41, 256, 240, frame(0x33)),
            "pause must retain the latest source frame");
    require(mailbox.next_decision(1).kind == RenderDecisionKind::IDLE,
            "pause must stop presentation");
    mailbox.set_paused(false);
    decision = mailbox.next_decision(1);
    require(decision.kind == RenderDecisionKind::UPLOAD_AND_PRESENT,
            "resume must present the retained new frame");
    mailbox.complete(decision, true);

    require(mailbox.destroy_surface(1), "current surface destroy must succeed");
    require(mailbox.next_decision(1).kind == RenderDecisionKind::IDLE,
            "destroyed surface must not present");
    require(!mailbox.destroy_surface(1), "duplicate destroy must be rejected");

    require(mailbox.create_surface(2, 1280, 1200), "new surface generation must be accepted");
    require(!mailbox.submit_source_frame(1, 42, 256, 240, frame(0x44)),
            "old callbacks must not publish into a rebuilt surface");
    require(mailbox.submit_source_frame(2, 42, 256, 240, frame(0x55)),
            "rebuilt surface must accept its own frame");
    decision = mailbox.next_decision(2);
    require(decision.kind == RenderDecisionKind::UPLOAD_AND_PRESENT,
            "rebuilt surface must force a fresh upload");
    mailbox.complete(decision, false);
    mailbox.record_external_present(2, true);

    const auto status = mailbox.status();
    require(status.surface_generation == 2, "status must report current surface generation");
    require(status.source_frames == 3, "status must count accepted unique source frames");
    require(status.uploaded_frames == 3, "external motion upload must be counted");
    require(status.presented_frames == 3, "external motion present must be counted");
    require(status.present_failures == 1, "failed presentation must be observable");

    return 0;
}
