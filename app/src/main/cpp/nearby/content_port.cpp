#include "content_port.hpp"
#include <flynes/product/dual_start_identity.hpp>
#include "wire/sha256.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <utility>

namespace flynes::android::nearby {
namespace {
bool nonzero(const std::uint8_t* bytes, std::size_t size) {
    return std::any_of(bytes, bytes + size, [](std::uint8_t b) { return b != 0; });
}
bool utf8(const std::uint8_t* data, std::size_t size) {
    for (std::size_t i = 0; i < size;) {
        const auto first = data[i++];
        if (first == 0) return false;
        if (first < 0x80) continue;
        std::uint32_t cp = 0, minimum = 0;
        std::size_t count = 0;
        if (first >= 0xC2 && first <= 0xDF) { cp = first & 31; count = 1; minimum = 0x80; }
        else if (first >= 0xE0 && first <= 0xEF) { cp = first & 15; count = 2; minimum = 0x800; }
        else if (first >= 0xF0 && first <= 0xF4) { cp = first & 7; count = 3; minimum = 0x10000; }
        else return false;
        if (count > size - i) return false;
        while (count--) {
            if ((data[i] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (data[i++] & 63);
        }
        if (cp < minimum || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) return false;
    }
    return true;
}
bool valid_token(const fly_session_op_token_v2* token) {
    return token && token->struct_size == FLY_SESSION_OP_TOKEN_V2_SIZE &&
        token->abi_version == 2 && token->scope.struct_size == FLY_SESSION_SCOPE_V2_SIZE &&
        token->scope.abi_version == 2 && token->scope.reserved_zero == 0 &&
        token->scope.kind >= FLY_SESSION_SCOPE_ENGINE_V2 &&
        token->scope.kind <= FLY_SESSION_SCOPE_GAME_V2 && token->operation_id != 0;
}
}

struct ContentPort::Context final {
    std::atomic<unsigned> references{1};
    std::mutex mutex;
    bool closed = false;
    QueryStatus status;
    Callbacks callbacks;
    explicit Context(Callbacks value) : callbacks(std::move(value)) {}
    static void retain(void* p) { ++static_cast<Context*>(p)->references; }
    static void release(void* p) { auto* c = static_cast<Context*>(p); if (--c->references == 0) delete c; }
    static fly_session_result_v2 cancel(void* p, const fly_session_op_token_v2* token) {
        if (!p || !valid_token(token)) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto* c = static_cast<Context*>(p);
        std::lock_guard<std::mutex> lock(c->mutex);
        // Query publishes its completion before returning; no deferred work exists.
        // A canceled token's already queued event is rejected by the engine journal.
        return c->closed ? FLY_SESSION_V2_CLOSED : FLY_SESSION_V2_OK;
    }
    static fly_session_result_v2 query(void* p, const fly_session_op_token_v2* token,
                                         std::uint32_t index, fly_session_inbox_v2_t* inbox) {
        if (!p || !valid_token(token) || !inbox) return FLY_SESSION_V2_INVALID_ARGUMENT;
        auto* c = static_cast<Context*>(p);
        std::lock_guard<std::mutex> lock(c->mutex);
        if (c->closed) return FLY_SESSION_V2_CLOSED;
        c->status.attempted = true;
        auto finish = [c](fly_session_result_v2 result) { c->status.result = result; return result; };
        try {
            std::vector<std::uint8_t> metadata, record;
            auto result = c->callbacks.query ? c->callbacks.query(index, &metadata) : FLY_SESSION_V2_EMPTY;
            if (result != FLY_SESSION_V2_OK) return finish(result);
            result = encode_record(metadata, &record);
            if (result != FLY_SESSION_V2_OK) return finish(result);
            const auto hash = flynes::session::wire::domain_hash("flynes-content-choice-v1", record.data(), record.size());
            fly_session_buffer_v2_t* buffer = nullptr;
            const fly_session_bytes_v2 bytes{record.data(), static_cast<std::uint32_t>(record.size()), 0};
            result = fly_session_buffer_create_copy_v2(bytes, &buffer);
            if (result != FLY_SESSION_V2_OK) return finish(result);
            fly_session_provider_hash_event_v2 payload{};
            payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
            payload.abi_version = 2;
            payload.resource = static_cast<std::uint64_t>(index) + 1;
            payload.buffer = buffer;
            std::copy(hash.begin(), hash.end(), payload.hash);
            fly_session_port_event_v2 event{};
            event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
            event.abi_version = 2; event.token = *token; event.event_sequence = 1;
            event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
            event.terminal = 1; event.result = FLY_SESSION_V2_OK;
            event.payload_kind = FLY_SESSION_PROVIDER_CONTENT_CHOICE_V2;
            event.payload_size = sizeof(payload);
            std::memcpy(event.payload, &payload, sizeof(payload));
            result = fly_session_deliver_v2(inbox, &event);
            fly_session_buffer_release_v2(buffer);
            return finish(result);
        } catch (...) { return finish(FLY_SESSION_V2_UNAVAILABLE); }
    }
};

ContentPort::ContentPort(Callbacks callbacks) : context_(new Context(std::move(callbacks))) {}
ContentPort::~ContentPort() {
    {
        std::lock_guard<std::mutex> lock(context_->mutex);
        context_->closed = true;
        try { if (context_->callbacks.close) context_->callbacks.close(); } catch (...) {}
        context_->callbacks = {};
    }
    Context::release(context_);
}
fly_session_content_port_v2 ContentPort::port() const noexcept {
    fly_session_content_port_v2 value{};
    value.struct_size = FLY_SESSION_CONTENT_PORT_V2_SIZE; value.abi_version = 2;
    value.context = context_; value.retain = Context::retain; value.release = Context::release;
    value.query = Context::query; value.cancel = Context::cancel;
    return value;
}
ContentPort::QueryStatus ContentPort::query_status() const {
    std::lock_guard<std::mutex> lock(context_->mutex);
    return context_->status;
}
bool ContentPort::validate(const fly_session_content_port_v2& port, const std::uint8_t ref[16]) {
    if (!port.context || !ref) return false;
    auto* c = static_cast<Context*>(port.context);
    std::lock_guard<std::mutex> lock(c->mutex);
    if (c->closed || !c->callbacks.validate) return false;
    try { return c->callbacks.validate(ref); } catch (...) { return false; }
}
fly_session_result_v2 ContentPort::encode_record(const std::vector<std::uint8_t>& input,
                                                  std::vector<std::uint8_t>* out) {
    if (!out || input.size() < 57 || input.size() > 120 || input[0] != 0 || input[1] != 1 ||
        input[2] || input[3] || input[52] || input[53] || input[54] ||
        input[55] == 0 || input[55] > 64 || input.size() != 56u + input[55] ||
        !nonzero(input.data() + 4, 16) || !nonzero(input.data() + 20, 32) ||
        !utf8(input.data() + 56, input[55])) return FLY_SESSION_V2_INVALID_ARGUMENT;
    *out = input; (*out)[1] = 2;
    const auto identity = flynes::product::canonical_dual_start_identity_v1();
    for (const auto* value : {&identity.core_id, &identity.profile_id, &identity.options_id})
        out->insert(out->end(), value->begin(), value->end());
    return FLY_SESSION_V2_OK;
}
}
