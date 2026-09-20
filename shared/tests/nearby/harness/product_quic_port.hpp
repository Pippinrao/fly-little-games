/*
 * Product Quinn adapter for fly_session_quic_port_v2.
 *
 * Discovery/GATT/crypto stay on the loopback fixture. This port is the only
 * path that may claim a real QUIC connection: listen/connect, handshake
 * facts, exporter, and stream read/write go through flynes_quic_provider_*
 * and localhost sockets. Completions are queued off the Quinn thread and
 * delivered on the pump thread because the engine rejects terminals posted
 * from inside a provider callback.
 */

#ifndef FLYNES_TESTS_NEARBY_HARNESS_PRODUCT_QUIC_PORT_HPP
#define FLYNES_TESTS_NEARBY_HARNESS_PRODUCT_QUIC_PORT_HPP

#include "flynes_quic_provider.h"
#include "two_engine_loopback_fixture.hpp"
#include "wire/quic_contract.hpp"
#include "wire/sha256.hpp"
#include "ports/quic_provider_facts.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace flynes::session::quic_port {

inline void retain_context(void*) {}
inline void release_context(void*) {}

inline std::uint64_t load_u64be(const std::uint8_t* bytes)
{
    std::uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
        value = (value << 8) | bytes[i];
    return value;
}

