#include "napi/native_api.h"
#include "nearby_quic_spike.h"
#include <array>
#include <new>

namespace {
struct Job {
    napi_async_work work = nullptr;
    napi_deferred deferred = nullptr;
    std::array<char, 129> bind{};
    std::array<char, 129> peer{};
    std::array<char, 183> pin{};
    size_t bindLength = 0;
    size_t peerLength = 0;
    size_t pinLength = 0;
    std::array<uint8_t, NEARBY_QUIC_RESULT_CAPACITY> output{};
    int32_t status = 3;
};

bool Check(napi_env env, napi_status status, const char* message)
{
    if (status == napi_ok) return true;
    if (napi_throw_error(env, nullptr, message) != napi_ok) {
        napi_fatal_error("NearbyQuicProbe", NAPI_AUTO_LENGTH, message, NAPI_AUTO_LENGTH);
    }
    return false;
}

bool Read(napi_env env, napi_value value, char* target, size_t capacity, size_t& length)
{
    napi_valuetype type;
    if (!Check(env, napi_typeof(env, value, &type), "Cannot inspect argument")) return false;
    if (type != napi_string) return Check(env, napi_invalid_arg, "Expected three strings");
    if (!Check(env, napi_get_value_string_utf8(env, value, nullptr, 0, &length), "Cannot measure string")) return false;
    if (length == 0 || length >= capacity) return Check(env, napi_invalid_arg, "Empty or oversized argument");
    size_t copied = 0;
    if (!Check(env, napi_get_value_string_utf8(env, value, target, capacity, &copied), "Cannot copy string")) return false;
    return copied == length || Check(env, napi_invalid_arg, "String changed during copy");
}

void Execute(napi_env, void* data)
{
    auto* job = static_cast<Job*>(data);
    // Worker owns copied input and bounded output; no NAPI calls on this thread.
    job->status = nearby_quic_probe_client(
        reinterpret_cast<const uint8_t*>(job->bind.data()), job->bindLength,
        reinterpret_cast<const uint8_t*>(job->peer.data()), job->peerLength,
        reinterpret_cast<const uint8_t*>(job->pin.data()), job->pinLength,
        job->output.data(), job->output.size());
}

void Complete(napi_env env, napi_status status, void* data)
{
    auto* job = static_cast<Job*>(data);
    napi_value text = nullptr;
    napi_value error = nullptr;
    const char* result = status == napi_ok ? reinterpret_cast<const char*>(job->output.data()) : "ERROR native work cancelled";
    if (Check(env, napi_create_string_utf8(env, result, NAPI_AUTO_LENGTH, &text), "Cannot create result")) {
        if (status == napi_ok && job->status == 0) {
            Check(env, napi_resolve_deferred(env, job->deferred, text), "Cannot resolve probe");
        } else if (Check(env, napi_create_error(env, nullptr, text, &error), "Cannot create probe error")) {
            Check(env, napi_reject_deferred(env, job->deferred, error), "Cannot reject probe");
        }
    }
    const napi_status deleted = napi_delete_async_work(env, job->work);
    delete job;
    Check(env, deleted, "Cannot delete completed work");
}

napi_value RunClient(napi_env env, napi_callback_info info)
{
    size_t count = 4;
    napi_value args[4]{};
    if (!Check(env, napi_get_cb_info(env, info, &count, args, nullptr, nullptr), "Cannot read arguments")) return nullptr;
    if (count != 3) { Check(env, napi_invalid_arg, "Expected exactly three arguments"); return nullptr; }
    auto* job = new (std::nothrow) Job;
    if (!job) { Check(env, napi_generic_failure, "Cannot allocate probe work"); return nullptr; }
    if (!Read(env, args[0], job->bind.data(), job->bind.size(), job->bindLength) ||
        !Read(env, args[1], job->peer.data(), job->peer.size(), job->peerLength) ||
        !Read(env, args[2], job->pin.data(), job->pin.size(), job->pinLength)) {
        delete job;
        return nullptr;
    }
    napi_value name = nullptr;
    napi_value promise = nullptr;
    if (!Check(env, napi_create_string_utf8(env, "NearbyQuicProbe", NAPI_AUTO_LENGTH, &name), "Cannot name work") ||
        !Check(env, napi_create_promise(env, &job->deferred, &promise), "Cannot create promise") ||
        !Check(env, napi_create_async_work(env, nullptr, name, Execute, Complete, job, &job->work), "Cannot create work")) {
        delete job;
        return nullptr;
    }
    const napi_status queued = napi_queue_async_work(env, job->work);
    if (queued != napi_ok) {
        const napi_status deleted = napi_delete_async_work(env, job->work);
        delete job;
        Check(env, deleted, "Cannot delete unqueued work");
        Check(env, queued, "Cannot queue work");
        return nullptr;
    }
    return promise;
}

napi_value Init(napi_env env, napi_value exports)
{
    napi_property_descriptor property = {"runClient", nullptr, RunClient, nullptr, nullptr, nullptr, napi_default, nullptr};
    return Check(env, napi_define_properties(env, exports, 1, &property), "Cannot export runClient") ? exports : nullptr;
}
} // namespace

static napi_module module = {1, 0, nullptr, Init, "nearbyprobe", nullptr, {0}};
extern "C" __attribute__((constructor)) void RegisterNearbyProbe() { napi_module_register(&module); }
