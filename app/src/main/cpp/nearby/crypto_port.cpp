#include "crypto_port.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <limits>
#include <new>
#include <stdexcept>
#include <thread>

namespace flynes::android::nearby {
struct CryptoPort::Context {
    std::atomic<unsigned> references{1};
    std::shared_ptr<Backend> backend;
    bool closed = false;
    bool bound = false;
    std::array<std::uint8_t, 16> engine{};
    explicit Context(std::shared_ptr<Backend> value) : backend(std::move(value)) {}
};
namespace {
using Context = CryptoPort::Context;
using Operation = CryptoPort::Operation;
using Bytes = fly_session_bytes_v2;
void retain(void* value) { static_cast<Context*>(value)->references.fetch_add(1); }
void release(void* value) {
    auto* context = static_cast<Context*>(value);
    if (context->references.fetch_sub(1) == 1) delete context;
}
fly_session_result_v2 validate(const fly_session_op_token_v2* value) {
    if (!value) return FLY_SESSION_V2_INVALID_ARGUMENT;
    if (value->struct_size != FLY_SESSION_OP_TOKEN_V2_SIZE || value->abi_version != 2 ||
        value->scope.struct_size != FLY_SESSION_SCOPE_V2_SIZE || value->scope.abi_version != 2)
        return FLY_SESSION_V2_ABI_MISMATCH;
    if (value->scope.reserved_zero || !value->operation_id ||
        value->scope.kind < FLY_SESSION_SCOPE_ENGINE_V2 || value->scope.kind > FLY_SESSION_SCOPE_GAME_V2)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    return FLY_SESSION_V2_OK;
}
bool same(const fly_session_op_token_v2& a, const fly_session_op_token_v2& b) {
    return a.struct_size == b.struct_size && a.abi_version == b.abi_version &&
        std::memcmp(a.engine_instance_id, b.engine_instance_id, 16) == 0 &&
        a.scope.struct_size == b.scope.struct_size && a.scope.abi_version == b.scope.abi_version &&
        a.scope.kind == b.scope.kind && a.scope.reserved_zero == b.scope.reserved_zero &&
        std::memcmp(a.scope.link_id, b.scope.link_id, 16) == 0 &&
        std::memcmp(a.scope.branch_id, b.scope.branch_id, 16) == 0 &&
        a.connection_generation == b.connection_generation && a.config_revision == b.config_revision &&
        a.authority_term == b.authority_term && a.writer_generation == b.writer_generation &&
        a.timeline_epoch == b.timeline_epoch && a.seat_revision == b.seat_revision &&
        a.mode_generation == b.mode_generation && a.media_generation == b.media_generation &&
        a.operation_id == b.operation_id && std::memcmp(a.transition_id, b.transition_id, 16) == 0;
}
struct Job {
    Context* context = nullptr;
    fly_session_inbox_v2_t* inbox = nullptr;
    CryptoPort::Request request;
    fly_session_port_event_v2 event{};
    fly_session_buffer_v2_t* buffer = nullptr;
    bool cancelled = false;
    bool completed = false;
    std::size_t charge = 0;
    std::size_t input_slot = CryptoPort::kMaximumSecrets;
    std::size_t output_slot = CryptoPort::kMaximumSecrets;
    std::chrono::steady_clock::time_point retry{};
    ~Job() {
        fly_session_buffer_release_v2(buffer);
        if (inbox) fly_session_inbox_release_v2(inbox);
        if (context) release(context);
    }
};
struct SecretSlot {
    Context* context = nullptr;
    std::unique_ptr<CryptoPort::Secret> secret;
    fly_session_resource_handle_v2 id = 0;
    fly_session_op_token_v2 producer{};
    std::uint32_t bytes = 0;
    unsigned pins = 0;
    bool retired = false;
};
class Worker {
public:
    std::mutex mutex;
    std::condition_variable cv;
    std::array<std::unique_ptr<Job>, CryptoPort::kMaximumJobs> jobs;
    std::size_t charged = 0;
    std::array<SecretSlot, CryptoPort::kMaximumSecrets> secrets;
    std::uint64_t next_secret = 1;
    std::thread::id thread_id;
    Worker() {
        std::thread thread([this] { run(); });
        thread_id = thread.get_id(); thread.detach();
    }
    std::size_t find_secret(Context* context, fly_session_resource_handle_v2 id) {
        for (std::size_t i = 0; i < secrets.size(); ++i)
            if (secrets[i].id == id && secrets[i].context == context && !secrets[i].retired && secrets[i].secret)
                return i;
        return secrets.size();
    }
    std::size_t reserve_secret(Context* context, const fly_session_op_token_v2& producer,
                               std::uint32_t bytes) {
        if (next_secret >= (std::uint64_t{1} << 63)) return secrets.size();
        for (std::size_t i = 0; i < secrets.size(); ++i) if (!secrets[i].id) {
            auto& slot = secrets[i];
            slot.context = context; retain(context);
            slot.id = next_secret++; slot.producer = producer; slot.bytes = bytes;
            return i;
        }
        return secrets.size();
    }
    static Worker& instance() {
        // Process lifetime: an owner must never join a possibly blocked JCA call.
        static auto* worker = new Worker;
        return *worker;
    }
private:
    void complete(Job& job) {
        CryptoPort::Result result;
        auto* input = job.input_slot < secrets.size() ? secrets[job.input_slot].secret.get() : nullptr;
        try { result = job.context->backend->execute(job.request, input); }
        catch (const std::bad_alloc&) { result.result = FLY_SESSION_V2_OUT_OF_MEMORY; }
        catch (...) { result.result = FLY_SESSION_V2_UNAVAILABLE; }
        const bool deriving = job.request.operation == Operation::Hkdf;
        if (deriving && result.result == FLY_SESSION_V2_OK && (!result.secret || !result.bytes.empty()))
            result.result = FLY_SESSION_V2_UNAVAILABLE;
        if (!deriving && result.secret) {
            result.secret->close(); result.secret.reset(); result.result = FLY_SESSION_V2_UNAVAILABLE;
        }
        if (result.result > FLY_SESSION_V2_OK) result.result = FLY_SESSION_V2_UNAVAILABLE;
        const bool verification = job.request.operation == Operation::Verify;
        if (result.result == FLY_SESSION_V2_OK && !deriving && result.bytes.size() != job.request.count)
            result.result = FLY_SESSION_V2_UNAVAILABLE;
        if (result.result == FLY_SESSION_V2_OK && !verification && !deriving) {
            const Bytes bytes{result.bytes.data(), static_cast<std::uint32_t>(result.bytes.size()), 0};
            result.result = fly_session_buffer_create_copy_v2(bytes, &job.buffer);
        }
        job.event.result = result.result;
        if (result.result == FLY_SESSION_V2_OK && deriving) {
            std::lock_guard<std::mutex> lock(mutex);
            auto& slot = secrets[job.output_slot];
            slot.secret = std::move(result.secret);
            fly_session_provider_resource_event_v2 payload{};
            payload.struct_size = sizeof(payload); payload.abi_version = 2;
            payload.resource = slot.id; payload.generation = job.event.token.connection_generation;
            job.event.payload_size = sizeof(payload);
            std::memcpy(job.event.payload, &payload, sizeof(payload));
        } else if (result.result == FLY_SESSION_V2_OK && !verification) {
            fly_session_provider_buffer_event_v2 payload{};
            payload.struct_size = sizeof(payload); payload.abi_version = 2;
            payload.buffer = job.buffer; payload.logical_size = result.bytes.size();
            payload.generation = job.event.token.connection_generation;
            job.event.payload_size = sizeof(payload);
            std::memcpy(job.event.payload, &payload, sizeof(payload));
        } else {
            fly_session_provider_end_event_v2 payload{};
            payload.struct_size = sizeof(payload); payload.abi_version = 2;
            job.event.payload_size = sizeof(payload);
            std::memcpy(job.event.payload, &payload, sizeof(payload));
        }
        if (result.secret) { result.secret->close(); result.secret.reset(); }
    }
    void run() {
        std::unique_lock<std::mutex> lock(mutex);
        std::size_t cursor = 0;
        for (;;) {
            for (auto& slot : secrets) if (slot.id && slot.retired && slot.pins == 0) {
                auto secret = std::move(slot.secret);
                auto* context = slot.context;
                slot = {};
                lock.unlock();
                if (secret) { secret->close(); secret.reset(); }
                release(context);
                lock.lock();
            }
            Job* ready = nullptr;
            std::size_t selected = 0;
            for (std::size_t offset = 0; offset < jobs.size(); ++offset) {
                const auto index = (cursor + offset) % jobs.size();
                auto* item = jobs[index].get();
                if (item && (item->cancelled || !item->completed || item->retry <= std::chrono::steady_clock::now())) {
                    ready = item; selected = index; break;
                }
            }
            if (!ready) {
                // Closing a later slot releases the lock. An earlier slot may
                // have been retired meanwhile, with its notification already
                // consumed before this wait. Recheck the cleanup predicate.
                if (std::any_of(secrets.begin(), secrets.end(), [](const auto& slot) {
                    return slot.id && slot.retired && slot.pins == 0;
                })) continue;
                const bool pending = std::any_of(jobs.begin(), jobs.end(), [](const auto& job) { return !!job; });
                if (pending) cv.wait_for(lock, std::chrono::milliseconds(10));
                else cv.wait(lock); // no timer wakeups while completely idle
                continue;
            }
            cursor = (selected + 1) % jobs.size();
            if (!ready->cancelled && !ready->completed) {
                lock.unlock(); complete(*ready); lock.lock();
                ready->completed = true;
            }
            bool published = false;
            if (!ready->cancelled) {
                // Publication reservation; never invert context/engine locks.
                lock.unlock();
                const auto delivered = fly_session_deliver_v2(ready->inbox, &ready->event);
                lock.lock();
                published = delivered == FLY_SESSION_V2_OK || delivered == FLY_SESSION_V2_ACCEPTED ||
                            delivered == FLY_SESSION_V2_DUPLICATE;
                if (!ready->cancelled && delivered == FLY_SESSION_V2_BACKPRESSURE) {
                    ready->retry = std::chrono::steady_clock::now() + std::chrono::milliseconds(10);
                    continue;
                }
            }
            if (ready->input_slot < secrets.size()) --secrets[ready->input_slot].pins;
            if (ready->output_slot < secrets.size()) {
                auto& slot = secrets[ready->output_slot];
                --slot.pins;
                if (!published || ready->cancelled || ready->event.result != FLY_SESSION_V2_OK) slot.retired = true;
            }
            charged -= ready->charge;
            auto retired = std::move(jobs[selected]);
            lock.unlock(); retired.reset(); lock.lock();
        }
    }
};
fly_session_result_v2 submit(void* value, const fly_session_op_token_v2* token,
    Operation operation, std::uint32_t count, const std::array<Bytes,4>& inputs,
    std::uint32_t kind, fly_session_inbox_v2_t* inbox, fly_session_resource_handle_v2 secret = 0) {
    const auto valid = validate(token);
    if (valid != FLY_SESSION_V2_OK) return valid;
    if (!value || !inbox) return FLY_SESSION_V2_INVALID_ARGUMENT;
    for (const auto& input : inputs)
        if (input.reserved_zero || (input.size && !input.data)) return FLY_SESSION_V2_INVALID_ARGUMENT;
    try {
        auto& worker = Worker::instance();
        auto* context = static_cast<Context*>(value);
        std::lock_guard<std::mutex> lock(worker.mutex);
        if (context->closed) return FLY_SESSION_V2_CLOSED;
        if (context->bound && std::memcmp(context->engine.data(), token->engine_instance_id, 16))
            return FLY_SESSION_V2_STALE;
        std::size_t input_slot = worker.secrets.size();
        if (operation == Operation::Hkdf || operation == Operation::Hmac || operation == Operation::Seal || operation == Operation::Open) {
            input_slot = worker.find_secret(context, secret);
            if (input_slot == worker.secrets.size()) return FLY_SESSION_V2_STALE;
            if ((operation == Operation::Seal || operation == Operation::Open) &&
                worker.secrets[input_slot].bytes != 32) return FLY_SESSION_V2_INVALID_ARGUMENT;
        }
        auto slot = std::find_if(worker.jobs.begin(), worker.jobs.end(), [](const auto& job) { return !job; });
        std::size_t charge = count;
        for (const auto& input : inputs) charge += input.size;
        if (slot == worker.jobs.end() || charge > CryptoPort::kMaximumBytes - worker.charged)
            return FLY_SESSION_V2_BACKPRESSURE;
        for (const auto& job : worker.jobs)
            if (job && job->context == context && same(job->event.token, *token)) return FLY_SESSION_V2_DUPLICATE;
        auto job = std::make_unique<Job>();
        job->request.context = context;
        job->request.operation = operation; job->request.count = count;
        for (std::size_t index = 0; index < inputs.size(); ++index)
            if (inputs[index].size) job->request.inputs[index].assign(inputs[index].data,
                inputs[index].data + inputs[index].size);
        if (operation == Operation::Hkdf) {
            job->output_slot = worker.reserve_secret(context, *token, count);
            if (job->output_slot == worker.secrets.size()) return FLY_SESSION_V2_BACKPRESSURE;
            ++worker.secrets[job->output_slot].pins;
        }
        job->input_slot = input_slot;
        if (input_slot < worker.secrets.size()) ++worker.secrets[input_slot].pins;
        job->event.struct_size = FLY_SESSION_PORT_EVENT_V2_SIZE; job->event.abi_version = 2;
        job->event.token = *token; job->event.event_sequence = 1; job->event.terminal = 1;
        job->event.event_kind = FLY_SESSION_PORT_EVENT_OPERATION_V2;
        job->event.payload_kind = kind;
        job->charge = charge;
        retain(context); job->context = context;
        fly_session_inbox_retain_v2(inbox); job->inbox = inbox;
        std::copy_n(token->engine_instance_id, 16, context->engine.begin()); context->bound = true;
        worker.charged += charge; *slot = std::move(job); worker.cv.notify_one();
        return FLY_SESSION_V2_ACCEPTED;
    } catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}
fly_session_result_v2 random(void* value, const fly_session_op_token_v2* token,
                            std::uint32_t count, Bytes purpose, fly_session_inbox_v2_t* inbox) {
    if (!count || count > 4096 || purpose.size > 1024) return FLY_SESSION_V2_INVALID_ARGUMENT;
    return submit(value, token, Operation::Random, count, {purpose,{}, {}, {}},
                  FLY_SESSION_PROVIDER_CRYPTO_RANDOM_V2, inbox);
}
fly_session_result_v2 cancel(void* value, const fly_session_op_token_v2* token) {
    const auto valid = validate(token);
    if (valid != FLY_SESSION_V2_OK) return valid;
    if (!value) return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto* context = static_cast<Context*>(value);
    auto& worker = Worker::instance();
    std::lock_guard<std::mutex> lock(worker.mutex);
    if (context->closed) return FLY_SESSION_V2_CLOSED;
    if (context->bound && std::memcmp(context->engine.data(), token->engine_instance_id, 16))
        return FLY_SESSION_V2_STALE;
    for (auto& job : worker.jobs)
        if (job && job->context == context && same(job->event.token, *token)) job->cancelled = true;
    // Engine-only invariant: caller can cancel only its unconsumed producer.
    // This is not permission for arbitrary clients to revoke transferred keys.
    for (auto& slot : worker.secrets)
        if (slot.id && slot.context == context && same(slot.producer, *token)) slot.retired = true;
    worker.cv.notify_one();
    return FLY_SESSION_V2_OK;
}
fly_session_result_v2 hkdf(void* value, const fly_session_op_token_v2* token, fly_session_resource_handle_v2 secret,
    Bytes salt, Bytes info, std::uint32_t count, fly_session_inbox_v2_t* inbox) {
    if (salt.size > 1024 || !info.size || info.size > 1024 || !count || count > 8160)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    return submit(value, token, Operation::Hkdf, count, {salt,info,{},{}},
                  FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2, inbox, secret);
}
fly_session_result_v2 aead(bool seal, void* value, const fly_session_op_token_v2* token,
    fly_session_resource_handle_v2 secret, Bytes nonce, Bytes aad, Bytes input, fly_session_inbox_v2_t* inbox) {
    if (nonce.size != 12 || aad.size > 4096 ||
        (seal ? input.size > 1024*1024-16 : input.size < 16 || input.size > 1024*1024))
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    return submit(value, token, seal ? Operation::Seal : Operation::Open,
                  seal ? input.size + 16 : input.size - 16, {nonce,aad,input,{}},
                  FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2, inbox, secret);
}
fly_session_result_v2 seal(void* value, const fly_session_op_token_v2* token, fly_session_resource_handle_v2 secret,
    Bytes nonce, Bytes aad, Bytes input, fly_session_inbox_v2_t* inbox) { return aead(true,value,token,secret,nonce,aad,input,inbox); }
fly_session_result_v2 open(void* value, const fly_session_op_token_v2* token, fly_session_resource_handle_v2 secret,
    Bytes nonce, Bytes aad, Bytes input, fly_session_inbox_v2_t* inbox) { return aead(false,value,token,secret,nonce,aad,input,inbox); }
fly_session_result_v2 verify(void* value, const fly_session_op_token_v2* token, Bytes key, Bytes domain,
    const std::uint8_t* digest, Bytes signature, fly_session_inbox_v2_t* inbox) {
    if (key.size != 65 || !key.data || key.data[0] != 4 || !domain.size || domain.size > 1024 ||
        !digest || signature.size != 64) return FLY_SESSION_V2_INVALID_ARGUMENT;
    return submit(value, token, Operation::Verify, 0, {key,domain,{digest,32,0},signature},
                  FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2, inbox);
}
fly_session_result_v2 hmac(void* value, const fly_session_op_token_v2* token, fly_session_resource_handle_v2 secret,
    Bytes input, fly_session_inbox_v2_t* inbox) {
    if (!input.size || input.size > 4096) return FLY_SESSION_V2_INVALID_ARGUMENT;
    return submit(value, token, Operation::Hmac, 32, {input,{},{},{}},
                  FLY_SESSION_PROVIDER_CRYPTO_MAC_V2, inbox, secret);
}
fly_session_result_v2 release_secret(void* value, fly_session_resource_handle_v2 id) {
    if (!value || !id) return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto& worker = Worker::instance();
    std::lock_guard<std::mutex> lock(worker.mutex);
    auto* context = static_cast<Context*>(value);
    if (context->closed) return FLY_SESSION_V2_CLOSED;
    const auto index = worker.find_secret(context, id);
    if (index == worker.secrets.size()) return FLY_SESSION_V2_STALE;
    worker.secrets[index].retired = true; worker.cv.notify_one();
    return FLY_SESSION_V2_OK;
}
}
CryptoPort::CryptoPort(std::shared_ptr<Backend> backend) : context_(nullptr) {
    if (!backend) throw std::invalid_argument("crypto backend required");
    Worker::instance(); // startup failure happens before a callable port exists
    context_ = new Context(std::move(backend));
}
CryptoPort::~CryptoPort() { close(); release(context_); }
fly_session_crypto_port_v2 CryptoPort::port() const noexcept {
    fly_session_crypto_port_v2 result{};
    result.struct_size = sizeof(result); result.abi_version = 2; result.context = context_;
    result.retain = retain; result.release = release; result.random = random; result.hkdf = hkdf;
    result.aead_seal = seal; result.aead_open = open; result.verify_prehashed = verify;
    result.hmac_sha256 = hmac; result.release_secret = release_secret; result.cancel = cancel;
    return result;
}
void CryptoPort::close() noexcept {
    try {
        auto& worker = Worker::instance();
        std::lock_guard<std::mutex> lock(worker.mutex);
        context_->closed = true;
        for (auto& job : worker.jobs) if (job && job->context == context_) job->cancelled = true;
        for (auto& slot : worker.secrets) if (slot.id && slot.context == context_) slot.retired = true;
        worker.cv.notify_one();
    } catch (...) { /* Startup failed: no worker and no admitted work exist. */ }
}
fly_session_result_v2 CryptoPort::adopt(Context* context, std::unique_ptr<Secret>& secret, std::uint32_t bytes,
    const fly_session_op_token_v2& producer, fly_session_resource_handle_v2* out) {
    const auto valid = validate(&producer);
    if (valid != FLY_SESSION_V2_OK) return valid;
    if (!context || !secret || !bytes || bytes > 8160 || !out) return FLY_SESSION_V2_INVALID_ARGUMENT;
    auto& worker = Worker::instance();
    if (std::this_thread::get_id() != worker.thread_id) return FLY_SESSION_V2_INVALID_STATE;
    std::lock_guard<std::mutex> lock(worker.mutex);
    if (context->closed) return FLY_SESSION_V2_CLOSED;
    if (context->bound && std::memcmp(context->engine.data(), producer.engine_instance_id, 16))
        return FLY_SESSION_V2_STALE;
    const auto index = worker.reserve_secret(context, producer, bytes);
    if (index == worker.secrets.size()) return FLY_SESSION_V2_BACKPRESSURE;
    worker.secrets[index].secret = std::move(secret);
    std::copy_n(producer.engine_instance_id, 16, context->engine.begin()); context->bound = true;
    *out = worker.secrets[index].id;
    return FLY_SESSION_V2_OK;
}
}
