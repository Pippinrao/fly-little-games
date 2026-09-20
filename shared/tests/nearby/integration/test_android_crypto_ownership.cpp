// Engine ownership oracle only. Existing loopback crypto is a deterministic
// protocol fixture, NOT cryptographic evidence and NOT the Android adapter.
// Link wrappers observe real public-engine provider calls without changing them.
#include "../harness/two_engine_loopback_fixture.hpp"
#include "crypto_port.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

using namespace flynes::session::loopback;
using flynes::android::nearby::CryptoPort;

namespace {
struct Observer {
    void* context = nullptr;
    fly_session_operation_cancel_v2 cancel = nullptr;
    fly_session_crypto_release_secret_v2 release = nullptr;
    std::vector<fly_session_op_token_v2> cancelled;
    std::vector<fly_session_resource_handle_v2> released;
    const fly_session_crypto_port_v2* adapter = nullptr;
    fly_session_op_token_v2 adapter_producer{};
    fly_session_resource_handle_v2 adapter_output = 0;
};
std::vector<Observer> observers;
Observer& observer(void* context) {
    return *std::find_if(observers.begin(), observers.end(),
        [context](const Observer& value) { return value.context == context; });
}
bool same(const fly_session_op_token_v2& a, const fly_session_op_token_v2& b) {
    return std::memcmp(&a, &b, sizeof(a)) == 0;
}
fly_session_result_v2 observe_cancel(void* context, const fly_session_op_token_v2* token) {
    auto& item = observer(context);
    item.cancelled.push_back(*token);
    if (item.adapter && same(item.adapter_producer, *token))
        check(item.adapter->cancel(item.adapter->context, token) == FLY_SESSION_V2_OK,
              "real engine producer cancel reaches actual adapter");
    return item.cancel(context, token);
}
fly_session_result_v2 observe_release(void* context, fly_session_resource_handle_v2 handle) {
    auto& item = observer(context);
    item.released.push_back(handle);
    if (item.adapter && item.adapter_output == handle)
        check(item.adapter->release_secret(item.adapter->context, handle) == FLY_SESSION_V2_OK,
              "real engine owned-resource release reaches actual adapter");
    return item.release(context, handle);
}

struct DeliveryProbe {
    std::mutex mutex;
    std::condition_variable cv;
    fly_session_op_token_v2 token{};
    fly_session_resource_handle_v2 output = 0;
    bool delivered = false;
};
std::mutex probe_mutex;
std::shared_ptr<DeliveryProbe> delivery_probe;
struct BackendState {
    std::mutex mutex;
    std::condition_variable cv;
    fly_session_resource_handle_v2 root = 0;
    int closes = 0;
};
struct OpaqueFixtureSecret : CryptoPort::Secret {
    std::shared_ptr<BackendState> state;
    explicit OpaqueFixtureSecret(std::shared_ptr<BackendState> value) : state(std::move(value)) {}
    void close() noexcept override {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->closes; state->cv.notify_all();
    }
};
struct LifetimeBackend : CryptoPort::Backend {
    std::shared_ptr<BackendState> state = std::make_shared<BackendState>();
    fly_session_op_token_v2 root_producer{};
    CryptoPort::Result execute(const CryptoPort::Request& request, CryptoPort::Secret* input) override {
        CryptoPort::Result result;
        result.result = FLY_SESSION_V2_OK;
        if (request.operation == CryptoPort::Operation::Random) {
            std::unique_ptr<CryptoPort::Secret> secret = std::make_unique<OpaqueFixtureSecret>(state);
            fly_session_resource_handle_v2 root = 0;
            check(CryptoPort::adopt(request.context,secret, 32, root_producer, &root) == FLY_SESSION_V2_OK,
                  "driver adopts opaque fixture root on production worker");
            if (secret) secret->close();
            { std::lock_guard<std::mutex> lock(state->mutex); state->root = root; state->cv.notify_all(); }
            result.bytes.resize(request.count);
        } else if (request.operation == CryptoPort::Operation::Hkdf) {
            check(input != nullptr, "actual adapter pins input secret for derivation");
            result.secret = std::make_unique<OpaqueFixtureSecret>(state);
        } else if (request.operation == CryptoPort::Operation::Hmac) {
            check(input != nullptr, "transferred output has a real subsequent adapter lease");
            result.bytes.resize(32);
        }
        return result;
    }
};

struct Pair {
    LoopbackWorld world;
    EngineFixture first{world, LoopbackSide::Initiator};
    EngineFixture second{world, LoopbackSide::Responder};
    LoopbackTransport transport;
    PumpState first_pump;
    PumpState second_pump;
    RelayReport relay;

