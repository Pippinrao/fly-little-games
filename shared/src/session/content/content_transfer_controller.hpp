#ifndef FLYNES_SESSION_CONTENT_CONTENT_TRANSFER_CONTROLLER_HPP
#define FLYNES_SESSION_CONTENT_CONTENT_TRANSFER_CONTROLLER_HPP

#include "content_offer_scheduler.hpp"
#include "content_offer_wire.hpp"

#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace flynes::session::content {

class ContentTransferControllerV1 final
{
public:
    enum class EffectKind : std::uint8_t
    {
        OpenStream = 1,
        Write = 2,
        GrantRead = 3
    };

    struct Effect final
    {
        EffectKind kind = EffectKind::OpenStream;
        bool accept = false;
        std::uint32_t opener_role = 0;
        fly_session_resource_handle_v2 connection = 0;
        fly_session_resource_handle_v2 stream = 0;
        std::vector<std::uint8_t> bytes{};
        std::uint64_t read_credit = 0;
        std::uint32_t stream_kind =
            static_cast<std::uint32_t>(5); /* QuicChannel::Rom */
        std::uint32_t expected_payload_kind = 0;
    };

    ContentTransferControllerV1() = default;
    ContentTransferControllerV1(const ContentTransferControllerV1&) = delete;
    ContentTransferControllerV1& operator=(const ContentTransferControllerV1&) =
        delete;

    void reset(ContentOfferRoleV1 role) noexcept;
    void attach_connection(fly_session_resource_handle_v2 connection,
                           bool local_is_listener) noexcept;

    fly_session_result_v2 offer(const ContentOfferDeclV1& decl,
                                const std::uint8_t* payload,
                                std::size_t payload_size) noexcept;
    fly_session_result_v2 accept_offer(const ContentOfferDeclV1& decl) noexcept;
    fly_session_result_v2 approve_send() noexcept;
    fly_session_result_v2 approve_receive() noexcept;
    fly_session_result_v2 approve_import() noexcept;
    fly_session_result_v2 cancel() noexcept;
    fly_session_result_v2 note_friend_saved() noexcept;
    fly_session_result_v2 on_clock(std::uint64_t continuous_ns) noexcept;

    [[nodiscard]] std::optional<Effect> poll_effect();
    fly_session_result_v2 on_stream_open(
        fly_session_resource_handle_v2 send,
        fly_session_resource_handle_v2 recv) noexcept;
    fly_session_result_v2 on_write_complete() noexcept;
    fly_session_result_v2 ingest_remote_bytes(const std::uint8_t* bytes,
                                              std::size_t size) noexcept;

    [[nodiscard]] const ContentOfferSchedulerV1& scheduler() const noexcept
    {
        return scheduler_;
    }
    [[nodiscard]] bool stream_open() const noexcept { return stream_open_; }
    [[nodiscard]] std::uint32_t bulk_writes() const noexcept
    {
        return bulk_writes_;
    }

private:
    void queue_payload_frames() noexcept;
    fly_session_result_v2 consume_records() noexcept;

    ContentOfferSchedulerV1 scheduler_{};
    ContentOfferRoleV1 role_ = ContentOfferRoleV1::Offerer;
    fly_session_resource_handle_v2 connection_ = 0;
    fly_session_resource_handle_v2 send_stream_ = 0;
    fly_session_resource_handle_v2 receive_stream_ = 0;
    bool local_is_listener_ = false;
    bool want_stream_ = false;
    bool stream_open_ = false;
    bool open_outstanding_ = false;
    bool write_outstanding_ = false;
    bool read_outstanding_ = false;
    bool want_read_ = false;
    bool frames_queued_ = false;
    std::uint32_t bulk_writes_ = 0;
    std::vector<std::uint8_t> payload_{};
    std::deque<std::vector<std::uint8_t>> write_queue_{};
    std::vector<std::uint8_t> read_accumulator_{};
};

} // namespace flynes::session::content

#endif
