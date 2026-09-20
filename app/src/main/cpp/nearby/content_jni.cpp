#include "content_jni.hpp"
#include "session_owner.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <memory>

namespace flynes::android::nearby {
namespace {
class AttachedEnv final {
public:
    explicit AttachedEnv(JavaVM* vm) : vm_(vm) {
        const auto status = vm_->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6);
        if (status == JNI_EDETACHED) {
            attached_ = vm_->AttachCurrentThread(&env, nullptr) == JNI_OK;
            if (!attached_) env = nullptr;
        } else if (status != JNI_OK) env = nullptr;
    }
    ~AttachedEnv() { if (attached_) vm_->DetachCurrentThread(); }
    JNIEnv* env = nullptr;
private:
    JavaVM* vm_;
    bool attached_ = false;
};
struct JavaMetadata final {
    JavaVM* vm = nullptr;
    jobject provider = nullptr;
    jmethodID query = nullptr, validate = nullptr, close = nullptr;
    ~JavaMetadata() {
        if (!provider) return;
        AttachedEnv attached(vm);
        if (attached.env) attached.env->DeleteGlobalRef(provider);
    }
};
bool exception(JNIEnv* env) {
    if (!env->ExceptionCheck()) return false;
    env->ExceptionClear();
    return true;
}
}
bool java_content_callbacks(JNIEnv* env, jobject provider, ContentPort::Callbacks* out) {
    if (!env || !provider || !out) return false;
    auto state = std::make_shared<JavaMetadata>();
    if (env->GetJavaVM(&state->vm) != JNI_OK) return false;
    auto type = env->GetObjectClass(provider);
    if (!type) { exception(env); return false; }
    state->query = env->GetMethodID(type, "query", "(I)[B");
    state->validate = env->GetMethodID(type, "isCurrent", "([B)Z");
    state->close = env->GetMethodID(type, "close", "()V");
    env->DeleteLocalRef(type);
    if (exception(env) || !state->query || !state->validate || !state->close) return false;
    state->provider = env->NewGlobalRef(provider);
    if (exception(env) || !state->provider) return false;
    out->query = [state](std::uint32_t index, std::vector<std::uint8_t>* bytes) {
        if (index > static_cast<std::uint32_t>((std::numeric_limits<jint>::max)())) return FLY_SESSION_V2_EMPTY;
        AttachedEnv attached(state->vm); auto* e = attached.env;
        if (!e) return FLY_SESSION_V2_UNAVAILABLE;
        auto row = static_cast<jbyteArray>(e->CallObjectMethod(state->provider, state->query, static_cast<jint>(index)));
        if (exception(e)) { if (row) e->DeleteLocalRef(row); return FLY_SESSION_V2_UNAVAILABLE; }
        if (!row) return FLY_SESSION_V2_EMPTY;
        const auto size = e->GetArrayLength(row);
        if (size < 57 || size > 120) { e->DeleteLocalRef(row); return FLY_SESSION_V2_INVALID_ARGUMENT; }
        // Bound allocation before copying the untrusted Java record.
        bytes->resize(static_cast<std::size_t>(size));
        e->GetByteArrayRegion(row, 0, size, reinterpret_cast<jbyte*>(bytes->data()));
        e->DeleteLocalRef(row);
        return exception(e) ? FLY_SESSION_V2_UNAVAILABLE : FLY_SESSION_V2_OK;
    };
    out->validate = [state](const std::uint8_t* ref) {
        AttachedEnv attached(state->vm); auto* e = attached.env;
        if (!e) return false;
        auto value = e->NewByteArray(16);
        if (!value) { exception(e); return false; }
        e->SetByteArrayRegion(value, 0, 16, reinterpret_cast<const jbyte*>(ref));
        bool current = e->CallBooleanMethod(state->provider, state->validate, value) == JNI_TRUE;
        e->DeleteLocalRef(value);
        return !exception(e) && current;
    };
    out->close = [state] {
        AttachedEnv attached(state->vm);
        if (attached.env) {
            attached.env->CallVoidMethod(state->provider, state->close);
            exception(attached.env);
        }
    };
    return true;
}
}