    Pair() {
        reset_loopback_clock_ns();
        first.platform.ready(); second.platform.ready();
        first.executor.run_all(); second.executor.run_all();
        std::vector<fly_session_action_descriptor_v2> first_actions, second_actions;
        first.snapshot(&first_actions); second.snapshot(&second_actions);
        const auto* invite = find_action(first_actions, FLY_SESSION_ACTION_CREATE_INVITE_V2);
        const auto* join = find_action(second_actions, FLY_SESSION_ACTION_JOIN_CODE_V2);
        check(invite && join, "ownership fixture exposes real invite/join actions");
        if (invite && join) {
            submit(first, *invite, 1001, false);
            submit(second, *join, 1002, true);
        }
        for (auto& action : first_actions) fly_session_approval_token_release_v2(action.approval_token);
        for (auto& action : second_actions) fly_session_approval_token_release_v2(action.approval_token);
        transport.attach(LoopbackRole::AdvertiserPeripheral, first);
        transport.attach(LoopbackRole::ScannerCentral, second);
        check(transport.connect_ends() == 2, "ownership fixture connects both public engines");
    }

    bool reach(const char* label, EngineFixture::Crypto::Request* found) {
        for (int round = 0; round < 12000; ++round) {
            first.executor.run_all(); second.executor.run_all();
            for (std::size_t index = static_cast<std::size_t>(first_pump.crypto_hkdfs);
                 index < first.crypto.hkdf_requests.size(); ++index) {
                const auto& request = first.crypto.hkdf_requests[index];
                if (std::string(request.info.begin(), request.info.end()) == label) {
                    check(index == static_cast<std::size_t>(first_pump.crypto_hkdfs),
                          "target producer is the actual next pending HKDF");
                    *found = request;
                    return true;
                }
            }
            relay_gatt(transport, relay);
            relay_quic(transport, relay);
            // executor queues were drained above; each call answers at most one
            // provider request, without consuming its newly enqueued completion.
            pump_once(first, first_pump);
            pump_once(second, second_pump);
            if (pairing_sas_is_offered(first) && pairing_sas_is_offered(second)) {
                confirm_pairing_sas_when_the_abi_asks(first, 20000 + round);
                confirm_pairing_sas_when_the_abi_asks(second, 40000 + round);
            }
        }
        return false;
    }
};

void ownership_case(const char* label, bool consumed, const char* mutation) {
    observers.clear();
    Pair pair;
    EngineFixture::Crypto::Request request;
    const bool reached = pair.reach(label, &request);
    check(reached, "all six real scheduler families must reach their HKDF producer");
    if (reached) {
        auto backend = std::make_shared<LifetimeBackend>();
        CryptoPort adapter(backend);
        const auto port = adapter.port();
        backend->root_producer = request.token;
        backend->root_producer.operation_id += 1000000;
        check(port.random(port.context, &backend->root_producer, 1, {}, pair.first.crypto.inbox) == FLY_SESSION_V2_ACCEPTED,
              "driver root initialization is admitted asynchronously");
        fly_session_resource_handle_v2 root = 0;
        {
            std::unique_lock<std::mutex> lock(backend->state->mutex);
            check(backend->state->cv.wait_for(lock, std::chrono::seconds(3), [&] { return backend->state->root != 0; }),
                  "driver root initialization completes");
            root = backend->state->root;
        }
        auto probe = std::make_shared<DeliveryProbe>();
        probe->token = request.token;
        { std::lock_guard<std::mutex> lock(probe_mutex); delivery_probe = probe; }
        check(port.hkdf(port.context, &request.token, root,
              {request.salt.data(), static_cast<std::uint32_t>(request.salt.size()),0},
              {request.info.data(), static_cast<std::uint32_t>(request.info.size()),0},
              request.size, pair.first.crypto.inbox) == FLY_SESSION_V2_ACCEPTED,
              "real engine request is bridged into production adapter");
        fly_session_resource_handle_v2 output = 0;
        {
            std::unique_lock<std::mutex> lock(probe->mutex);
            check(probe->cv.wait_for(lock, std::chrono::seconds(3), [&] { return probe->delivered; }),
                  "production adapter publishes into real engine inbox");
            output = probe->output;
        }
        // Explicit ownership, never a numerical handle-range heuristic. No more
        // fixture completions/allocations occur before this case shuts down.
        const bool distinct = output != 0 && pair.world.find(output) == nullptr;
        check(distinct, "adapter output must not alias any live fixture resource");
        ++pair.first_pump.crypto_hkdfs;
        auto& calls = observer(&pair.first.crypto);
        calls.adapter = &port; calls.adapter_producer = request.token;
        if (distinct) calls.adapter_output = output;
        if (consumed) {
            pair.first.executor.run_all();
            auto use = request.token; use.operation_id += 2000000;
            const std::uint8_t exact_input[]{1};
            check(port.hmac_sha256(port.context, &use, output, {exact_input,1,0}, pair.first.crypto.inbox) == FLY_SESSION_V2_ACCEPTED,
                  "consumed producer secret remains usable by actual adapter HMAC before release");
            check(pair.first.crypto.hkdf_requests.size() >
                      static_cast<std::size_t>(pair.first_pump.crypto_hkdfs),
                  "consumption advances actual scheduler to its next derivation");
        }
        const auto shutdown = fly_session_begin_shutdown_v2(pair.first.engine, 99001);
        check(shutdown == FLY_SESSION_V2_ACCEPTED, "public shutdown invokes actual current-effect cancellation");
        // Mutation acts ONLY on the observation copied after real engine calls;
        // it neither supplies a provider response nor changes engine decisions.
        auto observed = calls.cancelled;
        if (consumed && std::strcmp(mutation, "--mutate-consumed") == 0)
            observed.push_back(request.token);
        if (!consumed && std::strcmp(mutation, "--mutate-pending") == 0)
            observed.erase(std::remove_if(observed.begin(), observed.end(),
                [&](const auto& token) { return same(token, request.token); }), observed.end());
        const auto producer_cancels = std::count_if(observed.begin(), observed.end(),
            [&](const auto& token) { return same(token, request.token); });
        check(consumed ? producer_cancels == 0 : producer_cancels == 1,
              consumed ? "consumed producer is NEVER cancelled again" :
                         "accepted but unconsumed producer is cancelled exactly once");
        const bool explicitly_released = std::find(calls.released.begin(), calls.released.end(), output)
            != calls.released.end();
        check(consumed ? explicitly_released : !explicitly_released,
              consumed ? "consumed secret ownership ends through release_secret" :
                         "engine has not taken ownership of queued resource");
        if (!consumed) {
            auto use = request.token; use.operation_id += 3000000;
            const std::uint8_t info[]{1};
            check(port.hkdf(port.context, &use, output, {}, {info,1,0}, 32, pair.first.crypto.inbox) == FLY_SESSION_V2_STALE,
                  "pending producer cancellation revokes actual adapter output admission");
        }
        adapter.close();
        {
            std::unique_lock<std::mutex> lock(backend->state->mutex);
            check(backend->state->cv.wait_for(lock, std::chrono::seconds(3), [&] { return backend->state->closes == 2; }),
                  "each case closes both opaque adapter secrets exactly once");
        }
        calls.adapter = nullptr;
        { std::lock_guard<std::mutex> lock(probe_mutex); delivery_probe.reset(); }
        std::printf("OWNERSHIP %s %s\n", label, consumed ? "consumed" : "pending");
    }
    for (auto* fixture : {&pair.first, &pair.second}) {
        fixture->bearer.cancel_result = FLY_SESSION_V2_OK;
        fly_session_begin_shutdown_v2(fixture->engine, 99002);
        fixture->executor.run_all();
        check(fly_session_destroy_v2(fixture->engine) == FLY_SESSION_V2_OK,
              "ownership fixture shuts down without answering cancelled work");
        fixture->engine = nullptr;
    }
}
} // namespace

