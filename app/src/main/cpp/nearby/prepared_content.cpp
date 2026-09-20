#include "prepared_content.hpp"
#include <array>
#include <algorithm>
#include <cstring>
#include <limits>
#include <mutex>
namespace flynes::android::nearby {
namespace {
bool nonzero(const std::uint8_t* value, std::size_t count) {
    return std::any_of(value, value + count, [](std::uint8_t byte) { return byte != 0; });
}
bool same_scope(const fly_session_scope_v2& a, const fly_session_scope_v2& b) {
    return a.kind == b.kind && std::memcmp(a.link_id, b.link_id, 16) == 0 &&
        std::memcmp(a.branch_id, b.branch_id, 16) == 0;
}
bool lobby(const fly_session_snapshot_v2& snap) {
    return snap.link_state == FLY_SESSION_LINK_CONNECTED_LOBBY_V2 &&
        snap.game_state == FLY_SESSION_GAME_NOT_STARTED_V2;
}
}
struct PreparedContent::State {
    std::mutex mutex;
    fly_session_content_port_v2 content{};
    bool closed = false, bound = false, armed = false;
    std::uint64_t next = 0, ticket = 0, revision = 0;
    std::array<std::uint8_t, 16> source{};
    std::array<std::uint8_t, 32> hash{}, config{};
    fly_session_scope_v2 scope{};
    fly_session_dual_content_ref_v2 expected{};
    std::shared_ptr<const std::vector<std::uint8_t>> bytes;
    void clear() { ticket = 0; bytes.reset(); bound = false; armed = false; revision = 0; }
    bool current(const fly_session_snapshot_v2& snap) {
        if (closed || ticket == 0) return false;
        if (!lobby(snap) || !same_scope(scope, snap.scope) ||
            !ContentPort::validate(content, source.data()) ||
            (bound && (revision != snap.pending_config_revision ||
              std::memcmp(config.data(), snap.pending_config_id, 32) != 0 ||
              std::memcmp(hash.data(), snap.dual_content_hash, 32) != 0))) {
            clear(); return false;
        }
        return true;
    }
};
PreparedContent::PreparedContent(fly_session_content_port_v2 content) : state_(new State) {
    state_->content = content;
    if (content.retain) content.retain(content.context);
}
PreparedContent::~PreparedContent() {
    close();
    if (state_->content.release) state_->content.release(state_->content.context);
}
fly_session_result_v2 PreparedContent::begin(const fly_session_snapshot_v2& snap,
    const fly_session_game_choice_v2& choice, std::uint64_t* out) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (s.closed) return FLY_SESSION_V2_CLOSED;
    s.clear();
    if (!out || !choice.selectable || !nonzero(choice.source_choice_ref, 16) || !nonzero(choice.content_id, 32))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (!lobby(snap)) return FLY_SESSION_V2_INVALID_STATE;
    if (!ContentPort::validate(s.content, choice.source_choice_ref)) return FLY_SESSION_V2_STALE;
    if (s.next == (std::numeric_limits<std::uint64_t>::max)()) return FLY_SESSION_V2_UNAVAILABLE;
    s.ticket = ++s.next; s.scope = snap.scope;
    std::copy_n(choice.source_choice_ref, 16, s.source.data());
    std::copy_n(choice.content_id, 32, s.hash.data());
    *out = s.ticket; return FLY_SESSION_V2_OK;
}
fly_session_result_v2 PreparedContent::stage(std::uint64_t ticket, const std::uint8_t* ref,
    const std::uint8_t* hash, std::shared_ptr<const std::vector<std::uint8_t>> bytes,
    const fly_session_snapshot_v2& snap) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (s.closed) return FLY_SESSION_V2_CLOSED;
    if (!ticket || ticket != s.ticket) return FLY_SESSION_V2_STALE;
    if (!s.current(snap)) return FLY_SESSION_V2_STALE;
    if (!ref || !hash || !bytes || bytes->empty() || bytes->size() > 8u * 1024u * 1024u ||
        std::memcmp(ref, s.source.data(), 16) || std::memcmp(hash, s.hash.data(), 32)) {
        s.clear(); return FLY_SESSION_V2_INVALID_ARGUMENT;
    }
    s.bytes = std::move(bytes); return FLY_SESSION_V2_OK;
}
fly_session_result_v2 PreparedContent::bind_config(const fly_session_snapshot_v2& snap) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (!s.current(snap) || !s.bytes) return FLY_SESSION_V2_STALE;
    if (!snap.pending_config_revision || !nonzero(snap.pending_config_id, 32) ||
        std::memcmp(s.hash.data(), snap.dual_content_hash, 32)) {
        s.clear(); return FLY_SESSION_V2_STALE;
    }
    std::copy_n(snap.pending_config_id, 32, s.config.data());
    s.revision = snap.pending_config_revision; s.bound = true; return FLY_SESSION_V2_OK;
}
bool PreparedContent::can_confirm(const fly_session_snapshot_v2& snap) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    return s.current(snap) && s.bound && bool(s.bytes);
}
fly_session_result_v2 PreparedContent::finish_selection(fly_session_result_v2 admission, std::uint64_t request,
    const fly_session_notice_v2& receipt, const fly_session_snapshot_v2& snap) {
    if (admission < 0) { cancel(); return admission; }
    if (!request || receipt.kind != FLY_SESSION_NOTICE_ACTION_RESULT_V2 || receipt.request_id != request ||
        receipt.outcome != FLY_SESSION_ACTION_APPLIED_V2 || receipt.result != FLY_SESSION_V2_OK) {
        cancel();
        return receipt.request_id == request && receipt.result < 0 ? receipt.result : FLY_SESSION_V2_UNAVAILABLE;
    }
    return bind_config(snap);
}
fly_session_result_v2 PreparedContent::authorize_start(fly_session_v2_t* engine,
    const fly_session_approval_token_v2_t* approval, std::uint64_t* revision) {
    if (!revision) { cancel(); return FLY_SESSION_V2_INVALID_ARGUMENT; }
    // Never hold resolver state across an engine call. This helper runs before submit,
    // not inside the synchronous runtime callback which already holds engine mutex.
    fly_session_view_v2_t* view = nullptr;
    auto result = fly_session_acquire_view_v2(engine, &view);
    fly_session_snapshot_v2 snap{}; snap.struct_size = FLY_SESSION_SNAPSHOT_V2_SIZE; snap.abi_version = 2;
    if (result == FLY_SESSION_V2_OK) {
        result = fly_session_view_read_v2(view, &snap); fly_session_view_release_v2(view);
    }
    fly_session_dual_start_ref_v2 expected{};
    expected.struct_size = FLY_SESSION_DUAL_START_REF_V2_SIZE; expected.abi_version = 2;
    if (result == FLY_SESSION_V2_OK) result = fly_session_read_dual_start_ref_v2(engine, approval, &expected);
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (result != FLY_SESSION_V2_OK) { s.clear(); return result; }
    if (!s.current(snap) || !s.bytes || !s.bound ||
        s.revision != expected.pending_config_revision ||
        std::memcmp(s.config.data(), expected.pending_config_id, 32) ||
        std::memcmp(s.source.data(), expected.source_choice_ref, 16) ||
        std::memcmp(s.hash.data(), expected.content.content_hash, 32)) {
        s.clear(); return FLY_SESSION_V2_STALE;
    }
    s.expected = expected.content; s.armed = true; *revision = expected.view_revision;
    return FLY_SESSION_V2_OK;
}
flynes::product::AuthorizedRom PreparedContent::resolve(const fly_session_dual_content_ref_v2& ref) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (s.closed || !s.armed || !s.bytes || !s.ticket ||
        std::memcmp(ref.session_id, s.expected.session_id, 16) ||
        std::memcmp(ref.branch_id, s.expected.branch_id, 16) ||
        std::memcmp(ref.content_hash, s.expected.content_hash, 32) || ref.timeline_epoch != s.expected.timeline_epoch)
        return {};
    if (!ContentPort::validate(s.content, s.source.data())) { s.clear(); return {}; }
    flynes::product::AuthorizedRom result{FLY_SESSION_V2_OK, std::move(s.bytes)};
    s.clear(); return result;
}
void PreparedContent::reconcile(const fly_session_snapshot_v2& snap) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex); (void)s.current(snap);
}
void PreparedContent::cancel(std::uint64_t ticket) {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex);
    if (ticket == 0 || ticket == s.ticket) s.clear();
}
void PreparedContent::close() {
    auto& s = *state_; std::lock_guard<std::mutex> lock(s.mutex); s.closed = true; s.clear();
}
}
