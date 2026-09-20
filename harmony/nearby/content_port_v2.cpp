#include "content_port_v2.hpp"
#include <flynes/product/dual_start_identity.hpp>
#include "session/wire/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <mutex>
#include <set>

namespace flynes::harmony::nearby {
namespace {
bool valid_token(const fly_session_op_token_v2* token) {
    return token && token->struct_size == FLY_SESSION_OP_TOKEN_V2_SIZE && token->abi_version == 2 &&
        token->scope.struct_size == FLY_SESSION_SCOPE_V2_SIZE && token->scope.abi_version == 2 &&
        token->scope.reserved_zero == 0 && token->scope.kind >= FLY_SESSION_SCOPE_ENGINE_V2 &&
        token->scope.kind <= FLY_SESSION_SCOPE_GAME_V2 && token->operation_id != 0;
}
std::string bounded_name(const std::string& name) {
    auto size = std::min<std::size_t>(64, name.size());
    while (size < name.size() && size && (static_cast<unsigned char>(name[size]) & 0xc0) == 0x80) --size;
    return size ? name.substr(0, size) : "?";
}
}
struct ContentPortV2::State {
    std::atomic<unsigned> references{1};
    mutable std::mutex mutex;
    Random random;
    bool closed = false, available = false, enumerating = false;
    std::uint64_t publication = 0;
    std::vector<Selection> rows;
    QueryStatus status;
    explicit State(Random value) : random(std::move(value)) {}
    static void retain(void* p) { ++static_cast<State*>(p)->references; }
    static void release(void* p) { auto* s = static_cast<State*>(p); if (--s->references == 0) delete s; }
    fly_session_result_v2 record(std::uint32_t index, std::vector<std::uint8_t>* out) {
        std::lock_guard<std::mutex> lock(mutex);
        status.attempted = true;
        auto finish = [this](fly_session_result_v2 result) { status.result = result; return result; };
        if (!out) return finish(FLY_SESSION_V2_INVALID_ARGUMENT);
        out->clear();
        if (closed) return finish(FLY_SESSION_V2_CLOSED);
        if (!available) return finish(FLY_SESSION_V2_UNAVAILABLE);
        if (index == 0) enumerating = true;
        if (!enumerating) return finish(FLY_SESSION_V2_STALE);
        if (index >= rows.size()) return finish(FLY_SESSION_V2_EMPTY);
        try {
            const auto& row = rows[index];
            const auto name = bounded_name(row.display_name);
            std::vector<std::uint8_t> record(56, 0);
            record[1] = 2; record[55] = static_cast<std::uint8_t>(name.size());
            std::copy(row.ref.begin(), row.ref.end(), record.begin() + 4);
            std::copy(row.payload_hash.begin(), row.payload_hash.end(), record.begin() + 20);
            record.insert(record.end(), name.begin(), name.end());
            const auto identity = flynes::product::canonical_dual_start_identity_v1();
            for (const auto* part : {&identity.core_id, &identity.profile_id, &identity.options_id})
                record.insert(record.end(), part->begin(), part->end());
            *out = std::move(record);
            return finish(FLY_SESSION_V2_OK);
        } catch (...) { return finish(FLY_SESSION_V2_UNAVAILABLE); }
    }
    static fly_session_result_v2 query(void* p, const fly_session_op_token_v2* token,
                                       std::uint32_t index, fly_session_inbox_v2_t* inbox) {
        if (!p || !inbox || !valid_token(token)) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto* self = static_cast<State*>(p);
        try {
            std::vector<std::uint8_t> record;
            const auto result = self->record(index, &record);
            if (result != FLY_SESSION_V2_OK) return result;
            const auto hash = flynes::session::wire::domain_hash("flynes-content-choice-v1", record.data(), record.size());
            fly_session_buffer_v2_t* buffer = nullptr;
            auto copied = fly_session_buffer_create_copy_v2({record.data(), static_cast<std::uint32_t>(record.size()), 0}, &buffer);
            if (copied != FLY_SESSION_V2_OK) return copied;
            fly_session_provider_hash_event_v2 payload{};
            payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE; payload.abi_version = 2;
            payload.resource = static_cast<std::uint64_t>(index) + 1; payload.buffer = buffer;
            std::copy(hash.begin(), hash.end(), payload.hash);
            fly_session_port_event_v2 event{};
            event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE; event.abi_version = 2;
            event.token = *token; event.event_sequence = 1; event.terminal = 1;
            event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2; event.result = FLY_SESSION_V2_OK;
            event.payload_kind = FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2; event.payload_size = sizeof(payload);
            std::memcpy(event.payload, &payload, sizeof(payload));
            copied = fly_session_deliver_v2(inbox, &event);
            fly_session_buffer_release_v2(buffer);
            return copied;
        } catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
    }
    static fly_session_result_v2 cancel(void* p, const fly_session_op_token_v2* token) {
        if (!p || !valid_token(token)) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto* self = static_cast<State*>(p);
        std::lock_guard<std::mutex> lock(self->mutex);
        // Synchronous query already published; engine journal rejects a cancelled late event.
        return self->closed ? FLY_SESSION_V2_CLOSED : FLY_SESSION_V2_OK;
    }
};
ContentPortV2::ContentPortV2(Random random) : state_(new State(std::move(random))) {}
ContentPortV2::~ContentPortV2() { close(); State::release(state_); }
fly_session_result_v2 ContentPortV2::publish(const fly_catalog_snapshot_t* snapshot, std::uint64_t policy) {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (state_->closed) return FLY_SESSION_V2_CLOSED;
    state_->available = false; state_->enumerating = false; state_->rows.clear();
    if (!snapshot || !policy || !state_->random || state_->publication == std::numeric_limits<std::uint64_t>::max())
        return FLY_SESSION_V2_UNAVAILABLE;
    ++state_->publication;
    try {
        std::uint64_t count = 0, generation = 0;
        if (fly_catalog_snapshot_count(snapshot, &count) != FLY_RESULT_OK ||
            fly_catalog_snapshot_generation(snapshot, &generation) != FLY_RESULT_OK) return FLY_SESSION_V2_UNAVAILABLE;
        std::vector<Selection> rows;
        std::set<Ref> refs;
        for (std::uint64_t i = 0; i < count; ++i) {
            std::array<char, 4097> canonical{}, variant{}, path{};
            std::array<char, 1025> name{};
            fly_catalog_entry entry{};
            entry.struct_size = FLY_CATALOG_ENTRY_V1_SIZE; entry.version = 1;
            entry.canonical_id_utf8 = canonical.data(); entry.canonical_id_capacity = static_cast<std::uint32_t>(canonical.size());
            entry.variant_id_utf8 = variant.data(); entry.variant_id_capacity = static_cast<std::uint32_t>(variant.size());
            entry.source_relative_path_utf8 = path.data(); entry.source_relative_path_capacity = static_cast<std::uint32_t>(path.size());
            entry.display_name_utf8 = name.data(); entry.display_name_capacity = static_cast<std::uint32_t>(name.size());
            if (fly_catalog_snapshot_get(snapshot, i, &entry) != FLY_RESULT_OK) return FLY_SESSION_V2_UNAVAILABLE;
            if (entry.compatibility_state != FLY_COMPATIBILITY_PLAYABLE || entry.freshness != FLY_CATALOG_FRESHNESS_FRESH) continue;
            if (rows.size() == 4096) return FLY_SESSION_V2_UNAVAILABLE;
            Selection row;
            row.catalog_generation = generation; row.policy_revision = policy;
            row.physical_size = entry.physical_size; row.payload_size = entry.payload_size;
            row.scope = entry.source_scope; row.package_format = entry.package_format;
            row.canonical_id = canonical.data(); row.variant_id = variant.data();
            row.relative_path = path.data(); row.display_name = name.data();
            std::copy_n(entry.source_uuid, 16, row.source_uuid.begin());
            std::copy_n(entry.physical_sha256, 32, row.physical_hash.begin());
            std::copy_n(entry.payload_sha256, 32, row.payload_hash.begin());
            std::copy_n(entry.payload_sha1, 20, row.payload_sha1.begin());
            std::copy_n(entry.payload_crc32, 4, row.payload_crc.begin());
            std::array<std::uint8_t, FLY_SCAN_MAX_ZIP_NAME_BYTES> zip{};
            std::uint32_t required = 0;
            if (fly_catalog_snapshot_get_zip_locator(snapshot, i, zip.data(), static_cast<std::uint32_t>(zip.size()), &required, &row.zip_offset) != FLY_RESULT_OK)
                return FLY_SESSION_V2_UNAVAILABLE;
            row.zip_raw_name.assign(zip.begin(), zip.begin() + required);
            Ref entropy{};
            if (!state_->random(entropy) || std::all_of(entropy.begin(), entropy.end(), [](auto b) { return b == 0; }))
                return FLY_SESSION_V2_UNAVAILABLE;
            // Salt with publication/row so even a broken repeating entropy source cannot
            // revive a reference when the same publication is supplied again.
            std::array<std::uint8_t, 32> seed{};
            std::copy(entropy.begin(), entropy.end(), seed.begin());
            for (unsigned b = 0; b < 8; ++b) {
                seed[16 + b] = static_cast<std::uint8_t>(state_->publication >> (b * 8));
                seed[24 + b] = static_cast<std::uint8_t>(i >> (b * 8));
            }
            const auto hash = flynes::session::wire::domain_hash("flynes-harmony-source-ref-v1", seed.data(), seed.size());
            std::copy_n(hash.begin(), 16, row.ref.begin());
            if (std::all_of(row.ref.begin(), row.ref.end(), [](auto b) { return b == 0; }) || !refs.insert(row.ref).second)
                return FLY_SESSION_V2_UNAVAILABLE;
            rows.push_back(std::move(row));
        }
        state_->rows = std::move(rows); state_->available = true;
        return FLY_SESSION_V2_OK;
    } catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}
void ContentPortV2::invalidate_policy() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->available = false; state_->enumerating = false; state_->rows.clear();
}
bool ContentPortV2::resolve(const Ref& ref, Selection* out) const {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (!out || state_->closed || !state_->available) return false;
    for (const auto& row : state_->rows) if (row.ref == ref) { *out = row; return true; }
    return false;
}
fly_session_result_v2 ContentPortV2::query_record(std::uint32_t index, std::vector<std::uint8_t>* out) { return state_->record(index, out); }
ContentPortV2::QueryStatus ContentPortV2::query_status() const { std::lock_guard<std::mutex> lock(state_->mutex); return state_->status; }
fly_session_content_port_v2 ContentPortV2::port() const noexcept {
    fly_session_content_port_v2 result{};
    result.struct_size = FLY_SESSION_CONTENT_PORT_V2_SIZE; result.abi_version = 2; result.context = state_;
    result.retain = State::retain; result.release = State::release;
    result.query = State::query; result.cancel = State::cancel;
    return result;
}
void ContentPortV2::close() {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->closed = true; state_->available = false; state_->enumerating = false; state_->rows.clear();
}
}