extern "C" fly_session_result_v2 __real_fly_session_create_v2(
    const fly_session_config_v2*, const fly_session_ports_v2*, fly_session_v2_t**);
extern "C" fly_session_result_v2 __wrap_fly_session_create_v2(
    const fly_session_config_v2* config, const fly_session_ports_v2* ports,
    fly_session_v2_t** engine) {
    auto copy = *ports;
    auto crypto = *ports->crypto;
    Observer entry{};
    entry.context = crypto.context; entry.cancel = crypto.cancel; entry.release = crypto.release_secret;
    observers.push_back(std::move(entry));
    crypto.cancel = observe_cancel;
    crypto.release_secret = observe_release;
    copy.crypto = &crypto;
    return __real_fly_session_create_v2(config, &copy, engine);
}
extern "C" fly_session_result_v2 __real_fly_session_deliver_v2(
    fly_session_inbox_v2_t*, const fly_session_port_event_v2*);
extern "C" fly_session_result_v2 __wrap_fly_session_deliver_v2(
    fly_session_inbox_v2_t* inbox, const fly_session_port_event_v2* event) {
    const auto result = __real_fly_session_deliver_v2(inbox, event);
    std::shared_ptr<DeliveryProbe> probe;
    { std::lock_guard<std::mutex> lock(probe_mutex); probe = delivery_probe; }
    if (probe && same(probe->token, event->token) && event->payload_kind == FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2) {
        std::lock_guard<std::mutex> lock(probe->mutex);
        check(result == FLY_SESSION_V2_ACCEPTED, "real engine accepts actual adapter resource event");
        fly_session_provider_resource_event_v2 payload{};
        if (event->result == FLY_SESSION_V2_OK && event->payload_size == sizeof(payload))
            std::memcpy(&payload, event->payload, sizeof(payload));
        probe->output = payload.resource; probe->delivered = true; probe->cv.notify_all();
    }
    return result;
}

int main(int argc, char** argv) {
    const char* mutation = argc > 1 ? argv[1] : "";
    const char* labels[] = {
        "flynes-pair-reveal-i2r-v1", "flynes-pair-control-i2r-v1",
        "flynes-pair-gatt-i2r-v1", "flynes-initial-bearer-credential-i2r-v1",
        "flynes-endpoint-offer-i2r-v1", "flynes-pair-quic-bind-i2r-v1"
    };
    for (const auto* label : labels) {
        ownership_case(label, false, mutation);
        ownership_case(label, true, mutation);
    }
    std::printf("ownership failures=%d (fixture crypto; no JCA claim)\n", failures);
    return failures == 0 ? 0 : 1;
}
