#ifndef FLYNES_SESSION_CONTENT_CONTENT_OFFER_SCHEDULER_HPP
#define FLYNES_SESSION_CONTENT_CONTENT_OFFER_SCHEDULER_HPP

/*
 * Task 11 / CONTENT-DUAL: offer, permission, transfer, check, import.
 *
 * Send, receive and import consents are independent. Saving a friend does
 * not grant any of them. The ROM bulk stream may open only after send AND
 * receive match. Hash/format success does not import; APPROVE_IMPORT does.
 * STREAM is deferred: a reject or failed transfer leaves DUAL unavailable.
 */

#include "flynes/flynes_session.h"

#include <array>
#include <cstdint>
#include <vector>

namespace flynes::session::content {

inline constexpr std::uint32_t kContentRomMaxBytesV1 = 8u * 1024u * 1024u;
inline constexpr std::uint64_t kContentStallTimeoutNsV1 =
    10ull * 1000ull * 1000ull * 1000ull;

enum class ContentOfferRoleV1 : std::uint8_t
{
    Offerer = 1,
    Receiver = 2
};

enum class ContentOfferStateV1 : std::uint8_t
{
    Idle = 0,
    Offered = 1,
    WaitingConsent = 2,
    Transferring = 3,
    Checking = 4,
    WaitingImport = 5,
    Imported = 6,
    Failed = 7,
    Cancelled = 8
};

enum class ContentOfferFailV1 : std::uint8_t
{
    None = 0,
    Oversize = 1,
    HashMismatch = 2,
    OffsetOutOfRange = 3,
    StallTimeout = 4,
    Cancelled = 5,
    DiskFull = 6,
    MultiPayload = 7,
    Format = 8
};

struct ContentOfferDeclV1 final
{
    std::array<std::uint8_t, 16> offer_id{};
    std::array<std::uint8_t, 32> content_id{};
    std::uint32_t declared_length = 0;
    std::array<std::uint8_t, 32> payload_sha256{};
};

class ContentOfferSchedulerV1 final
{
public:
    ContentOfferSchedulerV1() = default;
    ContentOfferSchedulerV1(const ContentOfferSchedulerV1&) = delete;
    ContentOfferSchedulerV1& operator=(const ContentOfferSchedulerV1&) = delete;

    void reset(ContentOfferRoleV1 role) noexcept;
    fly_session_result_v2 offer(const ContentOfferDeclV1& decl) noexcept;
    fly_session_result_v2 accept_offer(const ContentOfferDeclV1& decl) noexcept;
    fly_session_result_v2 approve_send() noexcept;
    fly_session_result_v2 approve_receive() noexcept;
    fly_session_result_v2 approve_import() noexcept;
    fly_session_result_v2 cancel() noexcept;
    fly_session_result_v2 note_friend_saved() noexcept;
    fly_session_result_v2 on_clock(std::uint64_t continuous_ns) noexcept;
    fly_session_result_v2 append_bytes(std::uint32_t offset,
                                       const std::uint8_t* data,
                                       std::size_t size) noexcept;
    fly_session_result_v2 note_disk_full() noexcept;
    fly_session_result_v2 note_multi_payload() noexcept;
    fly_session_result_v2 note_format_invalid() noexcept;

    [[nodiscard]] bool send_consented() const noexcept { return send_ok_; }
    [[nodiscard]] bool receive_consented() const noexcept { return receive_ok_; }
    [[nodiscard]] bool import_consented() const noexcept { return import_ok_; }
    [[nodiscard]] bool may_open_rom_stream() const noexcept;
    [[nodiscard]] bool catalog_published() const noexcept
    {
        return catalog_published_;
    }
    [[nodiscard]] std::uint32_t transferred_bytes() const noexcept
    {
        return transferred_;
    }
    [[nodiscard]] bool staging_live() const noexcept { return staging_live_; }
    [[nodiscard]] bool dual_available() const noexcept
    {
        return catalog_published_;
    }
    [[nodiscard]] ContentOfferStateV1 state() const noexcept { return state_; }
    [[nodiscard]] ContentOfferFailV1 fail_reason() const noexcept
    {
        return fail_;
    }
    [[nodiscard]] const ContentOfferDeclV1& decl() const noexcept
    {
        return decl_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& staging() const noexcept
    {
        return staging_;
    }

private:
    fly_session_result_v2 bind_decl(const ContentOfferDeclV1& decl) noexcept;
    void fail(ContentOfferFailV1 reason) noexcept;
    void discard_staging() noexcept;
    void maybe_enter_transfer() noexcept;
    void maybe_finish_check() noexcept;

    ContentOfferRoleV1 role_ = ContentOfferRoleV1::Offerer;
    ContentOfferStateV1 state_ = ContentOfferStateV1::Idle;
    ContentOfferFailV1 fail_ = ContentOfferFailV1::None;
    ContentOfferDeclV1 decl_{};
    bool send_ok_ = false;
    bool receive_ok_ = false;
    bool import_ok_ = false;
    bool catalog_published_ = false;
    bool staging_live_ = false;
    std::uint32_t transferred_ = 0;
    std::uint64_t last_progress_ns_ = 0;
    bool clock_seen_ = false;
    std::vector<std::uint8_t> staging_{};
};

} // namespace flynes::session::content

#endif
