/*
 * Production Quinn adapter for fly_session_quic_port_v2. Completions are queued
 * off the Quinn thread and drained on the owner pump thread.
 */
#pragma once

#include "flynes_quic_provider.h"

#include <flynes/flynes_session.h>
#include "wire/quic_contract.hpp"
#include "wire/sha256.hpp"
#include "ports/quic_provider_facts.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace flynes::android::nearby {

inline void quic_retain_noop(void*) {}
inline void quic_release_noop(void*) {}

inline fly_session_result_v2 quic_unavailable_start(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_resource_handle_v2,
    const fly_session_quic_connect_policy_v2*, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_inspect(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_exporter(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_bytes_v2, fly_session_bytes_v2, std::uint32_t,
    fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_stream(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, std::uint32_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_control(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint64_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_datagram(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_resource_handle_v2, std::uint64_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_query(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_unavailable_close(
    void*, const fly_session_op_token_v2*, fly_session_resource_handle_v2,
    std::uint32_t, fly_session_inbox_v2_t*)
{
    return FLY_SESSION_V2_UNAVAILABLE;
}
inline fly_session_result_v2 quic_ok_cancel(void*, const fly_session_op_token_v2*)
{
    return FLY_SESSION_V2_OK;
}

inline std::uint64_t quic_load_u64be(const std::uint8_t* bytes)
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

inline std::string quic_decode_ipv4_endpoint(const std::uint8_t* bytes,
                                             std::size_t size)
{
    if (bytes == nullptr || size != 18)
        return {};
    const unsigned port =
        (static_cast<unsigned>(bytes[16]) << 8) | bytes[17];
    char text[64];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u:%u", bytes[12], bytes[13],
                  bytes[14], bytes[15], port);
    return text;
}

inline fly_session_result_v2 quic_map_provider_result(std::int32_t result)
{
    return result == FLYNES_QUIC_OK ? FLY_SESSION_V2_OK
                                    : FLY_SESSION_V2_UNAVAILABLE;
}

inline void quic_deliver_end(fly_session_inbox_v2_t* inbox,
                             const fly_session_op_token_v2& token,
                             std::uint32_t kind, fly_session_result_v2 result)
{
    if (inbox == nullptr)
        return;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = result;
    event.payload_kind = kind;
    fly_session_provider_end_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_END_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)fly_session_deliver_v2(inbox, &event);
}

inline void quic_deliver_resource(fly_session_inbox_v2_t* inbox,
                                  const fly_session_op_token_v2& token,
                                  std::uint32_t kind,
                                  fly_session_resource_handle_v2 resource,
                                  std::uint64_t value0 = 0)
{
    if (inbox == nullptr)
        return;
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_resource_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_RESOURCE_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.generation = token.connection_generation;
    payload.value0 = value0;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)fly_session_deliver_v2(inbox, &event);
}

inline void quic_deliver_buffer(fly_session_inbox_v2_t* inbox,
                                const fly_session_op_token_v2& token,
                                std::uint32_t kind, const std::uint8_t* bytes,
                                std::size_t size, int terminal)
{
    if (inbox == nullptr)
        return;
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        size == 0 ? nullptr : bytes, static_cast<std::uint32_t>(size), 0};
    if (fly_session_buffer_create_copy_v2(source, &buffer) != FLY_SESSION_V2_OK)
    {
        quic_deliver_end(inbox, token, kind, FLY_SESSION_V2_OUT_OF_MEMORY);
        return;
    }
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = static_cast<std::uint32_t>(terminal);
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_buffer_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_BUFFER_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.buffer = buffer;
    payload.logical_size = size;
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)fly_session_deliver_v2(inbox, &event);
    fly_session_buffer_release_v2(buffer);
}

inline void quic_deliver_hash_buffer(fly_session_inbox_v2_t* inbox,
                                     const fly_session_op_token_v2& token,
                                     std::uint32_t kind,
                                     fly_session_resource_handle_v2 resource,
                                     const std::uint8_t* bytes, std::size_t size,
                                     const std::array<std::uint8_t, 32>& hash)
{
    if (inbox == nullptr)
        return;
    fly_session_buffer_v2_t* buffer = nullptr;
    const fly_session_bytes_v2 source{
        bytes, static_cast<std::uint32_t>(size), 0};
    if (fly_session_buffer_create_copy_v2(source, &buffer) != FLY_SESSION_V2_OK)
    {
        quic_deliver_end(inbox, token, kind, FLY_SESSION_V2_OUT_OF_MEMORY);
        return;
    }
    fly_session_port_event_v2 event{};
    event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
    event.abi_version = FLY_SESSION_ABI_VERSION_2;
    event.token = token;
    event.event_sequence = 1;
    event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
    event.terminal = 1;
    event.result = FLY_SESSION_V2_OK;
    event.payload_kind = kind;
    fly_session_provider_hash_event_v2 payload{};
    payload.struct_size = FLY_SESSION_PROVIDER_HASH_EVENT_V2_SIZE;
    payload.abi_version = FLY_SESSION_ABI_VERSION_2;
    payload.resource = resource;
    payload.buffer = buffer;
    std::copy(hash.begin(), hash.end(), payload.hash);
    event.payload_size = sizeof(payload);
    std::memcpy(event.payload, &payload, sizeof(payload));
    (void)fly_session_deliver_v2(inbox, &event);
    fly_session_buffer_release_v2(buffer);
}

inline bool quic_deliver_handshake(fly_session_inbox_v2_t* inbox,
                                   const fly_session_op_token_v2& token,
                                   fly_session_resource_handle_v2 connection,
                                   const std::vector<std::uint8_t>& raw)
{
    flynes::session::wire::QuicHandshakeFactsV2 wire_facts{};
    if (!flynes::session::ports::decode_quic_provider_facts(
            raw.data(), raw.size(), &wire_facts))
    {
        quic_deliver_end(inbox, token, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                         FLY_SESSION_V2_CONTRACT_VIOLATION);
        return false;
    }
    std::array<std::uint8_t, flynes::session::wire::kQuicHandshakeFactsWireSizeV2>
        encoded{};
    if (flynes::session::wire::encode_quic_handshake_facts_v2(wire_facts,
                                                              &encoded) !=
        flynes::session::wire::Status::Ok)
    {
        quic_deliver_end(inbox, token, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                         FLY_SESSION_V2_CONTRACT_VIOLATION);
        return false;
    }
    const auto hash = flynes::session::wire::sha256(encoded.data(), encoded.size());
    quic_deliver_hash_buffer(inbox, token, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                             connection, encoded.data(), encoded.size(), hash);
    return true;
}

class ProductQuicPort final
{
public:
    ProductQuicPort()
    {
        FlynesQuicCallbacks callbacks{};
        callbacks.struct_size = static_cast<std::uint32_t>(sizeof(callbacks));
        callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
        callbacks.context = mailbox_;
        callbacks.retain = retain_mailbox;
        callbacks.release = release_mailbox;
        callbacks.completion = &on_complete;
        provider_ = flynes_quic_provider_create(&callbacks);
        if (provider_ == nullptr)
            return;
        if (!generate_self_signed_blocking(std::chrono::seconds(5)))
            return;
        (void)prelisten_blocking(std::chrono::seconds(5));
    }

    ~ProductQuicPort()
    {
        // Rust shutdown is asynchronous. Its retained mailbox must outlive this
        // adapter, and must no longer call an owner that is being destroyed.
        set_wakeup({});
        for (auto& item : pending_)
            fly_session_inbox_release_v2(item.second.inbox);
        pending_.clear();
        if (provider_ != nullptr)
            flynes_quic_provider_release(provider_);
        release_mailbox(mailbox_);
    }

    ProductQuicPort(const ProductQuicPort&) = delete;
    ProductQuicPort& operator=(const ProductQuicPort&) = delete;

    // Notification only: the owner schedules drain() on its serial executor.
    void set_wakeup(std::function<void()> wake)
    {
        std::lock_guard<std::mutex> lock(mailbox_->mutex);
        mailbox_->wake = std::move(wake);
    }

    [[nodiscard]] bool ready() const noexcept
    {
        return provider_ != nullptr && listener_handle_ != 0;
    }
    [[nodiscard]] const char* provider_type() const noexcept
    {
        return "FlynesQuicProvider/Quinn";
    }
    [[nodiscard]] const char* listen_address() const noexcept
    {
        return listen_address_.c_str();
    }
    [[nodiscard]] const std::array<std::uint8_t, 32>& der_spki_hash() const noexcept
    {
        return der_spki_hash_;
    }
    // Security/bearer bootstrap must use this provider's material and endpoint.
    [[nodiscard]] std::uint64_t material_handle() const noexcept { return material_handle_; }
    [[nodiscard]] const std::vector<std::uint8_t>& der_spki() const noexcept { return der_spki_; }
    [[nodiscard]] std::array<std::uint8_t, 18> listen_endpoint() const
    {
        std::array<std::uint8_t, 18> endpoint{};
        unsigned a = 0, b = 0, c = 0, d = 0, port = 0;
#ifdef _MSC_VER
        const int matched = sscanf_s(listen_address_.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#else
        const int matched = std::sscanf(listen_address_.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#endif
        if (matched != 5)
            return endpoint;
        endpoint[12] = static_cast<std::uint8_t>(a);
        endpoint[13] = static_cast<std::uint8_t>(b);
        endpoint[14] = static_cast<std::uint8_t>(c);
        endpoint[15] = static_cast<std::uint8_t>(d);
        endpoint[16] = static_cast<std::uint8_t>(port >> 8);
        endpoint[17] = static_cast<std::uint8_t>(port);
        return endpoint;
    }
    [[nodiscard]] int listens() const noexcept { return listens_; }
    [[nodiscard]] int connects() const noexcept { return connects_; }
    [[nodiscard]] int handshake_inspections() const noexcept { return inspections_; }
    [[nodiscard]] int exporters() const noexcept { return exporters_; }
    [[nodiscard]] int streams_opened() const noexcept { return static_cast<int>(streams_.size()); }
    [[nodiscard]] std::uint64_t connection() const noexcept { return connection_; }
    [[nodiscard]] std::uint64_t control_stream() const noexcept
    {
        return control_stream_;
    }
    [[nodiscard]] std::uint64_t bytes_written() const noexcept
    {
        return bytes_written_;
    }
    [[nodiscard]] std::uint64_t bytes_read() const noexcept { return bytes_read_; }
    [[nodiscard]] std::int32_t last_stream_result() const noexcept
    {
        return last_stream_result_;
    }
    [[nodiscard]] int pending_count() const
    {
        std::scoped_lock lock(mutex_, mailbox_->mutex);
        return static_cast<int>(pending_.size() + mailbox_->queue.size());
    }

    bool wait_connected(std::chrono::milliseconds timeout)
    {
        return wait_until(timeout, [this] { return connection_ != 0; });
    }
    bool wait_stream(std::chrono::milliseconds timeout)
    {
        return wait_until(timeout, [this] { return control_stream_ != 0; });
    }
    [[nodiscard]] bool handshake_inspected() const noexcept
    {
        return handshake_inspected_;
    }
    bool wait_inspected(std::chrono::milliseconds timeout)
    {
        return wait_until(timeout, [this] { return handshake_inspected_; });
    }

    fly_session_quic_port_v2 port()
    {
        fly_session_quic_port_v2 value{};
        value.struct_size = FLY_SESSION_QUIC_PORT_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        value.context = this;
        value.retain = quic_retain_noop;
        value.release = quic_release_noop;
        value.listen = &listen;
        value.connect = &connect;
        value.inspect_handshake = &inspect;
        value.exporter = &exporter;
        value.open_uni = quic_unavailable_stream;
        value.open_bidi = &open_bidi;
        value.accept_uni = quic_unavailable_stream;
        value.accept_bidi = &accept_bidi;
        value.write = &write;
        value.finish = quic_unavailable_control;
        value.reset = quic_unavailable_control;
        value.grant_read_credit = &grant_read;
        value.send_datagram = quic_unavailable_datagram;
        value.payload_budget = quic_unavailable_query;
        value.stats = quic_unavailable_query;
        value.close = &close;
        value.cancel = &cancel;
        return value;
    }

    int drain()
    {
        std::deque<Completion> batch;
        {
            std::lock_guard<std::mutex> lock(mailbox_->mutex);
            batch.swap(mailbox_->queue);
        }
        for (auto& item : batch)
            apply(item);
        return static_cast<int>(batch.size());
    }

private:
    enum class OpKind : std::uint8_t
    {
        None = 0,
        GenerateMaterial,
        Listen,
        Accept,
        Connect,
        Inspect,
        Exporter,
        OpenBidi,
        AcceptBidi,
        Write,
        Read,
        Close
    };

    struct Completion final
    {
        std::uint64_t operation = 0;
        std::int32_t result = 0;
        std::uint64_t resource = 0;
        std::vector<std::uint8_t> bytes;
    };

    struct Mailbox final
    {
        std::atomic<unsigned> references{1};
        std::mutex mutex;
        std::deque<Completion> queue;
        std::function<void()> wake;
    };

    static void retain_mailbox(void* context)
    {
        static_cast<Mailbox*>(context)->references.fetch_add(1, std::memory_order_relaxed);
    }

    static void release_mailbox(void* context)
    {
        auto* box = static_cast<Mailbox*>(context);
        if (box->references.fetch_sub(1, std::memory_order_acq_rel) == 1) delete box;
    }

    struct Pending final
    {
        OpKind kind = OpKind::None;
        fly_session_op_token_v2 token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        bool cancel_requested = false;
    };

    struct StreamMap final
    {
        std::uint64_t send = 0;
        std::uint64_t recv = 0;
    };

    template <typename Pred>
    bool wait_until(std::chrono::milliseconds timeout, Pred pred)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            drain();
            if (pred())
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        drain();
        return pred();
    }

    static void on_complete(void* context, std::uint64_t operation,
                            std::int32_t result, std::uint32_t, std::uint64_t resource,
                            const std::uint8_t* bytes, std::size_t size)
    {
        auto* box = static_cast<Mailbox*>(context);
        Completion item{};
        item.operation = operation;
        item.result = result;
        item.resource = resource;
        if (bytes != nullptr && size > 0)
            item.bytes.assign(bytes, bytes + size);
        std::lock_guard<std::mutex> lock(box->mutex);
        box->queue.push_back(std::move(item));
        if (box->wake) box->wake();
    }

    bool remember(std::uint64_t operation, OpKind kind,
                  const fly_session_op_token_v2* token = nullptr,
                  fly_session_inbox_v2_t* inbox = nullptr)
    {
        Pending pending{};
        pending.kind = kind;
        if (token != nullptr)
            pending.token = *token;
        pending.inbox = inbox;
        std::lock_guard<std::mutex> lock(mutex_);
        const auto inserted = pending_.emplace(operation, pending).second;
        if (inserted && inbox != nullptr)
            fly_session_inbox_retain_v2(inbox);
        return inserted;
    }

    void rollback_rejected_submit(std::uint64_t operation)
    {
        fly_session_inbox_v2_t* inbox = nullptr;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto found = pending_.find(operation);
            if (found == pending_.end()) return;
            inbox = found->second.inbox;
            pending_.erase(found);
        }
        fly_session_inbox_release_v2(inbox);
    }

    static ProductQuicPort* self(void* context)
    {
        return static_cast<ProductQuicPort*>(context);
    }

    static fly_session_result_v2 cancel(void* context, const fly_session_op_token_v2* token)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr) return FLY_SESSION_V2_INVALID_ARGUMENT;
        bool request_provider_cancel = false;
        {
            std::lock_guard<std::mutex> lock(port->mutex_);
            auto it = port->pending_.find(token->operation_id);
            if (it == port->pending_.end()) return FLY_SESSION_V2_OK;
            if (std::memcmp(&it->second.token, token, sizeof(*token)) != 0)
                return FLY_SESSION_V2_STALE;
            request_provider_cancel = !it->second.cancel_requested;
            it->second.cancel_requested = true;
        }
        // The Rust completion may already be queued (including a successful
        // creator result). Keep its original token and inbox until apply()
        // hands that exact completion to the engine.
        if (request_provider_cancel)
            (void)flynes_quic_provider_cancel(port->provider_, token->operation_id);
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 listen(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2, fly_session_bytes_v2,
        fly_session_resource_handle_v2, const fly_session_quic_connect_policy_v2*,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr || port->listener_handle_ == 0)
            return FLY_SESSION_V2_UNAVAILABLE;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Accept, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_accept(port->provider_, op,
                                        port->listener_handle_) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 connect(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2, fly_session_bytes_v2 endpoint,
        fly_session_resource_handle_v2,
        const fly_session_quic_connect_policy_v2* policy,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr || policy == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        const auto peer = quic_decode_ipv4_endpoint(endpoint.data, endpoint.size);
        if (peer.empty())
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        static constexpr char kBind[] = "127.0.0.1:0";
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Connect, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_connect(
                port->provider_, op,
                reinterpret_cast<const std::uint8_t*>(kBind), sizeof(kBind) - 1,
                reinterpret_cast<const std::uint8_t*>(peer.data()), peer.size(),
                policy->expected_der_spki_hash, 32, 30000) != FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 inspect(void* context,
                                         const fly_session_op_token_v2* token,
                                         fly_session_resource_handle_v2 connection,
                                         fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Inspect, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_inspect_handshake(port->provider_, op,
                                                   connection) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 exporter(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, fly_session_bytes_v2 label,
        fly_session_bytes_v2 exporter_context, std::uint32_t output_size,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        constexpr char expected_label[] = "EXPORTER-flynes-nearby-v1";
        if (label.data == nullptr || label.size != sizeof(expected_label) - 1 ||
            std::memcmp(label.data, expected_label, sizeof(expected_label) - 1) != 0 ||
            exporter_context.data == nullptr || exporter_context.size != 32 || output_size != 32)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Exporter, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_exporter(port->provider_, op, connection,
                                          exporter_context.data,
                                          exporter_context.size) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 close(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, std::uint32_t reason,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr || connection == 0)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        if (!port->remember(token->operation_id, OpKind::Close, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_close(port->provider_, token->operation_id, connection, reason)
                != FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(token->operation_id);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 open_bidi(void* context,
                                           const fly_session_op_token_v2* token,
                                           fly_session_resource_handle_v2 connection,
                                           std::uint32_t, std::uint32_t,
                                           fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::OpenBidi, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_open_bidi(port->provider_, op, connection) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 accept_bidi(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, std::uint32_t, std::uint32_t,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::AcceptBidi, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_accept_bidi(port->provider_, op, connection) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 write(void* context,
                                       const fly_session_op_token_v2* token,
                                       fly_session_resource_handle_v2 stream,
                                       fly_session_buffer_v2_t* buffer,
                                       std::uint32_t finish,
                                       fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        auto mapped = port->streams_.find(stream);
        const std::uint64_t send =
            mapped == port->streams_.end() ? stream : mapped->second.send;
        std::uint64_t size = 0;
        std::vector<std::uint8_t> copy;
        if (buffer != nullptr &&
            fly_session_buffer_size_v2(buffer, &size) == FLY_SESSION_V2_OK &&
            size > 0)
        {
            copy.resize(static_cast<std::size_t>(size));
            fly_session_write_bytes_v2 destination{copy.data(), size};
            std::uint64_t written = 0;
            if (fly_session_buffer_read_v2(buffer, 0, destination, &written) !=
                    FLY_SESSION_V2_OK ||
                written != size)
                return FLY_SESSION_V2_CONTRACT_VIOLATION;
        }
        port->bytes_written_ += size;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Write, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_write(port->provider_, op, send,
                                       copy.empty() ? nullptr : copy.data(),
                                       copy.size(), finish) != FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 grant_read(void* context,
                                            const fly_session_op_token_v2* token,
                                            fly_session_resource_handle_v2 stream,
                                            std::uint64_t credit,
                                            fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port == nullptr || token == nullptr)
            return FLY_SESSION_V2_UNAVAILABLE;
        auto mapped = port->streams_.find(stream);
        const std::uint64_t recv =
            mapped == port->streams_.end() ? stream : mapped->second.recv;
        const std::uint64_t op = token->operation_id;
        if (!port->remember(op, OpKind::Read, token, inbox))
            return FLY_SESSION_V2_DUPLICATE;
        if (flynes_quic_provider_grant_read_credit(
                port->provider_, op, recv, static_cast<std::size_t>(credit)) !=
            FLYNES_QUIC_ACCEPTED)
        {
            port->rollback_rejected_submit(op);
            return FLY_SESSION_V2_UNAVAILABLE;
        }
        return FLY_SESSION_V2_ACCEPTED;
    }

    bool wait_kind(OpKind kind, std::chrono::milliseconds timeout)
    {
        return wait_until(timeout, [this, kind] {
            if (kind == OpKind::GenerateMaterial)
                return material_handle_ != 0;
            if (kind == OpKind::Listen)
                return listener_handle_ != 0;
            return false;
        });
    }

    bool generate_self_signed_blocking(std::chrono::milliseconds timeout)
    {
        const std::uint64_t op = next_internal_++;
        if (!remember(op, OpKind::GenerateMaterial))
            return false;
        if (flynes_quic_provider_generate_self_signed(provider_, op) !=
            FLYNES_QUIC_ACCEPTED)
        {
            rollback_rejected_submit(op);
            return false;
        }
        return wait_kind(OpKind::GenerateMaterial, timeout);
    }

    bool prelisten_blocking(std::chrono::milliseconds timeout)
    {
        if (material_handle_ == 0)
            return false;
        static std::atomic<unsigned> next_port{49152};
        for (int attempt = 0; attempt < 32; ++attempt)
        {
            const unsigned port = next_port.fetch_add(1);
            if (port < 49152 || port > 65535)
            {
                next_port.store(49152);
                continue;
            }
            char bind[32];
            std::snprintf(bind, sizeof(bind), "127.0.0.1:%u", port);
            const std::uint64_t op = next_internal_++;
            if (!remember(op, OpKind::Listen))
                continue;
            if (flynes_quic_provider_listen(
                    provider_, op, reinterpret_cast<const std::uint8_t*>(bind),
                    std::strlen(bind), material_handle_, 30000) !=
                FLYNES_QUIC_ACCEPTED)
            {
                rollback_rejected_submit(op);
                continue;
            }
            if (wait_kind(OpKind::Listen, timeout) && listener_handle_ != 0)
            {
                unsigned a = 0, b = 0, c = 0, d = 0, bound = 0;
#ifdef _MSC_VER
                const int matched = sscanf_s(listen_address_.c_str(), "%u.%u.%u.%u:%u", &a, &b,
                                             &c, &d, &bound);
#else
                const int matched = std::sscanf(listen_address_.c_str(), "%u.%u.%u.%u:%u", &a, &b,
                                                &c, &d, &bound);
#endif
                if (matched == 5 &&
                    bound >= 49152)
                    return true;
                listener_handle_ = 0;
                listen_address_.clear();
            }
        }
        return false;
    }

    void apply(Completion& item)
    {
        Pending pending{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = pending_.find(item.operation);
            if (found == pending_.end())
                return;
            pending = found->second;
            pending_.erase(found);
        }
        if (pending.kind == OpKind::GenerateMaterial && item.result == FLYNES_QUIC_OK)
        {
            material_handle_ = item.resource;
            der_spki_ = item.bytes;
            der_spki_hash_ = flynes::session::wire::sha256(
                der_spki_.empty() ? nullptr : der_spki_.data(), der_spki_.size());
        }
        if (pending.kind == OpKind::Listen && item.result == FLYNES_QUIC_OK)
        {
            listener_handle_ = item.resource;
            listen_address_.assign(item.bytes.begin(), item.bytes.end());
            ++listens_;
        }
        if ((pending.kind == OpKind::Accept || pending.kind == OpKind::Connect) &&
            item.result == FLYNES_QUIC_OK && item.resource != 0)
        {
            connection_ = item.resource;
            if (pending.kind == OpKind::Connect) ++connects_;
        }
        if (pending.kind == OpKind::OpenBidi || pending.kind == OpKind::AcceptBidi)
        {
            last_stream_result_ = item.result;
            last_stream_bytes_ = item.bytes.size();
            if (item.result == FLYNES_QUIC_OK && item.resource != 0)
            {
                const std::uint64_t recv = item.bytes.size() == 8
                    ? quic_load_u64be(item.bytes.data())
                    : 0;
                const auto handle = next_stream_++;
                streams_[handle] = StreamMap{item.resource, recv};
                control_stream_ = handle;
            }
        }
        if (pending.kind == OpKind::Read && item.result == FLYNES_QUIC_OK)
            bytes_read_ += item.bytes.size();
        if (pending.kind == OpKind::Inspect)
        {
            ++inspections_;
            handshake_inspected_ = item.result == FLYNES_QUIC_OK &&
                quic_deliver_handshake(pending.inbox, pending.token,
                                       connection_, item.bytes);
            if (item.result != FLYNES_QUIC_OK)
                quic_deliver_end(pending.inbox, pending.token,
                                 FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                                 quic_map_provider_result(item.result));
            fly_session_inbox_release_v2(pending.inbox);
            return;
        }
        if (pending.inbox != nullptr)
        {
            if (pending.kind == OpKind::Accept || pending.kind == OpKind::Connect)
            {
                if (item.result == FLYNES_QUIC_OK && item.resource != 0)
                    quic_deliver_resource(pending.inbox, pending.token,
                                          FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2,
                                          item.resource);
                else
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2,
                                     quic_map_provider_result(item.result));
            }
            else if (pending.kind == OpKind::Exporter)
            {
                ++exporters_;
                if (item.result == FLYNES_QUIC_OK && item.bytes.size() == 32)
                    quic_deliver_buffer(pending.inbox, pending.token,
                                        FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
                                        item.bytes.data(), item.bytes.size(), 1);
                else
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
                                     quic_map_provider_result(item.result));
            }
            else if (pending.kind == OpKind::OpenBidi ||
                     pending.kind == OpKind::AcceptBidi)
            {
                if (item.result == FLYNES_QUIC_OK && item.bytes.size() == 8)
                    quic_deliver_resource(pending.inbox, pending.token,
                                          FLY_SESSION_PROVIDER_QUIC_STREAM_V2,
                                          control_stream_, control_stream_);
                else
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_STREAM_V2,
                                     quic_map_provider_result(item.result));
            }
            else if (pending.kind == OpKind::Write || pending.kind == OpKind::Close)
            {
                if (item.result == FLYNES_QUIC_OK)
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_END_V2,
                                     FLY_SESSION_V2_OK);
                else
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_END_V2,
                                     quic_map_provider_result(item.result));
            }
            else if (pending.kind == OpKind::Read)
            {
                if (item.result == FLYNES_QUIC_OK)
                    quic_deliver_buffer(pending.inbox, pending.token,
                                        FLY_SESSION_PROVIDER_QUIC_DATA_V2,
                                        item.bytes.data(), item.bytes.size(), 0);
                else
                    quic_deliver_end(pending.inbox, pending.token,
                                     FLY_SESSION_PROVIDER_QUIC_DATA_V2,
                                     quic_map_provider_result(item.result));
            }
            fly_session_inbox_release_v2(pending.inbox);
        }
    }

    FlynesQuicProvider* provider_ = nullptr;
    Mailbox* mailbox_ = new Mailbox();
    mutable std::mutex mutex_{};
    std::unordered_map<std::uint64_t, Pending> pending_{};
    std::unordered_map<std::uint64_t, StreamMap> streams_{};
    std::uint64_t next_internal_ = (std::uint64_t{1} << 40);
    std::uint64_t next_stream_ = 0x9001;
    std::uint64_t material_handle_ = 0;
    std::uint64_t listener_handle_ = 0;
    std::uint64_t connection_ = 0;
    bool handshake_inspected_ = false;
    std::uint64_t control_stream_ = 0;
    std::uint64_t bytes_written_ = 0;
    std::uint64_t bytes_read_ = 0;
    std::int32_t last_stream_result_ = 0;
    std::size_t last_stream_bytes_ = 0;
    std::vector<std::uint8_t> der_spki_{};
    std::array<std::uint8_t, 32> der_spki_hash_{};
    std::string listen_address_{};
    int listens_ = 0;
    int connects_ = 0;
    int inspections_ = 0;
    int exporters_ = 0;
};

} // namespace flynes::android::nearby
