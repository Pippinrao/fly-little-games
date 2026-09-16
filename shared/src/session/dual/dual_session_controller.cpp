#include "dual_session_controller.hpp"

#include "../wire/app_frame.hpp"
#include "../wire/sha256.hpp"

#include <algorithm>
#include <cstring>
#include <new>

namespace flynes::session::dual {
namespace {

constexpr char kContentChoiceDomain[] = "flynes-content-choice-v1";
constexpr char kOwnerKeyDomain[] = "flynes-dual-owner-key-v1";
constexpr std::uint16_t kCanonicalInputTag = 0xFF02u;
constexpr std::uint64_t kStateCommitReadCredit = 256u;
constexpr std::size_t kFramedInputBytes =
    6u + kDualInputWireBytesV1;

std::uint16_t load_u16be(const std::uint8_t* bytes) noexcept
{
    return static_cast<std::uint16_t>(
        (static_cast<std::uint16_t>(bytes[0]) << 8u) | bytes[1]);
}

std::uint32_t load_u32be(const std::uint8_t* bytes) noexcept
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24u) |
           (static_cast<std::uint32_t>(bytes[1]) << 16u) |
           (static_cast<std::uint32_t>(bytes[2]) << 8u) |
           static_cast<std::uint32_t>(bytes[3]);
}

fly_session_result_v2 read_parsed_buffer(const ParsedProviderEvent& parsed,
                                         std::vector<std::uint8_t>* out)
{
    if (out == nullptr || parsed.buffer == nullptr)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    std::uint64_t size = 0;
    if (fly_session_buffer_size_v2(parsed.buffer, &size) != FLY_SESSION_V2_OK)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try
    {
        out->assign(static_cast<std::size_t>(size), 0);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    if (size == 0)
        return FLY_SESSION_V2_OK;
    fly_session_write_bytes_v2 destination{out->data(), size};
    std::uint64_t written = 0;
    if (fly_session_buffer_read_v2(parsed.buffer, 0, destination, &written) !=
            FLY_SESSION_V2_OK ||
        written != size)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    return FLY_SESSION_V2_OK;
}

} // namespace

std::array<std::uint8_t, 32> DualSessionController::owner_key(
    const std::array<std::uint8_t, 65>& public_key) noexcept
{
    return wire::domain_hash(kOwnerKeyDomain, public_key.data(),
                             public_key.size());
}

void DualSessionController::begin_catalog() noexcept
{
    catalog_started_ = true;
    catalog_complete_ = false;
    catalog_query_outstanding_ = false;
    catalog_index_ = 0;
    choices_.clear();
    view_dirty_ = true;
}

