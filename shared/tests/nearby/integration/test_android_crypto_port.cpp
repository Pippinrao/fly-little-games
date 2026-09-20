#include "crypto_port.hpp"
#include "ports/provider_events.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <functional>
#include <mutex>
#include <stdexcept>
#include <thread>

using flynes::android::nearby::CryptoPort;
namespace {
int failures = 0;
void check(bool yes, const char* message) {
    if (!yes) { ++failures; std::fprintf(stderr, "FAIL: %s\n", message); }
}
struct Inbox {
    std::atomic<int> refs{1};
    std::mutex mutex;
    std::condition_variable cv;
    int attempts = 0;
    int backpressure = 0;
    fly_session_result_v2 delivery = FLY_SESSION_V2_ACCEPTED;
    fly_session_port_event_v2 event{};
    flynes::session::ParsedProviderEvent parsed;
    ~Inbox() {
        std::unique_lock<std::mutex> lock(mutex);
        check(cv.wait_for(lock, std::chrono::seconds(3), [&] { return refs == 1; }),
              "worker releases retained inbox before test destroys it");
    }
    bool wait(int count = 1) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return attempts >= count; });
    }
    bool idle() {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, std::chrono::seconds(3), [&] { return refs == 1; });
    }
    fly_session_inbox_v2_t* handle() { return reinterpret_cast<fly_session_inbox_v2_t*>(this); }
};
struct State {
    std::mutex mutex;
    std::condition_variable cv;
    int calls = 0;
    int closed = 0;
    bool blocked = false;
    CryptoPort::Request seen;
    std::thread::id thread;
    fly_session_result_v2 result = FLY_SESSION_V2_OK;
    std::function<void(CryptoPort::Context*)> on_worker;
};
struct TestSecret : CryptoPort::Secret {
    std::shared_ptr<State> state;
    explicit TestSecret(std::shared_ptr<State> value) : state(std::move(value)) {}
    void close() noexcept override {
        std::lock_guard<std::mutex> lock(state->mutex);
        ++state->closed; state->cv.notify_all();
    }
};
// Transport/lifetime seam only, not a cryptographic implementation/oracle.
struct Backend : CryptoPort::Backend {
    std::shared_ptr<State> state = std::make_shared<State>();
    CryptoPort::Result execute(const CryptoPort::Request& request, CryptoPort::Secret*) override {
        if (state->on_worker) state->on_worker(request.context);
        std::unique_lock<std::mutex> lock(state->mutex);
        ++state->calls; state->seen = request; state->thread = std::this_thread::get_id();
        state->cv.notify_all();
        state->cv.wait(lock, [&] { return !state->blocked; });
        CryptoPort::Result result;
        result.result = state->result;
        if (request.operation == CryptoPort::Operation::Hkdf)
            result.secret = std::make_unique<TestSecret>(state);
        else if (request.operation == CryptoPort::Operation::Random)
            result.bytes.assign(request.count, 0x59); // fixture data, never crypto evidence
        else if (request.operation == CryptoPort::Operation::Hmac)
            result.bytes.resize(32);
        else if (request.operation == CryptoPort::Operation::Seal || request.operation == CryptoPort::Operation::Open)
            result.bytes.resize(request.count);
        return result;
    }
};
fly_session_op_token_v2 token(std::uint64_t id) {
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE; value.abi_version = 2;
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE; value.scope.abi_version = 2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2; value.operation_id = id;
    value.engine_instance_id[0] = 9; value.connection_generation = 17;
    return value;
}
void tests() {
    bool rejected = false;
    try { CryptoPort absent(nullptr); }
    catch (const std::invalid_argument&) { rejected = true; }
    check(rejected, "missing backend rejected before worker can dereference it");
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend);
    const auto port = owner.port();
    const bool complete = port.struct_size == FLY_SESSION_CRYPTO_PORT_V2_SIZE &&
        port.abi_version == 2 && port.context && port.retain && port.release &&
        port.random && port.hkdf && port.aead_seal && port.aead_open &&
        port.verify_prehashed && port.hmac_sha256 && port.release_secret && port.cancel;
    check(complete, "production adapter exposes every required ABI callback");
    if (!complete) return;
    Inbox inbox;
    auto first = token(1);
    std::uint8_t purpose[]{1,2,3};
    check(port.random(port.context, &first, 8, {purpose, 3, 0}, inbox.handle()) == FLY_SESSION_V2_ACCEPTED,
          "random admission returns ACCEPTED");
    check(inbox.wait(), "worker publishes a typed terminal");
    check(inbox.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2 &&
          inbox.event.terminal == 1 && inbox.parsed.value0 == 8,
          "random terminal has exact BUFFER contract");
    check(backend->state->thread != std::this_thread::get_id(), "JCA/backend never runs on admission thread");
    check(port.random(port.context, &first, 4097, {purpose, 3, 0}, inbox.handle()) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "oversize random rejected synchronously");
    auto foreign = token(2); foreign.engine_instance_id[0] ^= 1;
    check(port.random(port.context, &foreign, 8, {purpose, 3, 0}, inbox.handle()) == FLY_SESSION_V2_STALE,
          "different engine cannot reuse retained context");
    Inbox retry;
    retry.backpressure = 2;
    auto second = token(2);
    check(port.random(port.context, &second, 16, {purpose, 3, 0}, retry.handle()) == FLY_SESSION_V2_ACCEPTED,
          "retry job admitted");
    check(retry.wait(3), "same worker retries bounded outbox");
    check(backend->state->calls == 2, "backpressure does not repeat random computation");
    Inbox verified;
    auto third = token(3);
    std::uint8_t public_key[65]{4}, digest[32]{}, signature[64]{};
    check(port.verify_prehashed(port.context, &third, {public_key,65,0}, {purpose,3,0},
                               digest, {signature,64,0}, verified.handle()) == FLY_SESSION_V2_ACCEPTED,
          "verify admission uses existing backend asynchronously");
    check(verified.wait(), "verification completes");
    check(verified.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2 &&
          verified.event.result == FLY_SESSION_V2_OK && verified.parsed.buffer == nullptr,
          "verification success is typed END, not buffer");
    {
        std::lock_guard<std::mutex> lock(backend->state->mutex);
        backend->state->result = FLY_SESSION_V2_UNAVAILABLE;
    }
    Inbox provider_error;
    auto fourth = token(4);
    check(port.verify_prehashed(port.context, &fourth, {public_key,65,0}, {purpose,3,0},
                               digest, {signature,64,0}, provider_error.handle()) == FLY_SESSION_V2_ACCEPTED,
          "provider failure is asynchronous");
    check(provider_error.wait(), "provider failure completes");
    check(provider_error.event.result == FLY_SESSION_V2_UNAVAILABLE &&
          provider_error.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2,
          "provider failure does not become authentication failure");
    owner.close();
    check(port.random(port.context, &second, 8, {purpose, 3, 0}, retry.handle()) == FLY_SESSION_V2_CLOSED,
          "closed context rejects admission");
}
void secret_tests() {
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend);
    const auto port = owner.port();
    fly_session_resource_handle_v2 root = 0;
    auto producer = token(100);
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        if (root) return;
        std::unique_ptr<CryptoPort::Secret> secret = std::make_unique<TestSecret>(backend->state);
        check(CryptoPort::adopt(context, secret, 32, producer, &root) == FLY_SESSION_V2_OK,
              "worker adopts existing opaque secret without importing bytes");
        if (secret) secret->close();
    };
    Inbox bootstrap;
    auto bootstrap_token = token(101);
    const std::uint8_t input[]{1,2,3};
    check(port.random(port.context, &bootstrap_token, 1, {input,3,0}, bootstrap.handle()) == FLY_SESSION_V2_ACCEPTED,
          "test bootstrap executes on the same controlled worker");
    check(bootstrap.wait(), "test bootstrap finishes");
    if (!root) return;
    Inbox sealed;
    auto seal_token = token(110);
    const std::uint8_t nonce[12]{};
    check(port.aead_seal(port.context, &seal_token, root, {nonce,12,0}, {}, {}, sealed.handle()) == FLY_SESSION_V2_ACCEPTED,
          "AEAD seal admits empty plaintext with a live 32-byte key");
    check(sealed.wait(), "AEAD seal completes");
    check(sealed.parsed.value0 == 16 && sealed.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2,
          "AEAD seal preserves 16-byte tag framing");
    Inbox opened;
    auto open_token = token(111);
    const std::uint8_t tagged[16]{};
    check(port.aead_open(port.context, &open_token, root, {nonce,12,0}, {}, {tagged,16,0}, opened.handle()) == FLY_SESSION_V2_ACCEPTED,
          "AEAD open admits tag-only ciphertext");
    check(opened.wait(), "empty plaintext open completes");
    check(opened.event.result == FLY_SESSION_V2_OK && opened.parsed.value0 == 0 && opened.parsed.buffer,
          "empty plaintext is a real zero-byte buffer, not failure");
    check(port.aead_open(port.context, &open_token, root, {nonce,12,0}, {}, {tagged,15,0}, opened.handle()) == FLY_SESSION_V2_INVALID_ARGUMENT,
          "short AEAD tag rejected before backend");
    Inbox derived;
    auto derive_token = token(102);
    check(port.hkdf(port.context, &derive_token, root, {}, {input,3,0}, 32, derived.handle()) == FLY_SESSION_V2_ACCEPTED,
          "HKDF resolves genuine opaque registry membership");
    check(derived.wait(), "HKDF resource completes");
    const auto child = derived.parsed.resource;
    check(child != 0 && child != root && derived.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2,
          "HKDF publishes a new resource not secret bytes");
    Inbox mac;
    auto mac_token = token(103);
    check(port.hmac_sha256(port.context, &mac_token, child, {input,3,0}, mac.handle()) == FLY_SESSION_V2_ACCEPTED,
          "derived handle transfers into subsequent MAC operation");
    check(mac.wait(), "MAC completes through registry");
    check(mac.parsed.value0 == 32 && mac.event.payload_kind == FLY_SESSION_PROVIDER_CRYPTO_MAC_V2,
          "MAC completion is a 32-byte buffer");
    check(port.release_secret(port.context, child) == FLY_SESSION_V2_OK,
          "transferred child uses explicit release, never old producer cancel");
    Inbox pending;
    auto pending_token = token(105);
    check(port.hkdf(port.context, &pending_token, root, {}, {input,3,0}, 32, pending.handle()) == FLY_SESSION_V2_ACCEPTED,
          "independent pending producer admitted");
    check(pending.wait(), "pending producer reaches inbox without consumer use");
    check(port.cancel(port.context, &pending_token) == FLY_SESSION_V2_OK,
          "engine-pending producer cancellation retires its output");
    auto stale_token = token(104);
    check(port.hmac_sha256(port.context, &stale_token, pending.parsed.resource, {input,3,0}, mac.handle()) == FLY_SESSION_V2_STALE,
          "cancelled producer output cannot admit new use");
    check(port.release_secret(port.context, root) == FLY_SESSION_V2_OK,
          "explicit secret release revokes root membership");
    check(port.release_secret(port.context, root) == FLY_SESSION_V2_STALE,
          "released secret cannot be released twice");
    owner.close();
}
void bounded_cancel_test() {
    auto backend = std::make_shared<Backend>();
    backend->state->blocked = true;
    auto owner = std::make_unique<CryptoPort>(backend);
    const auto port = owner->port();
    port.retain(port.context);
    Inbox inbox;
    std::uint8_t purpose[]{7,8,9};
    std::array<fly_session_op_token_v2, 17> tokens;
    for (std::size_t index = 0; index < tokens.size(); ++index) {
        tokens[index] = token(200 + index);
        const auto admitted = port.random(port.context, &tokens[index], 16, {purpose,3,0}, inbox.handle());
        check(admitted == (index < 16 ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_BACKPRESSURE),
              "exactly 16 total running/queued/outbox job slots");
    }
    {
        std::unique_lock<std::mutex> lock(backend->state->mutex);
        check(backend->state->cv.wait_for(lock, std::chrono::seconds(3), [&] { return backend->state->calls == 1; }),
              "one controlled worker enters blocked operation");
        purpose[0] = 99;
        check(backend->state->seen.inputs[0][0] == 7, "admission owns an immutable bounded input copy");
    }
    for (std::size_t index = 0; index < 16; ++index)
        check(port.cancel(port.context, &tokens[index]) == FLY_SESSION_V2_OK,
              "queued/running cancel does not wait for backend");
    owner.reset(); // retained context survives the owner, but is closed
    check(port.random(port.context, &tokens[16], 16, {}, inbox.handle()) == FLY_SESSION_V2_CLOSED,
          "retained context remains valid and closed after owner destruction");
    {
        std::lock_guard<std::mutex> lock(backend->state->mutex);
        backend->state->blocked = false; backend->state->cv.notify_all();
    }
    {
        std::unique_lock<std::mutex> lock(inbox.mutex);
        check(inbox.cv.wait_for(lock, std::chrono::seconds(3), [&] { return inbox.refs == 1; }),
              "cancelled jobs release every retained inbox");
        check(inbox.attempts == 0, "queued and late-running cancelled results never reserve publication");
    }
    check(backend->state->calls == 1, "cancelled queued operations never execute backend");
    port.release(port.context);
}
void secret_capacity_test() {
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend);
    const auto port = owner.port();
    std::vector<fly_session_resource_handle_v2> ids;
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        if (!ids.empty()) return;
        for (unsigned index = 0; index < 65; ++index) {
            std::unique_ptr<CryptoPort::Secret> secret = std::make_unique<TestSecret>(backend->state);
            fly_session_resource_handle_v2 id = 0;
            const auto result = CryptoPort::adopt(context, secret, 32, token(300+index), &id);
            check(result == (index < 64 ? FLY_SESSION_V2_OK : FLY_SESSION_V2_BACKPRESSURE),
                  "64 secret slots include all live and retiring handles");
            if (result == FLY_SESSION_V2_OK) ids.push_back(id);
            if (secret) secret->close(); // rejected adoption retains worker ownership
        }
    };
    Inbox bootstrap;
    auto request = token(399);
    check(port.random(port.context,&request,1,{},bootstrap.handle()) == FLY_SESSION_V2_ACCEPTED,
          "secret-capacity bootstrap admitted");
    check(bootstrap.wait(), "secret-capacity bootstrap completes");
    check(ids.size() == 64, "no secret map growth beyond fixed slots");
    owner.close();
    std::unique_lock<std::mutex> lock(backend->state->mutex);
    check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed == 65; }),
          "full registry closes without needing an ordinary queue slot");
}
void byte_budget_and_pin_test() {
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend);
    const auto port = owner.port();
    fly_session_resource_handle_v2 root = 0;
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        if (root) return;
        std::unique_ptr<CryptoPort::Secret> secret = std::make_unique<TestSecret>(backend->state);
        check(CryptoPort::adopt(context,secret,32,token(400),&root) == FLY_SESSION_V2_OK, "pin test root adoption");
        if (secret) secret->close();
    };
    Inbox bootstrap;
    auto request = token(401);
    check(port.random(port.context,&request,1,{},bootstrap.handle()) == FLY_SESSION_V2_ACCEPTED,
          "pin-test bootstrap admitted");
    check(bootstrap.wait(), "pin-test bootstrap completes");
    if (!root) return;
    auto other_backend = std::make_shared<Backend>();
    CryptoPort other(other_backend);
    const auto foreign = other.port();
    const std::uint8_t exact[]{1};
    check(foreign.hmac_sha256(foreign.context,&request,root,{exact,1,0},bootstrap.handle()) == FLY_SESSION_V2_STALE,
          "same engine token cannot move a handle across retained contexts");
    { std::lock_guard<std::mutex> lock(backend->state->mutex); backend->state->blocked = true; }
    const std::vector<std::uint8_t> input(1024*1024-16), aad(4096), nonce(12);
    Inbox seal;
    auto first = token(402), second = token(403);
    const auto bytes = [](const auto& value) { return fly_session_bytes_v2{
        value.data(), static_cast<std::uint32_t>(value.size()),0}; };
    check(port.aead_seal(port.context,&first,root,bytes(nonce),bytes(aad),bytes(input),seal.handle()) == FLY_SESSION_V2_ACCEPTED,
          "one maximum bounded AEAD job fits native byte budget");
    check(port.aead_seal(port.context,&second,root,bytes(nonce),bytes(aad),bytes(input),seal.handle()) == FLY_SESSION_V2_BACKPRESSURE,
          "second large job exceeds 4 MiB before reaching 16 slots");
    {
        std::unique_lock<std::mutex> lock(backend->state->mutex);
        check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->calls == 2; }),
              "AEAD is running while its input lease is pinned");
    }
    check(port.release_secret(port.context,root) == FLY_SESSION_V2_OK, "release revokes pinned secret admission");
    check(port.hmac_sha256(port.context,&second,root,{exact,1,0},seal.handle()) == FLY_SESSION_V2_STALE,
          "released pinned key cannot admit another operation");
    {
        std::lock_guard<std::mutex> lock(backend->state->mutex);
        check(backend->state->closed == 0, "release never closes a key during backend use");
    }
    check(port.cancel(port.context,&first) == FLY_SESSION_V2_OK, "running pinned operation cancellation is nonblocking");
    owner.close();
    {
        std::lock_guard<std::mutex> lock(backend->state->mutex);
        backend->state->blocked = false; backend->state->cv.notify_all();
    }
    {
        std::unique_lock<std::mutex> lock(backend->state->mutex);
        check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed == 1; }),
              "last lease closes the retired key once on worker");
    }
    check(seal.attempts == 0, "cancelled AEAD late output was not published");
}
void late_bootstrap_after_close_test() {
    auto backend = std::make_shared<Backend>();
    auto owner = std::make_unique<CryptoPort>(backend);
    const auto port = owner->port();
    struct Gate { std::mutex mutex; std::condition_variable cv; bool entered=false, proceed=false; } gate;
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        {
            std::unique_lock<std::mutex> lock(gate.mutex);
            gate.entered = true; gate.cv.notify_all();
            gate.cv.wait(lock,[&] { return gate.proceed; });
        }
        check(context != nullptr, "admitted bootstrap retains context independently of destroyed wrapper");
        std::unique_ptr<CryptoPort::Secret> secret = std::make_unique<TestSecret>(backend->state);
        fly_session_resource_handle_v2 untouched = 999;
        if (context) {
            check(CryptoPort::adopt(context,secret,32,token(500),&untouched) == FLY_SESSION_V2_CLOSED,
                  "deterministic late bootstrap fails CLOSED after owner destruction");
            check(secret != nullptr && untouched == 999,
                  "closed adoption neither transfers opaque secret nor writes output handle");
        }
        if (secret) secret->close();
    };
    Inbox inbox;
    auto request = token(501);
    check(port.random(port.context,&request,1,{},inbox.handle()) == FLY_SESSION_V2_ACCEPTED,
          "late-bootstrap fixture job admitted");
    {
        std::unique_lock<std::mutex> lock(gate.mutex);
        check(gate.cv.wait_for(lock,std::chrono::seconds(3),[&] { return gate.entered; }),
              "bootstrap reaches barrier before adoption");
    }
    owner.reset();
    { std::lock_guard<std::mutex> lock(gate.mutex); gate.proceed=true; gate.cv.notify_all(); }
    {
        std::unique_lock<std::mutex> lock(inbox.mutex);
        check(inbox.cv.wait_for(lock,std::chrono::seconds(3),[&] { return inbox.refs == 1; }),
              "late bootstrap releases retained context/inbox without wrapper access");
        check(inbox.attempts == 0, "late bootstrap cannot publish after close");
    }
    check(backend->state->closed == 1, "failed late bootstrap closes its still-owned secret once");
}
void cleanup_wakeup_test() {
    struct Gate {
        std::mutex mutex; std::condition_variable cv;
        bool entered=false, proceed=false; int closes=0;
    };
    struct Secret final : CryptoPort::Secret {
        std::shared_ptr<Gate> gate; bool barrier;
        Secret(std::shared_ptr<Gate> value, bool block) : gate(std::move(value)), barrier(block) {}
        void close() noexcept override {
            std::unique_lock<std::mutex> lock(gate->mutex);
            if (barrier) { gate->entered=true; gate->cv.notify_all();
                gate->cv.wait(lock,[&] { return gate->proceed; }); }
            ++gate->closes; gate->cv.notify_all();
        }
    };
    auto gate = std::make_shared<Gate>();
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend); const auto port=owner.port();
    std::array<fly_session_resource_handle_v2,2> handles{};
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        for (std::size_t i=0;i<handles.size();++i) {
            std::unique_ptr<CryptoPort::Secret> secret=std::make_unique<Secret>(gate,i==1);
            check(CryptoPort::adopt(context,secret,32,token(700+i),&handles[i])==FLY_SESSION_V2_OK,
                  "cleanup fixture reserves ordered opaque secret slots");
        }
    };
    Inbox inbox; auto request=token(702);
    check(port.random(port.context,&request,1,{},inbox.handle())==FLY_SESSION_V2_ACCEPTED,
          "cleanup fixture bootstrap admission");
    { std::unique_lock<std::mutex> lock(inbox.mutex);
      check(inbox.cv.wait_for(lock,std::chrono::seconds(3),[&] { return inbox.attempts==1 && inbox.refs==1; }),
            "bootstrap job fully releases before idle cleanup race"); }
    check(port.release_secret(port.context,handles[1])==FLY_SESSION_V2_OK,"retire later slot first");
    { std::unique_lock<std::mutex> lock(gate->mutex);
      check(gate->cv.wait_for(lock,std::chrono::seconds(3),[&] { return gate->entered; }),
            "worker is outside registry lock closing later slot"); }
    check(port.release_secret(port.context,handles[0])==FLY_SESSION_V2_OK,"retire already-scanned earlier slot");
    { std::lock_guard<std::mutex> lock(gate->mutex); gate->proceed=true; gate->cv.notify_all(); }
    { std::unique_lock<std::mutex> lock(gate->mutex);
      check(gate->cv.wait_for(lock,std::chrono::seconds(3),[&] { return gate->closes==2; }),
            "idle worker never loses earlier-slot retirement while cleanup lock was released"); }
    owner.close(); // also guarantees cleanup on RED without dangling test storage
    { std::unique_lock<std::mutex> lock(gate->mutex);
      check(gate->cv.wait_for(lock,std::chrono::seconds(3),[&] { return gate->closes==2; }),
            "cleanup regression fixture releases both secrets"); }
}
void admission_boundaries_test() {
    auto backend = std::make_shared<Backend>();
    CryptoPort owner(backend); const auto port = owner.port();
    std::uint64_t next = 800;
    std::array<std::uint8_t,4097> data{};
    const auto bytes = [&](std::uint32_t size) { return fly_session_bytes_v2{data.data(),size,0}; };
    // Rejections must be synchronous: no worker execution or retained inbox.
    const auto run = [&](const char* label, bool accepted, const auto& submit) {
        Inbox inbox; const auto request = token(next++);
        int before;
        { std::lock_guard<std::mutex> lock(backend->state->mutex); before = backend->state->calls; }
        check(submit(request,inbox) == (accepted ? FLY_SESSION_V2_ACCEPTED : FLY_SESSION_V2_INVALID_ARGUMENT),label);
        if (accepted) check(inbox.wait() && inbox.idle(),"boundary accepted job completes and releases inbox");
        else check(inbox.refs == 1 && inbox.attempts == 0,"rejected boundary owns no inbox or completion");
        { std::lock_guard<std::mutex> lock(backend->state->mutex);
          check(backend->state->calls == before + (accepted ? 1 : 0),"boundary invokes backend exactly when admitted"); }
    };
    struct RandomCase { std::uint32_t count, purpose; bool accepted; };
    for (const auto row : {RandomCase{0,0,false},{1,0,true},{4096,1024,true},{4097,0,false},{1,1025,false}})
        run("random count/purpose boundary",row.accepted,[&](const auto& t, Inbox& i) {
            return port.random(port.context,&t,row.count,bytes(row.purpose),i.handle()); });
    using Mutate = void(*)(fly_session_op_token_v2&);
    struct TokenCase { Mutate mutate; fly_session_result_v2 result; };
    const TokenCase malformed[]{
        {[](auto& t) { --t.struct_size; },FLY_SESSION_V2_ABI_MISMATCH},
        {[](auto& t) { ++t.abi_version; },FLY_SESSION_V2_ABI_MISMATCH},
        {[](auto& t) { --t.scope.struct_size; },FLY_SESSION_V2_ABI_MISMATCH},
        {[](auto& t) { ++t.scope.abi_version; },FLY_SESSION_V2_ABI_MISMATCH},
        {[](auto& t) { t.scope.reserved_zero=1; },FLY_SESSION_V2_INVALID_ARGUMENT},
        {[](auto& t) { t.operation_id=0; },FLY_SESSION_V2_INVALID_ARGUMENT},
        {[](auto& t) { t.scope.kind=0; },FLY_SESSION_V2_INVALID_ARGUMENT},
        {[](auto& t) { t.scope.kind=FLY_SESSION_SCOPE_GAME_V2+1; },FLY_SESSION_V2_INVALID_ARGUMENT}
    };
    for (const auto& row : malformed) {
        Inbox inbox; auto request=token(next++); row.mutate(request);
        int before;
        { std::lock_guard<std::mutex> lock(backend->state->mutex); before=backend->state->calls; }
        check(port.random(port.context,&request,1,{},inbox.handle())==row.result,"token ABI/prefix/reserved boundary");
        check(port.cancel(port.context,&request)==row.result,"cancel validates full token shape");
        check(inbox.refs==1 && inbox.attempts==0,"malformed token has no retained inbox or event");
        { std::lock_guard<std::mutex> lock(backend->state->mutex);
          check(backend->state->calls==before,"malformed token never executes backend"); }
    }
    for (int which=0;which<5;++which)
        run("null token/context/inbox and malformed bytes",false,[&](const auto& t, Inbox& i) {
            auto purpose=bytes(1);
            if (which==3) purpose.data=nullptr;
            if (which==4) purpose.reserved_zero=1;
            return port.random(which==1 ? nullptr : port.context,which==0 ? nullptr : &t,1,purpose,
                               which==2 ? nullptr : i.handle()); });
    fly_session_resource_handle_v2 root=0;
    backend->state->on_worker = [&](CryptoPort::Context* context) {
        if (root) return;
        std::unique_ptr<CryptoPort::Secret> secret=std::make_unique<TestSecret>(backend->state);
        check(CryptoPort::adopt(context,secret,32,token(899),&root)==FLY_SESSION_V2_OK,"boundary root adoption");
        if (secret) secret->close();
    };
    run("boundary root bootstrap",true,[&](const auto& t, Inbox& i) {
        return port.random(port.context,&t,1,{},i.handle()); });
    if (!root) { owner.close(); return; }
    struct HkdfCase { std::uint32_t salt,info,count; bool accepted; };
    for (const auto row : {HkdfCase{0,1,1,true},{1024,1024,8160,true},{1025,1,1,false},
                           {0,0,1,false},{0,1025,1,false},{0,1,0,false},{0,1,8161,false}})
        run("HKDF salt/info/count boundary",row.accepted,[&](const auto& t, Inbox& i) {
            const auto result=port.hkdf(port.context,&t,root,bytes(row.salt),bytes(row.info),row.count,i.handle());
            if (result==FLY_SESSION_V2_ACCEPTED) {
                check(i.wait() && i.idle(),"HKDF boundary publishes resource");
                check(i.parsed.resource!=0,"HKDF boundary has opaque output");
                check(port.release_secret(port.context,i.parsed.resource)==FLY_SESSION_V2_OK,"HKDF boundary releases output");
            }
            return result; });
    for (const auto size : {0u,1u,4096u,4097u})
        run("HMAC exact-input boundary",size==1 || size==4096,[&](const auto& t, Inbox& i) {
            return port.hmac_sha256(port.context,&t,root,bytes(size),i.handle()); });
    for (int which=0;which<6;++which)
        run("HKDF/HMAC reject null or reserved input bytes",false,[&](const auto& t, Inbox& i) {
            auto malformed=bytes(1);
            if (which%2) malformed.reserved_zero=1; else malformed.data=nullptr;
            if (which<2) return port.hkdf(port.context,&t,root,malformed,bytes(1),1,i.handle());
            if (which<4) return port.hkdf(port.context,&t,root,{},malformed,1,i.handle());
            return port.hmac_sha256(port.context,&t,root,malformed,i.handle()); });
    std::array<std::uint8_t,66> key{}; key[0]=4;
    std::array<std::uint8_t,65> signature{};
    std::array<std::uint8_t,32> digest{};
    struct VerifyCase { std::uint32_t key_size,domain,signature_size; std::uint8_t prefix; bool null_digest,accepted; };
    for (const auto row : {VerifyCase{65,1,64,4,false,true},{65,1024,64,4,false,true},
            {64,1,64,4,false,false},{66,1,64,4,false,false},{65,1,64,3,false,false},
            {65,0,64,4,false,false},{65,1025,64,4,false,false},{65,1,63,4,false,false},
            {65,1,65,4,false,false},{65,1,64,4,true,false}}) {
        key[0]=row.prefix;
        run("verification key/domain/digest/signature shape",row.accepted,[&](const auto& t, Inbox& i) {
            return port.verify_prehashed(port.context,&t,{key.data(),row.key_size,0},bytes(row.domain),
                row.null_digest ? nullptr : digest.data(),{signature.data(),row.signature_size,0},i.handle()); });
    }
    key[0]=4;
    for (int which=0;which<6;++which)
        run("verification rejects null or reserved byte fields",false,[&](const auto& t, Inbox& i) {
            fly_session_bytes_v2 public_key{key.data(),65,0}, domain=bytes(1), sig{signature.data(),64,0};
            auto& malformed=which<2 ? public_key : which<4 ? domain : sig;
            if (which%2) malformed.reserved_zero=1; else malformed.data=nullptr;
            return port.verify_prehashed(port.context,&t,public_key,domain,digest.data(),sig,i.handle()); });
    owner.close();
    std::unique_lock<std::mutex> lock(backend->state->mutex);
    check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed==3; }),
          "boundary root and two derived handles close exactly once");
}
void outbox_boundaries_test() {
    auto backend=std::make_shared<Backend>();
    CryptoPort owner(backend); const auto port=owner.port();
    fly_session_resource_handle_v2 root=0;
    backend->state->on_worker=[&](CryptoPort::Context* context) {
        if (root) return;
        std::unique_ptr<CryptoPort::Secret> secret=std::make_unique<TestSecret>(backend->state);
        check(CryptoPort::adopt(context,secret,32,token(1000),&root)==FLY_SESSION_V2_OK,"outbox root adoption");
        if (secret) secret->close();
    };
    Inbox bootstrap; auto boot=token(1001);
    check(port.random(port.context,&boot,1,{},bootstrap.handle())==FLY_SESSION_V2_ACCEPTED,"outbox bootstrap admission");
    check(bootstrap.wait() && bootstrap.idle(),"outbox bootstrap completes");
    if (!root) { owner.close(); return; }
    const std::uint8_t input=7;
    Inbox retry; retry.backpressure=2; auto derive=token(1002);
    check(port.hkdf(port.context,&derive,root,{}, {&input,1,0},32,retry.handle())==FLY_SESSION_V2_ACCEPTED,"derived retry admission");
    check(retry.wait(3) && retry.idle(),"derived outbox retries twice then accepts");
    { std::lock_guard<std::mutex> lock(backend->state->mutex);
      check(backend->state->calls==2,"two backpressure replies never repeat derivation"); }
    check(retry.parsed.resource!=0 && retry.event.event_sequence==1,"replayed derived handle and sequence are stable");
    check(port.release_secret(port.context,retry.parsed.resource)==FLY_SESSION_V2_OK,"accepted replay output release");
    int expected_closed=1;
    for (const auto rejection : {FLY_SESSION_V2_CLOSED,FLY_SESSION_V2_STALE}) {
        Inbox rejected; rejected.delivery=rejection; auto request=token(1003+expected_closed);
        check(port.hkdf(port.context,&request,root,{}, {&input,1,0},32,rejected.handle())==FLY_SESSION_V2_ACCEPTED,"rejected delivery job admitted");
        check(rejected.wait() && rejected.idle(),"nonretryable delivery releases retained job/inbox");
        ++expected_closed;
        { std::unique_lock<std::mutex> lock(backend->state->mutex);
          check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed==expected_closed; }),
                "nonretryable delivery retires unpublished secret exactly once"); }
        check(rejected.attempts==1,"CLOSED/STALE delivery is never retried");
        check(port.hmac_sha256(port.context,&request,rejected.parsed.resource,{&input,1,0},rejected.handle())==FLY_SESSION_V2_STALE,
              "nonretryable rejected resource cannot admit a new lease");
    }
    Inbox cancelled; cancelled.delivery=FLY_SESSION_V2_BACKPRESSURE; auto pending=token(1050);
    check(port.hkdf(port.context,&pending,root,{}, {&input,1,0},32,cancelled.handle())==FLY_SESSION_V2_ACCEPTED,
          "cancelled derived outbox admission");
    check(cancelled.wait(),"derived secret reaches backpressured outbox");
    check(port.cancel(port.context,&pending)==FLY_SESSION_V2_OK,"cancel retires unpublished outbox handle");
    check(cancelled.idle(),"cancelled derived outbox releases inbox");
    { std::unique_lock<std::mutex> lock(backend->state->mutex);
      check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed==4; }),
            "cancelled derived outbox closes secret once"); }
    check(port.hmac_sha256(port.context,&pending,cancelled.parsed.resource,{&input,1,0},cancelled.handle())==FLY_SESSION_V2_STALE,
          "cancelled unpublished handle cannot acquire another lease");
    // Hold all sixteen computed results in the outbox; no sleeping or worker blocking.
    std::array<Inbox,16> held;
    std::array<fly_session_op_token_v2,16> requests;
    for (std::size_t i=0;i<held.size();++i) {
        held[i].delivery=FLY_SESSION_V2_BACKPRESSURE; requests[i]=token(1100+i);
        check(port.random(port.context,&requests[i],4096,{},held[i].handle())==FLY_SESSION_V2_ACCEPTED,"outbox capacity admission");
        check(held[i].wait(),"outbox slot holds computed result");
    }
    Inbox overflow; auto extra=token(1200);
    check(port.random(port.context,&extra,1,{},overflow.handle())==FLY_SESSION_V2_BACKPRESSURE,"sixteen outbox results exhaust job capacity");
    for (const auto& request : requests)
        check(port.cancel(port.context,&request)==FLY_SESSION_V2_OK,"outbox cancellation admitted");
    for (auto& inbox : held) check(inbox.idle(),"cancelled outbox releases capacity and retained inbox");
    check(port.random(port.context,&extra,4096,{},overflow.handle())==FLY_SESSION_V2_ACCEPTED,"cancelled outbox capacity is reusable");
    check(overflow.wait() && overflow.idle(),"recovered outbox capacity completes");
    owner.close();
    std::unique_lock<std::mutex> lock(backend->state->mutex);
    check(backend->state->cv.wait_for(lock,std::chrono::seconds(3),[&] { return backend->state->closed==5; }),
          "all outbox fixture secrets close exactly once");
}
}
extern "C" void __wrap_fly_session_inbox_retain_v2(fly_session_inbox_v2_t* value) {
    ++reinterpret_cast<Inbox*>(value)->refs;
}
extern "C" void __wrap_fly_session_inbox_release_v2(fly_session_inbox_v2_t* value) {
    auto& inbox = *reinterpret_cast<Inbox*>(value);
    std::lock_guard<std::mutex> lock(inbox.mutex);
    --inbox.refs;
    inbox.cv.notify_all();
}
extern "C" fly_session_result_v2 __wrap_fly_session_deliver_v2(
    fly_session_inbox_v2_t* value, const fly_session_port_event_v2* event) {
    auto& inbox = *reinterpret_cast<Inbox*>(value);
    std::lock_guard<std::mutex> lock(inbox.mutex);
    ++inbox.attempts;
    if (inbox.attempts > 1)
        check(std::memcmp(&inbox.event, event, sizeof(*event)) == 0, "outbox retries exact same event");
    inbox.event = *event;
    const auto parsed = flynes::session::parse_provider_event_v2(*event, event->token,
        event->payload_kind, inbox.parsed);
    check(parsed == FLY_SESSION_V2_OK, "actual shared parser accepts completion");
    inbox.cv.notify_all();
    return inbox.attempts <= inbox.backpressure ? FLY_SESSION_V2_BACKPRESSURE : inbox.delivery;
}
int main(int argc, char** argv) {
    if (argc > 1 && std::strcmp(argv[1],"--cleanup-wakeup") == 0) {
        cleanup_wakeup_test(); return failures ? 1 : 0;
    }
    if (argc < 2 || std::strcmp(argv[1],"--late-context") != 0) {
        tests(); secret_tests(); bounded_cancel_test(); secret_capacity_test(); byte_budget_and_pin_test();
        admission_boundaries_test(); outbox_boundaries_test();
    }
    late_bootstrap_after_close_test();
    cleanup_wakeup_test();
    std::printf("crypto adapter failures=%d (transport seam only)\n", failures);
    return failures ? 1 : 0;
}