extern "C" {
JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeCreateWithProvider(
    JNIEnv* env, jclass, jobject provider, jlongArray out_handle)
{
    if (!provider || !out_handle || env->GetArrayLength(out_handle) < 1)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const jlong zero = 0;
    env->SetLongArrayRegion(out_handle, 0, 1, &zero);
    try {
        flynes::android::nearby::ContentPort::Callbacks callbacks;
        if (!flynes::android::nearby::java_content_callbacks(env, provider, &callbacks))
            return FLY_SESSION_V2_UNAVAILABLE;
        auto* owner = flynes::android::nearby::SessionOwner::create(std::move(callbacks));
        if (!owner) return FLY_SESSION_V2_UNAVAILABLE;
        const jlong handle = reinterpret_cast<jlong>(owner);
        env->SetLongArrayRegion(out_handle, 0, 1, &handle);
        return FLY_SESSION_V2_OK;
    } catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeSelectContent(
    JNIEnv* env, jclass, jlong handle, jbyteArray ref)
{
    if (!handle || !ref || env->GetArrayLength(ref) != 16) return FLY_SESSION_V2_INVALID_ARGUMENT;
    std::array<std::uint8_t, 16> bytes{};
    env->GetByteArrayRegion(ref, 0, 16, reinterpret_cast<jbyte*>(bytes.data()));
    if (env->ExceptionCheck()) return FLY_SESSION_V2_INVALID_ARGUMENT;
    return reinterpret_cast<flynes::android::nearby::SessionOwner*>(handle)->submit_action(
        FLY_SESSION_ACTION_SELECT_CONTENT_V2, bytes.data(), bytes.size());
}

JNIEXPORT jobjectArray JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeGameChoices(JNIEnv* env, jclass, jlong handle)
{
    if (!handle) return nullptr;
    std::vector<fly_session_game_choice_v2> choices;
    if (!reinterpret_cast<flynes::android::nearby::SessionOwner*>(handle)->read_game_choices(&choices)) return nullptr;
    auto type = env->FindClass("com/flynes/emu/NearbySessionOwner$GameChoice");
    if (!type) return nullptr;
    auto constructor = env->GetMethodID(type, "<init>", "([B[BZ[B[B)V");
    if (!constructor) { env->DeleteLocalRef(type); return nullptr; }
    auto result = env->NewObjectArray(static_cast<jsize>(choices.size()), type, nullptr);
    if (!result) { env->DeleteLocalRef(type); return nullptr; }
    for (std::size_t i = 0; i < choices.size(); ++i) {
        if (env->PushLocalFrame(8) != JNI_OK) { env->DeleteLocalRef(type); return nullptr; }
        const auto& choice = choices[i];
        auto bytes = [&](const void* source, std::size_t size) {
            auto value = env->NewByteArray(static_cast<jsize>(size));
            if (value) env->SetByteArrayRegion(value, 0, static_cast<jsize>(size), static_cast<const jbyte*>(source));
            return value;
        };
        auto ref = bytes(choice.source_choice_ref, 16);
        auto hash = bytes(choice.content_id, 32);
        auto name = bytes(choice.display_name, (std::min)(static_cast<std::size_t>(choice.display_name_size), sizeof(choice.display_name)));
        auto reason = bytes(choice.reason_key, strnlen(choice.reason_key, sizeof(choice.reason_key)));
        if (!env->ExceptionCheck()) {
            auto value = env->NewObject(type, constructor, ref, hash,
                static_cast<jboolean>(choice.selectable != 0), name, reason);
            if (value) env->SetObjectArrayElement(result, static_cast<jsize>(i), value);
        }
        env->PopLocalFrame(nullptr);
        if (env->ExceptionCheck()) { env->DeleteLocalRef(type); return nullptr; }
    }
    env->DeleteLocalRef(type);
    return result;
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeBeginContentPreparation(
    JNIEnv* env, jclass, jlong handle, jbyteArray ref, jlongArray out_ticket)
{
    if (!handle) return FLY_SESSION_V2_CLOSED;
    if (!ref || env->GetArrayLength(ref) != 16 || !out_ticket || env->GetArrayLength(out_ticket) < 1)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    std::array<std::uint8_t, 16> source{};
    env->GetByteArrayRegion(ref, 0, 16, reinterpret_cast<jbyte*>(source.data()));
    if (env->ExceptionCheck()) return FLY_SESSION_V2_INVALID_ARGUMENT;
    try {
        std::uint64_t ticket = 0;
        auto result = reinterpret_cast<flynes::android::nearby::SessionOwner*>(handle)->begin_content_preparation(source, &ticket);
        const jlong value = static_cast<jlong>(ticket);
        env->SetLongArrayRegion(out_ticket, 0, 1, &value);
        return result;
    } catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeCompleteContentPreparation(
    JNIEnv* env, jclass, jlong handle, jlong ticket, jbyteArray ref, jbyteArray hash, jbyteArray rom)
{
    if (!handle) return FLY_SESSION_V2_CLOSED;
    if (ticket <= 0 || !ref || env->GetArrayLength(ref) != 16 || !hash || env->GetArrayLength(hash) != 32 || !rom)
        return FLY_SESSION_V2_INVALID_ARGUMENT;
    const auto size = env->GetArrayLength(rom);
    if (size <= 0 || size > 8 * 1024 * 1024) return FLY_SESSION_V2_INVALID_ARGUMENT;
    try {
        std::array<std::uint8_t, 16> source{}; std::array<std::uint8_t, 32> digest{};
        auto bytes = std::make_shared<std::vector<std::uint8_t>>(static_cast<std::size_t>(size));
        env->GetByteArrayRegion(ref, 0, 16, reinterpret_cast<jbyte*>(source.data()));
        env->GetByteArrayRegion(hash, 0, 32, reinterpret_cast<jbyte*>(digest.data()));
        env->GetByteArrayRegion(rom, 0, size, reinterpret_cast<jbyte*>(bytes->data()));
        if (env->ExceptionCheck()) return FLY_SESSION_V2_INVALID_ARGUMENT;
        return reinterpret_cast<flynes::android::nearby::SessionOwner*>(handle)->complete_content_preparation(
            static_cast<std::uint64_t>(ticket), source, digest, std::move(bytes));
    } catch (const std::bad_alloc&) { return FLY_SESSION_V2_OUT_OF_MEMORY; }
    catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}

JNIEXPORT jint JNICALL
Java_com_flynes_emu_NearbySessionOwner_nativeCancelContentPreparation(JNIEnv*, jclass, jlong handle, jlong ticket)
{
    if (!handle) return FLY_SESSION_V2_CLOSED;
    if (ticket <= 0) return FLY_SESSION_V2_INVALID_ARGUMENT;
    try {
        return reinterpret_cast<flynes::android::nearby::SessionOwner*>(handle)->cancel_content_preparation(static_cast<std::uint64_t>(ticket));
    } catch (...) { return FLY_SESSION_V2_UNAVAILABLE; }
}
}