fly_session_result_v2 DualSessionController::on_content_empty() noexcept
{
    catalog_query_outstanding_ = false;
    catalog_complete_ = true;
    view_dirty_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::on_content_choice(
    const ParsedProviderEvent& parsed) noexcept
{
    catalog_query_outstanding_ = false;
    std::vector<std::uint8_t> record;
    const auto read = read_parsed_buffer(parsed, &record);
    if (read != FLY_SESSION_V2_OK)
        return read;
    const auto accepted =
        parse_choice_record(record.data(), record.size(), parsed.hash);
    if (accepted != FLY_SESSION_V2_OK)
        return accepted;
    ++catalog_index_;
    view_dirty_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::parse_choice_record(
    const std::uint8_t* bytes, std::size_t size,
    const std::array<std::uint8_t, 32>& expected_hash) noexcept
{
    if (bytes == nullptr || size < FLY_SESSION_CONTENT_CHOICE_V2_HEADER_SIZE)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;
    const auto hash =
        wire::domain_hash(kContentChoiceDomain, bytes, size);
    if (hash != expected_hash)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;
    if (load_u16be(bytes) != 1u || bytes[2] != 0 || bytes[3] != 0)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;
    const auto name_size = load_u32be(bytes + 52);
    if (name_size == 0 || name_size > FLY_SESSION_CONTENT_CHOICE_V2_MAX_NAME ||
        size != FLY_SESSION_CONTENT_CHOICE_V2_HEADER_SIZE + name_size)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;

    fly_session_game_choice_v2 choice{};
    choice.struct_size = FLY_SESSION_GAME_CHOICE_V2_SIZE;
    choice.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::memcpy(choice.source_choice_ref, bytes + 4, 16u);
    std::memcpy(choice.content_id, bytes + 20, 32u);
    choice.catalog_revision = 1;
    choice.progress_revision = 1;
    choice.selectable = 1;
    choice.display_name_size = name_size;
    std::memcpy(choice.display_name, bytes + 56, name_size);
    try
    {
        choices_.push_back(choice);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::select_content(
    const std::uint8_t choice_id[16]) noexcept
{
    if (choice_id == nullptr || !catalog_complete_ || frozen_)
        return FLY_SESSION_V2_INVALID_STATE;
    for (const auto& choice : choices_)
    {
        if (std::memcmp(choice.source_choice_ref, choice_id, 16u) == 0)
        {
            std::memcpy(selected_ref_.data(), choice.source_choice_ref, 16u);
            std::memcpy(selected_content_.data(), choice.content_id, 32u);
            selected_ = true;
            view_dirty_ = true;
            return FLY_SESSION_V2_OK;
        }
    }
    return FLY_SESSION_V2_INVALID_ARGUMENT;
}

fly_session_result_v2 DualSessionController::publish_imported_choice(
    const fly_session_game_choice_v2& choice) noexcept
{
    try
    {
        choices_.push_back(choice);
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    catalog_complete_ = true;
    view_dirty_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::start_dual(
    const DualStartInputsV1& inputs) noexcept
{
    if (!selected_ || running_ || frozen_)
        return FLY_SESSION_V2_INVALID_STATE;
    if (inputs.runtime == nullptr || inputs.quic_connection == 0)
        return FLY_SESSION_V2_UNAVAILABLE;

    DualInputKeyV1 key{};
    key.session_id = inputs.session_id;
    key.branch_id = inputs.branch_id;
    key.timeline_epoch = 1;
    key.seat_revision = 1;
    if (!dual_input_key_is_valid_v1(key))
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    const bool initiator =
        inputs.local_role == wire::PairRoleV1::Initiator;
    const auto initiator_key = owner_key(
        initiator ? inputs.local_signing_public : inputs.peer_signing_public);
    const auto responder_key = owner_key(
        initiator ? inputs.peer_signing_public : inputs.local_signing_public);
    std::array<DualOwnerKeyV1, kDualPortCountV1> owners{};
    owners[0].seat = 0;
    owners[0].signing_key_id = initiator_key;
    owners[1].seat = 1;
    owners[1].signing_key_id = responder_key;
    owners[2].seat = 2;
    owners[3].seat = 3;

    DualContentRefV1 content{};
    content.session_id = inputs.session_id;
    content.branch_id = inputs.branch_id;
    content.content_hash = selected_content_;
    content.timeline_epoch = 1;
    for (const auto byte : content.content_hash)
    {
        if (byte != 0)
            goto content_ok;
    }
    return FLY_SESSION_V2_INVALID_ARGUMENT;
content_ok:

    try
    {
        adapter_ = std::make_unique<CAbiDualRuntimePortV1>(inputs.runtime);
        scheduler_ = std::make_unique<DualRunSchedulerV1>(*adapter_);
    }
    catch (const std::bad_alloc&)
    {
        adapter_.reset();
        scheduler_.reset();
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    const auto began = scheduler_->begin(DualModeV1::Dual, content, key, owners);
    if (began != FLY_SESSION_V2_OK)
    {
        scheduler_.reset();
        adapter_.reset();
        return began;
    }

    context_ = key;
    content_ = content;
    owners_ = owners;
    local_seat_ = initiator ? 0 : 1;
    local_sequence_ = 1;
    batch_sequence_ = 1;
    last_digest_ = {};
    connection_ = inputs.quic_connection;
    local_is_listener_ = inputs.local_is_listener;
    want_stream_ = true;
    stream_open_ = false;
    want_read_ = false;
    running_ = true;
    paused_ = false;
    frozen_ = false;
    freeze_reason_ = FLY_SESSION_DUAL_FREEZE_NONE_V2;
    view_dirty_ = true;
    watchdog_.reset(last_clock_ns_);
    return FLY_SESSION_V2_OK;
}

void DualSessionController::on_clock(std::uint64_t continuous_ns) noexcept
{
    last_clock_ns_ = continuous_ns;
    if (!running_ && !frozen_ && !paused_)
        return;
    const auto state = watchdog_.on_clock(continuous_ns);
    if (state == flynes::session::recovery::LinkActivityStateV1::Frozen &&
        !frozen_ && !paused_)
        mark_frozen(FLY_SESSION_DUAL_FREEZE_AUTHENTICATED_ACTIVITY_TIMEOUT_V2);
    else if (state ==
             flynes::session::recovery::LinkActivityStateV1::ReconnectExpired)
        mark_frozen(FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2);
}

fly_session_result_v2 DualSessionController::queue_local_bundle(
    const DualPortInputArrayV1& samples) noexcept
{
    DualInputKeyV1 key = context_;
    key.frame_index = scheduler_->next_frame();
    DualInputBundleV1 bundle{};
    if (canonical_input_build_v1(key, local_seat_,
                                 owners_[local_seat_].signing_key_id, samples,
                                 &bundle) != DualInputStatusV1::Ok)
        return FLY_SESSION_V2_INVALID_ARGUMENT;

    DualInputAdmitV1 admit = DualInputAdmitV1::RejectedFrozen;
    const auto accepted =
        scheduler_->accept_local_input(samples, local_seat_, &admit);
    if (accepted != FLY_SESSION_V2_OK && accepted != FLY_SESSION_V2_ACCEPTED)
        return accepted;
    if (admit != DualInputAdmitV1::Accepted &&
        admit != DualInputAdmitV1::Duplicate)
        return FLY_SESSION_V2_INVALID_STATE;

    DualInputWireContextV1 wire_context{};
    wire_context.authority_term = 1;
    wire_context.mode_generation = 1;
    wire_context.batch_sequence = batch_sequence_;
    if (!checked_dual_next_sequence_v1(batch_sequence_, &batch_sequence_))
        return FLY_SESSION_V2_INVALID_STATE;

    std::uint8_t object[kDualInputWireBytesV1] = {};
    if (encode_dual_input_bundle_v1(bundle, wire_context, object) !=
        DualInputWireStatusV1::Ok)
        return FLY_SESSION_V2_CONTRACT_VIOLATION;

    std::vector<std::uint8_t> framed(kFramedInputBytes, 0);
    std::size_t written = 0;
    if (wire::encode_app_frame(kCanonicalInputTag, object, kDualInputWireBytesV1,
                               framed.data(), framed.size(), &written) !=
            wire::Status::Ok ||
        written != framed.size())
        return FLY_SESSION_V2_CONTRACT_VIOLATION;
    try
    {
        write_queue_.push_back(std::move(framed));
    }
    catch (const std::bad_alloc&)
    {
        return FLY_SESSION_V2_OUT_OF_MEMORY;
    }
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::submit_local(
    const fly_session_input_v2& input) noexcept
{
    if (!running_ || frozen_ || paused_ || !scheduler_)
        return FLY_SESSION_V2_INVALID_STATE;
    if (input.expected_seat_revision != context_.seat_revision)
        return FLY_SESSION_V2_STALE;

    std::uint32_t mask = input.buttons;
    if (input.struct_size >= FLY_SESSION_INPUT_V2_SIZE)
        mask = input.port_mask[local_seat_];
    mask = normalize_dual_port_mask_v1(mask);

    DualPortInputArrayV1 samples{};
    const auto base = local_sequence_;
    if (base == 0 || base > UINT64_MAX - 4u)
        return FLY_SESSION_V2_INVALID_STATE;
    for (std::uint32_t port = 0; port < kDualPortCountV1; ++port)
    {
        samples[port].sequence = base + port;
        samples[port].mask = (port == local_seat_) ? mask : 0u;
    }
    local_sequence_ = base + kDualPortCountV1;
    const auto queued = queue_local_bundle(samples);
    if (queued != FLY_SESSION_V2_OK)
        return queued;
    return try_step();
}

fly_session_result_v2 DualSessionController::pause() noexcept
{
    if (!running_ || frozen_ || !scheduler_)
        return FLY_SESSION_V2_INVALID_STATE;
    const auto result = scheduler_->pause(1);
    if (result != FLY_SESSION_V2_OK)
        return result;
    paused_ = true;
    freeze_reason_ = FLY_SESSION_DUAL_FREEZE_PAUSED_V2;
    view_dirty_ = true;
    return FLY_SESSION_V2_OK;
}

fly_session_result_v2 DualSessionController::disconnect() noexcept
{
    if (scheduler_)
        scheduler_->mark_transport_terminal();
    mark_frozen(FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2);
    return FLY_SESSION_V2_OK;
}

void DualSessionController::mark_frozen(std::uint32_t reason) noexcept
{
    frozen_ = true;
    running_ = false;
    if (freeze_reason_ == FLY_SESSION_DUAL_FREEZE_NONE_V2 ||
        (reason == FLY_SESSION_DUAL_FREEZE_TRANSPORT_TERMINAL_V2 &&
         freeze_reason_ ==
             FLY_SESSION_DUAL_FREEZE_AUTHENTICATED_ACTIVITY_TIMEOUT_V2))
        freeze_reason_ = reason;
    view_dirty_ = true;
}

std::optional<DualSessionController::Effect> DualSessionController::poll_effect()
{
    if (frozen_)
        return std::nullopt;
    if (catalog_started_ && !catalog_complete_ && !catalog_query_outstanding_)
    {
        Effect effect{};
        effect.kind = EffectKind::QueryContent;
        effect.content_index = catalog_index_;
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2;
        catalog_query_outstanding_ = true;
        return effect;
    }
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
    if (stream_open_ && want_read_ && !read_outstanding_ && receive_stream_ != 0)
    {
        Effect effect{};
        effect.kind = EffectKind::GrantRead;
        effect.stream = receive_stream_;
        effect.read_credit = kStateCommitReadCredit;
        effect.expected_payload_kind = FLY_SESSION_PROVIDER_QUIC_DATA_V2;
        read_outstanding_ = true;
        return effect;
    }
    return std::nullopt;
}

fly_session_result_v2 DualSessionController::complete(
    EffectKind kind, const fly_session_port_event_v2& event,
    const ParsedProviderEvent& parsed) noexcept
{
    if (kind == EffectKind::QueryContent)
    {
        if (event.result != FLY_SESSION_V2_OK)
            return event.result;
        return on_content_choice(parsed);
    }
    if (kind == EffectKind::OpenStream)
    {
        open_outstanding_ = false;
        if (event.result != FLY_SESSION_V2_OK)
            return event.result;
        if (parsed.resource == 0 || parsed.value0 == 0)
            return FLY_SESSION_V2_CONTRACT_VIOLATION;
        send_stream_ = parsed.resource;
        receive_stream_ = static_cast<fly_session_resource_handle_v2>(
            parsed.value0);
        stream_open_ = true;
        return FLY_SESSION_V2_OK;
    }
    if (kind == EffectKind::Write)
    {
        write_outstanding_ = false;
        if (event.result != FLY_SESSION_V2_OK)
            return event.result;
        if (!write_queue_.empty())
            write_queue_.pop_front();
        want_read_ = true;
        watchdog_.on_local_send_complete(last_clock_ns_);
        return FLY_SESSION_V2_OK;
    }
    if (kind == EffectKind::GrantRead)
    {
        read_outstanding_ = false;
        want_read_ = false;
        if (event.result != FLY_SESSION_V2_OK)
            return event.result;
        std::vector<std::uint8_t> received;
        const auto read = read_parsed_buffer(parsed, &received);
        if (read != FLY_SESSION_V2_OK)
            return read;
        return ingest_remote_bytes(received.data(), received.size());
    }
    return FLY_SESSION_V2_INVALID_STATE;
}

fly_session_result_v2 DualSessionController::ingest_remote_bytes(
    const std::uint8_t* bytes, std::size_t size) noexcept
{
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
    if (read_accumulator_.size() > 2u * kFramedInputBytes)
        return FLY_SESSION_V2_PROTOCOL_VIOLATION;

    while (true)
    {
        wire::AppFrameCursor cursor = wire::app_frame_cursor(
            wire::QuicChannel::StateCommit, read_accumulator_.data(),
            read_accumulator_.size());
        wire::AppFrame frame{};
        bool has_frame = false;
        const auto status = wire::next_app_frame(&cursor, &frame, &has_frame);
        if (status == wire::Status::Truncated ||
            (status == wire::Status::Ok && !has_frame))
            return FLY_SESSION_V2_OK;
        if (status != wire::Status::Ok || !has_frame)
            return FLY_SESSION_V2_PROTOCOL_VIOLATION;
        if (frame.frame_type_tag != kCanonicalInputTag)
            return FLY_SESSION_V2_PROTOCOL_VIOLATION;

        const std::uint8_t peer_seat = static_cast<std::uint8_t>(1u - local_seat_);
        DualInputBundleV1 bundle{};
        if (decode_dual_input_bundle_v1(
                frame.object_bytes, frame.object_size, peer_seat,
                owners_[peer_seat].signing_key_id, &bundle) !=
            DualInputWireStatusV1::Ok)
            return FLY_SESSION_V2_PROTOCOL_VIOLATION;
        watchdog_.on_verified_peer_activity(last_clock_ns_);
        DualInputAdmitV1 admit = DualInputAdmitV1::RejectedFrozen;
        const auto accepted =
            scheduler_->accept_remote_input(bundle, &admit);
        if (accepted != FLY_SESSION_V2_OK &&
            accepted != FLY_SESSION_V2_ACCEPTED &&
            admit != DualInputAdmitV1::Duplicate)
            return accepted;

        read_accumulator_.erase(read_accumulator_.begin(),
                                read_accumulator_.begin() +
                                    static_cast<std::ptrdiff_t>(cursor.offset));
        const auto stepped = try_step();
        if (stepped != FLY_SESSION_V2_OK)
            return stepped;
    }
}

fly_session_result_v2 DualSessionController::try_step() noexcept
{
    if (!scheduler_ || frozen_ || paused_)
        return FLY_SESSION_V2_OK;
    while (scheduler_->state() == DualSimStateV1::Running)
    {
        const auto frame = scheduler_->next_frame();
        if (!scheduler_->window().frame_all_real(frame))
            break;
        const auto stepped = scheduler_->step_next_frame();
        if (stepped != FLY_SESSION_V2_OK)
        {
            mark_frozen(static_cast<std::uint32_t>(scheduler_->freeze_reason()));
            return stepped;
        }
        last_digest_ = scheduler_->last_digest();
        view_dirty_ = true;
    }
    if (scheduler_->is_frozen())
        mark_frozen(static_cast<std::uint32_t>(scheduler_->freeze_reason()));
    return FLY_SESSION_V2_OK;
}

std::uint32_t DualSessionController::game_state() const noexcept
{
    if (frozen_ || paused_)
        return FLY_SESSION_GAME_FROZEN_V2;
    if (running_)
        return FLY_SESSION_GAME_RUNNING_V2;
    return FLY_SESSION_GAME_NOT_STARTED_V2;
}

std::uint32_t DualSessionController::freeze_reason() const noexcept
{
    if (paused_ && !frozen_)
        return FLY_SESSION_DUAL_FREEZE_PAUSED_V2;
    return freeze_reason_;
}

void DualSessionController::fill_snapshot(
    fly_session_snapshot_v2& snapshot) const noexcept
{
    snapshot.game_state = game_state();
    snapshot.dual_mode = 0;
    if (running_ || paused_ || frozen_)
        snapshot.dual_mode = FLY_SESSION_DUAL_MODE_DUAL_V2;
    snapshot.dual_state =
        frozen_ || paused_ ? FLY_SESSION_DUAL_FROZEN_V2
        : running_         ? FLY_SESSION_DUAL_RUNNING_V2
                           : FLY_SESSION_DUAL_UNLOADED_V2;
    snapshot.dual_freeze_reason = freeze_reason();
    snapshot.dual_local_seat = local_seat_;
    snapshot.dual_authority_seat = 0;
    snapshot.dual_seats_confirmed = selected_ ? 1u : 0u;
    snapshot.dual_seat_revision = context_.seat_revision;
    snapshot.dual_frame_index = scheduler_ ? scheduler_->current_frame() : 0;
    snapshot.dual_verified_through =
        scheduler_ ? scheduler_->state_verified_through() : 0;
    snapshot.dual_commit_frontier =
        scheduler_ ? scheduler_->commit_frontier() : 0;
    snapshot.dual_prediction_depth =
        scheduler_ ? scheduler_->prediction_depth() : 0;
    std::memcpy(snapshot.dual_content_hash, selected_content_.data(), 32u);
    std::memcpy(snapshot.dual_session_id, context_.session_id.data(), 16u);
    std::memcpy(snapshot.dual_branch_id, context_.branch_id.data(), 16u);
    std::memcpy(snapshot.dual_state_digest, last_digest_.state.data(), 32u);
    std::memcpy(snapshot.dual_frame_digest, last_digest_.frame.data(), 32u);
    std::memcpy(snapshot.dual_pcm_digest, last_digest_.pcm.data(), 32u);
}

void DualSessionController::fill_scope(fly_session_scope_v2& scope) const noexcept
{
    if (!running_ && !paused_ && !frozen_)
        return;
    scope.kind = FLY_SESSION_SCOPE_GAME_V2;
    std::memcpy(scope.link_id, context_.session_id.data(), 16u);
    std::memcpy(scope.branch_id, context_.branch_id.data(), 16u);
}

void DualSessionController::cancel() noexcept
{
    catalog_query_outstanding_ = false;
    open_outstanding_ = false;
    write_outstanding_ = false;
    read_outstanding_ = false;
    want_read_ = false;
    write_queue_.clear();
    if (scheduler_)
        scheduler_->shutdown();
}

} // namespace flynes::session::dual
