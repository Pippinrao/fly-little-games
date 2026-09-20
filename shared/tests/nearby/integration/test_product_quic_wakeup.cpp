#include <flynes/flynes_session.h>

void test_inbox_retain(fly_session_inbox_v2_t*);
void test_inbox_release(fly_session_inbox_v2_t*);
fly_session_result_v2 test_inbox_deliver(fly_session_inbox_v2_t*,
                                         const fly_session_port_event_v2*);

// Inject only the opaque engine-inbox boundary; all provider work uses the
// production ProductQuicPort and real Quinn callbacks.
#define fly_session_inbox_retain_v2 test_inbox_retain
#define fly_session_inbox_release_v2 test_inbox_release
#define fly_session_deliver_v2 test_inbox_deliver
#include "product_quic_port.hpp"
#undef fly_session_inbox_retain_v2
#undef fly_session_inbox_release_v2
#undef fly_session_deliver_v2

#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace {
struct Notification {
    std::mutex mutex;
    std::condition_variable cv;
    unsigned count = 0;
    void signal() { std::lock_guard<std::mutex> lock(mutex); ++count; cv.notify_all(); }
    bool wait() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [this] { return count > 0; });
    }
    unsigned snapshot() {
        std::lock_guard<std::mutex> lock(mutex);
        return count;
    }
    bool wait_after(unsigned previous) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3),
                           [this, previous] { return count > previous; });
    }
};
struct RetryInbox {
    int references = 1;
    int releases = 0;
    std::vector<fly_session_port_event_v2> attempts;
    fly_session_inbox_v2_t* handle() {
        return reinterpret_cast<fly_session_inbox_v2_t*>(this);
    }
};
int failures = 0;
void check(bool condition, const char* message) {
    if (!condition) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
fly_session_op_token_v2 token(std::uint64_t id) {
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE;
    value.abi_version = FLY_SESSION_ABI_VERSION_2;
    value.engine_instance_id[0] = 0xa6;
    value.connection_generation = 7;
    value.authority_term = 9;
    value.operation_id = id;
    return value;
}
}

void test_inbox_retain(fly_session_inbox_v2_t* handle) {
    if (handle != nullptr)
        ++reinterpret_cast<RetryInbox*>(handle)->references;
}
void test_inbox_release(fly_session_inbox_v2_t* handle) {
    if (handle != nullptr) {
        auto& inbox = *reinterpret_cast<RetryInbox*>(handle);
        --inbox.references;
        ++inbox.releases;
    }
}
fly_session_result_v2 test_inbox_deliver(fly_session_inbox_v2_t* handle,
                                         const fly_session_port_event_v2* event) {
    auto& inbox = *reinterpret_cast<RetryInbox*>(handle);
    inbox.attempts.push_back(*event);
    return inbox.attempts.size() == 1 ? FLY_SESSION_V2_BACKPRESSURE
                                      : FLY_SESSION_V2_ACCEPTED;
}

