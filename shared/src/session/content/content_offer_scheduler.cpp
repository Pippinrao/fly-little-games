#include "content_offer_scheduler.hpp"

#include "../wire/sha256.hpp"

#include <cstring>

namespace flynes::session::content {
namespace {

bool offer_id_nonzero(const std::array<std::uint8_t, 16>& id) noexcept
{
    for (auto byte : id)
    {
        if (byte != 0)
            return true;
    }
    return false;
}

bool content_id_nonzero(const std::array<std::uint8_t, 32>& id) noexcept
{
    for (auto byte : id)
    {
        if (byte != 0)
            return true;
    }
    return false;
}

} // namespace

void ContentOfferSchedulerV1::reset(ContentOfferRoleV1 role) noexcept
{
    role_ = role;
    state_ = ContentOfferStateV1::Idle;
    fail_ = ContentOfferFailV1::None;
    decl_ = {};
    send_ok_ = false;
    receive_ok_ = false;
    import_ok_ = false;
    catalog_published_ = false;
    staging_live_ = false;
    transferred_ = 0;
    last_progress_ns_ = 0;
    clock_seen_ = false;
    staging_.clear();
}

fly_session_result_v2 ContentOfferSchedulerV1::bind_decl(
    const ContentOfferDeclV1& decl) noexcept
{
    if (state_ != ContentOfferStateV1::Idle)
        return FLY_SESSION_V2_INVALID_STATE;
    if (!offer_id_nonzero(decl.offer_id) || !content_id_nonzero(decl.content_id) ||
        decl.declared_length == 0)
    {
        fail(ContentOfferFailV1::Format);
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (decl.declared_length > kContentRomMaxBytesV1)
    {
        fail(ContentOfferFailV1::Oversize);
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    decl_ = decl;
    staging_.assign(decl_.declared_length, 0);
    transferred_ = 0;
    staging_live_ = false;
    state_ = ContentOfferStateV1::Offered;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::offer(
    const ContentOfferDeclV1& decl) noexcept
{
    if (role_ != ContentOfferRoleV1::Offerer)
        return FLY_SESSION_V2_INVALID_STATE;
    return bind_decl(decl);
}

fly_session_result_v2 ContentOfferSchedulerV1::accept_offer(
    const ContentOfferDeclV1& decl) noexcept
{
    if (role_ != ContentOfferRoleV1::Receiver)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto bound = bind_decl(decl);
    if (bound != FLY_SESSION_V2_OK)
        return bound;
    maybe_enter_transfer();
    return FLY_SESSION_V2_OK;
}

void ContentOfferSchedulerV1::maybe_enter_transfer() noexcept
{
    if (state_ != ContentOfferStateV1::Offered &&
        state_ != ContentOfferStateV1::WaitingConsent)
        return;
    if (send_ok_ && receive_ok_)
        state_ = ContentOfferStateV1::Transferring;
    else
        state_ = ContentOfferStateV1::WaitingConsent;
}

fly_session_result_v2 ContentOfferSchedulerV1::approve_send() noexcept
{
    if (state_ == ContentOfferStateV1::Failed ||
        state_ == ContentOfferStateV1::Cancelled)
        return FLY_SESSION_V2_INVALID_STATE;
    if (state_ == ContentOfferStateV1::Idle)
    {
        if (role_ != ContentOfferRoleV1::Receiver)
            return FLY_SESSION_V2_INVALID_STATE;
        send_ok_ = true;
        return FLY_SESSION_V2_OK;
    }
    send_ok_ = true;
    maybe_enter_transfer();
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::approve_receive() noexcept
{
    if (state_ == ContentOfferStateV1::Failed ||
        state_ == ContentOfferStateV1::Cancelled)
        return FLY_SESSION_V2_INVALID_STATE;
    if (state_ == ContentOfferStateV1::Idle)
    {
        if (role_ != ContentOfferRoleV1::Receiver)
            return FLY_SESSION_V2_INVALID_STATE;
        receive_ok_ = true;
        return FLY_SESSION_V2_OK;
    }
    receive_ok_ = true;
    maybe_enter_transfer();
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::approve_import() noexcept
{
    if (state_ == ContentOfferStateV1::Failed ||
        state_ == ContentOfferStateV1::Cancelled)
        return FLY_SESSION_V2_INVALID_STATE;
    if (state_ != ContentOfferStateV1::WaitingImport &&
        state_ != ContentOfferStateV1::Imported)
        return FLY_SESSION_V2_INVALID_STATE;
    import_ok_ = true;
    catalog_published_ = true;
    state_ = ContentOfferStateV1::Imported;
    discard_staging();
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::cancel() noexcept
{
    if (state_ == ContentOfferStateV1::Imported)
        return FLY_SESSION_V2_INVALID_STATE;
    fail_ = ContentOfferFailV1::Cancelled;
    state_ = ContentOfferStateV1::Cancelled;
    discard_staging();
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::note_friend_saved() noexcept
{
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::on_clock(
    std::uint64_t continuous_ns) noexcept
{
    if (state_ == ContentOfferStateV1::Failed ||
        state_ == ContentOfferStateV1::Cancelled ||
        state_ == ContentOfferStateV1::Imported)
        return FLY_SESSION_V2_OK;
    if (!clock_seen_)
    {
        last_progress_ns_ = continuous_ns;
        clock_seen_ = true;
        return FLY_SESSION_V2_OK;
    }
    if (state_ == ContentOfferStateV1::Transferring && staging_live_ &&
        continuous_ns >= last_progress_ns_ &&
        (continuous_ns - last_progress_ns_) >= kContentStallTimeoutNsV1)
    {
        fail(ContentOfferFailV1::StallTimeout);
        return FLY_SESSION_V2_TIMEOUT;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::append_bytes(
    std::uint32_t offset, const std::uint8_t* data, std::size_t size) noexcept
{
    if (!may_open_rom_stream())
        return FLY_SESSION_V2_UNAVAILABLE;
    if (data == nullptr && size != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (size > kContentRomMaxBytesV1)
    {
        fail(ContentOfferFailV1::Oversize);
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    const auto end = static_cast<std::uint64_t>(offset) + size;
    if (end > decl_.declared_length)
    {
        fail(ContentOfferFailV1::OffsetOutOfRange);
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    if (staging_.size() != decl_.declared_length)
        staging_.assign(decl_.declared_length, 0);
    if (size != 0)
        std::memcpy(staging_.data() + offset, data, size);
    if (end > transferred_)
        transferred_ = static_cast<std::uint32_t>(end);
    staging_live_ = true;
    maybe_finish_check();
    if (state_ == ContentOfferStateV1::Failed)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentOfferSchedulerV1::note_disk_full() noexcept
{
    if (state_ == ContentOfferStateV1::Idle)
        return FLY_SESSION_V2_INVALID_STATE;
    fail(ContentOfferFailV1::DiskFull);
    return FLY_SESSION_V2_UNAVAILABLE;
}

fly_session_result_v2 ContentOfferSchedulerV1::note_multi_payload() noexcept
{
    if (state_ == ContentOfferStateV1::Idle)
        return FLY_SESSION_V2_INVALID_STATE;
    fail(ContentOfferFailV1::MultiPayload);
    return FLY_SESSION_V2_INVALID_ARGUMENT;
}

fly_session_result_v2 ContentOfferSchedulerV1::note_format_invalid() noexcept
{
    if (state_ == ContentOfferStateV1::Idle)
        return FLY_SESSION_V2_INVALID_STATE;
    fail(ContentOfferFailV1::Format);
    return FLY_SESSION_V2_INVALID_ARGUMENT;
}

bool ContentOfferSchedulerV1::may_open_rom_stream() const noexcept
{
    if (!send_ok_ || !receive_ok_)
        return false;
    if (role_ == ContentOfferRoleV1::Receiver)
        return true;
    return state_ == ContentOfferStateV1::Transferring ||
           state_ == ContentOfferStateV1::Checking ||
           state_ == ContentOfferStateV1::WaitingImport;
}

void ContentOfferSchedulerV1::fail(ContentOfferFailV1 reason) noexcept
{
    fail_ = reason;
    state_ = ContentOfferStateV1::Failed;
    catalog_published_ = false;
    import_ok_ = false;
    discard_staging();
}

void ContentOfferSchedulerV1::discard_staging() noexcept
{
    staging_.clear();
    staging_live_ = false;
    transferred_ = 0;
}

void ContentOfferSchedulerV1::maybe_finish_check() noexcept
{
    if (state_ != ContentOfferStateV1::Transferring)
        return;
    if (transferred_ < decl_.declared_length)
        return;
    state_ = ContentOfferStateV1::Checking;
    const auto got =
        wire::sha256(staging_.data(), static_cast<std::size_t>(decl_.declared_length));
    if (got != decl_.payload_sha256)
    {
        fail(ContentOfferFailV1::HashMismatch);
        return;
    }
    state_ = ContentOfferStateV1::WaitingImport;
}

} // namespace flynes::session::content