inline std::array<std::uint8_t, 18> encode_ipv4_endpoint(const std::string& text)
{
    std::array<std::uint8_t, 18> out{};
    unsigned a = 0, b = 0, c = 0, d = 0, port = 0;
#ifdef _MSC_VER
    const int matched = sscanf_s(text.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#else
    const int matched = std::sscanf(text.c_str(), "%u.%u.%u.%u:%u", &a, &b, &c, &d, &port);
#endif
    if (matched != 5)
        return out;
    out[12] = static_cast<std::uint8_t>(a);
    out[13] = static_cast<std::uint8_t>(b);
    out[14] = static_cast<std::uint8_t>(c);
    out[15] = static_cast<std::uint8_t>(d);
    out[16] = static_cast<std::uint8_t>(port >> 8);
    out[17] = static_cast<std::uint8_t>(port);
    return out;
}

inline std::string decode_ipv4_endpoint(const std::uint8_t* bytes, std::size_t size)
{
    if (bytes == nullptr || size != 18)
        return {};
    const unsigned port =
        (static_cast<unsigned>(bytes[16]) << 8) | bytes[17];
    char text[64];
    std::snprintf(text, sizeof(text), "127.0.0.1:%u", port);
    if (bytes[12] != 0 || bytes[13] != 0 || bytes[14] != 0 || bytes[15] != 0)
        std::snprintf(text, sizeof(text), " %u.%u.%u.%u:%u" + 1, bytes[12],
                      bytes[13], bytes[14], bytes[15], port);
    return text;
}

class ProductQuicPort final
{
public:
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

    ProductQuicPort()
    {
        FlynesQuicCallbacks callbacks{};
        callbacks.struct_size = static_cast<std::uint32_t>(sizeof(callbacks));
        callbacks.abi_version = FLYNES_QUIC_PROVIDER_ABI_V1;
        callbacks.context = this;
        callbacks.retain = retain_context;
        callbacks.release = release_context;
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
        if (provider_ != nullptr)
            flynes_quic_provider_release(provider_);
        fly_session_inbox_release_v2(last_read_inbox_);
    }

    ProductQuicPort(const ProductQuicPort&) = delete;
    ProductQuicPort& operator=(const ProductQuicPort&) = delete;

    [[nodiscard]] bool ready() const noexcept { return provider_ != nullptr; }
    [[nodiscard]] const char* provider_type() const noexcept
    {
        return "FlynesQuicProvider/Quinn";
    }
    [[nodiscard]] const std::string& listen_address() const noexcept
    {
        return listen_address_;
    }
    [[nodiscard]] std::array<std::uint8_t, 18> listen_endpoint() const
    {
        return encode_ipv4_endpoint(listen_address_);
    }
    [[nodiscard]] std::uint64_t material_handle() const noexcept
    {
        return material_handle_;
    }
    [[nodiscard]] const std::vector<std::uint8_t>& der_spki() const noexcept
    {
        return der_spki_;
    }
    [[nodiscard]] std::uint64_t bytes_written() const noexcept
    {
        return bytes_written_;
    }
    [[nodiscard]] std::uint64_t bytes_read() const noexcept { return bytes_read_; }
    [[nodiscard]] int listens() const noexcept { return listens_; }
    [[nodiscard]] int connects() const noexcept { return connects_; }
    [[nodiscard]] int handshake_inspections() const noexcept
    {
        return inspections_;
    }
    [[nodiscard]] int exporters() const noexcept { return exporters_; }
    [[nodiscard]] int streams_opened() const noexcept { return streams_opened_; }
    [[nodiscard]] int pending_ops() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(pending_.size() + queue_.size());
    }
    [[nodiscard]] FlynesQuicProvider* raw() const noexcept { return provider_; }

    struct ReadFailure final {
        fly_session_port_event_v2 event{};
        fly_session_result_v2 admission = FLY_SESSION_V2_INVALID_STATE;
    };
    std::vector<ReadFailure> read_failures;
    int cancelled_operations = 0;
    [[nodiscard]] int pending_reads() const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return static_cast<int>(std::count_if(pending_.begin(), pending_.end(),
            [](const auto& entry) { return entry.second.kind == OpKind::Read; }));
    }
    bool close_transport_for_test()
    {
        const auto op = next_internal_++;
        remember(op, OpKind::Close, {}, nullptr, false);
        return connection_ != 0 && flynes_quic_provider_close(provider_, op, connection_, 1) ==
            FLYNES_QUIC_ACCEPTED;
    }
    bool prioritize_oldest_read_failure_for_test()
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& entry : pending_)
            if (entry.second.kind == OpKind::Read &&
                (preferred_failure_operation_ == 0 || entry.first < preferred_failure_operation_))
                preferred_failure_operation_ = entry.first;
        return preferred_failure_operation_ != 0;
    }
    fly_session_result_v2 replay_failure(const ReadFailure& failure)
    {
        return last_read_inbox_ == nullptr ? FLY_SESSION_V2_CLOSED :
            fly_session_deliver_v2(last_read_inbox_, &failure.event);
    }

    bool generate_self_signed_blocking(std::chrono::milliseconds timeout)
    {
        const std::uint64_t op = next_internal_++;
        remember(op, OpKind::GenerateMaterial, {}, nullptr, false);
        if (flynes_quic_provider_generate_self_signed(provider_, op) !=
            FLYNES_QUIC_ACCEPTED)
            return false;
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
            remember(op, OpKind::Listen, {}, nullptr, true);
            if (flynes_quic_provider_listen(
                    provider_, op, reinterpret_cast<const std::uint8_t*>(bind),
                    std::strlen(bind), material_handle_, 30000) !=
                FLYNES_QUIC_ACCEPTED)
                continue;
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

    fly_session_quic_port_v2 port()
    {
        fly_session_quic_port_v2 value{};
        value.struct_size = FLY_SESSION_QUIC_PORT_V2_SIZE;
        value.abi_version = FLY_SESSION_ABI_VERSION_2;
        value.context = this;
        value.retain = loopback::retain_noop;
        value.release = loopback::release_noop;
        value.listen = &listen;
        value.connect = &connect;
        value.inspect_handshake = &inspect;
        value.exporter = &exporter;
        value.open_uni = loopback::unavailable_quic_stream;
        value.open_bidi = &open_bidi;
        value.accept_uni = loopback::unavailable_quic_stream;
        value.accept_bidi = &accept_bidi;
        value.write = &write;
        value.finish = loopback::unavailable_quic_control;
        value.reset = loopback::unavailable_quic_control;
        value.grant_read_credit = &grant_read;
        value.send_datagram = loopback::unavailable_quic_datagram;
        value.payload_budget = loopback::unavailable_quic_query;
        value.stats = loopback::unavailable_quic_query;
        value.close = &close;
        value.cancel = &cancel;
        return value;
    }

    int drain()
    {
        std::deque<Completion> batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            // Fault tests select a legal ordering of actual queued callbacks:
            // control failure first, so its cleanup must cancel the sibling read.
            // Wait for that genuine terminal instead of manufacturing any event.
            if (preferred_failure_operation_ != 0) {
                const auto first = std::find_if(queue_.begin(), queue_.end(),
                    [this](const Completion& item) {
                        return item.operation == preferred_failure_operation_ &&
                            item.result != FLYNES_QUIC_OK;
                    });
                if (first == queue_.end()) return 0;
                std::rotate(queue_.begin(), first, std::next(first));
                preferred_failure_operation_ = 0;
            }
            batch.swap(queue_);
        }
        int delivered = 0;
        for (auto& item : batch)
        {
            if (apply(item))
                ++delivered;
        }
        return delivered;
    }

