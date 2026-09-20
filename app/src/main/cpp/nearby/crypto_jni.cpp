#include "crypto_jni.hpp"
#include <array>
#include <utility>
namespace flynes::android::nearby {
namespace {
#ifdef FLYNES_CRYPTO_JNI_TEST
thread_local int wrap_failure = 0;
thread_local int close_count = 0;
thread_local bool verify_failure = false;
#endif
class Env final {
public:
    JavaVM* vm;
    JNIEnv* value = nullptr;
    bool attached = false;
    explicit Env(JavaVM* java_vm) : vm(java_vm) {
        const auto status = vm->GetEnv(reinterpret_cast<void**>(&value), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            attached = vm->AttachCurrentThread(&value,nullptr) == JNI_OK;
            if (!attached) value = nullptr;
        } else if (status != JNI_OK) value = nullptr;
    }
    ~Env() { if (attached) vm->DetachCurrentThread(); }
};
JNIEnv* worker_env(JavaVM* vm) {
    // One attachment per process-lifetime worker, never a cached cross-thread JNIEnv.
    thread_local Env attached(vm);
    return attached.vm == vm ? attached.value : nullptr;
}
struct LocalFrame {
    JNIEnv* env;
    ~LocalFrame() { env->PopLocalFrame(nullptr); }
};
struct Metadata {
    JavaVM* vm = nullptr;
    jobject context = nullptr;
    jobject provider = nullptr;
    jclass type = nullptr;
    jclass secret_type = nullptr;
    // Order also defines classification: OOM, bad GCM tag, invalid argument,
    // unsupported primitive, permission denial. Everything else is unavailable.
    std::array<jclass,5> errors{};
    jmethodID constructor = nullptr, random = nullptr, hkdf = nullptr;
    jmethodID seal = nullptr, open = nullptr, verify = nullptr, hmac = nullptr, close = nullptr;
    bool initialized = false; // accessed only by this context's controlled worker
    fly_session_result_v2 initialization = FLY_SESSION_V2_UNAVAILABLE;
    ~Metadata() {
        if (!vm) return;
        Env attached(vm); auto* env = attached.value;
        if (!env) return;
        if (context) env->DeleteGlobalRef(context);
        if (provider) env->DeleteGlobalRef(provider);
        if (type) env->DeleteGlobalRef(type);
        if (secret_type) env->DeleteGlobalRef(secret_type);
        for (auto error : errors) if (error) env->DeleteGlobalRef(error);
    }
    fly_session_result_v2 exception(JNIEnv* env) const {
        if (!env->ExceptionCheck()) return FLY_SESSION_V2_OK;
        auto failure = env->ExceptionOccurred();
        env->ExceptionClear();
        const fly_session_result_v2 codes[]{FLY_SESSION_V2_OUT_OF_MEMORY, FLY_SESSION_V2_AUTH_FAILED,
            FLY_SESSION_V2_INVALID_ARGUMENT, FLY_SESSION_V2_UNSUPPORTED, FLY_SESSION_V2_PERMISSION_DENIED};
        fly_session_result_v2 result = FLY_SESSION_V2_UNAVAILABLE;
        for (std::size_t i=0;i<errors.size();++i) if (errors[i] && env->IsInstanceOf(failure,errors[i])) {
            result = codes[i]; break;
        }
        if (failure) env->DeleteLocalRef(failure);
        return result;
    }
};
// Cleanup must neither call Java with a pending exception nor replace the
// original allocation failure with an exception raised by cleanup itself.
void close_java_secret(const Metadata& state, JNIEnv* env, jobject value) noexcept {
    auto original = env->ExceptionOccurred();
    if (original) env->ExceptionClear();
    env->CallVoidMethod(value,state.close);
#ifdef FLYNES_CRYPTO_JNI_TEST
    ++close_count;
#endif
    if (env->ExceptionCheck()) env->ExceptionClear();
    if (original) { env->Throw(original); env->DeleteLocalRef(original); }
}
struct LocalSecretOwner {
    const Metadata& state;
    JNIEnv* env;
    jobject value;
    ~LocalSecretOwner() { if (value) close_java_secret(state,env,value); }
};
class JavaSecret final : public CryptoPort::Secret {
public:
    std::shared_ptr<Metadata> state;
    jobject object = nullptr;
    explicit JavaSecret(std::shared_ptr<Metadata> value) : state(std::move(value)) {}
    ~JavaSecret() override { close(); }
    void close() noexcept override {
        if (!object) return;
        auto* env = worker_env(state->vm);
        if (!env) return;
        close_java_secret(*state,env,object);
        env->DeleteGlobalRef(object); object = nullptr;
    }
};
std::unique_ptr<CryptoPort::Secret> wrap_secret(const std::shared_ptr<Metadata>& state,
                                              JNIEnv* env, jobject value) {
    if (!value || !env->IsInstanceOf(value,state->secret_type)) return {};
    LocalSecretOwner local{*state,env,value};
#ifdef FLYNES_CRYPTO_JNI_TEST
    const int failure = wrap_failure; wrap_failure = 0;
    if (failure == 1) throw std::bad_alloc();
#endif
    auto secret = std::make_unique<JavaSecret>(state);
#ifdef FLYNES_CRYPTO_JNI_TEST
    if (failure == 2) {
        env->ThrowNew(state->errors[0], "test allocation failure");
        return {};
    }
#endif
    auto global = env->NewGlobalRef(value);
    if (env->ExceptionCheck()) {
        if (global) env->DeleteGlobalRef(global);
        return {};
    }
    if (!global) throw std::bad_alloc();
    secret->object = global;
    local.value = nullptr;
    return secret;
}
class JavaBackend final : public CryptoPort::Backend {
public:
    std::shared_ptr<Metadata> state;
    explicit JavaBackend(std::shared_ptr<Metadata> value) : state(std::move(value)) {}
    CryptoPort::Result execute(const CryptoPort::Request& request, CryptoPort::Secret* opaque) override {
        CryptoPort::Result result;
        auto* env = worker_env(state->vm);
        if (!env) return result;
        if (env->PushLocalFrame(24) != JNI_OK) {
            result.result = state->exception(env); return result;
        }
        LocalFrame frame{env};
        if (!state->initialized) {
            state->initialized = true;
            auto provider = env->NewObject(state->type,state->constructor,state->context);
            state->initialization = state->exception(env);
            if (state->initialization == FLY_SESSION_V2_OK && provider) {
                state->provider = env->NewGlobalRef(provider);
                state->initialization = state->exception(env);
            }
            if (!state->provider && state->initialization == FLY_SESSION_V2_OK)
                state->initialization = FLY_SESSION_V2_UNAVAILABLE;
        }
        if (state->initialization != FLY_SESSION_V2_OK) {
            result.result = state->initialization; return result;
        }
        const auto operation = request.operation;
        const bool needs_secret = operation != CryptoPort::Operation::Random && operation != CryptoPort::Operation::Verify;
        auto* secret = dynamic_cast<JavaSecret*>(opaque);
        if (needs_secret && (!secret || !secret->object || secret->state != state)) return result;
        const std::size_t argument_count = operation == CryptoPort::Operation::Random ? 0 :
            operation == CryptoPort::Operation::Verify ? 4 : operation == CryptoPort::Operation::Hmac ? 1 :
            operation == CryptoPort::Operation::Hkdf ? 2 : 3;
        std::array<jbyteArray,4> arguments{};
        for (std::size_t index=0;index<argument_count;++index) {
            const auto& bytes = request.inputs[index];
            arguments[index] = env->NewByteArray(static_cast<jsize>(bytes.size()));
            result.result = state->exception(env);
            if (result.result != FLY_SESSION_V2_OK) return result;
            if (!arguments[index]) { result.result = FLY_SESSION_V2_OUT_OF_MEMORY; return result; }
            if (!bytes.empty()) env->SetByteArrayRegion(arguments[index],0,static_cast<jsize>(bytes.size()),
                reinterpret_cast<const jbyte*>(bytes.data()));
            result.result = state->exception(env);
            if (result.result != FLY_SESSION_V2_OK) return result;
        }
        jobject output = nullptr;
        switch (operation) {
        case CryptoPort::Operation::Random:
            output = env->CallObjectMethod(state->provider,state->random,static_cast<jint>(request.count)); break;
        case CryptoPort::Operation::Hkdf:
            output = env->CallObjectMethod(state->provider,state->hkdf,secret->object,arguments[0],arguments[1],
                                          static_cast<jint>(request.count)); break;
        case CryptoPort::Operation::Seal:
        case CryptoPort::Operation::Open:
            output = env->CallObjectMethod(state->provider,
                operation == CryptoPort::Operation::Seal ? state->seal : state->open,
                secret->object,arguments[0],arguments[1],arguments[2]); break;
        case CryptoPort::Operation::Hmac:
            output = env->CallObjectMethod(state->provider,state->hmac,secret->object,arguments[0]); break;
        case CryptoPort::Operation::Verify: {
#ifdef FLYNES_CRYPTO_JNI_TEST
            if (verify_failure) {
                verify_failure = false;
                // Bootstrap exception class only; no provider algorithm override.
                auto type = env->FindClass("java/security/SignatureException");
                if (type) env->ThrowNew(type,"test provider failure");
                result.result = state->exception(env);
                return result;
            }
#endif
            const auto verified = env->CallBooleanMethod(state->provider,state->verify,
                arguments[0],arguments[1],arguments[2],arguments[3]);
            result.result = state->exception(env);
            if (result.result == FLY_SESSION_V2_OK && verified != JNI_TRUE) result.result = FLY_SESSION_V2_AUTH_FAILED;
            return result;
        }
        }
        result.result = state->exception(env);
        if (result.result != FLY_SESSION_V2_OK) return result;
        if (!output) { result.result = FLY_SESSION_V2_UNAVAILABLE; return result; }
        if (operation == CryptoPort::Operation::Hkdf) {
            result.secret = wrap_secret(state,env,output);
            result.result = state->exception(env);
            if (!result.secret && result.result == FLY_SESSION_V2_OK) result.result = FLY_SESSION_V2_UNAVAILABLE;
            return result;
        }
        const auto array = static_cast<jbyteArray>(output);
        const auto size = env->GetArrayLength(array);
        result.result = state->exception(env);
        if (result.result != FLY_SESSION_V2_OK) return result;
        if (size < 0 || static_cast<std::uint32_t>(size) != request.count) {
            result.result = FLY_SESSION_V2_UNAVAILABLE; return result;
        }
        result.bytes.resize(static_cast<std::size_t>(size));
        if (size) env->GetByteArrayRegion(array,0,size,reinterpret_cast<jbyte*>(result.bytes.data()));
        result.result = state->exception(env);
        return result;
    }
};
bool resolve_class(JNIEnv* env, const char* name, jclass* output) {
    auto local = env->FindClass(name);
    if (!local || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    *output = static_cast<jclass>(env->NewGlobalRef(local));
    env->DeleteLocalRef(local);
    if (!*output || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}
bool resolve_method(JNIEnv* env, jclass type, const char* name, const char* signature, jmethodID* output) {
    *output = env->GetMethodID(type,name,signature);
    if (!*output || env->ExceptionCheck()) { env->ExceptionClear(); return false; }
    return true;
}
}
std::shared_ptr<CryptoPort::Backend> java_crypto_backend(JNIEnv* env, jobject context) {
    if (!env || !context) return {};
    auto state = std::make_shared<Metadata>();
    if (env->GetJavaVM(&state->vm) != JNI_OK) return {};
    if (!resolve_class(env,"com/flynes/emu/nearby/AndroidSecureProvider",&state->type) ||
        !resolve_class(env,"com/flynes/emu/nearby/AndroidSecureProvider$SecretHandle",&state->secret_type)) return {};
    const char* errors[]{"java/lang/OutOfMemoryError","javax/crypto/AEADBadTagException",
        "java/lang/IllegalArgumentException","java/lang/UnsupportedOperationException","java/lang/SecurityException"};
    for (std::size_t i=0;i<state->errors.size();++i)
        if (!resolve_class(env,errors[i],&state->errors[i])) return {};
    if (!resolve_method(env,state->type,"<init>","(Landroid/content/Context;)V",&state->constructor) ||
        !resolve_method(env,state->type,"random","(I)[B",&state->random) ||
        !resolve_method(env,state->type,"hkdfSha256","(Lcom/flynes/emu/nearby/AndroidSecureProvider$SecretHandle;[B[BI)Lcom/flynes/emu/nearby/AndroidSecureProvider$SecretHandle;",&state->hkdf) ||
        !resolve_method(env,state->type,"aeadSeal","(Lcom/flynes/emu/nearby/AndroidSecureProvider$SecretHandle;[B[B[B)[B",&state->seal) ||
        !resolve_method(env,state->type,"aeadOpen","(Lcom/flynes/emu/nearby/AndroidSecureProvider$SecretHandle;[B[B[B)[B",&state->open) ||
        !resolve_method(env,state->type,"hmacSha256","(Lcom/flynes/emu/nearby/AndroidSecureProvider$SecretHandle;[B)[B",&state->hmac) ||
        !resolve_method(env,state->type,"verifyPrehashed","([B[B[B[B)Z",&state->verify) ||
        !resolve_method(env,state->secret_type,"close","()V",&state->close)) return {};
    state->context = env->NewGlobalRef(context);
    if (state->exception(env) != FLY_SESSION_V2_OK || !state->context) return {};
    return std::make_shared<JavaBackend>(std::move(state));
}
std::unique_ptr<CryptoPort::Secret> java_crypto_secret(
    const std::shared_ptr<CryptoPort::Backend>& backend, JNIEnv* env, jobject secret) {
    const auto java = std::dynamic_pointer_cast<JavaBackend>(backend);
    if (!java || !env) return {};
    auto result = wrap_secret(java->state,env,secret);
    if (java->state->exception(env) != FLY_SESSION_V2_OK) return {};
    return result;
}
#ifdef FLYNES_CRYPTO_JNI_TEST
void crypto_jni_test_fail_next_wrap(int point) { wrap_failure = point; }
void crypto_jni_test_fail_next_verify() { verify_failure = true; }
int crypto_jni_test_close_count() { return close_count; }
#endif
}
