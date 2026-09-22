#include <jni.h>

#include <flynes/flynes_nearby_mvp.h>

#include <cstdint>
#include <string>

namespace {
fly_lan_mvp_session* session_from(jlong handle) {
    return reinterpret_cast<fly_lan_mvp_session*>(static_cast<std::uintptr_t>(handle));
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_flynes_emu_ui_NearbyMvpPeerTestBridge_nativeCreate(JNIEnv*, jclass) {
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(fly_lan_mvp_create()));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_com_flynes_emu_ui_NearbyMvpPeerTestBridge_nativeJoin(
        JNIEnv* env, jclass, jlong handle, jstring local_ipv4, jstring invite) {
    if (handle == 0 || local_ipv4 == nullptr || invite == nullptr) return JNI_FALSE;
    const char* local = env->GetStringUTFChars(local_ipv4, nullptr);
    const char* qr = env->GetStringUTFChars(invite, nullptr);
    if (local == nullptr || qr == nullptr) {
        if (local != nullptr) env->ReleaseStringUTFChars(local_ipv4, local);
        if (qr != nullptr) env->ReleaseStringUTFChars(invite, qr);
        return JNI_FALSE;
    }
    const auto size = static_cast<std::size_t>(env->GetStringUTFLength(invite));
    const int result = fly_lan_mvp_join(session_from(handle), local, qr, size);
    env->ReleaseStringUTFChars(invite, qr);
    env->ReleaseStringUTFChars(local_ipv4, local);
    return result == 1 ? JNI_TRUE : JNI_FALSE;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_flynes_emu_ui_NearbyMvpPeerTestBridge_nativeSnapshot(
        JNIEnv* env, jclass, jlong handle) {
    fly_lan_mvp_snapshot snapshot{};
    if (handle == 0 || !fly_lan_mvp_snapshot_read(session_from(handle), &snapshot)) return nullptr;
    const jint values[2] = {static_cast<jint>(snapshot.state), static_cast<jint>(snapshot.reason)};
    jintArray result = env->NewIntArray(2);
    if (result != nullptr) env->SetIntArrayRegion(result, 0, 2, values);
    return result;
}

extern "C" JNIEXPORT jbyteArray JNICALL
Java_com_flynes_emu_ui_NearbyMvpPeerTestBridge_nativeSessionId(
        JNIEnv* env, jclass, jlong handle) {
    fly_lan_mvp_snapshot snapshot{};
    if (handle == 0 || !fly_lan_mvp_snapshot_read(session_from(handle), &snapshot)) return nullptr;
    jbyteArray result = env->NewByteArray(16);
    if (result != nullptr) {
        env->SetByteArrayRegion(result, 0, 16, reinterpret_cast<const jbyte*>(snapshot.session_id));
    }
    return result;
}

extern "C" JNIEXPORT void JNICALL
Java_com_flynes_emu_ui_NearbyMvpPeerTestBridge_nativeDestroy(
        JNIEnv*, jclass, jlong handle) {
    if (handle != 0) fly_lan_mvp_destroy(session_from(handle));
}