private:
    struct Pending final
    {
        OpKind kind = OpKind::None;
        fly_session_op_token_v2 token{};
        fly_session_inbox_v2_t* inbox = nullptr;
        bool engine_visible = false;
        std::uint64_t follow_up = 0;
        fly_session_resource_handle_v2 stream = 0;
        std::uint64_t credit = 0;
    };

    struct StreamMap final
    {
        std::uint64_t send = 0;
        std::uint64_t recv = 0;
    };

    static void on_complete(void* context, std::uint64_t operation,
                            std::int32_t result, std::uint32_t, std::uint64_t resource,
                            const std::uint8_t* bytes, std::size_t size)
    {
        auto* self = static_cast<ProductQuicPort*>(context);
        Completion item{};
        item.operation = operation;
        item.result = result;
        item.resource = resource;
        if (bytes != nullptr && size > 0)
            item.bytes.assign(bytes, bytes + size);
        std::lock_guard<std::mutex> lock(self->mutex_);
        self->queue_.push_back(std::move(item));
    }

    void remember(std::uint64_t operation, OpKind kind,
                  const fly_session_op_token_v2& token,
                  fly_session_inbox_v2_t* inbox, bool engine_visible)
    {
        Pending pending{};
        pending.kind = kind;
        pending.token = token;
        pending.inbox = inbox;
        pending.engine_visible = engine_visible;
        if (inbox != nullptr)
            fly_session_inbox_retain_v2(inbox);
        std::lock_guard<std::mutex> lock(mutex_);
        pending_[operation] = pending;
    }

    bool wait_kind(OpKind kind, std::chrono::milliseconds timeout)
    {
        const auto deadline = std::chrono::steady_clock::now() + timeout;
        while (std::chrono::steady_clock::now() < deadline)
        {
            drain();
            if (kind == OpKind::GenerateMaterial && material_handle_ != 0)
                return true;
            if (kind == OpKind::Listen && listener_handle_ != 0)
                return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        drain();
        return (kind == OpKind::GenerateMaterial && material_handle_ != 0) ||
               (kind == OpKind::Listen && listener_handle_ != 0);
    }

    fly_session_result_v2 map_result(std::int32_t result) const
    {
        if (result == FLYNES_QUIC_OK)
            return FLY_SESSION_V2_OK;
        if (result == FLYNES_QUIC_CANCELLED)
            return FLY_SESSION_V2_CANCELLED;
        if (result == FLYNES_QUIC_CLOSED)
            return FLY_SESSION_V2_UNAVAILABLE;
        if (result == FLYNES_QUIC_INVALID_ARGUMENT ||
            result == FLYNES_QUIC_INVALID_HANDLE)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        return FLY_SESSION_V2_UNAVAILABLE;
    }

    bool apply(Completion& item)
    {
        Pending pending{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto found = pending_.find(item.operation);
            if (found == pending_.end())
                return false;
            pending = found->second;
            pending_.erase(found);
        }
        if (pending.kind == OpKind::GenerateMaterial)
        {
            if (item.result == FLYNES_QUIC_OK)
            {
                material_handle_ = item.resource;
                der_spki_ = item.bytes;
            }
            if (pending.inbox != nullptr)
                fly_session_inbox_release_v2(pending.inbox);
            return true;
        }
        if (pending.kind == OpKind::Listen)
        {
            if (item.result == FLYNES_QUIC_OK)
            {
                listener_handle_ = item.resource;
                listen_address_.assign(item.bytes.begin(), item.bytes.end());
                ++listens_;
            }
            if (pending.inbox != nullptr)
                fly_session_inbox_release_v2(pending.inbox);
            return true;
        }
        if (pending.kind == OpKind::Accept || pending.kind == OpKind::Connect)
        {
            const bool ok = item.result == FLYNES_QUIC_OK && item.resource != 0;
            if (ok)
            {
                connection_ = item.resource;
                if (pending.kind == OpKind::Connect)
                    ++connects_;
            }
            if (pending.engine_visible && pending.inbox != nullptr)
            {
                if (ok)
                    loopback::deliver_provider_resource(
                        pending.inbox, pending.token,
                        FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2, item.resource);
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.kind == OpKind::Inspect)
        {
            ++inspections_;
            if (pending.inbox != nullptr)
            {
                if (item.result == FLYNES_QUIC_OK)
                    deliver_handshake(pending, item.bytes);
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.kind == OpKind::Exporter)
        {
            ++exporters_;
            if (pending.inbox != nullptr)
            {
                if (item.result == FLYNES_QUIC_OK && item.bytes.size() == 32)
                    loopback::deliver_provider_buffer(
                        pending.inbox, pending.token,
                        FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2, item.bytes.data(),
                        item.bytes.size());
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_EXPORTER_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.kind == OpKind::OpenBidi || pending.kind == OpKind::AcceptBidi)
        {
            if (pending.inbox != nullptr)
            {
                if (item.result == FLYNES_QUIC_OK && item.bytes.size() == 8)
                {
                    const std::uint64_t recv = load_u64be(item.bytes.data());
                    const auto handle = next_stream_++;
                    streams_[handle] = StreamMap{item.resource, recv};
                    ++streams_opened_;
                    loopback::deliver_provider_resource_pair(
                        pending.inbox, pending.token,
                        FLY_SESSION_PROVIDER_QUIC_STREAM_V2, handle, handle);
                }
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_STREAM_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.kind == OpKind::Write)
        {
            if (pending.inbox != nullptr)
            {
                if (item.result == FLYNES_QUIC_OK)
                    loopback::deliver_provider_end(
                        pending.inbox, pending.token,
                        FLY_SESSION_PROVIDER_QUIC_END_V2);
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_END_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.kind == OpKind::Read)
        {
            if (item.result == FLYNES_QUIC_OK)
                bytes_read_ += item.bytes.size();
            if (pending.inbox != nullptr)
            {
                if (item.result == FLYNES_QUIC_OK)
                    loopback::deliver_provider_stream_data(
                        pending.inbox, pending.token, item.bytes.data(),
                        item.bytes.size());
                else
                    deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_DATA_V2,
                                    map_result(item.result));
                fly_session_inbox_release_v2(pending.inbox);
            }
            return true;
        }
        if (pending.inbox != nullptr)
            fly_session_inbox_release_v2(pending.inbox);
        return true;
    }

    void deliver_handshake(const Pending& pending,
                           const std::vector<std::uint8_t>& raw)
    {
        wire::QuicHandshakeFactsV2 wire_facts{};
        if (!ports::decode_quic_provider_facts(raw.data(), raw.size(), &wire_facts))
        {
            deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                            FLY_SESSION_V2_CONTRACT_VIOLATION);
            return;
        }
        std::array<std::uint8_t, wire::kQuicHandshakeFactsWireSizeV2> encoded{};
        if (wire::encode_quic_handshake_facts_v2(wire_facts, &encoded) !=
            wire::Status::Ok)
        {
            std::fprintf(stderr,
                         "quic handshake encode failed pin=%u verified=%u "
                         "tls=%u.%u alpn0=%d\n",
                         static_cast<unsigned>(wire_facts.pin_verifier_invoked),
                         static_cast<unsigned>(wire_facts.peer_certificate_verified),
                         wire_facts.tls_major, wire_facts.tls_minor,
                         static_cast<int>(wire_facts.alpn[0]));
            deliver_failure(pending, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
                            FLY_SESSION_V2_UNAVAILABLE);
            return;
        }
        const auto hash = wire::sha256(encoded.data(), encoded.size());
        loopback::deliver_provider_hash_buffer(
            pending.inbox, pending.token, FLY_SESSION_PROVIDER_QUIC_HANDSHAKE_V2,
            pending.token.connection_generation == 0 ? connection_ : connection_,
            encoded.data(), encoded.size(), hash);
    }

    void deliver_failure(const Pending& pending, std::uint32_t kind,
                         fly_session_result_v2 result)
    {
        fly_session_port_event_v2 event{};
        event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE;
        event.abi_version = FLY_SESSION_ABI_VERSION_2;
        event.token = pending.token;
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
        std::fprintf(stderr, "quic fail kind=0x%x result=%d op=%llu\n",
                     static_cast<unsigned>(kind), static_cast<int>(result),
                     static_cast<unsigned long long>(pending.token.operation_id));
        const auto admission = fly_session_deliver_v2(pending.inbox, &event);
        if (pending.kind == OpKind::Read) {
            read_failures.push_back(ReadFailure{event, admission});
            if (last_read_inbox_ == nullptr) {
                last_read_inbox_ = pending.inbox;
                fly_session_inbox_retain_v2(last_read_inbox_);
            }
        }
    }

    static ProductQuicPort* self(void* context)
    {
        return static_cast<ProductQuicPort*>(context);
    }

    static fly_session_result_v2 listen(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2, fly_session_bytes_v2,
        fly_session_resource_handle_v2, const fly_session_quic_connect_policy_v2*,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        if (port->listener_handle_ == 0)
            return FLY_SESSION_V2_UNAVAILABLE;
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Accept, *token, inbox, true);
        std::fprintf(stderr, "quic listen/accept op=%llu listener=%llu\n",
                     static_cast<unsigned long long>(op),
                     static_cast<unsigned long long>(port->listener_handle_));
        if (flynes_quic_provider_accept(port->provider_, op,
                                        port->listener_handle_) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
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
        if (policy == nullptr)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        const auto peer = decode_ipv4_endpoint(endpoint.data, endpoint.size);
        if (peer.empty())
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        static constexpr char kBind[] = "127.0.0.1:0";
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Connect, *token, inbox, true);
        std::fprintf(stderr,
                     "quic connect op=%llu peer=%s pin=%02x%02x%02x%02x\n",
                     static_cast<unsigned long long>(op), peer.c_str(),
                     policy->expected_der_spki_hash[0],
                     policy->expected_der_spki_hash[1],
                     policy->expected_der_spki_hash[2],
                     policy->expected_der_spki_hash[3]);
        if (flynes_quic_provider_connect(
                port->provider_, op,
                reinterpret_cast<const std::uint8_t*>(kBind), sizeof(kBind) - 1,
                reinterpret_cast<const std::uint8_t*>(peer.data()), peer.size(),
                policy->expected_der_spki_hash, 32, 30000) != FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        port->peer_address_ = peer;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 inspect(void* context,
                                         const fly_session_op_token_v2* token,
                                         fly_session_resource_handle_v2 connection,
                                         fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Inspect, *token, inbox, true);
        if (flynes_quic_provider_inspect_handshake(port->provider_, op,
                                                   connection) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 exporter(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, fly_session_bytes_v2,
        fly_session_bytes_v2 exporter_context, std::uint32_t, fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Exporter, *token, inbox, true);
        if (flynes_quic_provider_exporter(port->provider_, op, connection,
                                          exporter_context.data,
                                          exporter_context.size) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 open_bidi(void* context,
                                           const fly_session_op_token_v2* token,
                                           fly_session_resource_handle_v2 connection,
                                           std::uint32_t, std::uint32_t,
                                           fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::OpenBidi, *token, inbox, true);
        if (flynes_quic_provider_open_bidi(port->provider_, op, connection) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 accept_bidi(
        void* context, const fly_session_op_token_v2* token,
        fly_session_resource_handle_v2 connection, std::uint32_t, std::uint32_t,
        fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::AcceptBidi, *token, inbox, true);
        if (flynes_quic_provider_accept_bidi(port->provider_, op, connection) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
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
        port->remember(op, OpKind::Write, *token, inbox, true);
        if (flynes_quic_provider_write(port->provider_, op, send,
                                       copy.empty() ? nullptr : copy.data(),
                                       copy.size(), finish) != FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 grant_read(void* context,
                                            const fly_session_op_token_v2* token,
                                            fly_session_resource_handle_v2 stream,
                                            std::uint64_t credit,
                                            fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        auto mapped = port->streams_.find(stream);
        const std::uint64_t recv =
            mapped == port->streams_.end() ? stream : mapped->second.recv;
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Read, *token, inbox, true);
        if (flynes_quic_provider_grant_read_credit(
                port->provider_, op, recv, static_cast<std::size_t>(credit)) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 close(void* context,
                                       const fly_session_op_token_v2* token,
                                       fly_session_resource_handle_v2 connection,
                                       std::uint32_t reason,
                                       fly_session_inbox_v2_t* inbox)
    {
        auto* port = self(context);
        const std::uint64_t op = token->operation_id;
        port->remember(op, OpKind::Close, *token, inbox, true);
        if (flynes_quic_provider_close(port->provider_, op, connection, reason) !=
            FLYNES_QUIC_ACCEPTED)
            return FLY_SESSION_V2_UNAVAILABLE;
        return FLY_SESSION_V2_ACCEPTED;
    }

    static fly_session_result_v2 cancel(void* context,
                                        const fly_session_op_token_v2* token)
    {
        auto* port = self(context);
        flynes_quic_provider_cancel(port->provider_, token->operation_id);
        ++port->cancelled_operations;
        return FLY_SESSION_V2_OK;
    }

    FlynesQuicProvider* provider_ = nullptr;
    fly_session_inbox_v2_t* last_read_inbox_ = nullptr;
    std::uint64_t preferred_failure_operation_ = 0;
    mutable std::mutex mutex_{};
    std::deque<Completion> queue_{};
    std::unordered_map<std::uint64_t, Pending> pending_{};
    std::unordered_map<std::uint64_t, StreamMap> streams_{};
    std::uint64_t next_internal_ = (std::uint64_t{1} << 40);
    std::uint64_t next_stream_ = 0x9001;
    std::uint64_t material_handle_ = 0;
    std::uint64_t listener_handle_ = 0;
    std::uint64_t connection_ = 0;
    std::vector<std::uint8_t> der_spki_{};
    std::string listen_address_{};
    std::string peer_address_{};
    std::uint64_t bytes_written_ = 0;
    std::uint64_t bytes_read_ = 0;
    int listens_ = 0;
    int connects_ = 0;
    int inspections_ = 0;
    int exporters_ = 0;
    int streams_opened_ = 0;
};

} // namespace flynes::session::quic_port

#endif
