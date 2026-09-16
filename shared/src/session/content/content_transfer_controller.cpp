#include "content_transfer_controller.hpp"

#include "../wire/pair_handshake.hpp"

#include <cstring>

namespace flynes::session::content {
namespace wire = flynes::session::wire;
namespace {

constexpr std::uint64_t kRomReadCredit = 65536;

std::uint32_t load_u32be(const std::uint8_t* p) noexcept
{
    return (static_cast<std::uint32_t>(p[0]) << 24u) |
           (static_cast<std::uint32_t>(p[1]) << 16u) |
           (static_cast<std::uint32_t>(p[2]) << 8u) |
           static_cast<std::uint32_t>(p[3]);
}

} // namespace

void ContentTransferControllerV1::reset(ContentOfferRoleV1 role) noexcept
{
    scheduler_.reset(role);
    role_ = role;
    want_stream_ = false;
    stream_open_ = false;
    open_outstanding_ = false;
    write_outstanding_ = false;
    read_outstanding_ = false;
    want_read_ = false;
    frames_queued_ = false;
    bulk_writes_ = 0;
    payload_.clear();
    write_queue_.clear();
    read_accumulator_.clear();
}

void ContentTransferControllerV1::attach_connection(
    fly_session_resource_handle_v2 connection, bool local_is_listener) noexcept
{
    connection_ = connection;
    local_is_listener_ = local_is_listener;
}

fly_session_result_v2 ContentTransferControllerV1::offer(
    const ContentOfferDeclV1& decl, const std::uint8_t* payload,
    std::size_t payload_size) noexcept
{
    if (payload == nullptr && payload_size != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (payload_size != decl.declared_length)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto bound = scheduler_.offer(decl);
    if (bound != FLY_SESSION_V2_OK)
        return bound;
    try
    {
        payload_.assign(payload, payload + payload_size);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentTransferControllerV1::accept_offer(
    const ContentOfferDeclV1& decl) noexcept
{
    return scheduler_.accept_offer(decl);
}

fly_session_result_v2 ContentTransferControllerV1::approve_send() noexcept
{
    const auto result = scheduler_.approve_send();
    if (result == FLY_SESSION_V2_OK && scheduler_.may_open_rom_stream())
        want_stream_ = true;
    return result;
}

fly_session_result_v2 ContentTransferControllerV1::approve_receive() noexcept
{
    const auto result = scheduler_.approve_receive();
    if (result == FLY_SESSION_V2_OK && scheduler_.may_open_rom_stream())
        want_stream_ = true;
    return result;
}

fly_session_result_v2 ContentTransferControllerV1::approve_import() noexcept
{
    return scheduler_.approve_import();
}

fly_session_result_v2 ContentTransferControllerV1::cancel() noexcept
{
    want_stream_ = false;
    write_queue_.clear();
    payload_.clear();
    return scheduler_.cancel();
}

fly_session_result_v2 ContentTransferControllerV1::note_friend_saved() noexcept
{
    return scheduler_.note_friend_saved();
}

fly_session_result_v2 ContentTransferControllerV1::on_clock(
    std::uint64_t continuous_ns) noexcept
{
    return scheduler_.on_clock(continuous_ns);
}

void ContentTransferControllerV1::queue_payload_frames() noexcept
{
    if (frames_queued_ || role_ != ContentOfferRoleV1::Offerer)
        return;
    std::vector<std::uint8_t> offer;
    if (!encode_content_offer_v1(scheduler_.decl(), &offer))
        return;
    write_queue_.push_back(std::move(offer));
    std::uint32_t offset = 0;
    while (offset < payload_.size())
    {
        const auto remaining = payload_.size() - offset;
        const auto slice = remaining > kContentChunkMaxPayloadV1
                               ? kContentChunkMaxPayloadV1
                               : remaining;
        std::vector<std::uint8_t> chunk;
        if (!encode_content_chunk_v1(scheduler_.decl().offer_id, offset,
                                     payload_.data() + offset, slice, &chunk))
            return;
        write_queue_.push_back(std::move(chunk));
        offset += static_cast<std::uint32_t>(slice);
    }
    frames_queued_ = true;
}

std::optional<ContentTransferControllerV1::Effect>
ContentTransferControllerV1::poll_effect()
{
    if (scheduler_.state() == ContentOfferStateV1::Failed ||
        scheduler_.state() == ContentOfferStateV1::Cancelled)
        return std::nullopt;
    if (want_stream_ && !stream_open_ && !open_outstanding_ &&
        connection_ != 0)
    {
        Effect effect{};
        effect.kind = EffectKind::OpenStream;
        effect.accept = local_is_listener_;
        effect.opener_role = static_cast<std::uint32_t>(
            local_is_listener_ ? wire::PairRoleV1::Responder
                               : wire::PairRoleV1::Initiator);
        effect.connection = connection_;
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_STREAM_V2;
        open_outstanding_ = true;
        return effect;
    }
    if (stream_open_ && role_ == ContentOfferRoleV1::Offerer &&
        !frames_queued_ && scheduler_.may_open_rom_stream())
        queue_payload_frames();
    if (stream_open_ && !write_outstanding_ && !write_queue_.empty())
    {
        Effect effect{};
        effect.kind = EffectKind::Write;
        effect.stream = send_stream_;
        effect.bytes = write_queue_.front();
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_END_V2;
        write_outstanding_ = true;
        return effect;
    }
    if (stream_open_ && want_read_ && !read_outstanding_ &&
        receive_stream_ != 0)
    {
        Effect effect{};
        effect.kind = EffectKind::GrantRead;
        effect.stream = receive_stream_;
        effect.read_credit = kRomReadCredit;
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
        read_outstanding_ = true;
        return effect;
    }
    return std::nullopt;
}

fly_session_result_v2 ContentTransferControllerV1::on_stream_open(
    fly_session_resource_handle_v2 send,
    fly_session_resource_handle_v2 recv) noexcept
{
    open_outstanding_ = false;
    if (send == 0 || recv == 0)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    send_stream_ = send;
    receive_stream_ = recv;
    stream_open_ = true;
    if (role_ == ContentOfferRoleV1::Receiver)
        want_read_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentTransferControllerV1::on_write_complete() noexcept
{
    write_outstanding_ = false;
    if (!write_queue_.empty())
        write_queue_.pop_front();
    ++bulk_writes_;
    want_read_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentTransferControllerV1::ingest_remote_bytes(
    const std::uint8_t* bytes, std::size_t size) noexcept
{
    read_outstanding_ = false;
    want_read_ = false;
    if (bytes == nullptr && size != 0)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    try
    {
        read_accumulator_.insert(read_accumulator_.end(), bytes, bytes + size);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    const auto consumed = consume_records();
    if (consumed != FLY_SESSION_V2_OK)
        return consumed;
    const auto state = scheduler_.state();
    if (state != ContentOfferStateV1::WaitingImport &&
        state != ContentOfferStateV1::Imported &&
        state != ContentOfferStateV1::Failed &&
        state != ContentOfferStateV1::Cancelled)
        want_read_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 ContentTransferControllerV1::consume_records() noexcept
{
    while (read_accumulator_.size() >= 5u)
    {
        const auto body_len = load_u32be(read_accumulator_.data());
        const auto total = 4u + static_cast<std::size_t>(body_len);
        if (body_len < 1u || total > 65536u)
            return FLY_SESSION_V2_PROTOCOL_VIOLATION;
        if (read_accumulator_.size() < total)
            return FLY_SESSION_V2_OK;
        const auto type = read_accumulator_[4];
        const auto* body = read_accumulator_.data() + 5;
        const auto body_size = static_cast<std::size_t>(body_len) - 1u;
        if (type == kContentOfferTypeV1)
        {
            ContentOfferDeclV1 decl{};
            if (!decode_content_offer_v1(body, body_size, &decl))
                return FLY_SESSION_V2_PROTOCOL_VIOLATION;
            if (role_ == ContentOfferRoleV1::Receiver &&
                scheduler_.state() == ContentOfferStateV1::Idle)
            {
                const auto accepted = scheduler_.accept_offer(decl);
                if (accepted != FLY_SESSION_V2_OK)
                    return accepted;
            }
        }
        else if (type == kContentChunkTypeV1)
        {
            std::array<std::uint8_t, 16> offer_id{};
            std::uint32_t offset = 0;
            const std::uint8_t* payload = nullptr;
            std::size_t payload_size = 0;
            if (!decode_content_chunk_v1(body, body_size, &offer_id, &offset,
                                         &payload, &payload_size))
                return FLY_SESSION_V2_PROTOCOL_VIOLATION;
            if (std::memcmp(offer_id.data(), scheduler_.decl().offer_id.data(),
                            16) != 0)
                return FLY_SESSION_V2_PROTOCOL_VIOLATION;
            const auto appended =
                scheduler_.append_bytes(offset, payload, payload_size);
            if (appended != FLY_SESSION_V2_OK)
                return appended;
        }
        else
        {
            return FLY_SESSION_V2_PROTOCOL_VIOLATION;
        }
        read_accumulator_.erase(read_accumulator_.begin(),
                                read_accumulator_.begin() +
                                    static_cast<std::ptrdiff_t>(total));
    }
    return FLY_SESSION_V2_OK;
}

} // namespace flynes::session::content