int main() {
    Notification listener_notice, connector_notice;
    flynes::android::nearby::ProductQuicPort listener, connector;
    check(listener.ready() && connector.ready(), "real Quinn listeners ready");
    listener.set_wakeup([&] { listener_notice.signal(); });
    connector.set_wakeup([&] { connector_notice.signal(); });
    auto lp = listener.port();
    auto cp = connector.port();
    auto lt = token(100), ct = token(101);
    auto endpoint = listener.listen_endpoint();
    fly_session_bytes_v2 endpoint_bytes{endpoint.data(), static_cast<std::uint32_t>(endpoint.size()), 0};
    fly_session_quic_connect_policy_v2 policy{};
    policy.struct_size = FLY_SESSION_QUIC_CONNECT_POLICY_V2_SIZE;
    policy.abi_version = FLY_SESSION_ABI_VERSION_2;
    std::copy(listener.der_spki_hash().begin(), listener.der_spki_hash().end(),
              policy.expected_der_spki_hash);
    check(lp.listen(lp.context, &lt, 0, {}, 0, nullptr, nullptr) == FLY_SESSION_V2_ACCEPTED,
          "listen accepted");
    check(cp.connect(cp.context, &ct, 0, endpoint_bytes, 0, &policy, nullptr) == FLY_SESSION_V2_ACCEPTED,
          "connect accepted");
    // No snapshot, drain, polling or test pump may wake the owner for the callback.
    check(listener_notice.wait(), "listener completion actively wakes idle owner");
    check(connector_notice.wait(), "connector completion actively wakes idle owner");
    listener.drain(); connector.drain();
    check(listener.connection() != 0 && connector.connection() != 0, "real connections completed");
    auto pending = token(102);
    check(lp.listen(lp.context, &pending, 0, {}, 0, nullptr, nullptr) == FLY_SESSION_V2_ACCEPTED,
          "pending accept accepted");
    check(lp.inspect_handshake(lp.context, &pending, listener.connection(), nullptr)
              == FLY_SESSION_V2_DUPLICATE,
          "duplicate operation ID cannot replace pending accept");
    auto stale = pending;
    stale.connection_generation = 99;
    check(lp.cancel(lp.context, &stale) == FLY_SESSION_V2_STALE,
          "wrong-generation cancellation cannot cancel current operation");
    const auto before_cancel = listener_notice.snapshot();
    check(lp.cancel(lp.context, &pending) == FLY_SESSION_V2_ACCEPTED,
          "pending cancel waits for its exact provider terminal");
    check(listener.pending_count() > 0,
          "pending cancel keeps ownership until callback handoff");
    check(listener_notice.wait_after(before_cancel), "cancel terminal wakes owner");
    listener.drain();
    check(listener.pending_count() == 0,
          "cancel terminal releases pending operation after handoff");
    auto invalid_export = token(103);
    constexpr char wrong_label[] = "wrong-exporter";
    const fly_session_bytes_v2 label{
        reinterpret_cast<const std::uint8_t*>(wrong_label), sizeof(wrong_label) - 1, 0};
    check(cp.exporter(cp.context, &invalid_export, connector.connection(), label, {}, 32, nullptr)
              == FLY_SESSION_V2_INVALID_ARGUMENT, "invalid exporter parameters rejected synchronously");
    check(connector.pending_count() == 0, "synchronous rejection retains no pending operation");
    auto rejected = token(0); // The provider rejects zero operation IDs before scheduling.
    check(cp.inspect_handshake(cp.context, &rejected, connector.connection(), nullptr)
              != FLY_SESSION_V2_ACCEPTED, "provider synchronous rejection is returned to caller");
    check(connector.pending_count() == 0, "rejected inspect retains no pending inbox");
    rejected = token(105);
    check(cp.write(cp.context, &rejected, 0, nullptr, 2, nullptr)
              != FLY_SESSION_V2_ACCEPTED, "noncanonical FIN flag rejected synchronously");
    check(connector.pending_count() == 0, "rejected write retains no pending inbox");
    rejected = token(106);
    check(cp.grant_read_credit(cp.context, &rejected, 0, 0, nullptr)
              != FLY_SESSION_V2_ACCEPTED, "zero read credit rejected synchronously");
    check(connector.pending_count() == 0, "rejected read retains no pending inbox");

    const auto first_server_connection = listener.connection();
    Notification second_connector_notice;
    flynes::android::nearby::ProductQuicPort second_connector;
    check(second_connector.ready(), "second real Quinn connector ready");
    second_connector.set_wakeup([&] { second_connector_notice.signal(); });
    auto second_port = second_connector.port();
    auto second_accept = token(107), second_connect = token(108);
    RetryInbox connect_inbox;
    const auto before_second_accept = listener_notice.snapshot();
    check(lp.listen(lp.context, &second_accept, 0, {}, 0, nullptr, nullptr)
              == FLY_SESSION_V2_ACCEPTED, "second accept accepted");
    check(second_port.connect(second_port.context, &second_connect, 0,
                              endpoint_bytes, 0, &policy, connect_inbox.handle())
              == FLY_SESSION_V2_ACCEPTED, "second connect accepted");
    check(listener_notice.wait_after(before_second_accept), "second accept wakes owner");
    check(second_connector_notice.wait(), "second connect wakes owner");
    listener.drain(); second_connector.drain();
    check(connect_inbox.attempts.size() == 1 && connect_inbox.references == 2 &&
              second_connector.pending_count() > 0,
          "backpressured Connect resource retains its original inbox and Pending");
    second_connector.drain();
    check(connect_inbox.attempts.size() == 2 &&
              std::memcmp(&connect_inbox.attempts[0], &connect_inbox.attempts[1],
                          sizeof(fly_session_port_event_v2)) == 0 &&
              std::memcmp(&connect_inbox.attempts[1].token, &second_connect,
                          sizeof(second_connect)) == 0 &&
              connect_inbox.attempts[1].event_sequence == 1 &&
              connect_inbox.attempts[1].payload_kind ==
                  FLY_SESSION_PROVIDER_QUIC_CONNECTION_V2 &&
              connect_inbox.references == 1 && connect_inbox.releases == 1 &&
              second_connector.pending_count() == 0,
          "Connect resource retries identical original event and releases once");
    fly_session_provider_resource_event_v2 connected_payload{};
    if (connect_inbox.attempts.size() == 2)
        std::memcpy(&connected_payload, connect_inbox.attempts[1].payload,
                    sizeof(connected_payload));
    check(connect_inbox.attempts.size() == 2 &&
              connect_inbox.attempts[1].payload_size == sizeof(connected_payload) &&
              connected_payload.resource == second_connector.connection() &&
              second_connector.connects() == 1,
          "Connect retry preserves the provider resource without repeating side effects");
    second_connector.drain();
    check(connect_inbox.attempts.size() == 2 && second_connector.connects() == 1,
          "settled Connect has no further retry or side effect");
    const auto second_server_connection = listener.connection();
    check(first_server_connection != 0 && second_server_connection != 0 &&
              first_server_connection != second_server_connection,
          "two server connections have distinct handles");

    auto first_stream = token(109), second_stream = token(110);
    auto before_stream = listener_notice.snapshot();
    check(lp.open_bidi(lp.context, &first_stream, first_server_connection, 0, 1, nullptr)
              == FLY_SESSION_V2_ACCEPTED, "first server stream accepted");
    check(listener_notice.wait_after(before_stream), "first stream wakes owner");
    listener.drain();
    before_stream = listener_notice.snapshot();
    check(lp.open_bidi(lp.context, &second_stream, second_server_connection, 0, 1, nullptr)
              == FLY_SESSION_V2_ACCEPTED, "second server stream accepted");
    check(listener_notice.wait_after(before_stream), "second stream wakes owner");
    listener.drain();
    check(listener.streams_opened() == 2, "two connections own separate stream mappings");
    const auto remaining_stream = listener.control_stream();

    auto close_first = token(111);
    RetryInbox close_inbox;
    const auto before_close_first = listener_notice.snapshot();
    check(lp.close(lp.context, &close_first, first_server_connection, 0,
                   close_inbox.handle())
              == FLY_SESSION_V2_ACCEPTED, "first server connection close accepted");
    check(listener_notice.wait_after(before_close_first), "first close wakes owner");
    listener.drain();
    check(close_inbox.attempts.size() == 1 && close_inbox.references == 2 &&
              listener.pending_count() > 0 && listener.streams_opened() == 2,
          "backpressured Close END retains inbox and defers stream retirement");
    listener.drain();
    check(close_inbox.attempts.size() == 2 &&
              std::memcmp(&close_inbox.attempts[0], &close_inbox.attempts[1],
                          sizeof(fly_session_port_event_v2)) == 0 &&
              std::memcmp(&close_inbox.attempts[1].token, &close_first,
                          sizeof(close_first)) == 0 &&
              close_inbox.attempts[1].event_sequence == 1 &&
              close_inbox.attempts[1].payload_kind == FLY_SESSION_PROVIDER_QUIC_END_V2 &&
              close_inbox.attempts[1].result == FLY_SESSION_V2_OK &&
              close_inbox.references == 1 && close_inbox.releases == 1 &&
              listener.pending_count() == 0,
          "Close END retries identical original event and releases once");
    check(listener.streams_opened() == 1 && listener.control_stream() == remaining_stream,
          "closing first connection retains only second connection stream");
    listener.drain();
    check(close_inbox.attempts.size() == 2 && listener.streams_opened() == 1,
          "settled Close has no further retry or cleanup");

    auto closing = token(104);
    check(cp.close(cp.context, &closing, connector.connection(), 0, nullptr) == FLY_SESSION_V2_ACCEPTED,
          "production close is connected to the real provider");
    listener.set_wakeup({}); connector.set_wakeup({}); second_connector.set_wakeup({});
    return failures == 0 ? 0 : 1;
}
