// Test APK only. Same production adapter/JNI source, independent native artifact.
// This sink exercises the C table and real shared payload parser, not an engine.
#include "crypto_jni.hpp"
#include "ports/provider_events.hpp"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <functional>
#include <mutex>

using flynes::android::nearby::CryptoPort;
using flynes::android::nearby::java_crypto_backend;
using flynes::android::nearby::java_crypto_secret;
struct Captured {
    fly_session_result_v2 result = FLY_SESSION_V2_UNAVAILABLE;
    std::uint32_t kind = 0;
    fly_session_resource_handle_v2 secret = 0;
    std::vector<std::uint8_t> bytes;
};
struct fly_session_inbox_v2_handle {
    std::atomic<unsigned> refs{1};
    std::mutex mutex;
    std::condition_variable cv;
    bool done = false;
    fly_session_op_token_v2 expected{};
    Captured captured;
};
extern "C" void fly_session_inbox_retain_v2(fly_session_inbox_v2_t* value) { ++value->refs; }
extern "C" void fly_session_inbox_release_v2(fly_session_inbox_v2_t* value) {
    if (--value->refs == 0) delete value;
}
extern "C" fly_session_result_v2 fly_session_deliver_v2(
    fly_session_inbox_v2_t* sink, const fly_session_port_event_v2* event) {
    std::lock_guard<std::mutex> lock(sink->mutex);
    flynes::session::ParsedProviderEvent parsed;
    const auto status = flynes::session::parse_provider_event_v2(*event, sink->expected,
        event->payload_kind, parsed);
    if (status != FLY_SESSION_V2_OK) return status;
    sink->captured.result = event->result; sink->captured.kind = event->payload_kind;
    sink->captured.secret = parsed.resource;
    if (parsed.buffer) {
        std::uint64_t size = 0, written = 0;
        if (fly_session_buffer_size_v2(parsed.buffer, &size) != FLY_SESSION_V2_OK || size > 1024*1024)
            return FLY_SESSION_V2_INVALID_ARGUMENT;
        sink->captured.bytes.resize(static_cast<std::size_t>(size));
        const fly_session_write_bytes_v2 out{sink->captured.bytes.data(), size};
        if (fly_session_buffer_read_v2(parsed.buffer, 0, out, &written) != FLY_SESSION_V2_OK || written != size)
            return FLY_SESSION_V2_UNAVAILABLE;
    }
    sink->done = true; sink->cv.notify_all();
    return FLY_SESSION_V2_ACCEPTED;
}
namespace {
fly_session_op_token_v2 token(std::uint64_t id) {
    fly_session_op_token_v2 value{};
    value.struct_size = FLY_SESSION_OP_TOKEN_V2_SIZE; value.abi_version = 2;
    value.scope.struct_size = FLY_SESSION_SCOPE_V2_SIZE; value.scope.abi_version = 2;
    value.scope.kind = FLY_SESSION_SCOPE_LINK_V2; value.operation_id = id;
    value.engine_instance_id[0] = 17; value.connection_generation = 23;
    return value;
}
fly_session_bytes_v2 bytes(const std::vector<std::uint8_t>& value) {
    return {value.data(), static_cast<std::uint32_t>(value.size()), 0};
}
Captured call(std::uint64_t id, const std::function<fly_session_result_v2(
    const fly_session_op_token_v2*, fly_session_inbox_v2_t*)>& operation) {
    auto* sink = new fly_session_inbox_v2_handle;
    sink->expected = token(id);
    Captured result;
    const auto admitted = operation(&sink->expected, sink);
    if (admitted == FLY_SESSION_V2_ACCEPTED) {
        std::unique_lock<std::mutex> lock(sink->mutex);
        if (sink->cv.wait_for(lock, std::chrono::seconds(20), [&] { return sink->done; }))
            result = sink->captured;
        else result.result = FLY_SESSION_V2_TIMEOUT;
    } else result.result = admitted;
    fly_session_inbox_release_v2(sink); // jobs retain heap sink even after timeout
    return result;
}
struct RealMaterialBackend : CryptoPort::Backend {
    std::shared_ptr<CryptoPort::Backend> delegate;
    JavaVM* vm = nullptr;
    jobject context = nullptr;
    jclass helpers = nullptr;
    jmethodID create_material = nullptr;
    std::array<fly_session_resource_handle_v2,2> roots{};
    std::array<std::vector<std::uint8_t>,5> verification;
    bool initialized = false;
    int fail_wrap = 0;
    int failed_wrap_closes = 0;
    int total_closes = 0;
    bool fail_verify = false;
    ~RealMaterialBackend() override {
        JNIEnv* env = nullptr;
        bool attached = vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED;
        if (attached && vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return;
        if (env) { env->DeleteGlobalRef(context); env->DeleteGlobalRef(helpers); }
        if (attached) vm->DetachCurrentThread();
    }
    CryptoPort::Result execute(const CryptoPort::Request& request, CryptoPort::Secret* secret) override {
        if (!initialized) {
            initialized = true;
            JNIEnv* env = nullptr;
            if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_EDETACHED &&
                vm->AttachCurrentThread(&env, nullptr) != JNI_OK) return {};
            if (!env || env->PushLocalFrame(32) != JNI_OK) return {};
            auto values = static_cast<jobjectArray>(env->CallStaticObjectMethod(helpers, create_material, context));
            if (env->ExceptionCheck() || !values) {
                env->ExceptionClear(); env->PopLocalFrame(nullptr); return {};
            }
            bool ok = env->GetArrayLength(values) == 7;
            for (int index = 0; ok && index < 2; ++index) {
                auto object = env->GetObjectArrayElement(values, index);
                auto handle = java_crypto_secret(delegate, env, object);
                ok = handle && CryptoPort::adopt(request.context,handle, 32, token(1000+index), &roots[index]) == FLY_SESSION_V2_OK;
                if (handle) handle->close();
            }
            for (int index = 0; ok && index < 5; ++index) {
                auto value = static_cast<jbyteArray>(env->GetObjectArrayElement(values,index+2));
                const auto size = value ? env->GetArrayLength(value) : 0;
                ok = size > 0 && size <= 1024;
                if (ok) {
                    verification[index].resize(static_cast<std::size_t>(size));
                    env->GetByteArrayRegion(value,0,size,reinterpret_cast<jbyte*>(verification[index].data()));
                }
            }
            if (env->ExceptionCheck()) { env->ExceptionClear(); ok = false; }
            env->PopLocalFrame(nullptr);
            if (!ok) return {};
        }
        const auto before = flynes::android::nearby::crypto_jni_test_close_count();
        if (fail_wrap) flynes::android::nearby::crypto_jni_test_fail_next_wrap(fail_wrap);
        if (fail_verify) flynes::android::nearby::crypto_jni_test_fail_next_verify();
        try {
            auto result = delegate->execute(request, secret);
            total_closes = flynes::android::nearby::crypto_jni_test_close_count();
            failed_wrap_closes = total_closes - before;
            return result;
        } catch (...) {
            total_closes = flynes::android::nearby::crypto_jni_test_close_count();
            failed_wrap_closes = total_closes - before;
            throw;
        }
    }
};
int exercise(JNIEnv* env, jobject context, jclass helpers) {
    auto production = java_crypto_backend(env, context);
    if (!production) return 1; // RED: real JNI provider factory not implemented
    auto backend = std::make_shared<RealMaterialBackend>();
    backend->delegate = production;
    if (env->GetJavaVM(&backend->vm) != JNI_OK) return 2;
    backend->context = env->NewGlobalRef(context);
    backend->helpers = static_cast<jclass>(env->NewGlobalRef(helpers));
    backend->create_material = env->GetStaticMethodID(helpers,"createMaterial","(Landroid/content/Context;)[Ljava/lang/Object;");
    if (env->ExceptionCheck()) { env->ExceptionClear(); return 3; }
    CryptoPort owner(backend);
    const auto port = owner.port();
    int failures = 0;
    const auto expect = [&](bool yes) { if (!yes) ++failures; };
    const auto random = [&](std::uint64_t id, std::uint32_t count) {
        return call(id,[&](auto* token,auto* inbox) { return port.random(port.context,token,count,{},inbox); });
    };
    const auto first = random(1,32), second = random(2,32), nonce = random(3,12);
    expect(first.result == 0 && first.bytes.size() == 32 && second.result == 0 && first.bytes != second.bytes);
    if (failures || !backend->roots[0] || !backend->roots[1]) { owner.close(); return failures+10; }
    std::vector<std::uint8_t> info{1,2,3};
    std::array<fly_session_resource_handle_v2,2> keys{};
    for (std::size_t i=0;i<2;++i) {
        const auto derived = call(4+i,[&](auto* token,auto* inbox) {
            return port.hkdf(port.context,token,backend->roots[i],bytes(first.bytes),bytes(info),32,inbox); });
        expect(derived.result == 0 && derived.secret != 0 && derived.kind == FLY_SESSION_PROVIDER_CRYPTO_SECRET_V2);
        keys[i] = derived.secret;
    }
    const auto mac = [&](std::uint64_t id, fly_session_resource_handle_v2 key) {
        return call(id,[&](auto* token,auto* inbox) { return port.hmac_sha256(port.context,token,key,bytes(second.bytes),inbox); });
    };
    const auto left_mac = mac(6,keys[0]), right_mac = mac(7,keys[1]);
    expect(left_mac.result == 0 && right_mac.result == 0 && left_mac.bytes.size() == 32 && left_mac.bytes == right_mac.bytes);
    const auto sealed = call(8,[&](auto* token,auto* inbox) {
        return port.aead_seal(port.context,token,keys[0],bytes(nonce.bytes),bytes(info),bytes(second.bytes),inbox); });
    expect(sealed.result == 0 && sealed.bytes.size() == second.bytes.size()+16);
    const auto opened = call(9,[&](auto* token,auto* inbox) {
        return port.aead_open(port.context,token,keys[1],bytes(nonce.bytes),bytes(info),bytes(sealed.bytes),inbox); });
    expect(opened.result == 0 && opened.bytes == second.bytes);
    auto tampered = sealed.bytes;
    if (!tampered.empty()) tampered.back() ^= 1;
    const auto bad_tag = call(10,[&](auto* token,auto* inbox) {
        return port.aead_open(port.context,token,keys[1],bytes(nonce.bytes),bytes(info),bytes(tampered),inbox); });
    expect(bad_tag.result == FLY_SESSION_V2_AUTH_FAILED && bad_tag.kind == FLY_SESSION_PROVIDER_CRYPTO_AEAD_V2);
    const auto verify = [&](std::uint64_t id) { return call(id,[&](auto* token,auto* inbox) {
        return port.verify_prehashed(port.context,token,bytes(backend->verification[0]),bytes(backend->verification[1]),
            backend->verification[2].data(),bytes(backend->verification[3]),inbox); }); };
    expect(verify(11).result == 0);
    backend->verification[1] = {0xff,0,0x81}; // opaque, not UTF-8 or rehashed domain
    expect(verify(30).result == 0);
    std::swap(backend->verification[3],backend->verification[4]);
    expect(verify(31).result == FLY_SESSION_V2_AUTH_FAILED);
    std::swap(backend->verification[3],backend->verification[4]);
    auto valid_key = backend->verification[0];
    backend->verification[0].assign(65,0); backend->verification[0][0]=4;
    expect(verify(32).result == FLY_SESSION_V2_AUTH_FAILED);
    backend->verification[0] = std::move(valid_key);
    backend->fail_verify = true;
    const auto provider_failure = verify(33);
    expect(provider_failure.result == FLY_SESSION_V2_UNAVAILABLE &&
           provider_failure.kind == FLY_SESSION_PROVIDER_CRYPTO_VERIFICATION_V2 &&
           provider_failure.secret == 0 && provider_failure.bytes.empty());
    backend->fail_verify = false;
    backend->verification[2][0] ^= 1;
    expect(verify(12).result == FLY_SESSION_V2_AUTH_FAILED);
    for (int failure = 1; failure <= 2; ++failure) {
        backend->fail_wrap = failure;
        const auto failed = call(20+failure,[&](auto* token,auto* inbox) {
            return port.hkdf(port.context,token,backend->roots[0],{},bytes(info),32,inbox); });
        expect(failed.result == FLY_SESSION_V2_OUT_OF_MEMORY && failed.secret == 0);
        expect(backend->failed_wrap_closes == 1);
    }
    backend->fail_wrap = 0;
    for (auto key : keys) expect(port.release_secret(port.context,key) == 0);
    for (auto root : backend->roots) expect(port.release_secret(port.context,root) == 0);
    expect(random(23,1).result == 0); // worker drains retired slots before next job
    expect(backend->total_closes == 6); // two failed locals + four normal transfers
    expect(mac(13,keys[0]).result == FLY_SESSION_V2_STALE);
    auto cancelled = token(14);
    expect(port.cancel(port.context,&cancelled) == 0);
    owner.close();
    expect(random(15,1).result == FLY_SESSION_V2_CLOSED);
    return failures;
}
}
extern "C" JNIEXPORT jint JNICALL Java_com_flynes_emu_nearby_AndroidCryptoPortTest_exercise(
    JNIEnv* env, jclass, jobject context, jclass helpers) {
    try { return exercise(env,context,helpers); }
    catch (...) { if (env->ExceptionCheck()) env->ExceptionClear(); return 999; }
}
